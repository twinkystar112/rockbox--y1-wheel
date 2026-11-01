/* Y1 Wheel edition - JVM tests for the Last.fm scrobbler, the lyrics
 * download and the Bluetooth (OBEX) sender, against mock_services.py.
 * Run with utils/wheel/tests/run.sh. GPLv2+ */
package org.rockbox.Helper;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStreamReader;
import java.io.OutputStreamWriter;
import java.io.Writer;
import java.net.HttpURLConnection;
import java.net.Socket;
import java.net.URL;
import java.security.MessageDigest;
import java.util.ArrayList;
import java.util.List;
import java.util.Random;

import org.json.JSONArray;
import org.json.JSONObject;

import android.content.Context;
import android.net.ConnectivityManager;

public class WheelTests
{
    static int httpPort, obexPort;
    static File sd;
    static int passed = 0, failed = 0;

    static void check(boolean ok, String what)
    {
        System.out.println((ok ? "  PASS  " : "  FAIL  ") + what);
        if (ok) passed++; else failed++;
    }

    static String http(String path) throws Exception
    {
        HttpURLConnection c = (HttpURLConnection) new URL("http://127.0.0.1:" + httpPort + path).openConnection();
        BufferedReader r = new BufferedReader(new InputStreamReader(c.getInputStream(), "UTF-8"));
        StringBuilder b = new StringBuilder();
        String l;
        while ((l = r.readLine()) != null) b.append(l);
        r.close();
        return b.toString();
    }

    static JSONArray calls(String method) throws Exception
    {
        JSONArray all = new JSONObject(http("/control/log")).getJSONArray("calls");
        JSONArray out = new JSONArray();
        for (int i = 0; i < all.length(); i++)
            if (method == null || method.equals(all.getJSONObject(i).optString("method")))
                out.put(all.getJSONObject(i));
        return out;
    }

    static String[] status() { return WheelScrobbler.getStatus().split("\t", -1); }
    static int pending() { return Integer.parseInt(status()[5]); }
    static boolean signedIn() { return "1".equals(status()[3]); }

    interface Cond { boolean ok() throws Exception; }
    static boolean waitFor(Cond c, long ms) throws Exception
    {
        long end = System.currentTimeMillis() + ms;
        while (System.currentTimeMillis() < end)
        {
            if (c.ok()) return true;
            Thread.sleep(40);
        }
        return c.ok();
    }

    static List<String> lines(File f) throws Exception
    {
        List<String> out = new ArrayList<String>();
        if (!f.exists()) return out;
        BufferedReader r = new BufferedReader(new InputStreamReader(new FileInputStream(f), "UTF-8"));
        String l;
        while ((l = r.readLine()) != null) out.add(l);
        r.close();
        return out;
    }

    static void write(File f, String s) throws Exception
    {
        f.getParentFile().mkdirs();
        Writer w = new OutputStreamWriter(new FileOutputStream(f), "UTF-8");
        w.write(s);
        w.close();
    }

    static long now() { return System.currentTimeMillis() / 1000L; }

    static void end(String artist, String title, int lengthMs, long playedMs)
    {
        WheelScrobbler.onTrackEnd(artist, title, "Album", "", 3, lengthMs, "",
                                  now() - playedMs / 1000, playedMs, true);
    }

    /* ------------------------------ Last.fm ------------------------------ */

