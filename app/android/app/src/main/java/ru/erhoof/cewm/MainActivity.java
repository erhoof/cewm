package ru.erhoof.cewm;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.view.KeyEvent;
import android.view.WindowManager;
import org.libsdl.app.SDLActivity;

/** SDLActivity that also asks for "all files access", so the ROM browser can read Downloads (ROMs are not in the APK). */
public class MainActivity extends SDLActivity {
    static boolean visible;
    private boolean asked;

    @Override
    protected void onCreate(Bundle b) {
        super.onCreate(b);
        if (Build.VERSION.SDK_INT >= 28) {
            // Let the app's top-offset slider handle camera clearance instead of Android letterboxing.
            WindowManager.LayoutParams params = getWindow().getAttributes();
            params.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(params);
        }
    }

    /** The system back action (button, gesture) goes to the app as an AC_BACK key instead of closing it. */
    @Override
    public void onBackPressed() {
        SDLActivity.onNativeKeyDown(KeyEvent.KEYCODE_BACK);
        SDLActivity.onNativeKeyUp(KeyEvent.KEYCODE_BACK);
    }

    @Override
    protected void onResume() {
        super.onResume();
        visible = true;
        // An update or reinstall can drop the grant: look again each time the app comes forward, but ask only once per process so a "no" does not loop.
        if (!asked && Build.VERSION.SDK_INT >= 30 && !Environment.isExternalStorageManager()) {
            asked = true;
            startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
        }
    }
    @Override protected void onPause() {
        visible = false;
        super.onPause();
    }
}
