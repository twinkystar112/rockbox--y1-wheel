/***************************************************************************
 * Y1 Wheel edition - network helpers for Last.fm and lyrics on the Y1.
 *
 *  - Wi-Fi on demand: the Rockbox-Y1 ROM enables the Wi-Fi chip and keeps
 *    the network in .rockbox/wifi.cfg (wifi-ssid / wifi-password). Like the
 *    lastfm_scrobbler and podcast plugins, we switch Wi-Fi on only for the
 *    moment it is needed and off again afterwards (battery).
 *  - HTTPS: Java's TLS on Android 4.2 is tried first (TLS 1.2 enabled, up to
 *    date roots, see WheelScrobbler.tls()); if that fails and the ROM ships
 *    /data/data/gocurl (Go TLS stack, used by the ROM's own plugins), the
 *    request goes through it instead.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
package org.rockbox.Helper;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;

import javax.net.ssl.HttpsURLConnection;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.Environment;

public class WheelNet
{
    static final String USER_AGENT = "Rockbox Y1 Wheel 1.0 (https://github.com/rockbox-y1/rockbox)";
    static String sGocurl = "/data/data/gocurl";
    static String sCacert = "/data/data/cacert.pem";
    private static Context sCtx;
    private static int sWifiUsers = 0;
    private static boolean sWeTurnedWifiOn = false;

    static void init(Context c) { if (sCtx == null && c != null) sCtx = c.getApplicationContext(); }

    static boolean online()
    {
        try {
            ConnectivityManager cm = (ConnectivityManager) sCtx.getSystemService(Context.CONNECTIVITY_SERVICE);
            NetworkInfo ni = cm.getActiveNetworkInfo();
            return ni != null && ni.isConnected();
        } catch (Throwable t) {
            return false;
        }
    }

    /* .rockbox/wifi.cfg ("key: value" lines), shared with the ROM's plugins */
    static Map<String, String> wifiCfg()
    {
        Map<String, String> m = new TreeMap<String, String>();
        File f = new File(new File(Environment.getExternalStorageDirectory(), ".rockbox"), "wifi.cfg");
        if (!f.exists())
            return m;
        try {
            BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
            String line;
            while ((line = r.readLine()) != null)
            {
                String t = line.trim();
                if (t.length() == 0 || t.startsWith("#") || t.startsWith("-")) continue;
                int c = t.indexOf(':');
                if (c <= 0) continue;
                String v = t.substring(c + 1).trim();
                if (v.length() >= 2 && v.startsWith("\"") && v.endsWith("\""))
                    v = v.substring(1, v.length() - 1);
                if (v.length() > 0 && !v.startsWith("YOUR_"))
                    m.put(t.substring(0, c).trim().toLowerCase(), v);
            }
            r.close();
        } catch (IOException e) { }
        return m;
    }

    static boolean wifiConfigured()
    {
        return wifiCfg().containsKey("wifi-ssid");
    }

    /* "could we get online if asked": already online, or Wi-Fi configured */
    static boolean reachable()
    {
        return online() || wifiConfigured();
    }

    /* blocking: call from a worker thread. Pair with release(). */
    static synchronized boolean acquire()
    {
        sWifiUsers++;
        if (online())
            return true;
        Map<String, String> w = wifiCfg();
        if (!w.containsKey("wifi-ssid"))
            return false;
        String r;
        try {
            r = Connectivity.connectWifi(w.get("wifi-ssid"),
                    w.containsKey("wifi-password") ? w.get("wifi-password") : "");
        } catch (Throwable t) {
            r = "Failed";
        }
        if ("Success".equals(r))
        {
            sWeTurnedWifiOn = true;
            return true;
        }
        return online();
    }

    static synchronized void release()
    {
        if (sWifiUsers > 0) sWifiUsers--;
        if (sWifiUsers == 0 && sWeTurnedWifiOn)
        {
            sWeTurnedWifiOn = false;
            try { Connectivity.disconnectWifi(); } catch (Throwable t) { }
        }
    }

    /* ------------------------------- HTTP ------------------------------- */

    static class Response
    {
        final int code; final String body;
        Response(int c, String b) { code = c; body = b; }
    }

    static Response request(String method, String url, String body) throws IOException
    {
        try {
            return javaRequest(method, url, body);
        } catch (IOException e) {
            if (!url.startsWith("https:") || !new File(sGocurl).canExecute())
                throw e;
            return gocurl(method, url, body);
        }
    }

    private static Response javaRequest(String method, String url, String body) throws IOException
    {
        HttpURLConnection c = (HttpURLConnection) new URL(url).openConnection();
        if (c instanceof HttpsURLConnection)
            ((HttpsURLConnection) c).setSSLSocketFactory(WheelScrobbler.tls());
        c.setConnectTimeout(15000);
        c.setReadTimeout(20000);
        c.setRequestProperty("User-Agent", USER_AGENT);
        if (body != null)
        {
            byte[] data = body.getBytes("UTF-8");
            c.setDoOutput(true);
            c.setRequestMethod(method);
            c.setRequestProperty("Content-Type", "application/x-www-form-urlencoded; charset=UTF-8");
            c.setFixedLengthStreamingMode(data.length);
            OutputStream os = c.getOutputStream();
            os.write(data);
            os.close();
        }
        int code = c.getResponseCode();
        InputStream in = code >= 400 ? c.getErrorStream() : c.getInputStream();
        String text = in == null ? "" : readAll(in);
        c.disconnect();
        return new Response(code, text);
    }

    /* the ROM's curl: arguments passed as an array, no shell quoting */
    private static Response gocurl(String method, String url, String body) throws IOException
    {
        List<String> cmd = new ArrayList<String>();
        cmd.add(sGocurl);
        if (new File(sCacert).exists()) { cmd.add("--cacert"); cmd.add(sCacert); }
        cmd.add("-s");
        if (body != null) { cmd.add("-X"); cmd.add(method); cmd.add("-d"); cmd.add(body); }
        cmd.add("--url"); cmd.add(url);
        Process p = new ProcessBuilder(cmd).redirectErrorStream(false).start();
        String out = readAll(p.getInputStream());
        try { p.waitFor(); } catch (InterruptedException e) { Thread.currentThread().interrupt(); }
        if (p.exitValue() != 0)
            throw new IOException("gocurl exit " + p.exitValue());
        /* gocurl -s prints only the body; Last.fm / LRCLIB errors are JSON */
        return new Response(out.length() > 0 ? 200 : 204, out);
    }

    static String readAll(InputStream in) throws IOException
    {
        ByteArrayOutputStream bo = new ByteArrayOutputStream();
        byte[] buf = new byte[4096];
        int n;
        while ((n = in.read(buf)) > 0) bo.write(buf, 0, n);
        in.close();
        return bo.toString("UTF-8");
    }
}
