package io.github.isaacrecomp;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;

/** Runs in a separate process: once the game's process has exited, launches the game again. */
public class RestartActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        final int pid = getIntent().getIntExtra("pid", -1);
        final String[] args = getIntent().getStringArrayExtra("args");
        final Handler h = new Handler(Looper.getMainLooper());
        h.postDelayed(new Runnable() {
            int tries = 0;
            @Override public void run() {
                // give the old process up to ~2 s to finish writing its files, then make sure it is gone
                if (pid > 0 && tries++ < 20 && new java.io.File("/proc/" + pid).exists()) { h.postDelayed(this, 100); return; }
                if (pid > 0) android.os.Process.killProcess(pid);
                Intent i = new Intent(RestartActivity.this, IsaacActivity.class);
                if (args != null) i.putExtra("args", args);
                i.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
                startActivity(i);
                finish();
                Runtime.getRuntime().exit(0);
            }
        }, 200);
    }
}
