package org.opennectar;

import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.ParcelFileDescriptor;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;

/**
 * Pantalla de inicio: el launcher de escritorio (Dear ImGui, el hub con
 * Pikmin y Pikmin 2) compilado como libnectar_launcher.so. El lado nativo
 * (pc_port/launcher/launcher_android_main.cpp) llama a dos métodos de aquí:
 *
 *   pickImageBlocking() — abre el selector de documentos y espera el
 *       descriptor de la imagen (o -1 si se cancela). La imagen no se copia.
 *   startGame(lib, dir) — abre NectarActivity con la biblioteca del juego
 *       (nectar, nectar-pal, nectar2, nectar2-pal) y su carpeta.
 *
 * Solo ISO/GCM: RVZ/WIA/GCZ necesitan Dolphin, que no existe aquí.
 */
public class LauncherActivity extends SDLActivity {
    private static final int PICK_IMAGE = 1;

    private final Object pickLock = new Object();
    private boolean pickDone;
    private int pickedFd = -1;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "nectar_launcher" };
    }

    @Override
    protected String[] getArguments() {
        // argv[1] de SDL_main: la carpeta interna de la app.
        return new String[] { getFilesDir().getAbsolutePath() };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
    }

    /** Llamado desde el hilo de SDL: bloquea hasta que el usuario elige o cancela. */
    public int pickImageBlocking() {
        synchronized (pickLock) {
            pickDone = false;
            pickedFd = -1;
        }
        runOnUiThread(() -> {
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            startActivityForResult(intent, PICK_IMAGE);
        });
        synchronized (pickLock) {
            while (!pickDone) {
                try {
                    pickLock.wait();
                } catch (InterruptedException e) {
                    return -1;
                }
            }
            return pickedFd;
        }
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode != PICK_IMAGE) return;
        int fd = -1;
        if (resultCode == RESULT_OK && data != null && data.getData() != null) {
            Uri uri = data.getData();
            try {
                ParcelFileDescriptor pfd = getContentResolver().openFileDescriptor(uri, "r");
                if (pfd != null) fd = pfd.detachFd(); // el lado nativo lo cierra
            } catch (FileNotFoundException | SecurityException ignored) {
            }
        }
        synchronized (pickLock) {
            pickedFd = fd;
            pickDone = true;
            pickLock.notifyAll();
        }
    }

    /**
     * Descarga `url` en `path` (portadas de GameTDB, último release de GitHub).
     * La llaman hilos del launcher; bloquea. Devuelve null si va bien o el error.
     */
    public String downloadFile(String url, String path) {
        File target = new File(path);
        File partial = new File(path + ".part");
        HttpURLConnection connection = null;
        try {
            File parent = target.getParentFile();
            if (parent != null) parent.mkdirs();
            URL current = new URL(url);
            // HttpURLConnection no sigue redirecciones entre http y https.
            for (int hop = 0; hop < 5; hop++) {
                connection = (HttpURLConnection) current.openConnection();
                connection.setRequestProperty("User-Agent", "OpenNectar");
                connection.setConnectTimeout(15000);
                connection.setReadTimeout(30000);
                connection.setInstanceFollowRedirects(true);
                int code = connection.getResponseCode();
                if (code >= 300 && code < 400 && connection.getHeaderField("Location") != null) {
                    current = new URL(current, connection.getHeaderField("Location"));
                    connection.disconnect();
                    continue;
                }
                if (code != 200) return "HTTP " + code;
                try (InputStream in = connection.getInputStream(); OutputStream out = new FileOutputStream(partial)) {
                    byte[] buffer = new byte[64 * 1024];
                    for (int n; (n = in.read(buffer)) > 0;) out.write(buffer, 0, n);
                }
                if (!partial.renameTo(target)) return "could not save " + path;
                return null;
            }
            return "too many redirects";
        } catch (Exception e) {
            partial.delete();
            return e.toString();
        } finally {
            if (connection != null) connection.disconnect();
        }
    }

    /** Abre una dirección en el navegador (el APK de una versión nueva). */
    public void openUrl(String url) {
        runOnUiThread(() -> {
            try {
                startActivity(new Intent(Intent.ACTION_VIEW, Uri.parse(url)));
            } catch (Exception ignored) {
            }
        });
    }

    /** Llamado desde el hilo de SDL antes de que el launcher termine. */
    public void startGame(String library, String gameDir) {
        Intent intent = new Intent(this, NectarActivity.class);
        intent.putExtra(NectarActivity.EXTRA_LIBRARY, library);
        intent.putExtra(NectarActivity.EXTRA_GAME_DIR, gameDir);
        startActivity(intent);
    }
}