    static void testScrobbler() throws Exception
    {
        System.out.println("Last.fm scrobbling");
        File rb = new File(sd, ".rockbox");
        write(new File(rb, "lastfm.cfg"),
              "api_key=testkey\napi_secret=testsecret\nusername=natalia\npassword=s3cret\n"
            + "api_url=http://127.0.0.1:" + httpPort + "/2.0/\n");
        Context ctx = new Context();
        WheelScrobbler.init(ctx);
        check(waitFor(new Cond() { public boolean ok() { return signedIn(); } }, 5000),
              "signs in with lastfm.cfg (auth.getMobileSession, api_sig accepted)");
        boolean pwGone = true;
        for (String l : lines(new File(rb, "lastfm.cfg")))
            if (l.trim().startsWith("password")) pwGone = false;
        check(pwGone, "password removed from lastfm.cfg after sign-in");
        check("natalia".equals(status()[4]), "user name shown");

        WheelScrobbler.onTrackStart("Artist A", "Song A", "Album", "", 1, 200000, "");
        check(waitFor(new Cond() { public boolean ok() throws Exception {
            return calls("track.updateNowPlaying").length() == 1; } }, 3000), "now playing sent");

        /* the Last.fm rules: > 30 s long, played half or 4 minutes */
        end("Artist A", "Too short", 25000, 25000);
        end("Artist A", "Skipped early", 200000, 90000);
        end("Artist A", "Half played", 200000, 100000);
        end("Artist B", "Long one, 4 min", 600000, 241000);
        end("", "No artist", 200000, 200000);
        check(waitFor(new Cond() { public boolean ok() throws Exception {
            return calls("track.scrobble").length() >= 1 && pending() == 0; } }, 5000),
              "queue flushed");
        JSONArray sc = calls("track.scrobble");
        int n = 0;
        List<String> titles = new ArrayList<String>();
        for (int i = 0; i < sc.length(); i++)
            for (int k = 0; sc.getJSONObject(i).has("track[" + k + "]"); k++, n++)
                titles.add(sc.getJSONObject(i).getString("track[" + k + "]"));
        check(n == 2 && titles.contains("Half played") && titles.contains("Long one, 4 min"),
              "only listens that count are scrobbled (got " + titles + ")");
        check(sc.getJSONObject(0).has("timestamp[0]") && sc.getJSONObject(0).has("duration[0]"),
              "timestamp and duration sent");

        List<String> log = lines(new File(sd, ".scrobbler-wheel.log"));
        check(log.size() == 8 && log.get(0).equals("#AUDIOSCROBBLER/1.1") && log.get(1).equals("#TZ/UTC"),
              ".scrobbler-wheel.log has the 1.1 header (UTC) and every play");
        int l = 0, s = 0;
        for (String x : log) { if (x.contains("\tL\t")) l++; if (x.contains("\tS\t")) s++; }
        check(l == 3 && s == 2, ".scrobbler-wheel.log marks listened (L) / skipped (S)");

        /* offline for a while: everything waits in the queue file */
        ConnectivityManager.online = false;
        for (int i = 0; i < 120; i++)
            end("Offline Artist", "Song " + i, 180000, 180000);
        check(waitFor(new Cond() { public boolean ok() { return pending() == 120; } }, 5000),
              "120 plays queued while offline");
        check(lines(new File(rb, ".lastfm-queue.tsv")).size() == 120, "queue kept on the SD card");
        int before = calls("track.scrobble").length();
        ConnectivityManager.online = true;
        WheelScrobbler.flush();
        check(waitFor(new Cond() { public boolean ok() { return pending() == 0; } }, 8000),
              "queue sent when back online");
        sc = calls("track.scrobble");
        List<Integer> sizes = new ArrayList<Integer>();
        for (int i = before; i < sc.length(); i++)
        {
            int k = 0;
            while (sc.getJSONObject(i).has("artist[" + k + "]")) k++;
            sizes.add(k);
        }
        check(sizes.toString().equals("[50, 50, 20]"), "sent in batches of 50 (got " + sizes + ")");
        check(lines(new File(rb, ".lastfm-queue.tsv")).size() == 0, "queue file emptied");

        /* a temporary Last.fm error keeps the play and retries */
        http("/control/fail_scrobble_once");
        end("Artist C", "Retry me", 200000, 200000);
        check(waitFor(new Cond() { public boolean ok() { return status()[9].length() > 0; } }, 4000)
              && pending() == 1, "temporary error: play kept in the queue");
        WheelScrobbler.flush();
        check(waitFor(new Cond() { public boolean ok() { return pending() == 0; } }, 4000)
              && status()[9].length() == 0, "sent on retry, error cleared");

        /* love */
        WheelMedia.now = new String[] { "Artist D", "Loved Song" };
        check(WheelScrobbler.loveCurrent(), "love accepted");
        check(waitFor(new Cond() { public boolean ok() throws Exception {
            return calls("track.love").length() == 1
                && calls("track.love").getJSONObject(0).optString("track").equals("Loved Song"); } }, 4000),
              "track.love sent");

        /* the session is revoked on the website: sign out, keep the plays */
        http("/control/expire_session");
        end("Artist E", "After revoke", 200000, 200000);
        check(waitFor(new Cond() { public boolean ok() { return !signedIn(); } }, 4000),
              "invalid session (error 9): signed out");
        check(pending() == 1, "...and the play is still queued");

        /* sign in again with the phone (QR code flow) */
        check(WheelScrobbler.webAuthStart(), "QR sign-in starts");
        check(waitFor(new Cond() { public boolean ok() { return WheelScrobbler.webAuthUrl().length() > 0; } }, 3000)
              && WheelScrobbler.webAuthUrl().startsWith("https://www.last.fm/api/auth/?api_key=testkey&token=TOKEN-123"),
              "QR shows the Last.fm authorise URL (" + WheelScrobbler.webAuthUrl() + ")");
        Thread.sleep(400);
        check(WheelScrobbler.webAuthState() == 0, "waits while not authorised (error 14)");
        http("/control/approve_token");
        check(waitFor(new Cond() { public boolean ok() { return WheelScrobbler.webAuthState() == 1; } }, 4000)
              && signedIn(), "signed in once approved on the phone");
        check(waitFor(new Cond() { public boolean ok() { return pending() == 0; } }, 4000),
              "the waiting play is sent with the new session");

        JSONArray all = calls(null);
        boolean jsonFmt = true;
        for (int i = 0; i < all.length(); i++)
            if (!"json".equals(all.getJSONObject(i).optString("format"))) jsonFmt = false;
        check(jsonFmt && status()[9].length() == 0, "every call signed and answered without errors");
    }

