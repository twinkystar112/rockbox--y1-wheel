package android.os;
public class HandlerThread {
    private final Looper l = new Looper();
    public HandlerThread(String name) { }
    public void start() { }
    public Looper getLooper() { return l; }
}
