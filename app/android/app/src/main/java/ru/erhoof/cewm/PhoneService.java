package ru.erhoof.cewm;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Handler;
import android.os.Looper;
import android.os.PowerManager;
import android.telecom.Call;
import android.telecom.CallAudioState;
import android.telecom.InCallService;
import android.telecom.TelecomManager;
import android.telecom.VideoProfile;
import java.util.ArrayList;
import java.util.concurrent.FutureTask;
import org.libsdl.app.SDL;

/** Telecom owns the call and audio; only protocol state crosses into the VM. */
public class PhoneService extends InCallService {
    private static final Handler MAIN = new Handler(Looper.getMainLooper());
    private static final ArrayList<Call> calls = new ArrayList<>();
    private static final java.util.HashSet<Call> incoming = new java.util.HashSet<>();
    private static boolean pending, cancelPending;
    private static Boolean pendingMute;
    private static android.content.Context app;
    private static PhoneService instance;
    private static boolean vmRunning;
    private PowerManager.WakeLock proximity;
    static void initialize(android.content.Context context) { app = context.getApplicationContext(); }
    @Override public void onCreate() { super.onCreate(); initialize(this); instance = this; }
    @Override public void onDestroy() {
        if (proximity != null && proximity.isHeld()) proximity.release();
        for (Call call : calls) call.unregisterCallback(callback);
        calls.clear(); incoming.clear();
        pending = cancelPending = false;
        instance = null;
        super.onDestroy();
    }
    private static String pendingNumber = "";
    private static long pendingSince;
    private final Call.Callback callback = new Call.Callback() {
        @Override public void onStateChanged(Call call, int state) { changed(); }
        @Override public void onDetailsChanged(Call call, Call.Details details) { changed(); }
    };

