package ru.erhoof.cewm;

import android.content.Context;
import android.net.ConnectivityManager;
import android.net.LinkProperties;
import android.net.Network;
import java.io.File;
import java.io.FileOutputStream;
import java.net.InetAddress;
import android.content.Intent;
import android.content.IntentFilter;
import android.net.wifi.WifiInfo;
import android.net.wifi.WifiManager;
import android.os.BatteryManager;
import android.telephony.TelephonyManager;
import org.libsdl.app.SDLActivity;

/** Battery, cellular and Wi-Fi state for the emulated phone, polled from native code (host/device_stats.cpp). */
public class Stats {
    private static String lastDns = "";

    /** libslirp's DNS proxy reads the host's servers from files/resolv.conf (Android has no /etc/resolv.conf); keep it current. */
    private static void writeDns(Context c) {
        try {
            ConnectivityManager cm = (ConnectivityManager) c.getSystemService(Context.CONNECTIVITY_SERVICE);
            Network n = cm.getActiveNetwork();
            LinkProperties lp = n == null ? null : cm.getLinkProperties(n);
            StringBuilder s = new StringBuilder();
            if (lp != null) for (InetAddress a : lp.getDnsServers()) s.append("nameserver ").append(a.getHostAddress()).append('\n');
            if (s.length() == 0) s.append("nameserver 1.1.1.1\nnameserver 8.8.8.8\n");  // no network yet, or Private DNS hides the servers
            String t = s.toString();
            if (t.equals(lastDns)) return;
            try (FileOutputStream f = new FileOutputStream(new File(c.getFilesDir(), "resolv.conf"))) { f.write(t.getBytes()); }
            lastDns = t;
        } catch (Exception e) { }
    }

    public static String poll() {
        Context c = SDLActivity.getContext();
        writeDns(c);
        StringBuilder s = new StringBuilder();
        try {
            Intent b = c.registerReceiver(null, new IntentFilter(Intent.ACTION_BATTERY_CHANGED));  // sticky: no permission
            int scale = b.getIntExtra(BatteryManager.EXTRA_SCALE, 100);
            s.append("batt=").append(b.getIntExtra(BatteryManager.EXTRA_LEVEL, 0) * 100 / (scale > 0 ? scale : 100)).append('\n');
            s.append("charging=").append(b.getIntExtra(BatteryManager.EXTRA_PLUGGED, 0) != 0 ? 1 : 0).append('\n');
        } catch (Exception e) { }
        try {
            TelephonyManager t = (TelephonyManager) c.getSystemService(Context.TELEPHONY_SERVICE);
            String op = t.getNetworkOperatorName();
            boolean sim = t.getSimState() == TelephonyManager.SIM_STATE_READY && op != null && !op.isEmpty();
            s.append("operator=").append(op == null ? "" : op).append('\n');
            s.append("plmn=").append(t.getNetworkOperator()).append('\n');
            int level = -1, act = -1;
            if (sim) {
                // the guest's CSQ scale is not linear in Android's bars: only a full 4/4 is 100
                try { level = new int[] {0, 20, 40, 60, 100}[t.getSignalStrength().getLevel()];} catch (SecurityException e) { level = 100; }  // READ_PHONE_STATE not granted
                try {
                    switch (t.getDataNetworkType()) {  // 3GPP AcT as the guest's modem reports it
                        case TelephonyManager.NETWORK_TYPE_GPRS: case TelephonyManager.NETWORK_TYPE_GSM: act = 0; break;
                        case TelephonyManager.NETWORK_TYPE_EDGE: act = 3; break;
                        case TelephonyManager.NETWORK_TYPE_UMTS: case TelephonyManager.NETWORK_TYPE_TD_SCDMA: act = 2; break;
                        case TelephonyManager.NETWORK_TYPE_UNKNOWN: act = -1; break;
                        default: act = 4;  // HSPA, LTE, NR...: WM draws "H"
                    }
                } catch (SecurityException e) { act = 4; }
            }
            s.append("cell=").append(level).append('\n');
            s.append("act=").append(act).append('\n');
        } catch (Exception e) { }
        try {
            WifiInfo w = ((WifiManager) c.getApplicationContext().getSystemService(Context.WIFI_SERVICE)).getConnectionInfo();
            s.append("wifi=").append(w.getNetworkId() == -1 ? -1 : WifiManager.calculateSignalLevel(w.getRssi(), 5) * 25).append('\n');
        } catch (Exception e) { }
        return s.toString();
    }
}
