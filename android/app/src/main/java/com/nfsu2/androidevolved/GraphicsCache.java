package com.nfsu2.androidevolved;

import android.app.Activity;
import android.app.ActivityManager;
import android.app.AlertDialog;
import android.os.Process;
import java.io.File;
import java.util.List;

/** Only disposable graphics caches in this application's private cache directory. */
final class GraphicsCache {
    // Keep in sync with configurePipelineCache() in main.cpp. Epoch 2 excludes
    // caches from the previous DXVK preparation; those files remain untouched.
    static final String DIRECTORY = "dxvk-v2";

    static void prompt(Activity activity) {
        new AlertDialog.Builder(activity).setTitle("Réinitialiser le cache graphique")
                .setMessage("La session de jeu en cours sera fermée. Les shaders seront recompilés en jouant ; tes parties et tes réglages sont conservés.")
                .setNegativeButton("Annuler", null)
                .setPositiveButton("Réinitialiser", (dialog, which) -> reset(activity)).show();
    }

    private static void reset(Activity activity) {
        try {
            ActivityManager manager = (ActivityManager) activity.getSystemService(Activity.ACTIVITY_SERVICE);
            if (manager == null) throw new IllegalStateException("Impossible de vérifier le processus du jeu.");
            List<ActivityManager.RunningAppProcessInfo> running = manager.getRunningAppProcesses();
            if (running != null) for (ActivityManager.RunningAppProcessInfo process : running)
                if (process.uid == Process.myUid() && process.processName.equals(activity.getPackageName() + ":game"))
                    Process.killProcess(process.pid);
            File cache = new File(activity.getCacheDir(), DIRECTORY);
            if (cache.exists()) {
                File backup = new File(activity.getCacheDir(), DIRECTORY + "-reset-" + System.currentTimeMillis());
                if (!cache.isDirectory() || !cache.renameTo(backup))
                    throw new IllegalStateException("Impossible de réinitialiser le cache graphique.");
            }
            new AlertDialog.Builder(activity).setTitle("Cache graphique prêt")
                    .setMessage("Le prochain lancement générera un nouveau cache. L'ancien est sauvegardé dans le cache privé de l'appli.")
                    .setPositiveButton("OK", null).show();
        } catch (RuntimeException error) {
            new AlertDialog.Builder(activity).setTitle("Cache graphique").setMessage(error.getMessage())
                    .setPositiveButton("OK", null).show();
        }
    }
}
