/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 *
 * Y1 Wheel edition - Bluetooth audio, driven from Rockbox's own menus.
 *
 *  - lists paired devices with their live A2DP state
 *  - connect / disconnect / forget / scan / pair without leaving Rockbox
 *  - remembers the last headphones and reconnects automatically
 *    (on start, when Bluetooth turns on, when the screen wakes and with
 *    back-off after an unexpected drop)
 *  - pauses playback when the headphones drop, optionally resumes
 *
 * Everything the native UI calls is non-blocking: state is cached by
 * broadcast receivers on the main thread.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 ****************************************************************************/
package org.rockbox.Helper;

import java.lang.reflect.Method;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.Set;

import android.bluetooth.BluetoothA2dp;
import android.bluetooth.BluetoothAdapter;
import android.bluetooth.BluetoothClass;
import android.bluetooth.BluetoothDevice;
import android.bluetooth.BluetoothProfile;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.content.SharedPreferences;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;

public class WheelBluetooth
{
    /* must match enum wheel_bt_state in apps/wheel/wheel_connect.h */
    static final int ST_PAIRED = 0, ST_CONNECTING = 1, ST_CONNECTED = 2,
                     ST_FOUND = 3, ST_PAIRING = 4;
    /* must match enum wheel_bt_option */
    static final int OPT_AUTORECONNECT = 0, OPT_PAUSE_ON_LOSS = 1, OPT_RESUME_ON_CONNECT = 2;

    private static final String PREFS = "wheel_bluetooth";
    private static final long[] RETRY_DELAYS_MS = { 4000, 12000, 30000, 60000 };
    private static final long RESUME_WINDOW_MS = 10 * 60 * 1000;
    private static final long SCAN_TIME_MS = 15000;
    /* a connection still "connecting" after this is stuck (Android 4.2 can
     * leave it so for ever): drop it and start again, as the user would */
    private static final long STUCK_MS = 12000;
    private static final int MAX_UNSTICK = 2;
    /* headphones that came in on their own (the link is up) but whose
     * audio didn't follow: ask for the audio after this */
    private static final long ACL_AUDIO_WAIT_MS = 3500;

    private static Context sCtx;
    private static Handler sMain;
    private static BluetoothAdapter sAdapter;
    private static volatile BluetoothA2dp sA2dp;
    private static SharedPreferences sPrefs;

    private static final Map<String, Integer> sStates = new LinkedHashMap<String, Integer>();
    private static final Map<String, String[]> sFound = new LinkedHashMap<String, String[]>();
    private static volatile boolean sScanning;
    private static String sPendingConnect;      /* connect once bonded */
    private static int sRetry;
    private static boolean sWasPlaying;         /* playback state as last reported */
    private static long sPausedByLossAt = -1;   /* we paused because audio dropped */
    private static String sLastConnected = "";
    private static String sWatch;               /* device a connect is under way to */
    private static int sUnstick;

    /* implemented in apps/hosted/android/wheel_connect_android.c */
    private static native void nativeSinkLost();
    private static native void nativeSinkBack();
    private static native void nativeAudioState(boolean connected);

    /* the native library may not be loaded yet when early broadcasts arrive */
    private static void pushAudioState()
    {
        boolean on = false;
        try {
            BluetoothA2dp a = sA2dp;
            on = a != null && !a.getConnectedDevices().isEmpty();
        } catch (Throwable t) { }
        try { nativeAudioState(on); } catch (Throwable t) { }
    }

    public static synchronized void init(Context c)
    {
        if (sCtx != null)
            return;
        sCtx = c.getApplicationContext();
        sMain = new Handler(Looper.getMainLooper());
        sPrefs = sCtx.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        try {
            sAdapter = BluetoothAdapter.getDefaultAdapter();
        } catch (Throwable t) {
            sAdapter = null;
        }
        if (sAdapter == null)
            return;

        IntentFilter f = new IntentFilter();
        f.addAction(BluetoothAdapter.ACTION_STATE_CHANGED);
        f.addAction(BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED);
        f.addAction(BluetoothDevice.ACTION_BOND_STATE_CHANGED);
        f.addAction(BluetoothDevice.ACTION_FOUND);
        f.addAction(BluetoothDevice.ACTION_NAME_CHANGED);
        f.addAction(BluetoothAdapter.ACTION_DISCOVERY_FINISHED);
        f.addAction(BluetoothDevice.ACTION_ACL_CONNECTED);
        f.addAction("android.bluetooth.device.action.PAIRING_REQUEST");
        f.addAction(Intent.ACTION_SCREEN_ON);
        f.setPriority(IntentFilter.SYSTEM_HIGH_PRIORITY - 1);
        sCtx.registerReceiver(sReceiver, f);
        bindA2dp();
        if (sPrefs.getString("tether", "").length() > 0)
            bindPan(); /* reconnect to the phone's internet used last time */
    }

