package android.os;
import java.util.ArrayList;
import java.util.IdentityHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
/* a single worker thread like a real Looper; delays are shortened by
 * wheel.timescale so tests don't wait minutes */
public class Handler {
    private final Looper l;
    private final Map<Runnable, List<ScheduledFuture<?>>> pending = new IdentityHashMap<Runnable, List<ScheduledFuture<?>>>();
    static final double SCALE = Double.parseDouble(System.getProperty("wheel.timescale", "0.02"));
    public Handler(Looper l) { this.l = l; }
    public boolean post(Runnable r) { return postDelayed(r, 0); }
    public synchronized boolean postDelayed(final Runnable r, long ms) {
        final ScheduledFuture<?>[] self = new ScheduledFuture<?>[1];
        Runnable wrap = new Runnable() { public void run() {
            synchronized (Handler.this) { List<ScheduledFuture<?>> f = pending.get(r); if (f != null) f.remove(self[0]); }
            try { r.run(); } catch (Throwable t) { t.printStackTrace(); }
        }};
        self[0] = l.ex.schedule(wrap, (long) (ms * SCALE), TimeUnit.MILLISECONDS);
        List<ScheduledFuture<?>> f = pending.get(r);
        if (f == null) { f = new ArrayList<ScheduledFuture<?>>(); pending.put(r, f); }
        f.add(self[0]);
        return true;
    }
    public synchronized void removeCallbacks(Runnable r) {
        List<ScheduledFuture<?>> f = pending.remove(r);
        if (f != null) for (ScheduledFuture<?> x : f) x.cancel(false);
    }
}
