package android.net;
public class ConnectivityManager {
    public static final String CONNECTIVITY_ACTION = "android.net.conn.CONNECTIVITY_CHANGE";
    public static volatile boolean online = true; /* tests flip this */
    public NetworkInfo getActiveNetworkInfo() { return new NetworkInfo(online); }
}