    @Override public void onCallAdded(Call call) {
        calls.add(call);
        if (call.getState() == Call.STATE_RINGING || (android.os.Build.VERSION.SDK_INT >= 29 && call.getDetails().getCallDirection() == Call.Details.DIRECTION_INCOMING)) incoming.add(call);
        pending = false;
        if (cancelPending && android.os.SystemClock.elapsedRealtime() - pendingSince <= 30000 && call.getState() != Call.STATE_RINGING && number(call).equals(pendingNumber)) { cancelPending = false; call.disconnect(); }
        call.registerCallback(callback, MAIN);
        if (pendingMute != null) { setMuted(pendingMute); pendingMute = null; }
        changed();
        if (showNativeIncoming(MainActivity.visible, vmRunning, call.getState()))
            startActivity(new Intent(this, PhoneActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_SINGLE_TOP));
    }
    @Override public void onCallRemoved(Call call) {
        call.unregisterCallback(callback);
        calls.remove(call);
        incoming.remove(call);
        changed();
    }
    private static void changed() {
        PhoneActivity.refreshCurrent();
        if (instance != null) { instance.updateProximity(instance.getCallAudioState()); instance.notifyCall(); }
    }
    static boolean useProximity(int state, int route) {
        return route == CallAudioState.ROUTE_EARPIECE &&
            (state == Call.STATE_CONNECTING || state == Call.STATE_DIALING || state == Call.STATE_ACTIVE);
    }
    static boolean showNativeIncoming(boolean visible, boolean running, int state) {
        return state == Call.STATE_RINGING && !(visible && running);
    }
    @Override public void onCallAudioStateChanged(CallAudioState audio) {
        super.onCallAudioStateChanged(audio);
        updateProximity(audio);
    }
    private void updateProximity(CallAudioState audio) {
        boolean enable = false;
        if (audio != null) for (Call call : calls) enable |= useProximity(call.getState(), audio.getRoute());
        if (enable && proximity == null) {
            PowerManager power = getSystemService(PowerManager.class);
            if (!power.isWakeLockLevelSupported(PowerManager.PROXIMITY_SCREEN_OFF_WAKE_LOCK)) return;
            proximity = power.newWakeLock(PowerManager.PROXIMITY_SCREEN_OFF_WAKE_LOCK, "CEWM:call-proximity");
            proximity.setReferenceCounted(false);
        }
        if (proximity == null) return;
        if (enable && !proximity.isHeld()) proximity.acquire();
        else if (!enable && proximity.isHeld()) proximity.release();
    }
    private void notifyCall() {
        android.app.NotificationManager notifications = getSystemService(android.app.NotificationManager.class);
        if (calls.isEmpty()) { notifications.cancel(41); return; }
        android.app.NotificationChannel channel = new android.app.NotificationChannel("calls", "Phone calls", android.app.NotificationManager.IMPORTANCE_HIGH);
        notifications.createNotificationChannel(channel);
        android.app.PendingIntent open = android.app.PendingIntent.getActivity(this, 0, new Intent(this, PhoneActivity.class), android.app.PendingIntent.FLAG_UPDATE_CURRENT | android.app.PendingIntent.FLAG_IMMUTABLE);
        android.app.Notification.Builder builder = new android.app.Notification.Builder(this, "calls")
            .setSmallIcon(android.R.drawable.sym_action_call).setContentTitle("CE/WM phone call")
            .setContentText(state()).setCategory(android.app.Notification.CATEGORY_CALL)
            .setVisibility(android.app.Notification.VISIBILITY_PRIVATE).setOngoing(true).setContentIntent(open);
        if (showNativeIncoming(MainActivity.visible, vmRunning, current().getState())) builder.setFullScreenIntent(open, true);
        notifications.notify(41, builder.build());
    }
    static java.util.List<Call> allCalls() { return new ArrayList<>(calls); }
    static Call current() {
        for (Call call : calls) if (call.getState() == Call.STATE_RINGING) return call;
        for (Call call : calls) if (call.getState() != Call.STATE_DISCONNECTED) return call;
        return calls.isEmpty() ? null : calls.get(0);
    }
    static String number(Call call) {
        Uri handle = call.getDetails().getHandle();
        return handle == null ? "" : handle.getSchemeSpecificPart();
    }
    static String state() {
        Call call = current();
        if (call == null) {
            if (pending && android.os.SystemClock.elapsedRealtime() - pendingSince > 30000) { pending = false; cancelPending = false; }
            return pending ? "1:0:" + pendingNumber : "0:0:";
        }
        int state;
        switch (call.getState()) {
            case Call.STATE_CONNECTING: case Call.STATE_SELECT_PHONE_ACCOUNT: state = 1; break;
            case Call.STATE_DIALING: state = 2; break;
            case Call.STATE_ACTIVE: state = 3; break;
            case Call.STATE_HOLDING: state = 4; break;
            case Call.STATE_RINGING: state = 6; break;
            case Call.STATE_DISCONNECTED: state = 0; break;
            default: state = 2;
        }
        return state + ":" + (incoming.contains(call) ? "1:" : "0:") + number(call);
    }
    public static String request(String command) {
        if (Looper.myLooper() == Looper.getMainLooper()) return execute(command);
        FutureTask<String> task = new FutureTask<>(() -> execute(command));
        MAIN.post(task);
        try { return task.get(); }
        catch (Exception e) { return ""; }
    }
    private static String execute(String command) {
        if (command.equals("vm:started") || command.equals("vm:stopped")) {
            vmRunning = command.equals("vm:started");
            return "ok";
        }
        if (command.equals("setup")) {
            android.content.Context context = SDL.getContext();
            if (context != null) context.startActivity(new Intent(context, PhoneActivity.class).addFlags(Intent.FLAG_ACTIVITY_NEW_TASK));
            return "ok";
        }
        if (command.equals("poll")) return state();
        android.content.Context context = app != null ? app : SDL.getContext();
        if (context == null) return "";
        TelecomManager telecom = context.getSystemService(TelecomManager.class);
        // Becoming the default phone is an explicit opt-in, and permits reliable Call controls.
        if (!context.getPackageName().equals(telecom.getDefaultDialerPackage())) return "";
        try {
            Call call = current();
            if (calls.size() > 1) return ""; // shortcut: WM supports one call; Android UI controls additional calls.
            if (command.startsWith("dial:")) {
                String number = command.substring(5);
                if (!number.matches("\\+?[0-9]{1,32}") || call != null || pending || context.checkSelfPermission(Manifest.permission.CALL_PHONE) != PackageManager.PERMISSION_GRANTED) return "";
                pending = true;
                cancelPending = false;
                pendingNumber = number;
                pendingSince = android.os.SystemClock.elapsedRealtime();
                try { telecom.placeCall(Uri.fromParts("tel", number, null), new android.os.Bundle()); }
                catch (RuntimeException e) { pending = false; throw e; }
            } else if (command.equals("hangup")) {
                if (call == null) { if (pending) { cancelPending = true; return "ok"; } return ""; }
                if (call.getState() == Call.STATE_RINGING) call.reject(false, null);
                else call.disconnect();
            } else if (command.equals("answer")) {
                if (call == null || call.getState() != Call.STATE_RINGING) return "";
                call.answer(VideoProfile.STATE_AUDIO_ONLY);
            } else if (command.equals("mute")) {
                if (instance == null || instance.getCallAudioState() == null) return Boolean.TRUE.equals(pendingMute) ? "1" : "0";
                return instance.getCallAudioState().isMuted() ? "1" : "0";
            } else if (command.equals("AT+CMUT=0") || command.equals("AT+CMUT=1")) {
                if (instance == null) pendingMute = command.endsWith("1");
                else instance.setMuted(command.endsWith("1"));
            } else if (command.equals("AT+CHLD=2")) {
                if (calls.size() != 1 || call == null) return "";
                if (call.getState() == Call.STATE_HOLDING) call.unhold();
                else if (call.getDetails().can(Call.Details.CAPABILITY_HOLD)) call.hold();
                else return "";
            } else if (command.startsWith("AT+VTS=")) {
                String digit = command.substring(7).replace("\"", "");
                if (call == null || call.getState() != Call.STATE_ACTIVE || !digit.matches("[0-9A-D*#]")) return "";
                call.playDtmfTone(digit.charAt(0));
                MAIN.postDelayed(call::stopDtmfTone, 150);
            } else return "";
            return "ok";
        } catch (RuntimeException e) {
            android.util.Log.w("CEWM-RIL", "Telecom rejected " + (command.startsWith("dial:") ? "dial" : command), e);
            return "";
        }
    }
}
