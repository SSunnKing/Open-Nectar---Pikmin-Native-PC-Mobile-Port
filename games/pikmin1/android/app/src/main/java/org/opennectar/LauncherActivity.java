package org.opennectar;

import android.content.Intent;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.Canvas;
import android.graphics.RectF;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.view.WindowManager;

import androidx.core.content.pm.ShortcutInfoCompat;
import androidx.core.content.pm.ShortcutManagerCompat;
import androidx.core.graphics.drawable.IconCompat;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileNotFoundException;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.CountDownLatch;

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

    // Shortcut intent (#336). launchPending starts true so the first native
    // read sees a cold start. rememberLaunch sets it again from onNewIntent.
    // The flag and the two strings move together under launchLock.
    private final Object launchLock = new Object();
    private boolean launchPending = true;
    private String launchExtra = "";
    private String launchData = "";

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "nectar_launcher" };
    }

    @Override
    protected String[] getArguments() {
        // argv[1] de SDL_main: la carpeta de datos (DataFolder, issue #176).
        return new String[] { DataFolder.current(this).getAbsolutePath() };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        // Before SDLActivity.onCreate: it treats any data URI as a dropped file.
        // opennectar://launch/pikmin1 is a shortcut, and the extra already names the game.
        rememberLaunch(getIntent());
        Intent opening = getIntent();
        if (opening != null && opening.getData() != null) {
            opening.setData(null);
            setIntent(opening);
        }
        super.onCreate(savedInstanceState);
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        rememberLaunch(intent);
        if (intent != null && intent.getData() != null) intent.setData(null);
        if (intent != null) setIntent(intent);
    }

    private void rememberLaunch(Intent intent) {
        synchronized (launchLock) {
            String extra = GameLaunch.extra(intent);
            String data = GameLaunch.data(intent);
            launchExtra = extra != null ? extra : "";
            launchData = data != null ? data : "";
            launchPending = true;
        }
    }

    /**
     * Llamado desde el hilo de SDL. null si no hay un atajo nuevo. Si lo hay,
     * [0] es el extra y [1] la URI (cadenas vacías si ese campo no venía).
     */
    public String[] takeLaunchChoice() {
        synchronized (launchLock) {
            if (!launchPending) return null;
            launchPending = false;
            return new String[] { launchExtra, launchData };
        }
    }

    /** Carpeta de las .so del APK (libnectar.so, libnectar2.so, ...). */
    public String nativeLibraryDir() {
        String dir = getApplicationInfo().nativeLibraryDir;
        return dir != null ? dir : "";
    }

    /**
     * Create shortcut. null si el launcher aceptó el pin; si no, un motivo
     * corto para el aviso del hub. Bloquea el hilo de SDL hasta el hilo de UI.
     */
    public String pinGameShortcut(String gameId, String title, byte[] png) {
        if (Looper.myLooper() == Looper.getMainLooper()) return pinGameShortcutOnUi(gameId, title, png);
        final String[] result = new String[1];
        CountDownLatch latch = new CountDownLatch(1);
        runOnUiThread(() -> {
            try {
                result[0] = pinGameShortcutOnUi(gameId, title, png);
            } catch (Exception e) {
                result[0] = "could not pin the shortcut";
            }
            latch.countDown();
        });
        try {
            latch.await();
        } catch (InterruptedException e) {
            return "could not pin the shortcut";
        }
        return result[0];
    }

    private String pinGameShortcutOnUi(String gameId, String title, byte[] png) {
        if (!"pikmin1".equals(gameId) && !"pikmin2".equals(gameId)) return "unknown game";
        if (title == null || title.isEmpty()) title = "pikmin2".equals(gameId) ? "Pikmin 2" : "Pikmin";
        if (!ShortcutManagerCompat.isRequestPinShortcutSupported(this)) {
            return "this home screen cannot pin shortcuts";
        }
        ShortcutInfoCompat info = new ShortcutInfoCompat.Builder(this, "pin-" + gameId)
                .setShortLabel(title)
                .setLongLabel(title)
                .setIcon(shortcutIcon(png))
                .setIntent(GameLaunch.view(this, gameId))
                .build();
        try {
            if (!ShortcutManagerCompat.requestPinShortcut(this, info, null)) {
                return "this home screen cannot pin shortcuts";
            }
        } catch (Exception e) {
            return "this home screen cannot pin shortcuts";
        }
        return null;
    }

    /** Cover as an adaptive icon, inset so the launcher mask keeps the picture.
     *  No cover (or a bad PNG): the Open Nectar icon. */
    private IconCompat shortcutIcon(byte[] png) {
        Bitmap decoded = png != null && png.length > 0 ? BitmapFactory.decodeByteArray(png, 0, png.length) : null;
        if (decoded == null || decoded.getWidth() < 1 || decoded.getHeight() < 1) {
            return IconCompat.createWithResource(this, R.mipmap.ic_launcher);
        }
        try {
            Bitmap adaptive = adaptiveCover(decoded);
            if (adaptive != decoded) decoded.recycle();
            return IconCompat.createWithAdaptiveBitmap(adaptive);
        } catch (Exception e) {
            return IconCompat.createWithResource(this, R.mipmap.ic_launcher);
        }
    }

    /** The cover sits in the inner 72/108 of the adaptive icon, so the mask doesn't crop it. */
    private static Bitmap adaptiveCover(Bitmap cover) {
        int size = Math.max(cover.getWidth(), cover.getHeight());
        Bitmap out = Bitmap.createBitmap(size, size, Bitmap.Config.ARGB_8888);
        Canvas canvas = new Canvas(out);
        float inner = size * (72f / 108f);
        float scale = Math.min(inner / cover.getWidth(), inner / cover.getHeight());
        float w = cover.getWidth() * scale;
        float h = cover.getHeight() * scale;
        float left = (size - w) / 2f;
        float top = (size - h) / 2f;
        canvas.drawBitmap(cover, null, new RectF(left, top, left + w, top + h), null);
        return out;
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

    /** Instala el pack de texturas que descargó el launcher (botón HD
     *  textures). Devuelve null si va bien o el error. */
    public String installTexturePack(String zipPath, String gameDir) {
        try {
            TexturePack.installZipFile(new File(zipPath), new File(gameDir));
            return null;
        } catch (Exception e) {
            return e.getMessage() == null ? e.toString() : e.getMessage();
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

    /** Botón Change folder: StartActivity elige la carpeta nueva, traslada
     *  todo y vuelve a abrir el launcher. Llamado desde el hilo de SDL antes
     *  de que el launcher termine. */
    public void changeDataFolder() {
        Intent intent = new Intent(this, StartActivity.class);
        intent.putExtra(StartActivity.EXTRA_CHANGE, true);
        startActivity(intent);
    }

    /** Llamado desde el hilo de SDL antes de que el launcher termine. */
    public void startGame(String library, String gameDir) {
        Intent intent = new Intent(this, NectarActivity.class);
        intent.putExtra(NectarActivity.EXTRA_LIBRARY, library);
        intent.putExtra(NectarActivity.EXTRA_GAME_DIR, gameDir);
        // The game is singleInstance in its own process. This brings that task
        // forward; if it is already up, onNewIntent receives this library.
        intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TOP
                | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(intent);
    }
}
