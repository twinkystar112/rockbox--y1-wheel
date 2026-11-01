/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - publishes what Rockbox is playing to the rest of
 * Android: Bluetooth AVRCP / car displays (RemoteControlClient), the
 * standard "com.android.music.*" broadcasts (used by MediaTek's AVRCP
 * service and by third-party scrobbler apps), and our own Last.fm
 * scrobbler.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
package org.rockbox.Helper;

import android.annotation.TargetApi;
import android.app.PendingIntent;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.media.AudioManager;
import android.media.MediaMetadataRetriever;
import android.media.RemoteControlClient;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

public class WheelMedia
{
    public static final int STATE_STOPPED = 0;
    public static final int STATE_PLAYING = 1;
    public static final int STATE_PAUSED  = 2;

    private static Context sContext;
    private static Handler sMain;
    private static Object sRcc; /* RemoteControlClient, API 14+ */

    /* current track */
    private static String sTitle = "", sArtist = "", sAlbum = "", sAlbumArtist = "",
                          sPath = "", sMbid = "", sArt = null;
    private static int sTrackNum, sLengthMs;
    private static long sTrackId;
    private static long sStartedUtc;     /* unix seconds when the track started */
    private static int sState = STATE_STOPPED;
    /* real listening time (wall clock while playing), robust against seeking */
    private static long sPlayedMs;
    private static long sPlayingSince = -1;

    public static synchronized void init(Context c)
    {
        if (sContext != null)
            return;
        sContext = c.getApplicationContext();
        sMain = new Handler(Looper.getMainLooper());
        sMain.post(new Runnable() { public void run() { registerRcc(); } });
    }

    /* ----------------------------------------------------------------- */
    /* called from native code (apps/hosted/android/wheel_connect_android.c) */
    /* ----------------------------------------------------------------- */

    public static void onTrackStart(String title, String artist, String album,
                                    String albumArtist, int trackNum, int lengthMs,
                                    String path, String mbid, String albumArt)
    {
        synchronized (WheelMedia.class)
        {
            sTitle = nz(title); sArtist = nz(artist); sAlbum = nz(album);
            sAlbumArtist = nz(albumArtist); sTrackNum = trackNum;
            sLengthMs = lengthMs; sPath = nz(path); sMbid = nz(mbid);
            sArt = albumArt;
            sTrackId = (sPath.hashCode() & 0x7fffffffL);
            sStartedUtc = System.currentTimeMillis() / 1000L;
            sPlayedMs = 0;
            /* a new track always starts playing */
            sState = STATE_PLAYING;
            sPlayingSince = SystemClock.elapsedRealtime();
        }
        WheelScrobbler.onTrackStart(sArtist, sTitle, sAlbum, sAlbumArtist,
                                    sTrackNum, sLengthMs, sMbid);
        publish(true);
    }

    /* elapsedMs is Rockbox's position when the track ended,
     * autoSkip = the track ran to its end (not skipped by the user) */
    public static void onTrackEnd(int elapsedMs, int lengthMs, boolean autoSkip)
    {
        long played;
        String artist, title, album, albumArtist, mbid;
        int trackNum, length;
        long started;
        synchronized (WheelMedia.class)
        {
            if (sPlayingSince >= 0)
            {
                sPlayedMs += SystemClock.elapsedRealtime() - sPlayingSince;
                sPlayingSince = SystemClock.elapsedRealtime();
            }
            /* gapless transitions keep the clock running; never claim more
             * listening than the position Rockbox reports + a little slack */
            played = Math.min(sPlayedMs, (long)elapsedMs + 2000);
            if (played <= 0)
                played = elapsedMs;
            artist = sArtist; title = sTitle; album = sAlbum;
            albumArtist = sAlbumArtist; mbid = sMbid; trackNum = sTrackNum;
            length = lengthMs > 0 ? lengthMs : sLengthMs;
            started = sStartedUtc;
        }
        if (title.length() > 0)
            WheelScrobbler.onTrackEnd(artist, title, album, albumArtist, trackNum,
                                      length, mbid, started, played, autoSkip);
    }

    /* called from RockboxPCM when the audio sink starts/pauses/stops */
    public static void onPlayState(int state)
    {
        synchronized (WheelMedia.class)
        {
            if (state == sState)
                return;
            long now = SystemClock.elapsedRealtime();
            if (sState == STATE_PLAYING && sPlayingSince >= 0)
                sPlayedMs += now - sPlayingSince;
            sPlayingSince = (state == STATE_PLAYING) ? now : -1;
            sState = state;
        }
        WheelBluetooth.onPlayStateChanged(state == STATE_PLAYING);
        publish(false);
    }

    /* {artist, title} of the track playing now, for "love" */
    public static synchronized String[] currentArtistTitle()
    {
        if (sTitle.length() == 0)
            return null;
        return new String[] { sArtist, sTitle };
    }

    public static synchronized boolean isPlaying()
    {
        return sState == STATE_PLAYING;
    }

