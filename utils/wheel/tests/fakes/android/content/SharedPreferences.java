package android.content;
import java.util.HashMap;
import java.util.Map;
public interface SharedPreferences {
    String getString(String k, String d);
    boolean getBoolean(String k, boolean d);
    int getInt(String k, int d);
    long getLong(String k, long d);
    Editor edit();
    interface Editor {
        Editor putString(String k, String v);
        Editor putBoolean(String k, boolean v);
        Editor putInt(String k, int v);
        Editor putLong(String k, long v);
        Editor remove(String k);
        boolean commit();
        void apply();
    }
    class MapPrefs implements SharedPreferences {
        final Map<String, Object> m = new HashMap<String, Object>();
        public synchronized String getString(String k, String d) { Object o = m.get(k); return o == null ? d : (String) o; }
        public synchronized boolean getBoolean(String k, boolean d) { Object o = m.get(k); return o == null ? d : (Boolean) o; }
        public synchronized int getInt(String k, int d) { Object o = m.get(k); return o == null ? d : (Integer) o; }
        public synchronized long getLong(String k, long d) { Object o = m.get(k); return o == null ? d : (Long) o; }
        public Editor edit() {
            final MapPrefs self = this;
            final Map<String, Object> pending = new HashMap<String, Object>();
            return new Editor() {
                public Editor putString(String k, String v) { pending.put(k, v); return this; }
                public Editor putBoolean(String k, boolean v) { pending.put(k, v); return this; }
                public Editor putInt(String k, int v) { pending.put(k, v); return this; }
                public Editor putLong(String k, long v) { pending.put(k, v); return this; }
                public Editor remove(String k) { pending.put(k, null); return this; }
                public boolean commit() {
                    synchronized (self) {
                        for (Map.Entry<String, Object> e : pending.entrySet())
                            if (e.getValue() == null) self.m.remove(e.getKey()); else self.m.put(e.getKey(), e.getValue());
                    }
                    return true;
                }
                public void apply() { commit(); }
            };
        }
    }
}