    /* ------------------------- native API ---------------------------- */

    public static boolean available() { return sAdapter != null; }

    public static boolean isEnabled()
    {
        return sAdapter != null && sAdapter.isEnabled();
    }

    public static void setEnabled(boolean on)
    {
        if (sAdapter == null)
            return;
        try {
            if (on) sAdapter.enable(); else sAdapter.disable();
        } catch (Throwable t) {
            Logger.e("WheelBluetooth enable: " + t);
        }
    }

    /* "state|audio|addr|name", connected device first */
    public static synchronized String[] getDevices()
    {
        List<String> out = new ArrayList<String>();
        if (!isEnabled())
            return new String[0];
        Set<BluetoothDevice> bonded;
        try {
            bonded = sAdapter.getBondedDevices();
        } catch (Throwable t) {
            return new String[0];
        }
        if (bonded == null)
            return new String[0];
        List<String> rest = new ArrayList<String>();
        for (BluetoothDevice d : bonded)
        {
            int st = stateOf(d);
            String line = st + "|" + (isAudio(d) ? 1 : 0) + "|" + d.getAddress() + "|" + nameOf(d);
            if (st == ST_CONNECTED) out.add(line); else rest.add(line);
        }
        out.addAll(rest);
        return out.toArray(new String[out.size()]);
    }

    public static boolean connect(String addr)
    {
        BluetoothDevice d = device(addr);
        if (d == null || !isEnabled())
            return false;
        if (d.getBondState() != BluetoothDevice.BOND_BONDED)
            return pair(addr);
        rememberLast(addr);
        sRetry = 0;
        sUnstick = 0;
        return a2dpConnect(d);
    }

    public static boolean disconnect(String addr)
    {
        BluetoothDevice d = device(addr);
        if (d == null || sA2dp == null)
            return false;
        /* a deliberate disconnect must not trigger auto-reconnect */
        synchronized (WheelBluetooth.class) { sLastConnected = ""; }
        sMain.removeCallbacks(sReconnect);
        return invoke(sA2dp, "disconnect", d);
    }

    public static boolean forget(String addr)
    {
        BluetoothDevice d = device(addr);
        if (d == null)
            return false;
        if (addr.equals(sPrefs.getString("last", "")))
            sPrefs.edit().remove("last").commit();
        disconnect(addr);
        return invoke(d, "removeBond");
    }

    public static synchronized boolean startScan()
    {
        if (!isEnabled())
            return false;
        sFound.clear();
        try {
            if (sAdapter.isDiscovering())
                sAdapter.cancelDiscovery();
            sScanning = sAdapter.startDiscovery();
        } catch (Throwable t) {
            sScanning = false;
        }
        sMain.removeCallbacks(sStopScan);
        sMain.postDelayed(sStopScan, SCAN_TIME_MS);
        return sScanning;
    }

    public static void stopScan()
    {
        sScanning = false;
        try {
            if (sAdapter != null && sAdapter.isDiscovering())
                sAdapter.cancelDiscovery();
        } catch (Throwable t) { }
    }

    public static boolean isScanning() { return sScanning; }

    /* devices found by the scan, audio devices first: "state|audio|addr|name" */
    public static synchronized String[] getScanResults()
    {
        List<String> audio = new ArrayList<String>(), other = new ArrayList<String>();
        for (Map.Entry<String, String[]> e : sFound.entrySet())
        {
            String[] v = e.getValue(); /* {name, audio} */
            String line = ST_FOUND + "|" + v[1] + "|" + e.getKey() + "|" + v[0];
            if ("1".equals(v[1])) audio.add(line); else other.add(line);
        }
        audio.addAll(other);
        return audio.toArray(new String[audio.size()]);
    }

