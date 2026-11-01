package android.content;
import android.content.res.Resources;
/* JVM test fake: just what the Wheel helpers use */
public class Context {
    public static final int MODE_PRIVATE = 0;
    public static final String CONNECTIVITY_SERVICE = "connectivity";
    private final java.util.Map<String, SharedPreferences> prefs = new java.util.HashMap<String, SharedPreferences>();
    public Object connectivity = new android.net.ConnectivityManager();
    public Context getApplicationContext() { return this; }
    public synchronized SharedPreferences getSharedPreferences(String name, int mode) {
        if (!prefs.containsKey(name)) prefs.put(name, new SharedPreferences.MapPrefs());
        return prefs.get(name);
    }
    public Intent registerReceiver(BroadcastReceiver r, IntentFilter f) { return null; }
    public Object getSystemService(String name) { return CONNECTIVITY_SERVICE.equals(name) ? connectivity : null; }
    public String getString(int id) { return ""; }
    public Resources getResources() { return new Resources(); }
}