    /* ------------------------------ lyrics ------------------------------ */

    static void testLyrics() throws Exception
    {
        System.out.println("Lyrics (LRCLIB API)");
        WheelLyrics.sBase = "http://127.0.0.1:" + httpPort + "/api/";
        File dir = new File(sd, "Music/Test");
        dir.mkdirs();
        File song = new File(dir, "01 Test Song.mp3");
        check(WheelLyrics.fetchNow("Test Artist", "Test Song", "Tests", 180, song.getPath()),
              "synced lyrics found");
        List<String> lrc = lines(new File(dir, "01 Test Song.lrc"));
        check(lrc.size() == 2 && lrc.get(0).startsWith("[00:01.00]"), "saved as .lrc next to the song");
        File p = new File(dir, "02 Plain.flac");
        check(WheelLyrics.fetchNow("Plain Artist", "Plain Song", "", 120, p.getPath())
              && lines(new File(dir, "02 Plain.lrc")).get(0).equals("only plain words"),
              "plain lyrics when there are no synced ones");
        File q = new File(dir, "03 Other.mp3");
        check(WheelLyrics.fetchNow("Someone Else", "Test Song", "", 181, q.getPath()),
              "search fallback (fuzzy match by title and duration)");
        File none = new File(dir, "04 Nothing.mp3");
        check(!WheelLyrics.fetchNow("Nobody", "Unknown", "", 100, none.getPath())
              && !new File(dir, "04 Nothing.lrc").exists(), "nothing found: no file written");
    }

    static boolean scrobbled(String title) throws Exception
    {
        JSONArray sc = calls("track.scrobble");
        for (int i = 0; i < sc.length(); i++)
            for (int k = 0; sc.getJSONObject(i).has("track[" + k + "]"); k++)
                if (sc.getJSONObject(i).getString("track[" + k + "]").equals(title))
                    return true;
        return false;
    }

