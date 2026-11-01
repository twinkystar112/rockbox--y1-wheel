/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - Last.fm scrobbler.
 *
 *  - follows Last.fm's rules: track > 30 s, listened >= 50 % or >= 4 min,
 *    measured as real listening time (seeking to the end doesn't count)
 *  - offline first: every scrobble goes to a queue on the SD card and is
 *    sent in batches of 50 whenever there is a connection
 *  - "Now Playing" updates while online
 *  - optional Audioscrobbler 1.1 log (/.scrobbler-wheel.log) with correct UTC
 *    (its own file: Rockbox's lastfm_scrobbler plugin writes /.scrobbler.log
 *    with local time, and mixing both would double scrobbles)
 *    timestamps, for players that never get online
 *  - TLS 1.2 + bundled root certificates so it works on Android 4.2
 *
 * Sign-in: put /.rockbox/lastfm.cfg on the SD card (see docs) with your
 * API key/secret and Last.fm username/password. After the first sign-in
 * the password is removed from that file; only the session key is kept.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
package org.rockbox.Helper;

import java.io.BufferedReader;
import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.net.HttpURLConnection;
import java.net.InetAddress;
import java.net.Socket;
import java.net.URL;
import java.net.URLEncoder;
import java.security.KeyStore;
import java.security.MessageDigest;
import java.security.cert.Certificate;
import java.security.cert.CertificateException;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collection;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;

import javax.net.ssl.HttpsURLConnection;
import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLSocket;
import javax.net.ssl.SSLSocketFactory;
import javax.net.ssl.TrustManager;
import javax.net.ssl.TrustManagerFactory;
import javax.net.ssl.X509TrustManager;

import org.json.JSONObject;
import org.rockbox.R;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.net.ConnectivityManager;
import android.net.NetworkInfo;
import android.os.Environment;
import android.os.Handler;
import android.os.HandlerThread;

public class WheelScrobbler
{
    private static final String API_HTTPS = "https://ws.audioscrobbler.com/2.0/";
    private static final String AUTH_URL  = "https://www.last.fm/api/auth/";
    private static final String CLIENT = "Rockbox Y1 Wheel 1.0";
    private static final int BATCH = 50;
    private static final long PERIODIC_MS = 15 * 60 * 1000;

    private static Context sCtx;
    private static SharedPreferences sPrefs;
    private static Handler sWorker;
    private static final List<String[]> sQueue = new ArrayList<String[]>();
    private static String sLastError = "";
    private static long sBackoffUntil = 0;
    private static SSLSocketFactory sTls;

    private static native void nativePending(int n);

    private static void pushPending()
    {
        int n;
        synchronized (WheelScrobbler.class) { n = sQueue.size(); }
        try { nativePending(n); } catch (Throwable t) { /* lib not loaded yet */ }
    }

    /* queue entry fields */
    private static final int Q_TS = 0, Q_ARTIST = 1, Q_TRACK = 2, Q_ALBUM = 3,
                             Q_ALBUMARTIST = 4, Q_TRACKNUM = 5, Q_DURATION = 6,
                             Q_MBID = 7, Q_FIELDS = 8;

    public static synchronized void init(Context c)
    {
        if (sCtx != null)
            return;
        sCtx = c.getApplicationContext();
        WheelNet.init(sCtx);
        sPrefs = sCtx.getSharedPreferences("wheel_lastfm", Context.MODE_PRIVATE);
        HandlerThread t = new HandlerThread("wheel-lastfm");
        t.start();
        sWorker = new Handler(t.getLooper());
        loadQueue();
        sCtx.registerReceiver(new BroadcastReceiver() {
            @Override public void onReceive(Context ctx, Intent i) {
                if (isOnline()) scheduleFlush(3000);
            }
        }, new IntentFilter(ConnectivityManager.CONNECTIVITY_ACTION));
        sWorker.postDelayed(sPeriodic, 20000);
        sWorker.postDelayed(new Runnable() { public void run() { pushPending(); } }, 8000);
        /* first start with a config file on the card: sign in automatically */
        if (!isSignedIn() && readConfig().containsKey("password"))
            signIn();
    }

    /* ------------------------------ native API ------------------------------ */

    /* enabled \t nowplaying \t log \t signedin \t user \t pending \t sent \t lastsync \t online \t error */
    public static String getStatus()
    {
        int pending;
        String err;
        synchronized (WheelScrobbler.class) { pending = sQueue.size(); err = sLastError; }
        StringBuilder b = new StringBuilder();
        b.append(isEnabled() ? 1 : 0).append('\t')
         .append(sPrefs.getBoolean("nowplaying", true) ? 1 : 0).append('\t')
         .append(sPrefs.getBoolean("log", true) ? 1 : 0).append('\t')
         .append(isSignedIn() ? 1 : 0).append('\t')
         .append(clean(sPrefs.getString("user", ""))).append('\t')
         .append(pending).append('\t')
         .append(sPrefs.getInt("sent", 0)).append('\t')
         .append(sPrefs.getLong("lastsync", 0)).append('\t')
         .append(isOnline() ? 1 : 0).append('\t')
         .append(clean(err));
        return b.toString();
    }

    public static void setEnabled(boolean on)    { sPrefs.edit().putBoolean("enabled", on).commit(); }
    public static void setNowPlaying(boolean on) { sPrefs.edit().putBoolean("nowplaying", on).commit(); }
    public static void setWriteLog(boolean on)   { sPrefs.edit().putBoolean("log", on).commit(); }

    public static boolean flush()
    {
        if (!isSignedIn())
            return false;
        sBackoffUntil = 0;
        if (isOnline())
        {
            scheduleFlush(0);
            return true;
        }
        if (!WheelNet.wifiConfigured())
            return false;
        /* Y1: switch Wi-Fi on just for this (wifi.cfg), then off again */
        sWorker.post(new Runnable() { public void run() {
            try {
                if (WheelNet.acquire())
                    sFlush.run();
            } finally {
                WheelNet.release();
            }
        }});
        return true;
    }

    /* api_url / auth_url in lastfm.cfg: any Audioscrobbler 2.0 compatible
     * service (e.g. Libre.fm: https://libre.fm/2.0/ and https://libre.fm/api/auth/) */
    private static void storeEndpoints(Map<String, String> cfg)
    {
        String api = cfg.containsKey("api_url") ? cfg.get("api_url") : API_HTTPS;
        if (!api.endsWith("/")) api += "/";
        sPrefs.edit().putString("api", api)
                     .putString("auth", cfg.containsKey("auth_url") ? cfg.get("auth_url") : AUTH_URL)
                     .commit();
    }

    /* returns false when there is nothing to sign in with */
    public static boolean signIn()
    {
        final Map<String, String> cfg = readConfig();
        storeEndpoints(cfg);
        if (cfg.containsKey("session_key") && cfg.containsKey("api_key") && cfg.containsKey("api_secret"))
        {
            sPrefs.edit().putString("sk", cfg.get("session_key"))
                         .putString("user", cfg.containsKey("username") ? cfg.get("username") : "")
                         .putString("key", cfg.get("api_key"))
                         .putString("secret", cfg.get("api_secret")).commit();
            setError("");
            scheduleFlush(1000);
            return true;
        }
        if (!cfg.containsKey("api_key") || !cfg.containsKey("api_secret")
            || !cfg.containsKey("username") || !cfg.containsKey("password"))
            return false;
        sWorker.post(new Runnable() { public void run() {
            try {
                WheelNet.acquire();
                doSignIn(cfg);
            } finally {
                WheelNet.release();
            }
        }});
        return true;
    }

    public static void signOut()
    {
        sPrefs.edit().remove("sk").remove("user").commit();
    }

    /* ---- sign in with a phone: show a QR code with Last.fm's auth URL ---- */
    private static volatile String sAuthUrl = "";
    private static volatile int sAuthState = 0; /* 0 waiting, 1 ok, -1 failed */
    private static volatile String sAuthToken = null;
    private static volatile long sAuthStarted;

    public static boolean webAuthStart()
    {
        final Map<String, String> cfg = readConfig();
        if (!cfg.containsKey("api_key") || !cfg.containsKey("api_secret") || !WheelNet.reachable())
            return false;
        authNetRelease();
        sAuthUrl = ""; sAuthState = 0; sAuthToken = null;
        sAuthStarted = System.currentTimeMillis();
        sPrefs.edit().putString("key", cfg.get("api_key"))
                     .putString("secret", cfg.get("api_secret")).commit();
        storeEndpoints(cfg);
        sWorker.post(new Runnable() { public void run() {
            sAuthHoldsNet = true;
            WheelNet.acquire();
            try {
                Map<String, String> p = new TreeMap<String, String>();
                p.put("method", "auth.getToken");
                String token = call(p).getString("token");
                sAuthToken = token;
                sAuthUrl = sPrefs.getString("auth", AUTH_URL) + "?api_key=" + cfg.get("api_key")
                         + "&token=" + token;
                sWorker.postDelayed(sAuthPoll, 4000);
            } catch (Exception e) {
                setError("Sign-in: " + e.getMessage());
                sAuthState = -1;
                authNetRelease();
            }
        }});
        return true;
    }

    private static boolean sAuthHoldsNet = false;
    private static synchronized void authNetRelease()
    {
        if (sAuthHoldsNet)
        {
            sAuthHoldsNet = false;
            WheelNet.release();
        }
    }

    private static final Runnable sAuthPoll = new Runnable() {
        public void run() {
            String token = sAuthToken;
            if (token == null || sAuthState != 0)
                return;
            if (System.currentTimeMillis() - sAuthStarted > 10 * 60 * 1000) {
                sAuthState = -1; /* tokens expire after 60 min; give up earlier */
                authNetRelease();
                return;
            }
            try {
                Map<String, String> p = new TreeMap<String, String>();
                p.put("method", "auth.getSession");
                p.put("token", token);
                JSONObject s = call(p).getJSONObject("session");
                sPrefs.edit().putString("sk", s.getString("key"))
                             .putString("user", s.optString("name", "")).commit();
                setError("");
                sAuthState = 1;
                sFlush.run(); /* while the network is up */
                authNetRelease();
                return;
            } catch (LastfmException e) {
                if (e.code != 14) { /* 14 = not authorised yet: keep waiting */
                    setError(e.getMessage());
                    sAuthState = -1;
                    authNetRelease();
                    return;
                }
            } catch (Exception e) {
                /* network hiccup: try again */
            }
            sWorker.postDelayed(this, 3000);
        }
    };

    public static String webAuthUrl() { return sAuthUrl; }
    public static int webAuthState() { return sAuthState; }
    public static void webAuthCancel()
    {
        sAuthToken = null;
        sWorker.removeCallbacks(sAuthPoll);
        sWorker.post(new Runnable() { public void run() { authNetRelease(); } });
    }

    /* ---- love the playing track ---- */
    private static File lovesFile() { return new File(rbDir(), ".lastfm-loves.tsv"); }

    public static boolean loveCurrent()
    {
        final String[] t = WheelMedia.currentArtistTitle();
        if (t == null || t[0].length() == 0 || t[1].length() == 0)
            return false;
        try { /* remember it even when offline; sent with the next flush */
            rbDir().mkdirs();
            Writer w = new OutputStreamWriter(new FileOutputStream(lovesFile(), true), "UTF-8");
            w.write(clean(t[0]) + "\t" + clean(t[1]) + "\n");
            w.close();
        } catch (IOException e) {
            return false;
        }
        scheduleFlush(1000);
        return true;
    }

    private static void flushLoves() throws Exception
    {
        File f = lovesFile();
        if (!f.exists() || f.length() == 0)
            return;
        List<String> left = new ArrayList<String>();
        BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
        String line;
        List<String> all = new ArrayList<String>();
        while ((line = r.readLine()) != null)
            if (line.indexOf('\t') > 0) all.add(line);
        r.close();
        for (int i = 0; i < all.size(); i++)
        {
            String[] a = all.get(i).split("\t", 2);
            Map<String, String> p = new TreeMap<String, String>();
            p.put("method", "track.love");
            p.put("artist", a[0]);
            p.put("track", a[1]);
            try {
                call(p);
            } catch (IOException e) {
                left.addAll(all.subList(i, all.size())); /* offline again */
                break;
            } catch (LastfmException e) {
                /* unknown track etc: drop it */
            }
        }
        Writer w = new OutputStreamWriter(new FileOutputStream(f, false), "UTF-8");
        for (String l : left) w.write(l + "\n");
        w.close();
    }

    /* ------------------------- from WheelMedia ------------------------------ */

    static void onTrackStart(final String artist, final String title, final String album,
                             final String albumArtist, final int trackNum, final int lengthMs,
                             final String mbid)
    {
        if (sCtx == null || !isEnabled() || !isSignedIn()
            || !sPrefs.getBoolean("nowplaying", true) || artist.length() == 0 || !isOnline())
            return;
        sWorker.post(new Runnable() { public void run() {
            Map<String, String> p = new TreeMap<String, String>();
            p.put("method", "track.updateNowPlaying");
            p.put("artist", artist);
            p.put("track", title);
            if (album.length() > 0) p.put("album", album);
            if (albumArtist.length() > 0 && !albumArtist.equals(artist)) p.put("albumArtist", albumArtist);
            if (trackNum > 0) p.put("trackNumber", Integer.toString(trackNum));
            if (lengthMs > 0) p.put("duration", Integer.toString(lengthMs / 1000));
            if (mbid.length() > 0) p.put("mbid", mbid);
            try {
                call(p);
            } catch (Exception e) {
                /* now playing is best effort */
            }
        }});
    }

    static void onTrackEnd(final String artist, final String title, final String album,
                           final String albumArtist, final int trackNum, final int lengthMs,
                           final String mbid, final long startedUtc,
                           final long playedMs, final boolean autoSkip)
    {
        if (sCtx == null)
            return;
        /* never do SD card I/O on the Rockbox (audio) thread */
        sWorker.post(new Runnable() { public void run() {
            recordTrackEnd(artist, title, album, albumArtist, trackNum, lengthMs,
                           mbid, startedUtc, playedMs, autoSkip);
        }});
    }

    private static void recordTrackEnd(String artist, String title, String album, String albumArtist,
                                       int trackNum, int lengthMs, String mbid, long startedUtc,
                                       long playedMs, boolean autoSkip)
    {
        boolean listened = lengthMs > 30000 && (playedMs >= lengthMs / 2 || playedMs >= 240000);
        if (sPrefs.getBoolean("log", true))
            appendLog(artist, album, title, trackNum, lengthMs, listened, startedUtc, mbid);
        if (!listened || !isEnabled() || artist.length() == 0 || title.length() == 0)
            return;
        String[] e = new String[Q_FIELDS];
        e[Q_TS] = Long.toString(startedUtc);
        e[Q_ARTIST] = clean(artist);
        e[Q_TRACK] = clean(title);
        e[Q_ALBUM] = clean(album);
        e[Q_ALBUMARTIST] = clean(albumArtist);
        e[Q_TRACKNUM] = trackNum > 0 ? Integer.toString(trackNum) : "";
        e[Q_DURATION] = Integer.toString(lengthMs / 1000);
        e[Q_MBID] = clean(mbid);
        synchronized (WheelScrobbler.class)
        {
            sQueue.add(e);
            appendQueueLine(e);
        }
        pushPending();
        scheduleFlush(5000);
    }

    /* ------------------------------- internals ------------------------------ */

    private static boolean isEnabled()  { return sPrefs != null && sPrefs.getBoolean("enabled", true); }
    private static boolean isSignedIn() { return sPrefs != null && sPrefs.getString("sk", "").length() > 0; }

    static boolean isOnline()
    {
        return WheelNet.online();
    }

    private static String clean(String s)
    {
        return s == null ? "" : s.replace('\t', ' ').replace('\n', ' ').replace('\r', ' ');
    }

    private static synchronized void setError(String e) { sLastError = e == null ? "" : e; }

    private static File rbDir()
    {
        return new File(Environment.getExternalStorageDirectory(), ".rockbox");
    }

    static Map<String, String> readConfig()
    {
        Map<String, String> m = new TreeMap<String, String>();
        /* a key/secret can be baked into the APK at build time (CI secret) */
        try {
            String k = sCtx.getString(R.string.wheel_lastfm_api_key).trim();
            String sec = sCtx.getString(R.string.wheel_lastfm_api_secret).trim();
            if (k.length() > 0 && sec.length() > 0) {
                m.put("api_key", k);
                m.put("api_secret", sec);
            }
        } catch (Throwable t) { }
        /* the Rockbox-Y1 ROM keeps scrobble settings in wifi.cfg: use them
         * too, so there is one place to fill in (lastfm.cfg wins) */
        Map<String, String> w = WheelNet.wifiCfg();
        String proto = w.containsKey("scrobble-protocol") ? w.get("scrobble-protocol") : "lastfm";
        if (proto.equalsIgnoreCase("lastfm"))
        {
            String[][] keys = { { "scrobble-apikey", "api_key" }, { "scrobble-sharedsecret", "api_secret" },
                                { "scrobble-user", "username" }, { "scrobble-password", "password" },
                                { "scrobble-apiurl", "api_url" } };
            for (String[] k : keys)
                if (w.containsKey(k[0]) && !m.containsKey(k[1]))
                    m.put(k[1], w.get(k[0]));
            if (m.containsKey("password"))
                m.put("password_from_wifi_cfg", "1"); /* never scrub the ROM's file */
        }
        File f = new File(rbDir(), "lastfm.cfg");
        if (!f.exists())
            return m;
        try {
            BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
            String line;
            while ((line = r.readLine()) != null)
            {
                line = line.trim();
                if (line.length() == 0 || line.startsWith("#")) continue;
                int eq = line.indexOf('=');
                if (eq < 0) eq = line.indexOf(':');
                if (eq <= 0) continue;
                String v = line.substring(eq + 1).trim();
                String k = line.substring(0, eq).trim().toLowerCase();
                if (v.length() > 0)
                    m.put(k, v);
                if (k.equals("password") && v.length() > 0)
                    m.remove("password_from_wifi_cfg"); /* ours: scrub it */
            }
            r.close();
        } catch (IOException e) { }
        return m;
    }

    /* after a successful sign-in, keep everything but the password */
    private static void scrubPassword()
    {
        File f = new File(rbDir(), "lastfm.cfg");
        try {
            BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
            StringBuilder out = new StringBuilder();
            String line;
            while ((line = r.readLine()) != null)
            {
                String t = line.trim().toLowerCase();
                if (t.startsWith("password"))
                    out.append("# password removed after a successful sign-in\n");
                else
                    out.append(line).append('\n');
            }
            r.close();
            Writer w = new OutputStreamWriter(new FileOutputStream(f), "UTF-8");
            w.write(out.toString());
            w.close();
        } catch (IOException e) {
            Logger.e("WheelScrobbler scrub: " + e);
        }
    }

    private static void doSignIn(Map<String, String> cfg)
    {
        Map<String, String> p = new TreeMap<String, String>();
        p.put("method", "auth.getMobileSession");
        p.put("username", cfg.get("username"));
        p.put("password", cfg.get("password"));
        try {
            sPrefs.edit().putString("key", cfg.get("api_key"))
                         .putString("secret", cfg.get("api_secret"))
                         .putBoolean("http", "1".equals(cfg.get("allow_http"))).commit();
            JSONObject r = call(p);
            JSONObject s = r.getJSONObject("session");
            sPrefs.edit().putString("sk", s.getString("key"))
                         .putString("user", s.optString("name", cfg.get("username"))).commit();
            if (!cfg.containsKey("password_from_wifi_cfg"))
                scrubPassword();
            setError("");
            scheduleFlush(1000);
        } catch (LastfmException e) {
            setError(e.getMessage());
        } catch (Exception e) {
            setError("Sign-in failed: " + e.getMessage());
        }
    }

    private static void scheduleFlush(long delay)
    {
        if (sWorker == null)
            return;
        sWorker.removeCallbacks(sFlush);
        sWorker.postDelayed(sFlush, delay);
    }

    private static final Runnable sPeriodic = new Runnable() {
        public void run() {
            if (isOnline()) sFlush.run();
            sWorker.postDelayed(this, PERIODIC_MS);
        }
    };

    private static final Runnable sFlush = new Runnable() {
        public void run() {
            if (isSignedIn() && isOnline())
            {
                try { flushLoves(); } catch (Exception e) { }
            }
            while (true)
            {
                if (!isSignedIn() || !isOnline() || System.currentTimeMillis() < sBackoffUntil)
                    return;
                List<String[]> batch;
                synchronized (WheelScrobbler.class)
                {
                    if (sQueue.isEmpty())
                        return;
                    batch = new ArrayList<String[]>(sQueue.subList(0, Math.min(BATCH, sQueue.size())));
                }
                Map<String, String> p = new TreeMap<String, String>();
                p.put("method", "track.scrobble");
                for (int i = 0; i < batch.size(); i++)
                {
                    String[] e = batch.get(i);
                    p.put("timestamp[" + i + "]", e[Q_TS]);
                    p.put("artist[" + i + "]", e[Q_ARTIST]);
                    p.put("track[" + i + "]", e[Q_TRACK]);
                    if (e[Q_ALBUM].length() > 0)       p.put("album[" + i + "]", e[Q_ALBUM]);
                    if (e[Q_ALBUMARTIST].length() > 0 && !e[Q_ALBUMARTIST].equals(e[Q_ARTIST]))
                        p.put("albumArtist[" + i + "]", e[Q_ALBUMARTIST]);
                    if (e[Q_TRACKNUM].length() > 0)    p.put("trackNumber[" + i + "]", e[Q_TRACKNUM]);
                    if (e[Q_DURATION].length() > 0)    p.put("duration[" + i + "]", e[Q_DURATION]);
                    if (e[Q_MBID].length() > 0)        p.put("mbid[" + i + "]", e[Q_MBID]);
                    p.put("chosenByUser[" + i + "]", "1");
                }
                try {
                    JSONObject r = call(p);
                    int accepted = batch.size();
                    try {
                        accepted = r.getJSONObject("scrobbles").getJSONObject("@attr").getInt("accepted");
                    } catch (Exception ignore) { }
                    synchronized (WheelScrobbler.class)
                    {
                        sQueue.subList(0, batch.size()).clear();
                        saveQueue();
                    }
                    pushPending();
                    sPrefs.edit().putInt("sent", sPrefs.getInt("sent", 0) + accepted)
                                 .putLong("lastsync", System.currentTimeMillis() / 1000L).commit();
                    setError("");
                } catch (LastfmException e) {
                    setError(e.getMessage());
                    switch (e.code)
                    {
                        case 9:  /* invalid session key: sign in again */
                        case 4:
                            signOut();
                            return;
                        case 29: /* rate limited */
                            sBackoffUntil = System.currentTimeMillis() + 5 * 60 * 1000;
                            scheduleFlush(5 * 60 * 1000 + 1000);
                            return;
                        default: /* 11/16 service offline etc: keep queue, retry later */
                            sBackoffUntil = System.currentTimeMillis() + 60 * 1000;
                            scheduleFlush(61 * 1000);
                            return;
                    }
                } catch (Exception e) {
                    setError("Network: " + e.getMessage());
                    sBackoffUntil = System.currentTimeMillis() + 60 * 1000;
                    scheduleFlush(61 * 1000);
                    return;
                }
            }
        }
    };

    /* ----------------------------- queue file ------------------------------- */

    private static File queueFile() { return new File(rbDir(), ".lastfm-queue.tsv"); }

    private static void loadQueue()
    {
        File f = queueFile();
        if (!f.exists())
            return;
        try {
            BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
            String line;
            synchronized (WheelScrobbler.class)
            {
                while ((line = r.readLine()) != null)
                {
                    String[] e = line.split("\t", -1);
                    if (e.length >= Q_FIELDS && e[Q_TS].length() > 0)
                        sQueue.add(Arrays.copyOf(e, Q_FIELDS));
                }
            }
            r.close();
        } catch (IOException e) {
            Logger.e("WheelScrobbler queue: " + e);
        }
    }

    private static String queueLine(String[] e)
    {
        StringBuilder b = new StringBuilder();
        for (int i = 0; i < Q_FIELDS; i++)
        {
            if (i > 0) b.append('\t');
            b.append(e[i]);
        }
        return b.append('\n').toString();
    }

    private static void appendQueueLine(String[] e)
    {
        try {
            rbDir().mkdirs();
            Writer w = new OutputStreamWriter(new FileOutputStream(queueFile(), true), "UTF-8");
            w.write(queueLine(e));
            w.close();
        } catch (IOException ex) {
            Logger.e("WheelScrobbler queue write: " + ex);
        }
    }

    private static void saveQueue() /* caller holds the class lock */
    {
        File tmp = new File(rbDir(), ".lastfm-queue.tmp");
        try {
            Writer w = new OutputStreamWriter(new FileOutputStream(tmp), "UTF-8");
            for (String[] e : sQueue)
                w.write(queueLine(e));
            w.close();
            if (!tmp.renameTo(queueFile()))
            {
                queueFile().delete();
                tmp.renameTo(queueFile());
            }
        } catch (IOException ex) {
            Logger.e("WheelScrobbler queue save: " + ex);
        }
    }

    /* ------------------ Audioscrobbler 1.1 log (offline) -------------------- */

    private static synchronized void appendLog(String artist, String album, String title,
                                               int trackNum, int lengthMs, boolean listened,
                                               long startedUtc, String mbid)
    {
        File f = new File(Environment.getExternalStorageDirectory(), ".scrobbler-wheel.log");
        try {
            boolean fresh = !f.exists() || f.length() == 0;
            Writer w = new OutputStreamWriter(new FileOutputStream(f, true), "UTF-8");
            if (fresh)
                w.write("#AUDIOSCROBBLER/1.1\n#TZ/UTC\n#CLIENT/" + CLIENT + "\n");
            w.write(clean(artist) + "\t" + clean(album) + "\t" + clean(title) + "\t"
                    + (trackNum > 0 ? trackNum : "") + "\t" + (lengthMs / 1000) + "\t"
                    + (listened ? "L" : "S") + "\t" + startedUtc + "\t" + clean(mbid) + "\n");
            w.close();
        } catch (IOException e) {
            Logger.e("WheelScrobbler log: " + e);
        }
    }

    /* ------------------------------ HTTP / API ------------------------------ */

    static class LastfmException extends Exception
    {
        final int code;
        LastfmException(int code, String msg) { super(msg); this.code = code; }
    }

    private static JSONObject call(Map<String, String> params) throws Exception
    {
        String key = sPrefs.getString("key", ""), secret = sPrefs.getString("secret", "");
        if (key.length() == 0 || secret.length() == 0)
            throw new LastfmException(10, "API key missing in lastfm.cfg");
        TreeMap<String, String> p = new TreeMap<String, String>(params);
        p.put("api_key", key);
        String sk = sPrefs.getString("sk", "");
        if (sk.length() > 0 && !"auth.getMobileSession".equals(p.get("method")))
            p.put("sk", sk);
        /* api_sig: md5 of name+value pairs sorted by name, then the secret */
        StringBuilder sig = new StringBuilder();
        for (Map.Entry<String, String> e : p.entrySet())
            sig.append(e.getKey()).append(e.getValue());
        sig.append(secret);
        p.put("api_sig", md5(sig.toString()));
        p.put("format", "json");

        StringBuilder body = new StringBuilder();
        for (Map.Entry<String, String> e : p.entrySet())
        {
            if (body.length() > 0) body.append('&');
            body.append(URLEncoder.encode(e.getKey(), "UTF-8")).append('=')
                .append(URLEncoder.encode(e.getValue(), "UTF-8"));
        }
        String api = sPrefs.getString("api", API_HTTPS);
        String resp;
        try {
            resp = post(api, body.toString());
        } catch (IOException tlsProblem) {
            /* very old TLS stacks: only if the user explicitly allowed it,
             * and never for the request that carries the password */
            if (!api.startsWith("https:") || !sPrefs.getBoolean("http", false)
                || "auth.getMobileSession".equals(p.get("method")))
                throw tlsProblem;
            resp = post("http:" + api.substring(6), body.toString());
        }
        JSONObject r = new JSONObject(resp);
        if (r.has("error"))
            throw new LastfmException(r.optInt("error"), "Last.fm: " + r.optString("message"));
        return r;
    }

    private static String post(String url, String body) throws IOException
    {
        return WheelNet.request("POST", url, body).body;
    }

    private static String md5(String s) throws Exception
    {
        byte[] d = MessageDigest.getInstance("MD5").digest(s.getBytes("UTF-8"));
        StringBuilder b = new StringBuilder();
        for (byte x : d) b.append(String.format("%02x", x & 0xff));
        return b.toString();
    }

    /* ----------- TLS 1.2 + modern roots on an Android 4.2 player ------------ */

    static synchronized SSLSocketFactory tls()
    {
        if (sTls != null)
            return sTls;
        try {
            TrustManagerFactory sysTmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
            sysTmf.init((KeyStore) null);
            final X509TrustManager sys = firstX509(sysTmf.getTrustManagers());

            KeyStore ks = KeyStore.getInstance(KeyStore.getDefaultType());
            ks.load(null, null);
            int n = 0;
            CertificateFactory cf = CertificateFactory.getInstance("X.509");
            n = addCerts(ks, cf, sCtx.getResources().openRawResource(R.raw.wheel_roots), n);
            File extra = new File(rbDir(), "cacerts");
            File[] files = extra.listFiles();
            if (files != null)
                for (File f : files)
                    n = addCerts(ks, cf, new FileInputStream(f), n);
            TrustManagerFactory extraTmf = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm());
            extraTmf.init(ks);
            final X509TrustManager more = firstX509(extraTmf.getTrustManagers());

            X509TrustManager both = new X509TrustManager() {
                public void checkClientTrusted(X509Certificate[] chain, String auth) throws CertificateException {
                    sys.checkClientTrusted(chain, auth);
                }
                public void checkServerTrusted(X509Certificate[] chain, String auth) throws CertificateException {
                    try {
                        sys.checkServerTrusted(chain, auth);
                    } catch (CertificateException e) {
                        more.checkServerTrusted(chain, auth);
                    }
                }
                public X509Certificate[] getAcceptedIssuers() {
                    X509Certificate[] a = sys.getAcceptedIssuers(), b = more.getAcceptedIssuers();
                    X509Certificate[] r = Arrays.copyOf(a, a.length + b.length);
                    System.arraycopy(b, 0, r, a.length, b.length);
                    return r;
                }
            };
            SSLContext ctx = SSLContext.getInstance("TLS");
            ctx.init(null, new TrustManager[] { both }, null);
            sTls = new Tls12Factory(ctx.getSocketFactory());
        } catch (Exception e) {
            Logger.e("WheelScrobbler TLS setup: " + e);
            sTls = new Tls12Factory(HttpsURLConnection.getDefaultSSLSocketFactory());
        }
        return sTls;
    }

    private static X509TrustManager firstX509(TrustManager[] tms)
    {
        for (TrustManager t : tms)
            if (t instanceof X509TrustManager)
                return (X509TrustManager) t;
        throw new IllegalStateException("no X509TrustManager");
    }

    private static int addCerts(KeyStore ks, CertificateFactory cf, InputStream in, int n)
    {
        try {
            Collection<? extends Certificate> certs = cf.generateCertificates(in);
            for (Certificate c : certs)
                ks.setCertificateEntry("wheel" + (n++), c);
        } catch (Exception e) {
            Logger.e("WheelScrobbler cert: " + e);
        } finally {
            try { in.close(); } catch (IOException e) { }
        }
        return n;
    }

    /* Android 4.1-4.4 support TLS 1.1/1.2 but don't enable them by default */
    static class Tls12Factory extends SSLSocketFactory
    {
        private final SSLSocketFactory d;
        Tls12Factory(SSLSocketFactory delegate) { d = delegate; }

        private Socket fix(Socket s)
        {
            if (s instanceof SSLSocket)
            {
                SSLSocket ss = (SSLSocket) s;
                Set<String> protos = new LinkedHashSet<String>();
                for (String p : ss.getSupportedProtocols())
                    if (p.equals("TLSv1.2") || p.equals("TLSv1.1") || p.equals("TLSv1"))
                        protos.add(p);
                if (!protos.isEmpty())
                    ss.setEnabledProtocols(protos.toArray(new String[protos.size()]));
                /* add the modern suites the stack knows but leaves disabled */
                Set<String> suites = new LinkedHashSet<String>();
                for (String c : ss.getSupportedCipherSuites())
                    if (c.contains("_GCM_") && (c.contains("ECDHE") || c.startsWith("TLS_RSA")))
                        suites.add(c);
                suites.addAll(Arrays.asList(ss.getEnabledCipherSuites()));
                ss.setEnabledCipherSuites(suites.toArray(new String[suites.size()]));
            }
            return s;
        }

        @Override public String[] getDefaultCipherSuites() { return d.getDefaultCipherSuites(); }
        @Override public String[] getSupportedCipherSuites() { return d.getSupportedCipherSuites(); }
        @Override public Socket createSocket(Socket s, String host, int port, boolean autoClose) throws IOException {
            return fix(d.createSocket(s, host, port, autoClose));
        }
        @Override public Socket createSocket(String host, int port) throws IOException {
            return fix(d.createSocket(host, port));
        }
        @Override public Socket createSocket(String host, int port, InetAddress la, int lp) throws IOException {
            return fix(d.createSocket(host, port, la, lp));
        }
        @Override public Socket createSocket(InetAddress host, int port) throws IOException {
            return fix(d.createSocket(host, port));
        }
        @Override public Socket createSocket(InetAddress a, int port, InetAddress la, int lp) throws IOException {
            return fix(d.createSocket(a, port, la, lp));
        }
    }
}
