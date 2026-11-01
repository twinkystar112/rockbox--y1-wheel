package android.net;
public class NetworkInfo {
    private final boolean c;
    NetworkInfo(boolean c) { this.c = c; }
    public boolean isConnected() { return c; }
}
