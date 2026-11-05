/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * SONORA - podcast feed and episode downloads for the native side.
 *
 * Only used when the user turns on "Network features" in SONORA's settings.
 * Wi-Fi is switched on through WheelNet (the .rockbox/wifi.cfg network)
 * only for the duration of a refresh and switched off afterwards. One
 * download runs at a time on its own thread; the native side polls it.
 * Files are written to the path given (a ".part" file) and nothing else:
 * renaming and the download manifest are the native side's job.
 *
 * HTTPS: Java's TLS first (WheelScrobbler.tls(): TLS 1.2, current roots);
 * if that fails and the ROM ships /data/data/gocurl, through it instead.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 ****************************************************************************/
package org.rockbox.Helper;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;

import javax.net.ssl.HttpsURLConnection;

public class SonoraNet
{
    static final int RUNNING = 0, DONE = 1, FAILED = 2, CANCELLED = 3;

    private static volatile int sState = DONE;
    private static volatile long sDone, sTotal;
    private static volatile int sCode;
    private static volatile boolean sCancel;
    private static Thread sThread;

    /* "could we get online if asked" */
    public static boolean supported()
    {
        try { return WheelNet.reachable(); } catch (Throwable t) { return false; }
    }

    /* Wi-Fi on if needed. Can take many seconds, so it runs on its own
     * thread: a blocking JNI call would stall every Rockbox thread,
     * audio decoding included. 0 = working, 1 = online, 2 = failed. */
    private static volatile int sBegin = 2;

    public static synchronized void beginAsync()
    {
        if (sBegin == 0)
            return;
        sBegin = 0;
        new Thread(new Runnable() {
            public void run() {
                boolean ok;
                try { ok = WheelNet.acquire(); } catch (Throwable t) { ok = false; }
                sBegin = ok ? 1 : 2;
            }
        }, "sonora-wifi").start();
    }

    public static int beginState()
    {
        return sBegin;
    }

    /* always called after beginAsync(), even when it failed (WheelNet counts
     * users). On a thread: release() waits for a slow acquire() to finish,
     * and the native side must not wait with it. */
    public static void end()
    {
        new Thread(new Runnable() {
            public void run() {
                try { WheelNet.release(); } catch (Throwable t) { }
            }
        }, "sonora-wifi-off").start();
    }

    public static synchronized boolean start(final String url, final String out)
    {
        if (sState == RUNNING)
            return false;
        sState = RUNNING;
        sDone = 0;
        sTotal = -1;
        sCode = 0;
        sCancel = false;
        sThread = new Thread(new Runnable() {
            public void run() {
                int code;
                try {
                    code = javaDownload(url, out);
                } catch (IOException e) {
                    code = -1;
                }
                if (code != 200 && !sCancel && url.startsWith("https:") &&
                    new File(WheelNet.sGocurl).canExecute())
                {
                    try {
                        code = gocurlDownload(url, out);
                    } catch (IOException e) {
                        code = -1;
                    }
                }
                sCode = code;
                sState = sCancel ? CANCELLED : (code == 200 ? DONE : FAILED);
            }
        }, "sonora-download");
        sThread.start();
        return true;
    }

    /* { state, bytes done, bytes total (-1 unknown), HTTP code } */
    public static long[] poll()
    {
        return new long[] { sState, sDone, sTotal, sCode };
    }

    public static void cancel()
    {
        sCancel = true;
    }

    private static int javaDownload(String url, String out) throws IOException
    {
        HttpURLConnection c = null;
        /* follow redirects ourselves: podcast hosts bounce http <-> https */
        for (int hop = 0; hop < 6; hop++)
        {
            c = (HttpURLConnection) new URL(url).openConnection();
            if (c instanceof HttpsURLConnection)
                ((HttpsURLConnection) c).setSSLSocketFactory(WheelScrobbler.tls());
            c.setInstanceFollowRedirects(false);
            c.setConnectTimeout(15000);
            c.setReadTimeout(30000);
            c.setRequestProperty("User-Agent", WheelNet.USER_AGENT);
            int code = c.getResponseCode();
            if (code >= 300 && code < 400)
            {
                String loc = c.getHeaderField("Location");
                c.disconnect();
                if (loc == null)
                    return code;
                url = new URL(new URL(url), loc).toString();
                continue;
            }
            if (code != 200)
            {
                c.disconnect();
                return code;
            }
            sTotal = c.getContentLength();
            InputStream in = c.getInputStream();
            OutputStream os = new FileOutputStream(out);
            byte[] buf = new byte[64 * 1024];
            int n;
            try {
                while (!sCancel && (n = in.read(buf)) > 0)
                {
                    os.write(buf, 0, n);
                    sDone += n;
                }
            } finally {
                os.close();
                in.close();
                c.disconnect();
            }
            return sCancel ? -2 : 200;
        }
        return -3; /* too many redirects */
    }

    /* the ROM's curl prints the body on stdout; Go's client follows
     * redirects by itself */
    private static int gocurlDownload(String url, String out) throws IOException
    {
        List<String> cmd = new ArrayList<String>();
        cmd.add(WheelNet.sGocurl);
        if (new File(WheelNet.sCacert).exists()) { cmd.add("--cacert"); cmd.add(WheelNet.sCacert); }
        cmd.add("-s");
        cmd.add("--url"); cmd.add(url);
        Process p = new ProcessBuilder(cmd).redirectErrorStream(false).start();
        InputStream in = p.getInputStream();
        OutputStream os = new FileOutputStream(out);
        sDone = 0;
        sTotal = -1;
        byte[] buf = new byte[64 * 1024];
        int n;
        try {
            while (!sCancel && (n = in.read(buf)) > 0)
            {
                os.write(buf, 0, n);
                sDone += n;
            }
        } finally {
            os.close();
            in.close();
        }
        if (sCancel)
        {
            p.destroy();
            return -2;
        }
        try { p.waitFor(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        return (p.exitValue() == 0 && sDone > 0) ? 200 : -1;
    }
}
