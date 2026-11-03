package io.github.alox_dev.redbound;

import android.app.Activity;
import android.content.Intent;
import android.net.Uri;
import android.os.Bundle;
import android.view.View;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;

public class RedboundActivity extends SDLActivity {
    private static final int PICK_DEB = 4711;
    private static volatile boolean sPicking = false;
    private static volatile String sPicked = null;

    @Override
    protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL2", "main" };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
    }

    public static void pickDeb() {
        final Activity act = (Activity) getContext();
        if (act == null) return;
        sPicking = true;
        act.runOnUiThread(() -> {
            Intent i = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            try {
                act.startActivityForResult(i, PICK_DEB);
            } catch (Exception e) {
                sPicking = false;
            }
        });
    }

    public static boolean isPicking() { return sPicking; }

    public static String takePicked() {
        String s = sPicked;
        sPicked = null;
        return s;
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        if (requestCode != PICK_DEB) {
            super.onActivityResult(requestCode, resultCode, data);
            return;
        }
        if (resultCode != RESULT_OK || data == null || data.getData() == null) {
            sPicking = false;
            return;
        }
        final Uri uri = data.getData();
        final File out = new File(getCacheDir(), "import.deb");
        new Thread(() -> {
            try (InputStream in = getContentResolver().openInputStream(uri); OutputStream os = new FileOutputStream(out)) {
                byte[] buf = new byte[1 << 16];
                int n;
                while ((n = in.read(buf)) > 0) os.write(buf, 0, n);
                sPicked = out.getAbsolutePath();
            } catch (Exception e) {
                sPicked = null;
            } finally {
                sPicking = false;
            }
        }).start();
    }
}
