/***************************************************************************
 * Y1 Wheel edition - fetch synced lyrics from LRCLIB (lrclib.net), a free,
 * open lyrics database, and save them as a .lrc next to the song so they
 * work offline from then on. Only runs when the user opens the lyrics view
 * for a song that has none.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 ****************************************************************************/
package org.rockbox.Helper;

import java.io.File;
import java.io.FileOutputStream;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.net.URLEncoder;


import org.json.JSONArray;
import org.json.JSONObject;

public class WheelLyrics
{
    private static final String BASE = "https://lrclib.net/api/";
    static String sBase = BASE; /* tests point this at a local server */
    private static volatile int sState = 0; /* 0 busy, 1 saved, -1 nothing */
    private static Thread sThread;

    public static boolean netAvailable()
    {
        return WheelNet.reachable(); /* online, or Wi-Fi can be switched on */
    }

    /* returns false right away when offline */
    public static synchronized boolean fetch(final String artist, final String title,
                                             final String album, final int durationSec,
                                             final String songPath)
    {
        if (!netAvailable() || title == null || title.length() == 0)
            return false;
        sState = 0;
        sThread = new Thread(new Runnable() { public void run() {
            boolean ok = false;
            try {
                if (WheelNet.acquire())
                    ok = fetchNow(artist, title, album, durationSec, songPath);
            } finally {
                WheelNet.release();
            }
            sState = ok ? 1 : -1;
        }}, "wheel-lyrics");
        sThread.start();
        return true;
    }

    public static int state() { return sState; }

    static boolean fetchNow(String artist, String title, String album, int dur, String songPath)
    {
        try {
            JSONObject best = null;
            String q = "get?track_name=" + enc(title) + "&artist_name=" + enc(artist == null ? "" : artist)
                     + (album != null && album.length() > 0 ? "&album_name=" + enc(album) : "")
                     + (dur > 0 ? "&duration=" + dur : "");
            String body = get(sBase + q);
            if (body != null)
                best = new JSONObject(body);
            if (best == null || (empty(best, "syncedLyrics") && empty(best, "plainLyrics")))
            {   /* fuzzy search, prefer synced results with a close duration */
                body = get(sBase + "search?track_name=" + enc(title)
                           + "&artist_name=" + enc(artist == null ? "" : artist));
                best = null;
                if (body != null)
                {
                    JSONArray arr = new JSONArray(body);
                    int bestScore = -1;
                    for (int i = 0; i < arr.length() && i < 10; i++)
                    {
                        JSONObject o = arr.getJSONObject(i);
                        int score = 0;
                        if (!empty(o, "syncedLyrics")) score += 4;
                        else if (!empty(o, "plainLyrics")) score += 1;
                        if (dur > 0 && Math.abs(o.optDouble("duration", 0) - dur) <= 3) score += 3;
                        if (score > bestScore) { bestScore = score; best = o; }
                    }
                }
            }
            if (best == null || best.optBoolean("instrumental", false))
                return false;
            String lrc = empty(best, "syncedLyrics") ? best.optString("plainLyrics", "")
                                                     : best.optString("syncedLyrics", "");
            if (lrc.trim().length() == 0)
                return false;
            int dot = songPath.lastIndexOf('.');
            File out = new File((dot > 0 ? songPath.substring(0, dot) : songPath) + ".lrc");
            Writer w = new OutputStreamWriter(new FileOutputStream(out), "UTF-8");
            w.write(lrc);
            if (!lrc.endsWith("\n")) w.write("\n");
            w.close();
            return true;
        } catch (Exception e) {
            Logger.e("WheelLyrics: " + e);
            return false;
        }
    }

    private static boolean empty(JSONObject o, String k)
    {
        return o.isNull(k) || o.optString(k, "").trim().length() == 0;
    }

    private static String enc(String s) throws Exception { return URLEncoder.encode(s, "UTF-8"); }

    private static String get(String url) throws Exception
    {
        WheelNet.Response r = WheelNet.request("GET", url, null);
        if (r.code >= 400)
            return null;
        return r.body;
    }
}