    public static boolean pair(String addr)
    {
        BluetoothDevice d = device(addr);
        if (d == null)
            return false;
        stopScan(); /* discovery slows pairing down a lot */
        synchronized (WheelBluetooth.class) { sPendingConnect = addr; }
        if (d.getBondState() == BluetoothDevice.BOND_BONDED)
            return connect(addr);
        return invoke(d, "createBond");
    }

    public static boolean getOption(int opt)
    {
        switch (opt)
        {
            case OPT_AUTORECONNECT:     return sPrefs.getBoolean("auto", true);
            case OPT_PAUSE_ON_LOSS:     return sPrefs.getBoolean("pause", true);
            case OPT_RESUME_ON_CONNECT: return sPrefs.getBoolean("resume", false);
        }
        return false;
    }

    public static void setOption(int opt, boolean v)
    {
        String key = opt == OPT_AUTORECONNECT ? "auto" : opt == OPT_PAUSE_ON_LOSS ? "pause" : "resume";
        sPrefs.edit().putBoolean(key, v).commit();
        if (opt == OPT_AUTORECONNECT && v)
            scheduleReconnect(500);
    }

    public static void openSystemSettings()
    {
        try {
            Intent i = new Intent(android.provider.Settings.ACTION_BLUETOOTH_SETTINGS);
            i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
            sCtx.startActivity(i);
        } catch (Throwable t) {
            Logger.e("WheelBluetooth settings: " + t);
        }
    }

    public static synchronized String connectedName()
    {
        if (sA2dp == null)
            return "";
        try {
            for (BluetoothDevice d : sA2dp.getConnectedDevices())
                return nameOf(d);
        } catch (Throwable t) { }
        return "";
    }

    /* from WheelMedia */
    static void onPlayStateChanged(boolean playing)
    {
        synchronized (WheelBluetooth.class)
        {
            sWasPlaying = playing;
            if (playing)
                sPausedByLossAt = -1;
        }
    }

    /* ---------------- internet through the phone (PAN) ---------------- */
    /* The phone shares its connection with "Bluetooth tethering"; the player
     * joins as a PAN user. BluetoothPan (profile 5) and its connect() are
     * hidden on Android 4.2 but present. */
    private static final int PROFILE_PAN = 5;
    private static volatile BluetoothProfile sPan;

    private static void bindPan()
    {
        try {
            sAdapter.getProfileProxy(sCtx, new BluetoothProfile.ServiceListener() {
                public void onServiceConnected(int profile, BluetoothProfile proxy) {
                    sPan = proxy;
                    String want;
                    synchronized (WheelBluetooth.class) { want = sPendingTether; sPendingTether = null; }
                    if (want == null) want = sPrefs.getString("tether", "");
                    if (want.length() > 0) tether(want, true);
                }
                public void onServiceDisconnected(int profile) { sPan = null; }
            }, PROFILE_PAN);
        } catch (Throwable t) {
            Logger.e("WheelBluetooth PAN proxy: " + t);
        }
    }

    private static String sPendingTether;

    public static boolean tether(String addr, boolean on)
    {
        BluetoothDevice d = device(addr);
        if (d == null || !isEnabled())
            return false;
        BluetoothProfile p = sPan;
        if (p == null)
        {
            if (!on) return false;
            synchronized (WheelBluetooth.class) { sPendingTether = addr; }
            bindPan();
            return true;
        }
        sPrefs.edit().putString("tether", on ? addr : "").commit();
        return invoke(p, on ? "connect" : "disconnect", d);
    }

    /* 0 off, 1 connecting, 2 connected */
    public static int tetherState(String addr)
    {
        BluetoothDevice d = device(addr);
        BluetoothProfile p = sPan;
        if (d == null || p == null)
            return 0;
        try {
            int s = p.getConnectionState(d);
            return s == BluetoothProfile.STATE_CONNECTED ? 2
                 : s == BluetoothProfile.STATE_CONNECTING ? 1 : 0;
        } catch (Throwable t) {
            return 0;
        }
    }

    /* --------------------------- internals --------------------------- */

    private static void bindA2dp()
    {
        try {
            sAdapter.getProfileProxy(sCtx, new BluetoothProfile.ServiceListener() {
                public void onServiceConnected(int profile, BluetoothProfile proxy) {
                    sA2dp = (BluetoothA2dp) proxy;
                    pushAudioState();
                    scheduleReconnect(1500); /* Rockbox just started */
                }
                public void onServiceDisconnected(int profile) {
                    sA2dp = null;
                }
            }, BluetoothProfile.A2DP);
        } catch (Throwable t) {
            Logger.e("WheelBluetooth proxy: " + t);
        }
    }

