package org.opennectar;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import java.io.File;
import java.io.FileNotFoundException;

/**
 * Primera ejecución (fase 7 de docs/ANDROID_PLAN.md). Si los assets ya están
 * instalados pasa directamente al juego; si no, pide la imagen del disco con
 * el selector de archivos del sistema (Storage Access Framework: sin permiso
 * de almacenamiento, la imagen no se copia) y la extrae en segundo plano.
 * Solo ISO/GCM: RVZ/WIA/GCZ necesitan Dolphin, que no existe aquí.
 */
public class InstallerActivity extends Activity implements Installer.Listener {
    private static final int PICK_IMAGE = 1;

    private TextView status;
    private ProgressBar progress;
    private Button pick;
    private Thread worker;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Installer.assetsInstalled(this)) {
            startGame();
            return;
        }

        int pad = dp(24);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER);
        root.setPadding(pad, pad, pad, pad);
        root.setBackgroundColor(Color.rgb(18, 26, 18));

        TextView title = new TextView(this);
        title.setText("Open Nectar");
        title.setTextSize(30);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        TextView intro = new TextView(this);
        intro.setText("The game data comes from your own Pikmin disc.\n"
                    + "Choose a GameCube disc image (.iso or .gcm) of Pikmin USA or Europe. "
                    + "It is read once and extracted here; the image itself is not copied.\n\n"
                    + "Compressed images (RVZ, WIA, GCZ) are not supported on Android: "
                    + "convert them to ISO with Dolphin on a PC first.");
        intro.setTextColor(Color.rgb(210, 220, 210));
        intro.setTextSize(15);
        intro.setPadding(0, dp(12), 0, dp(20));
        root.addView(intro);

        pick = new Button(this);
        pick.setText("Choose disc image (ISO / GCM)");
        pick.setOnClickListener(v -> pickImage());
        root.addView(pick);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(100);
        progress.setVisibility(View.GONE);
        progress.setPadding(0, dp(16), 0, 0);
        root.addView(progress);

        status = new TextView(this);
        status.setTextColor(Color.rgb(210, 220, 210));
        status.setTextSize(13);
        status.setPadding(0, dp(8), 0, 0);
        root.addView(status);

        setContentView(root);
    }

    private int dp(int value) {
        return (int) (value * getResources().getDisplayMetrics().density);
    }

    private void pickImage() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        startActivityForResult(intent, PICK_IMAGE);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_IMAGE || resultCode != RESULT_OK || data == null || data.getData() == null) return;
        install(data.getData());
    }

    private void install(Uri uri) {
        final ParcelFileDescriptor pfd;
        try {
            pfd = getContentResolver().openFileDescriptor(uri, "r");
        } catch (FileNotFoundException | SecurityException e) {
            status.setText("Could not open the file: " + e.getMessage());
            return;
        }
        if (pfd == null) {
            status.setText("Could not open the file.");
            return;
        }
        final int fd = pfd.getFd();

        String[] error = new String[1];
        String description = Installer.nativeInspect(fd, error);
        if (description == null) {
            status.setText(error[0] != null ? error[0] : "This is not a supported Pikmin disc image.");
            closeQuietly(pfd);
            return;
        }
        String build = Installer.nativeRequiredBuild(fd);
        if (build != null && !gameLibraryAvailable(build)) {
            status.setText(description + " needs the \"" + build + "\" build of the game, which this "
                         + "APK does not include yet.");
            closeQuietly(pfd);
            return;
        }

        pick.setEnabled(false);
        progress.setVisibility(View.VISIBLE);
        progress.setProgress(0);
        status.setText("Installing " + description + "...");
        final String gameDir = Installer.gameDir(this).getAbsolutePath();
        worker = new Thread(() -> {
            final String failure = Installer.nativeInstall(fd, gameDir, this);
            closeQuietly(pfd);
            runOnUiThread(() -> {
                if (failure == null) {
                    startGame();
                } else {
                    pick.setEnabled(true);
                    progress.setVisibility(View.GONE);
                    status.setText(failure);
                }
            });
        }, "nectar-installer");
        worker.start();
    }

    /** ¿Trae el APK la biblioteca del juego que pide este disco? Las .so se
     *  cargan directamente desde el APK (sin extraer), así que se mira dentro. */
    private boolean gameLibraryAvailable(String build) {
        try (java.util.zip.ZipFile apk = new java.util.zip.ZipFile(getApplicationInfo().sourceDir)) {
            return apk.getEntry("lib/arm64-v8a/lib" + build + ".so") != null;
        } catch (java.io.IOException e) {
            return true; // mejor intentar cargarla que negarse por un fallo de lectura
        }
    }

    @Override
    public void onProgress(int percent, String text) {
        runOnUiThread(() -> {
            progress.setProgress(percent);
            status.setText(percent + "%  " + text);
        });
    }

    private void startGame() {
        startActivity(new Intent(this, NectarActivity.class));
        finish();
    }

    private static void closeQuietly(ParcelFileDescriptor pfd) {
        try { pfd.close(); } catch (Exception ignored) {}
    }
}
