package ru.erhoof.cewm;

import android.Manifest;
import android.app.Activity;
import android.app.role.RoleManager;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;
import android.telecom.TelecomManager;
import android.view.WindowManager;
import android.widget.*;

/** Phone controls remain available if the VM is paused, stopped, or has not booted. */
public class PhoneActivity extends Activity {
    private static PhoneActivity visible;
    private TextView status;
    private EditText number;
    private LinearLayout callControls;
    private void button(LinearLayout layout, String title, Runnable action) {
        Button button = new Button(this);
        button.setText(title);
        button.setOnClickListener(v -> action.run());
        layout.addView(button);
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        PhoneService.initialize(this);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_SHOW_WHEN_LOCKED | WindowManager.LayoutParams.FLAG_TURN_SCREEN_ON);
        LinearLayout layout = new LinearLayout(this);
        layout.setOrientation(LinearLayout.VERTICAL);
        layout.setPadding(24, 24, 24, 24);
        status = new TextView(this); layout.addView(status);
        number = new EditText(this); number.setHint("Phone number"); number.setInputType(android.text.InputType.TYPE_CLASS_PHONE); layout.addView(number);
        button(layout, "Enable WM calls (make CE/WM default phone)", this::enableCalls);
        button(layout, "Call", () -> action("dial:" + number.getText().toString()));
        button(layout, "Answer", () -> action("answer"));
        button(layout, "Hang up / Reject", () -> action("hangup"));
        button(layout, "Mute / Unmute", () -> action("AT+CMUT=" + ("1".equals(PhoneService.request("mute")) ? "0" : "1")));
        button(layout, "Hold / Resume", () -> action("AT+CHLD=2"));
        callControls = new LinearLayout(this);
        callControls.setOrientation(LinearLayout.VERTICAL);
        layout.addView(callControls);
        button(layout, "Open Windows Mobile", () -> startActivity(new Intent(this, MainActivity.class)));
        ScrollView scroll = new ScrollView(this);
        scroll.addView(layout);
        setContentView(scroll);
        readIntent(getIntent());
    }
    private void enableCalls() {
        if (checkSelfPermission(Manifest.permission.CALL_PHONE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(Build.VERSION.SDK_INT >= 33 ? new String[]{Manifest.permission.CALL_PHONE, Manifest.permission.POST_NOTIFICATIONS} : new String[]{Manifest.permission.CALL_PHONE}, 2);
            return;
        }
        if (Build.VERSION.SDK_INT >= 29) {
            RoleManager roles = getSystemService(RoleManager.class);
            if (roles.isRoleAvailable(RoleManager.ROLE_DIALER) && !roles.isRoleHeld(RoleManager.ROLE_DIALER))
                startActivityForResult(roles.createRequestRoleIntent(RoleManager.ROLE_DIALER), 3);
        } else startActivityForResult(new Intent(TelecomManager.ACTION_CHANGE_DEFAULT_DIALER).putExtra(TelecomManager.EXTRA_CHANGE_DEFAULT_DIALER_PACKAGE_NAME, getPackageName()), 3);
    }
    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] grants) {
        super.onRequestPermissionsResult(request, permissions, grants);
        if (request == 2 && checkSelfPermission(Manifest.permission.CALL_PHONE) == PackageManager.PERMISSION_GRANTED) enableCalls();
    }
    @Override protected void onNewIntent(Intent intent) { super.onNewIntent(intent); readIntent(intent); }
    private void readIntent(Intent intent) {
        if (intent.getData() != null && "tel".equals(intent.getData().getScheme())) number.setText(intent.getData().getSchemeSpecificPart());
    }
    private void action(String command) {
        if (!"ok".equals(PhoneService.request(command))) Toast.makeText(this, "Call control unavailable: enable CE/WM as default phone and allow calls.", Toast.LENGTH_LONG).show();
        refresh();
    }
    @Override protected void onResume() { super.onResume(); visible = this; refresh(); }
    @Override protected void onPause() { if (visible == this) visible = null; super.onPause(); }
    static void refreshCurrent() { if (visible != null) visible.refresh(); }
    private void refresh() {
        String state = PhoneService.state();
        String[] names = {"Idle", "Dialing", "Connecting", "Active", "Held", "", "Incoming"};
        status.setText((getPackageName().equals(getSystemService(TelecomManager.class).getDefaultDialerPackage()) ? "WM calling enabled\n" : "WM calling disabled\n") + "CE/WM Phone: " + names[state.charAt(0) - '0'] + "\n" + state.substring(4));
        callControls.removeAllViews();
        if (PhoneService.allCalls().size() > 1) for (android.telecom.Call call : PhoneService.allCalls()) {
            TextView label = new TextView(this); label.setText(PhoneService.number(call)); callControls.addView(label);
            button(callControls, "Answer", () -> call.answer(android.telecom.VideoProfile.STATE_AUDIO_ONLY));
            button(callControls, "Hang up / Reject", () -> { if (call.getState() == android.telecom.Call.STATE_RINGING) call.reject(false, null); else call.disconnect(); });
            button(callControls, "Hold / Resume", () -> { if (call.getState() == android.telecom.Call.STATE_HOLDING) call.unhold(); else call.hold(); });
        }
    }
}