    private static int stateOf(BluetoothDevice d)
    {
        if (d.getBondState() == BluetoothDevice.BOND_BONDING)
            return ST_PAIRING;
        Integer cached = sStates.get(d.getAddress());
        BluetoothA2dp a = sA2dp;
        if (a != null)
        {
            try {
                int s = a.getConnectionState(d);
                if (s == BluetoothProfile.STATE_CONNECTED) return ST_CONNECTED;
                if (s == BluetoothProfile.STATE_CONNECTING) return ST_CONNECTING;
            } catch (Throwable t) { }
        }
        if (cached != null && cached == ST_CONNECTING)
            return ST_CONNECTING;
        return ST_PAIRED;
    }

    private static boolean isAudio(BluetoothDevice d)
    {
        try {
            BluetoothClass c = d.getBluetoothClass();
            if (c == null)
                return true; /* unknown: let the user try */
            return c.hasService(BluetoothClass.Service.AUDIO)
                || c.hasService(BluetoothClass.Service.RENDER)
                || c.getMajorDeviceClass() == BluetoothClass.Device.Major.AUDIO_VIDEO;
        } catch (Throwable t) {
            return true;
        }
    }

    private static String nameOf(BluetoothDevice d)
    {
        String n = null;
        try { n = d.getName(); } catch (Throwable t) { }
        if (n == null || n.length() == 0)
            n = d.getAddress();
        return n.replace('|', '/');
    }

    private static BluetoothDevice device(String addr)
    {
        if (sAdapter == null || addr == null || !BluetoothAdapter.checkBluetoothAddress(addr))
            return null;
        return sAdapter.getRemoteDevice(addr);
    }

    private static void rememberLast(String addr)
    {
        sPrefs.edit().putString("last", addr).commit();
        synchronized (WheelBluetooth.class) { sLastConnected = addr; }
    }

    /* BluetoothA2dp.connect()/disconnect() are hidden (but present) on 4.2 */
    private static boolean a2dpConnect(BluetoothDevice d)
    {
        BluetoothA2dp a = sA2dp;
        if (a == null)
        {
            synchronized (WheelBluetooth.class) { sPendingConnect = d.getAddress(); }
            return true; /* will connect once the profile proxy is up */
        }
        try {
            /* A2DP on 4.2 drives one sink at a time: drop the old one first */
            for (BluetoothDevice other : a.getConnectedDevices())
                if (!other.getAddress().equals(d.getAddress()))
                    invoke(a, "disconnect", other);
        } catch (Throwable t) { }
        synchronized (WheelBluetooth.class) { sStates.put(d.getAddress(), ST_CONNECTING); }
        boolean ok = invoke(a, "connect", d);
        if (!ok)
            synchronized (WheelBluetooth.class) { sStates.remove(d.getAddress()); }
        else
            watch(d.getAddress());
        return ok;
    }

    /* check on a connect that is under way */
    private static void watch(String addr)
    {
        synchronized (WheelBluetooth.class) { sWatch = addr; }
        sMain.removeCallbacks(sUnstickRun);
        sMain.postDelayed(sUnstickRun, STUCK_MS);
    }

    private static final Runnable sUnstickRun = new Runnable() {
        public void run() {
            String addr;
            synchronized (WheelBluetooth.class) { addr = sWatch; }
            BluetoothA2dp a = sA2dp;
            final BluetoothDevice d = device(addr);
            if (a == null || d == null)
                return;
            int st;
            try { st = a.getConnectionState(d); } catch (Throwable t) { return; }
            if (st != BluetoothProfile.STATE_CONNECTING)
                return;                       /* connected, or it gave up */
            if (sUnstick >= MAX_UNSTICK)
            {
                Logger.d("WheelBluetooth: " + addr + " still connecting, leaving it");
                synchronized (WheelBluetooth.class) { sStates.remove(addr); }
                return;
            }
            sUnstick++;
            Logger.d("WheelBluetooth: " + addr + " stuck connecting, starting again");
            invoke(a, "disconnect", d);
            sMain.postDelayed(new Runnable() {
                public void run() { a2dpConnect(d); } }, 1500);
        }
    };

