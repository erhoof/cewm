package ru.erhoof.cewm;

import android.content.Context;
import android.os.VibrationEffect;
import android.os.Vibrator;
import org.libsdl.app.SDLActivity;

/** The key-press tick of the soft button bar (host/haptic.cpp). */
public class Haptic {
    public static void tick() {
        Vibrator v = (Vibrator) SDLActivity.getContext().getSystemService(Context.VIBRATOR_SERVICE);
        if (v == null) return;
        if (android.os.Build.VERSION.SDK_INT >= 29) v.vibrate(VibrationEffect.createPredefined(VibrationEffect.EFFECT_TICK));
        else v.vibrate(VibrationEffect.createOneShot(10, VibrationEffect.DEFAULT_AMPLITUDE));
    }
}