    /* ----------------------------------------------------------------- */

    private static String nz(String s) { return s == null ? "" : s; }

    private static void publish(final boolean trackChanged)
    {
        if (sContext == null)
            return;
        final String title, artist, album, albumArtist, art;
        final int length, trackNum, state;
        final long id;
        synchronized (WheelMedia.class)
        {
            title = sTitle; artist = sArtist; album = sAlbum; albumArtist = sAlbumArtist;
            art = sArt; length = sLengthMs; trackNum = sTrackNum; state = sState; id = sTrackId;
        }
        /* standard broadcasts: MediaTek AVRCP + scrobbler apps listen to these */
        try {
            Intent i = new Intent(trackChanged ? "com.android.music.metachanged"
                                               : "com.android.music.playstatechanged");
            i.putExtra("id", id);
            i.putExtra("artist", artist);
            i.putExtra("album", album);
            i.putExtra("track", title);
            i.putExtra("duration", (long) length);
            i.putExtra("position", 0L);
            i.putExtra("playing", state == STATE_PLAYING);
            i.putExtra("playstate", state == STATE_PLAYING);
            i.putExtra("ListSize", 1L);
            sContext.sendBroadcast(i);
        } catch (Throwable t) {
            Logger.e("WheelMedia broadcast: " + t);
        }
        sMain.post(new Runnable() {
            public void run() { updateRcc(trackChanged, title, artist, album,
                                          albumArtist, trackNum, length, art, state); }
        });
    }

    @TargetApi(14)
    private static void registerRcc()
    {
        if (Build.VERSION.SDK_INT < 14 || sRcc != null)
            return;
        try {
            AudioManager am = (AudioManager) sContext.getSystemService(Context.AUDIO_SERVICE);
            ComponentName rec = new ComponentName(sContext.getPackageName(),
                    MediaButtonReceiver.MediaReceiver.class.getName());
            Intent mediaButtonIntent = new Intent(Intent.ACTION_MEDIA_BUTTON);
            mediaButtonIntent.setComponent(rec);
            PendingIntent pi = PendingIntent.getBroadcast(sContext, 0, mediaButtonIntent, 0);
            RemoteControlClient rcc = new RemoteControlClient(pi);
            rcc.setTransportControlFlags(RemoteControlClient.FLAG_KEY_MEDIA_PLAY
                    | RemoteControlClient.FLAG_KEY_MEDIA_PAUSE
                    | RemoteControlClient.FLAG_KEY_MEDIA_PLAY_PAUSE
                    | RemoteControlClient.FLAG_KEY_MEDIA_NEXT
                    | RemoteControlClient.FLAG_KEY_MEDIA_PREVIOUS
                    | RemoteControlClient.FLAG_KEY_MEDIA_STOP);
            am.registerMediaButtonEventReceiver(rec);
            am.registerRemoteControlClient(rcc);
            sRcc = rcc;
        } catch (Throwable t) {
            Logger.e("WheelMedia RemoteControlClient: " + t);
        }
    }

    @TargetApi(14)
    private static void updateRcc(boolean trackChanged, String title, String artist,
                                  String album, String albumArtist, int trackNum,
                                  int length, String art, int state)
    {
        if (!(sRcc instanceof RemoteControlClient))
            return;
        RemoteControlClient rcc = (RemoteControlClient) sRcc;
        try {
            if (trackChanged)
            {
                RemoteControlClient.MetadataEditor ed = rcc.editMetadata(true);
                ed.putString(MediaMetadataRetriever.METADATA_KEY_TITLE, title);
                ed.putString(MediaMetadataRetriever.METADATA_KEY_ARTIST, artist);
                ed.putString(MediaMetadataRetriever.METADATA_KEY_ALBUM, album);
                ed.putString(MediaMetadataRetriever.METADATA_KEY_ALBUMARTIST,
                             albumArtist.length() > 0 ? albumArtist : artist);
                ed.putLong(MediaMetadataRetriever.METADATA_KEY_CD_TRACK_NUMBER, trackNum);
                ed.putLong(MediaMetadataRetriever.METADATA_KEY_DURATION, length);
                if (art != null)
                {
                    BitmapFactory.Options o = new BitmapFactory.Options();
                    o.inSampleSize = 2; /* keep the binder transaction small */
                    Bitmap b = BitmapFactory.decodeFile(art, o);
                    if (b != null)
                        ed.putBitmap(RemoteControlClient.MetadataEditor.BITMAP_KEY_ARTWORK, b);
                }
                ed.apply();
            }
            rcc.setPlaybackState(state == STATE_PLAYING ? RemoteControlClient.PLAYSTATE_PLAYING
                               : state == STATE_PAUSED ? RemoteControlClient.PLAYSTATE_PAUSED
                               : RemoteControlClient.PLAYSTATE_STOPPED);
        } catch (Throwable t) {
            Logger.e("WheelMedia metadata: " + t);
        }
    }
}