    /* ------------------------- Wi-Fi on demand (Y1) ------------------------- */

    static void testWifiOnDemand() throws Exception
    {
        System.out.println("Wi-Fi on demand (Rockbox-Y1 wifi.cfg)");
        File rb = new File(sd, ".rockbox");
        File cfg = new File(rb, "wifi.cfg");
        check(!WheelNet.wifiConfigured(), "no wifi.cfg: Wi-Fi never touched");
        write(cfg, "# Rockbox-Y1 network settings\n"
                 + "wifi-ssid: \"Casa 2.4\"\nwifi-password: hunter2\n"
                 + "scrobble-protocol: lastfm\nscrobble-user: YOUR_USERNAME\n"
                 + "scrobble-apiKey: wifikey\nscrobble-sharedSecret: wifisecret\n");
        java.util.Map<String, String> w = WheelNet.wifiCfg();
        check("Casa 2.4".equals(w.get("wifi-ssid")) && "hunter2".equals(w.get("wifi-password"))
              && !w.containsKey("scrobble-user") && "wifikey".equals(w.get("scrobble-apikey")),
              "wifi.cfg parsed (quotes, case, YOUR_ placeholders ignored)");
        java.util.Map<String, String> c = WheelScrobbler.readConfig();
        check("testkey".equals(c.get("api_key")) && "natalia".equals(c.get("username")),
              "lastfm.cfg wins over wifi.cfg");
        File lf = new File(rb, "lastfm.cfg");
        File lfBak = new File(rb, "lastfm.cfg.bak");
        lf.renameTo(lfBak);
        c = WheelScrobbler.readConfig();
        check("wifikey".equals(c.get("api_key")) && "wifisecret".equals(c.get("api_secret")),
              "Last.fm key/secret taken from wifi.cfg (scrobble-apiKey / scrobble-sharedSecret)");
        lfBak.renameTo(lf);

        /* offline, Wi-Fi configured: plays wait, "Send now" switches Wi-Fi on and off */
        Connectivity.calls.clear();
        ConnectivityManager.online = false;
        end("Wifi Artist", "Queued offline", 200000, 200000);
        check(waitFor(new Cond() { public boolean ok() { return pending() == 1; } }, 3000)
              && Connectivity.calls.isEmpty(), "offline: play queued, Wi-Fi left alone");
        check(WheelScrobbler.flush(), "Send now accepted while offline (Wi-Fi configured)");
        check(waitFor(new Cond() { public boolean ok() { return pending() == 0; } }, 5000),
              "Wi-Fi switched on, queue sent");
        check(waitFor(new Cond() { public boolean ok() {
                  return Connectivity.calls.size() == 2 && !ConnectivityManager.online; } }, 3000)
              && Connectivity.calls.get(0).equals("connect Casa 2.4 hunter2")
              && Connectivity.calls.get(1).equals("disconnect"),
              "...and switched off again afterwards " + Connectivity.calls);

        /* lyrics: the same, from the lyrics view */
        Connectivity.calls.clear();
        File dir = new File(sd, "Music/Wifi");
        dir.mkdirs();
        check(WheelLyrics.netAvailable(), "lyrics available offline when Wi-Fi is configured");
        check(WheelLyrics.fetch("Test Artist", "Test Song", "Tests", 180, new File(dir, "a.mp3").getPath()),
              "lyrics fetch starts");
        check(waitFor(new Cond() { public boolean ok() { return WheelLyrics.state() == 1; } }, 5000)
              && new File(dir, "a.lrc").exists(), "lyrics downloaded over Wi-Fi on demand");
        check(waitFor(new Cond() { public boolean ok() { return !ConnectivityManager.online; } }, 3000)
              && Connectivity.calls.size() == 2, "Wi-Fi off again after the lyrics " + Connectivity.calls);

        /* already online (e.g. connected by the user): never switched off by us */
        Connectivity.calls.clear();
        ConnectivityManager.online = true;
        end("Wifi Artist", "Online already", 200000, 200000);
        WheelScrobbler.flush();
        check(waitFor(new Cond() { public boolean ok() throws Exception {
                  return scrobbled("Online already") && pending() == 0; } }, 4000)
              && Connectivity.calls.isEmpty() && ConnectivityManager.online,
              "already online: Wi-Fi state untouched");

        /* the network does not come up: the play stays queued */
        Connectivity.works = false;
        ConnectivityManager.online = false;
        end("Wifi Artist", "No network", 200000, 200000);
        waitFor(new Cond() { public boolean ok() { return pending() == 1; } }, 3000);
        WheelScrobbler.flush();
        Thread.sleep(500);
        check(pending() == 1 && !ConnectivityManager.online, "Wi-Fi failed: play kept for later (pending " + pending() + ", online " + ConnectivityManager.online + ", " + Connectivity.calls + ")");
        Connectivity.works = true;
        ConnectivityManager.online = true;
        WheelScrobbler.flush();
        waitFor(new Cond() { public boolean ok() { return pending() == 0; } }, 4000);
        cfg.delete();
    }