    private static boolean invoke(Object target, String method, BluetoothDevice arg)
    {
        try {
            Method m = target.getClass().getMethod(method, BluetoothDevice.class);
            Object r = m.invoke(target, arg);
            return !(r instanceof Boolean) || (Boolean) r;
        } catch (Throwable t) {
            Logger.e("WheelBluetooth " + method + ": " + t);
            return false;
        }
    }

    private static boolean invoke(BluetoothDevice target, String method)
    {
        try {
            Method m = BluetoothDevice.class.getMethod(method);
            Object r = m.invoke(target);
            return !(r instanceof Boolean) || (Boolean) r;
        } catch (Throwable t) {
            Logger.e("WheelBluetooth " + method + ": " + t);
            return false;
        }
    }

    private static final Runnable sStopScan = new Runnable() {
        public void run() { stopScan(); }
    };

    private static void scheduleReconnect(long delay)
    {
        sMain.removeCallbacks(sReconnect);
        sMain.postDelayed(sReconnect, delay);
    }

    private static final Runnable sReconnect = new Runnable() {
        public void run() {
            if (!getOption(OPT_AUTORECONNECT) || !isEnabled() || sA2dp == null)
                return;
            String last = sPrefs.getString("last", "");
            BluetoothDevice d = device(last);
            if (d == null || d.getBondState() != BluetoothDevice.BOND_BONDED)
                return;
            try {
                if (!sA2dp.getConnectedDevices().isEmpty())
                    return; /* something is already playing our audio */
                if (sA2dp.getConnectionState(d) == BluetoothProfile.STATE_CONNECTING)
                {   /* the headphones are on their way in: make sure they arrive */
                    watch(last);
                    return;
                }
                if (sA2dp.getConnectionState(d) != BluetoothProfile.STATE_DISCONNECTED)
                    return;
            } catch (Throwable t) { return; }
            Logger.d("WheelBluetooth: auto-reconnect to " + last + " (try " + (sRetry + 1) + ")");
            a2dpConnect(d);
            if (sRetry < RETRY_DELAYS_MS.length)
                scheduleReconnect(RETRY_DELAYS_MS[sRetry++]);
        }
    };

