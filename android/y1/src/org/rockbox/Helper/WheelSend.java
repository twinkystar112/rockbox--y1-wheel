/***************************************************************************
 * Y1 Wheel edition - send songs to a phone or computer over Bluetooth.
 *
 * A small OBEX Object Push (OPP) client over RFCOMM: CONNECT, then one PUT
 * per file (Name, Type, Length, Body...End-of-Body), then DISCONNECT.
 * Android phones, Windows/macOS/Linux computers accept this; iPhones do
 * not receive files over Bluetooth at all.
 *
 * The protocol part works on plain streams so it can be tested against a
 * local mock server (see utils/wheel/tests).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
package org.rockbox.Helper;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.Set;
import java.util.UUID;

import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothSocket;

public class WheelSend
{
    /* states shared with wheel_connect.h (enum wheel_send_state) */
    static final int IDLE = 0, CONNECTING = 1, WAITING = 2, SENDING = 3,
                     DONE = 4, FAILED = 5, REJECTED = 6;

    private static final UUID OPP = UUID.fromString("00001105-0000-1000-8000-00805F9B34FB");

    private static volatile int sState = IDLE, sFile = 0, sPercent = 0;
    private static volatile boolean sCancel;
    private static volatile Closeable sOpen;
    private static Thread sThread;

    interface Closeable { void close(); }

    /* ------------------------------ API ------------------------------ */

    public static boolean supported()
    {
        try {
            return BluetoothAdapter.getDefaultAdapter() != null;
        } catch (Throwable t) {
            return false;
        }
    }

    /* "state|audio|addr|name" like WheelBluetooth.getDevices() */
    public static String[] targets()
    {
        ArrayList<String> out = new ArrayList<String>();
        try {
            BluetoothAdapter a = BluetoothAdapter.getDefaultAdapter();
            Set<BluetoothDevice> bonded = a.getBondedDevices();
            for (BluetoothDevice d : bonded)
            {
                BluetoothClass c = d.getBluetoothClass();
                int major = c == null ? BluetoothClass.Device.Major.UNCATEGORIZED
                                      : c.getMajorDeviceClass();
                if (major != BluetoothClass.Device.Major.PHONE &&
                    major != BluetoothClass.Device.Major.COMPUTER &&
                    major != BluetoothClass.Device.Major.UNCATEGORIZED)
                    continue; /* headphones, speakers, watches... */
                String name = d.getName() == null ? d.getAddress() : d.getName();
                out.add("0|0|" + d.getAddress() + "|" + name);
            }
        } catch (Throwable t) {
            Logger.e("WheelSend targets: " + t);
        }
        return out.toArray(new String[out.size()]);
    }

    public static synchronized boolean start(final String addr, final String[] paths)
    {
        if (sThread != null && sThread.isAlive())
            return false;
        final BluetoothAdapter a;
        try {
            a = BluetoothAdapter.getDefaultAdapter();
            if (a == null || !a.isEnabled())
                return false;
        } catch (Throwable t) {
            return false;
        }
        sCancel = false;
        sState = CONNECTING; sFile = 0; sPercent = 0;
        sThread = new Thread(new Runnable() { public void run() {
            BluetoothSocket sock = null;
            try {
                a.cancelDiscovery(); /* discovery slows RFCOMM down a lot */
                BluetoothDevice dev = a.getRemoteDevice(addr);
                try {
                    sock = dev.createRfcommSocketToServiceRecord(OPP);
                    sock.connect();
                } catch (IOException e) {
                    /* some receivers only accept unauthenticated links */
                    if (sock != null) try { sock.close(); } catch (IOException e2) { }
                    sock = dev.createInsecureRfcommSocketToServiceRecord(OPP);
                    sock.connect();
                }
                final BluetoothSocket s = sock;
                sOpen = new Closeable() { public void close() {
                    try { s.close(); } catch (IOException e) { }
                }};
                sState = send(sock.getInputStream(), sock.getOutputStream(), paths);
            } catch (Throwable t) {
                Logger.e("WheelSend: " + t);
                sState = FAILED;
            } finally {
                sOpen = null;
                if (sock != null) try { sock.close(); } catch (IOException e) { }
            }
        }}, "wheel-send");
        sThread.start();
        return true;
    }

    /* state | file << 8 | percent << 20 */
    public static int progress()
    {
        return (sState & 0xff) | ((sFile & 0xfff) << 8) | ((sPercent & 0x7f) << 20);
    }

    public static void cancel()
    {
        sCancel = true;
        Closeable c = sOpen;
        if (c != null) c.close();
    }

    /* --------------------------- OBEX client --------------------------- */

    static final int OP_CONNECT = 0x80, OP_DISCONNECT = 0x81, OP_PUT = 0x02, OP_PUT_FINAL = 0x82;
    static final int RSP_CONTINUE = 0x90, RSP_SUCCESS = 0xA0;
    static final int HI_NAME = 0x01, HI_TYPE = 0x42, HI_LENGTH = 0xC3, HI_BODY = 0x48,
                     HI_END_BODY = 0x49, HI_CONN_ID = 0xCB;

    /* runs a whole session; returns DONE / FAILED / REJECTED */
    static int send(InputStream in, OutputStream out, String[] paths) throws IOException
    {
        int ourMax = 0x2000;
        /* CONNECT: version 1.0, flags 0, max packet */
        byte[] conn = { (byte)OP_CONNECT, 0, 7, 0x10, 0, (byte)(ourMax >> 8), (byte)ourMax };
        out.write(conn);
        out.flush();
        byte[] rsp = readPacket(in);
        if ((rsp[0] & 0xff) != RSP_SUCCESS || rsp.length < 7)
            return REJECTED;
        int theirMax = ((rsp[5] & 0xff) << 8) | (rsp[6] & 0xff);
        int max = Math.max(255, Math.min(ourMax, theirMax));
        long connId = -1;
        for (int p = 7; p + 5 <= rsp.length; )
        {   /* only the connection id matters here */
            int hi = rsp[p] & 0xff;
            if (hi == HI_CONN_ID) { connId = readInt(rsp, p + 1); break; }
            p += headerLength(rsp, p);
        }

        int result = DONE;
        for (int i = 0; i < paths.length && result == DONE; i++)
        {
            if (sCancel) { result = FAILED; break; }
            sFile = i; sPercent = 0;
            result = putFile(in, out, new File(paths[i]), max, connId, i == 0);
        }
        try {
            ByteArrayOutputStream d = new ByteArrayOutputStream();
            d.write(OP_DISCONNECT); d.write(0); d.write(3);
            byte[] pkt = withConnId(d.toByteArray(), connId);
            out.write(pkt);
            out.flush();
            readPacket(in);
        } catch (IOException e) { /* the other side may already be gone */ }
        return result;
    }

    private static int putFile(InputStream in, OutputStream out, File f, int max,
                               long connId, boolean first) throws IOException
    {
        long size = f.length();
        FileInputStream fis = new FileInputStream(f);
        try {
            /* first packet: name, type, length (and no body, so the
             * receiver can ask the user before we stream anything) */
            ByteArrayOutputStream h = new ByteArrayOutputStream();
            h.write(OP_PUT); h.write(0); h.write(0);
            if (connId >= 0) { h.write(HI_CONN_ID); writeInt(h, connId); }
            byte[] name = unicode(f.getName());
            h.write(HI_NAME); h.write((name.length + 3) >> 8); h.write(name.length + 3); h.write(name);
            byte[] type = (mimeOf(f.getName()) + "\0").getBytes("US-ASCII");
            h.write(HI_TYPE); h.write((type.length + 3) >> 8); h.write(type.length + 3); h.write(type);
            h.write(HI_LENGTH); writeInt(h, size);
            byte[] pkt = fixLength(h.toByteArray());
            sState = WAITING;
            out.write(pkt);
            out.flush();
            byte[] r = readPacket(in);
            int code = r[0] & 0xff;
            if (code != RSP_CONTINUE)
                return code == RSP_SUCCESS ? DONE : REJECTED;
            /* many receivers only ask the user once the body starts, so we
             * stay in WAITING until the first chunk is acknowledged */

            int room = max - 3 - 3 - (connId >= 0 ? 5 : 0);
            byte[] buf = new byte[room];
            long sent = 0;
            while (true)
            {
                if (sCancel) return FAILED;
                int n = readFully(fis, buf);
                boolean last = sent + n >= size || n < buf.length;
                ByteArrayOutputStream b = new ByteArrayOutputStream(n + 16);
                b.write(last ? OP_PUT_FINAL : OP_PUT); b.write(0); b.write(0);
                if (connId >= 0) { b.write(HI_CONN_ID); writeInt(b, connId); }
                b.write(last ? HI_END_BODY : HI_BODY);
                b.write((n + 3) >> 8); b.write(n + 3);
                b.write(buf, 0, n);
                out.write(fixLength(b.toByteArray()));
                out.flush();
                sent += n;
                sPercent = size > 0 ? (int)(sent * 100 / size) : 100;
                r = readPacket(in);
                code = r[0] & 0xff;
                sState = SENDING;
                if (last)
                    return code == RSP_SUCCESS ? DONE : REJECTED;
                if (code != RSP_CONTINUE)
                    return REJECTED;
            }
        } finally {
            fis.close();
        }
    }

    /* ----------------------------- helpers ----------------------------- */

    static byte[] readPacket(InputStream in) throws IOException
    {
        byte[] head = new byte[3];
        readExact(in, head, 0, 3);
        int len = ((head[1] & 0xff) << 8) | (head[2] & 0xff);
        if (len < 3) throw new IOException("bad OBEX length " + len);
        byte[] pkt = new byte[len];
        System.arraycopy(head, 0, pkt, 0, 3);
        readExact(in, pkt, 3, len - 3);
        return pkt;
    }

    private static void readExact(InputStream in, byte[] b, int off, int len) throws IOException
    {
        while (len > 0)
        {
            int n = in.read(b, off, len);
            if (n < 0) throw new IOException("closed");
            off += n; len -= n;
        }
    }

    private static int readFully(InputStream in, byte[] b) throws IOException
    {
        int off = 0;
        while (off < b.length)
        {
            int n = in.read(b, off, b.length - off);
            if (n < 0) break;
            off += n;
        }
        return off;
    }

    static int headerLength(byte[] p, int off)
    {
        switch (p[off] & 0xC0)
        {
            case 0x00: case 0x40: return ((p[off + 1] & 0xff) << 8) | (p[off + 2] & 0xff);
            case 0x80: return 2;
            default:   return 5;
        }
    }

    static long readInt(byte[] p, int off)
    {
        return ((long)(p[off] & 0xff) << 24) | ((p[off + 1] & 0xff) << 16)
             | ((p[off + 2] & 0xff) << 8) | (p[off + 3] & 0xff);
    }

    private static void writeInt(ByteArrayOutputStream o, long v)
    {
        o.write((int)(v >> 24)); o.write((int)(v >> 16)); o.write((int)(v >> 8)); o.write((int)v);
    }

    private static byte[] fixLength(byte[] p)
    {
        p[1] = (byte)(p.length >> 8);
        p[2] = (byte)p.length;
        return p;
    }

    private static byte[] withConnId(byte[] p, long connId)
    {
        if (connId < 0) return fixLength(p);
        ByteArrayOutputStream o = new ByteArrayOutputStream();
        o.write(p, 0, p.length);
        o.write(HI_CONN_ID); writeInt(o, connId);
        return fixLength(o.toByteArray());
    }

    /* OBEX names are UTF-16BE with a terminating null */
    static byte[] unicode(String s) throws IOException
    {
        byte[] u = s.getBytes("UTF-16BE");
        byte[] out = new byte[u.length + 2];
        System.arraycopy(u, 0, out, 0, u.length);
        return out;
    }

    static String mimeOf(String name)
    {
        String n = name.toLowerCase();
        if (n.endsWith(".mp3")) return "audio/mpeg";
        if (n.endsWith(".flac")) return "audio/flac";
        if (n.endsWith(".m4a") || n.endsWith(".aac") || n.endsWith(".alac")) return "audio/mp4";
        if (n.endsWith(".ogg") || n.endsWith(".oga")) return "audio/ogg";
        if (n.endsWith(".opus")) return "audio/opus";
        if (n.endsWith(".wav")) return "audio/x-wav";
        if (n.endsWith(".wma")) return "audio/x-ms-wma";
        if (n.endsWith(".lrc") || n.endsWith(".txt")) return "text/plain";
        if (n.endsWith(".jpg") || n.endsWith(".jpeg")) return "image/jpeg";
        return "application/octet-stream";
    }

    /* for the test harness: run a session on any streams */
    static int sendForTest(InputStream in, OutputStream out, String[] paths) throws IOException
    {
        sCancel = false;
        return send(in, out, paths);
    }
}