    /* --------------------------- Bluetooth send --------------------------- */

    static String md5(File f) throws Exception
    {
        MessageDigest d = MessageDigest.getInstance("MD5");
        FileInputStream in = new FileInputStream(f);
        byte[] b = new byte[8192];
        int n;
        while ((n = in.read(b)) > 0) d.update(b, 0, n);
        in.close();
        StringBuilder s = new StringBuilder();
        for (byte x : d.digest()) s.append(String.format("%02x", x & 0xff));
        return s.toString();
    }

    static void testSend() throws Exception
    {
        System.out.println("Send over Bluetooth (OBEX Object Push)");
        File dir = new File(sd, "Music/Send");
        dir.mkdirs();
        File a = new File(dir, "Canción ñ 01.mp3"), b = new File(dir, "Canción ñ 01.lrc");
        byte[] data = new byte[150000];
        new Random(1).nextBytes(data);
        FileOutputStream o = new FileOutputStream(a); o.write(data); o.close();
        write(b, "[00:01.00] hola\n");
        http("/control/reset");
        Socket s = new Socket("127.0.0.1", obexPort);
        int r = WheelSend.sendForTest(s.getInputStream(), s.getOutputStream(),
                                      new String[] { a.getPath(), b.getPath() });
        s.close();
        check(r == WheelSend.DONE, "both files sent");
        JSONArray got = new JSONObject(http("/control/log")).getJSONArray("obex");
        check(got.length() == 2, "receiver got 2 files");
        if (got.length() == 2)
        {
            JSONObject g = got.getJSONObject(0);
            check(g.getString("name").equals(a.getName()), "file name kept (UTF-16, accents): " + g.getString("name"));
            check(g.getString("type").equals("audio/mpeg"), "MIME type audio/mpeg");
            check(g.getLong("length") == data.length && g.getString("md5").equals(md5(a)),
                  "content identical (" + g.getLong("received") + " bytes in small packets)");
            check(got.getJSONObject(1).getString("md5").equals(md5(b)), "second file identical");
        }
        http("/control/obex_reject?on=1");
        s = new Socket("127.0.0.1", obexPort);
        r = WheelSend.sendForTest(s.getInputStream(), s.getOutputStream(), new String[] { a.getPath() });
        s.close();
        http("/control/obex_reject?on=0");
        check(r == WheelSend.REJECTED, "declined on the phone: reported as rejected");
    }

    public static void main(String[] args) throws Exception
    {
        httpPort = Integer.parseInt(args[0]);
        obexPort = Integer.parseInt(args[1]);
        sd = new File(System.getProperty("wheel.sdcard"));
        testScrobbler();
        testLyrics();
        testWifiOnDemand();
        testSend();
        System.out.println(passed + " passed, " + failed + " failed");
        System.exit(failed == 0 ? 0 : 1);
    }
}
