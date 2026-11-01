package android.os;
import java.util.concurrent.ScheduledThreadPoolExecutor;
public class Looper {
    final ScheduledThreadPoolExecutor ex = new ScheduledThreadPoolExecutor(1);
}