    private static final BroadcastReceiver sReceiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context c, Intent intent)
        {
            String action = intent.getAction();
            BluetoothDevice d = intent.getParcelableExtra(BluetoothDevice.EXTRA_DEVICE);

            if (BluetoothAdapter.ACTION_STATE_CHANGED.equals(action))
            {
                int s = intent.getIntExtra(BluetoothAdapter.EXTRA_STATE, -1);
                if (s == BluetoothAdapter.STATE_ON)
                {
                    sRetry = 0;
                    if (sA2dp == null) bindA2dp();
                    scheduleReconnect(2000);
                }
            }
            else if (BluetoothA2dp.ACTION_CONNECTION_STATE_CHANGED.equals(action) && d != null)
            {
                int s = intent.getIntExtra(BluetoothProfile.EXTRA_STATE, -1);
                int prev = intent.getIntExtra(BluetoothProfile.EXTRA_PREVIOUS_STATE, -1);
                synchronized (WheelBluetooth.class) { sStates.remove(d.getAddress()); }
                pushAudioState();
                if (s == BluetoothProfile.STATE_CONNECTED)
                {
                    rememberLast(d.getAddress());
                    sRetry = 0;
                    sUnstick = 0;
                    sMain.removeCallbacks(sReconnect);
                    sMain.removeCallbacks(sUnstickRun);
                    boolean resume;
                    synchronized (WheelBluetooth.class) {
                        resume = sPausedByLossAt >= 0 &&
                                 SystemClock.elapsedRealtime() - sPausedByLossAt < RESUME_WINDOW_MS;
                        sPausedByLossAt = -1;
                    }
                    if (resume && getOption(OPT_RESUME_ON_CONNECT))
                        sMain.postDelayed(new Runnable() { /* let the route settle */
                            public void run() {
                                try { nativeSinkBack(); } catch (Throwable t) { }
                            } }, 1200);
                }
                else if (s == BluetoothProfile.STATE_DISCONNECTED
                         && prev == BluetoothProfile.STATE_CONNECTED)
                {
                    boolean wasPlaying;
                    synchronized (WheelBluetooth.class) { wasPlaying = sWasPlaying; }
                    if (wasPlaying && getOption(OPT_PAUSE_ON_LOSS))
                    {
                        synchronized (WheelBluetooth.class) {
                            sPausedByLossAt = SystemClock.elapsedRealtime();
                        }
                        try { nativeSinkLost(); } catch (Throwable t) { }
                    }
                    /* dropped without the user asking: try to get it back */
                    boolean deliberate;
                    synchronized (WheelBluetooth.class) { deliberate = sLastConnected.length() == 0; }
                    if (!deliberate)
                    {
                        sRetry = 0;
                        sUnstick = 0;
                        scheduleReconnect(RETRY_DELAYS_MS[0]);
                    }
                }
                else if (s == BluetoothProfile.STATE_DISCONNECTED
                         && prev == BluetoothProfile.STATE_CONNECTING
                         && d.getAddress().equals(sPrefs.getString("last", ""))
                         && sRetry < RETRY_DELAYS_MS.length)
                {   /* an attempt to the usual headphones failed: the next try
                     * is already on the clock unless the retries ran out */
                    scheduleReconnect(RETRY_DELAYS_MS[sRetry]);
                }
            }
            else if (BluetoothDevice.ACTION_ACL_CONNECTED.equals(action) && d != null)
            {
                /* the usual headphones are back (turned on): if their audio
                 * doesn't come up by itself, ask for it */
                if (d.getAddress().equals(sPrefs.getString("last", "")))
                {
                    sRetry = 0;
                    sUnstick = 0;
                    scheduleReconnect(ACL_AUDIO_WAIT_MS);
                }
            }
            else if (BluetoothDevice.ACTION_BOND_STATE_CHANGED.equals(action) && d != null)
            {
                int s = intent.getIntExtra(BluetoothDevice.EXTRA_BOND_STATE, -1);
                String pending;
                synchronized (WheelBluetooth.class) { pending = sPendingConnect; }
                if (s == BluetoothDevice.BOND_BONDED && d.getAddress().equals(pending))
                {
                    synchronized (WheelBluetooth.class) { sPendingConnect = null; }
                    final String addr = d.getAddress();
                    sMain.postDelayed(new Runnable() {
                        public void run() { connect(addr); } }, 800);
                }
            }
            else if (BluetoothDevice.ACTION_FOUND.equals(action) && d != null)
            {
                if (d.getBondState() == BluetoothDevice.BOND_BONDED)
                    return;
                synchronized (WheelBluetooth.class) {
                    sFound.put(d.getAddress(), new String[] { nameOf(d), isAudio(d) ? "1" : "0" });
                }
            }
            else if (BluetoothDevice.ACTION_NAME_CHANGED.equals(action) && d != null)
            {
                synchronized (WheelBluetooth.class) {
                    String[] v = sFound.get(d.getAddress());
                    if (v != null) v[0] = nameOf(d);
                }
            }
            else if (BluetoothAdapter.ACTION_DISCOVERY_FINISHED.equals(action))
            {
                sScanning = false;
            }
            else if ("android.bluetooth.device.action.PAIRING_REQUEST".equals(action) && d != null)
            {
                /* Headphones without a screen use the PIN "0000" or "Just Works".
                 * Answer for them so pairing works with only the click wheel. */
                String pending;
                synchronized (WheelBluetooth.class) { pending = sPendingConnect; }
                if (!d.getAddress().equals(pending))
                    return;
                int variant = intent.getIntExtra("android.bluetooth.device.extra.PAIRING_VARIANT", -1);
                try {
                    if (variant == 0) /* PAIRING_VARIANT_PIN */
                    {
                        Method m = BluetoothDevice.class.getMethod("setPin", byte[].class);
                        m.invoke(d, (Object) "0000".getBytes("UTF-8"));
                        abortBroadcast();
                    }
                    else if (variant == 2 || variant == 3) /* PASSKEY_CONFIRMATION / CONSENT */
                    {
                        Method m = BluetoothDevice.class.getMethod("setPairingConfirmation", boolean.class);
                        m.invoke(d, true);
                        abortBroadcast();
                    }
                } catch (Throwable t) {
                    Logger.e("WheelBluetooth pairing: " + t); /* system dialog takes over */
                }
            }
            else if (Intent.ACTION_SCREEN_ON.equals(action))
            {
                sRetry = 0;
                scheduleReconnect(1000);
            }
        }
    };
}
