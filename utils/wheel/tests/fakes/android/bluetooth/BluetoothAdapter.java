package android.bluetooth;
public class BluetoothAdapter {
    public static BluetoothAdapter getDefaultAdapter() { return null; }
    public boolean isEnabled() { return false; }
    public java.util.Set<BluetoothDevice> getBondedDevices() { return new java.util.HashSet<BluetoothDevice>(); }
    public boolean cancelDiscovery() { return true; }
    public BluetoothDevice getRemoteDevice(String a) { return null; }
}
