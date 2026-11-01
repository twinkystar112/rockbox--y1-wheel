package org.rockbox.Helper;
/* test fake for the Rockbox-Y1 ROM's Wi-Fi helper: "connecting" just flips
 * the fake network state, and every call is recorded */
public class Connectivity {
    public static final java.util.List<String> calls = new java.util.ArrayList<String>();
    public static volatile boolean works = true;
    public static synchronized String connectWifi(String ssid, String password) {
        calls.add("connect " + ssid + " " + password);
        if (!works) return "Failed";
        android.net.ConnectivityManager.online = true;
        return "Success";
    }
    public static synchronized String disconnectWifi() {
        calls.add("disconnect");
        android.net.ConnectivityManager.online = false;
        return "Success";
    }
}
