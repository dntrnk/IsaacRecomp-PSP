package io.github.isaacrecomp;

import android.Manifest;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Environment;
import android.provider.Settings;
import android.content.Context;
import android.util.DisplayMetrics;
import android.view.SurfaceHolder;

import org.libsdl.app.SDLActivity;
import org.libsdl.app.SDLSurface;

import java.io.File;

/**
 * SDL activity of IsaacRecomp. The native program (libmain.so) calls the static methods below through JNI:
 * where the data folder is, whether we may read it, and how to restart after "Delete save".
 */
public class IsaacActivity extends SDLActivity {
    private static IsaacActivity sSelf;
    private static volatile boolean sWantStorage = false;
    private AlertDialog mDialog;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        sSelf = this;
        super.onCreate(savedInstanceState);
    }

    /**
     * The game may render at a lower resolution than the screen (SurfaceHolder.setFixedSize: the display hardware
     * scales it up for free). SDL normalizes touches by the surface's buffer size; with a fixed size the touch
     * coordinates are still in screen pixels, so normalize by the view's size instead.
     */
    static class IsaacSurface extends SDLSurface {
        IsaacSurface(Context context) { super(context); }
        @Override
        public void surfaceChanged(SurfaceHolder holder, int format, int width, int height) {
            super.surfaceChanged(holder, format, width, height);
            if (getWidth() > 0 && getHeight() > 0) { mWidth = getWidth(); mHeight = getHeight(); }
        }
    }

    @Override
    protected SDLSurface createSDLSurface(Context context) {
        return new IsaacSurface(context);
    }

    /** Shorter side of the screen in pixels (landscape: its height). */
    public static int screenHeight() {
        if (sSelf == null) return 0;
        DisplayMetrics m = new DisplayMetrics();
        sSelf.getWindowManager().getDefaultDisplay().getRealMetrics(m);
        return Math.min(m.widthPixels, m.heightPixels);
    }

    /** Render at this height (keeping the screen's aspect ratio); 0 or the screen height = native resolution. */
    public static void setRenderHeight(final int h) {
        if (sSelf == null) return;
        sSelf.runOnUiThread(() -> {
            if (mSurface == null) return;
            DisplayMetrics m = new DisplayMetrics();
            sSelf.getWindowManager().getDefaultDisplay().getRealMetrics(m);
            int W = Math.max(m.widthPixels, m.heightPixels), H = Math.min(m.widthPixels, m.heightPixels);
            if (h <= 0 || h >= H) mSurface.getHolder().setSizeFromLayout();
            else mSurface.getHolder().setFixedSize(Math.round((float) W * h / H) & ~1, h);
        });
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {
        String[] a = getIntent() != null ? getIntent().getStringArrayExtra("args") : null;
        return a != null ? a : new String[0];
    }

    @Override
    protected void onResume() {
        super.onResume();
        // back from the system settings: ask again if access is still missing
        if (sWantStorage) {
            if (hasStorageAccess()) { if (mDialog != null) mDialog.dismiss(); mDialog = null; }
            else showStorageDialog();
        }
    }

    /** The IsaacRecomp folder at the root of the internal storage (/storage/emulated/0/IsaacRecomp). */
    public static String rootDir() {
        File dir = new File(Environment.getExternalStorageDirectory(), "IsaacRecomp");
        dir.mkdirs();
        return dir.getAbsolutePath();
    }

    public static boolean hasStorageAccess() {
        if (Build.VERSION.SDK_INT >= 30) return Environment.isExternalStorageManager();
        return sSelf == null || sSelf.checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) == PackageManager.PERMISSION_GRANTED;
    }

    /** Called by the native program at startup when access is missing; it then waits until hasStorageAccess(). */
    public static void requestStorageAccess() {
        sWantStorage = true;
        if (sSelf != null) sSelf.runOnUiThread(() -> sSelf.showStorageDialog());
    }

    private void showStorageDialog() {
        if (mDialog != null && mDialog.isShowing()) return;
        String msg = Build.VERSION.SDK_INT >= 30
            ? "Isaac Recomp reads the game (Isaac.exe) and keeps its settings and saves in the \"IsaacRecomp\" folder of your internal storage.\n\nAllow \"All files access\" on the next screen, then come back."
            : "Isaac Recomp reads the game (Isaac.exe) and keeps its settings and saves in the \"IsaacRecomp\" folder of your internal storage.\n\nAllow access to your files on the next screen.";
        mDialog = new AlertDialog.Builder(this)
            .setTitle("Storage access")
            .setMessage(msg)
            .setCancelable(false)
            .setPositiveButton("Continue", (d, w) -> {
                mDialog = null;
                if (Build.VERSION.SDK_INT >= 30) {
                    try {
                        startActivity(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION, Uri.parse("package:" + getPackageName())));
                    } catch (Exception e) {
                        startActivity(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION));
                    }
                } else {
                    requestPermissions(new String[] { Manifest.permission.READ_EXTERNAL_STORAGE, Manifest.permission.WRITE_EXTERNAL_STORAGE }, 1);
                }
            })
            .setNegativeButton("Quit", (d, w) -> { mDialog = null; finishAndRemoveTask(); System.exit(0); })
            .create();
        mDialog.show();
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults) {
        super.onRequestPermissionsResult(requestCode, permissions, grantResults);
        if (requestCode == 1 && !hasStorageAccess()) showStorageDialog();
    }

    /**
     * "Delete save": start again with the same game selected. The native side exits right after this; a small
     * activity in another process (RestartActivity) waits for that and launches the game again.
     */
    public static void restartApp(String game) {
        if (sSelf == null) return;
        Intent i = new Intent(sSelf, RestartActivity.class);
        i.putExtra("args", new String[] { "--game", game });
        i.putExtra("pid", android.os.Process.myPid());
        i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        sSelf.startActivity(i);
    }
}
