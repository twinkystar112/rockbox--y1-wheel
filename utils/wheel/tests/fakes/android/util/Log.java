package android.util;
public class Log {
    public static int i(String t, String s) { return 0; }
    public static int d(String t, String s) { return 0; }
    public static int d(String t, String s, Throwable e) { return 0; }
    public static int e(String t, String s) { System.err.println("E/" + t + ": " + s); return 0; }
    public static int e(String t, String s, Throwable e) { System.err.println("E/" + t + ": " + s); return 0; }
    public static int w(String t, String s) { return 0; }
    public static int v(String t, String s) { return 0; }
}
