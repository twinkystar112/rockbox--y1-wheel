package android.os;
public class Environment {
    public static java.io.File getExternalStorageDirectory() {
        return new java.io.File(System.getProperty("wheel.sdcard", "/tmp/wheel-sdcard"));
    }
}
