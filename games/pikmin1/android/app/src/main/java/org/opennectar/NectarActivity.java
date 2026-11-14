package org.opennectar;

import android.content.Intent;
import android.os.Build;
import android.os.Bundle;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;
import android.view.WindowInsetsController;
import android.view.WindowManager;

import org.libsdl.app.SDLActivity;
import org.opennectar.SaveTransfer;
import org.opennectar.TexturePack;

/**
 * Actividad del juego. SDLActivity hace todo el trabajo (superficie, EGL,
 * entrada, ciclo de vida); aquí sólo se le dice qué bibliotecas cargar y se
 * oculta la interfaz del sistema para que el juego ocupe la pantalla entera.
 * libnectar.so exporta SDL_main, que es el main() de pc_port/pc_main.cpp.
 */
public class NectarActivity extends SDLActivity {
    /** Biblioteca del juego: nectar, nectar-pal (Pikmin), nectar2, nectar2-pal (Pikmin 2). */
    public static final String EXTRA_LIBRARY = "org.opennectar.LIBRARY";
    /** Carpeta del juego (la que tiene assets/ y pikmin_settings.conf). */
    public static final String EXTRA_GAME_DIR = "org.opennectar.GAME_DIR";
    /** Argumentos de línea de órdenes del juego (el menú online los pone al reiniciar). */
    public static final String EXTRA_ARGS = "org.opennectar.ARGS";

    @Override
    protected String[] getArguments() {
        String[] args = getIntent() != null ? getIntent().getStringArrayExtra(EXTRA_ARGS) : null;
        return args != null ? args : new String[0];
    }

    /** Lo llama pc_online_android_toast(): un aviso corto sobre el juego
     *  (p. ej. "código de respuesta copiado" mientras espera al anfitrión). */
    public void showToast(final String text) {
        runOnUiThread(() -> android.widget.Toast.makeText(this, text, android.widget.Toast.LENGTH_LONG).show());
    }

    /** Lo llama pc_online_relaunch() (menú online del título): vuelve a abrir
     *  el juego con estos argumentos, en un proceso nuevo. */
    public void restartWithArguments(final String[] args) {
        runOnUiThread(() -> {
            android.content.Intent intent = new android.content.Intent(this, NectarActivity.class);
            if (getIntent() != null && getIntent().getExtras() != null) intent.putExtras(getIntent().getExtras());
            intent.putExtra(EXTRA_ARGS, args);
            intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK
                | android.content.Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(intent);
            android.os.Process.killProcess(android.os.Process.myPid());
        });
    }

    /** La que pide el launcher; sin ella, la de siempre de Pikmin 1. */
    private java.io.File gameDirectory() {
        String dir = getIntent() != null ? getIntent().getStringExtra(EXTRA_GAME_DIR) : null;
        return dir != null ? new java.io.File(dir) : Installer.gameDir(this);
    }

    @Override
    public void loadLibraries() {
        // Antes de cargar el juego: sus inicializadores estáticos leen
        // pikmin_settings.conf del directorio actual (idioma del disco PAL).
        Installer.nativeChdir(gameDirectory().getAbsolutePath());
        super.loadLibraries();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", gameLibrary() };
    }

    /** "nectar" (USA) o "nectar-pal" (Europa), según el disco instalado: el
     *  instalador lo deja en assets/.pikmin-build. */
    private String gameLibrary() {
        String requested = getIntent() != null ? getIntent().getStringExtra(EXTRA_LIBRARY) : null;
        if (isGameLibrary(requested)) return requested;
        java.io.File marker = new java.io.File(gameDirectory(), "assets/.pikmin-build");
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(marker))) {
            String line = in.readLine();
            if (line != null) {
                line = line.trim();
                if (isGameLibrary(line)) return line;
            }
        } catch (java.io.IOException ignored) {}
        return "nectar";
    }

    private static boolean isGameLibrary(String name) {
        return name != null && (name.equals("nectar") || name.equals("nectar-pal")
                || name.equals("nectar2") || name.equals("nectar2-pal"));
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        // A shortcut can ask for the other game while this process is still
        // the one that loaded the first library. SDL will not load a second
        // .so here, so start clean the way the online menu already restarts.
        String next = intent != null ? intent.getStringExtra(EXTRA_LIBRARY) : null;
        String current = gameLibrary();
        if (next != null && isGameLibrary(next) && !next.equals(current)) {
            Intent restart = new Intent(this, NectarActivity.class);
            restart.putExtra(EXTRA_LIBRARY, next);
            if (intent.getStringExtra(EXTRA_GAME_DIR) != null) {
                restart.putExtra(EXTRA_GAME_DIR, intent.getStringExtra(EXTRA_GAME_DIR));
            }
            restart.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK);
            startActivity(restart);
            android.os.Process.killProcess(android.os.Process.myPid());
            return;
        }
        if (intent != null) setIntent(intent);
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // El menú F1 (nativo) llama a JNI para abrir el selector de packs o
        // reiniciar la app; necesita la actividad. Se registra aquí, después
        // de super.onCreate(): SDK libnectar*.so ya está cargada.
        TexturePack.nativeRegisterActivity(this);
        // Online co-op: the router to ask for a forwarded port.
        NetGateway.publish(this);
        // Una partida en red (el menú online relanza con argumentos): sin el
        // ahorro de energía de la Wi-Fi, que retiene los paquetes entrantes
        // 100-300 ms y deja el lockstep a cámara lenta.
        if (getArguments().length > 0) acquireLowLatencyWifi();
        // Dibujar también bajo el recorte de la cámara (notch) en vez de dejar
        // una franja negra.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.P) {
            WindowManager.LayoutParams lp = getWindow().getAttributes();
            lp.layoutInDisplayCutoutMode = WindowManager.LayoutParams.LAYOUT_IN_DISPLAY_CUTOUT_MODE_SHORT_EDGES;
            getWindow().setAttributes(lp);
        }
        hideSystemUi();
    }

    // ─── Packs de texturas (PLAN_TEXTURAS_HD, fase 2) ───────────────────────
    // Las llamadas llegan de JNI (menu F1) en el hilo del juego: reenviarlas
    // al hilo de UI para tocar la Actividad.

    /** Lo llama pc_texpack_android_open_picker() desde el menú F1. */
    public void openTexturePackPicker() {
        runOnUiThread(() -> TexturePack.openTexturePackPicker(this));
    }

    /** Selector separado: los modelos HD no son packs de texturas. */
    public void openModelPackPicker(final int kind) {
        runOnUiThread(() -> TexturePack.openModelPackPicker(this, kind));
    }

    /** Lo llama pc_texpack_android_restart() tras activar un pack. */
    public void restartTexturePacks() {
        TexturePack.restartTexturePacks(this);
    }

    // ─── Copia de seguridad de partidas (issue #36) ─────────────────────────

    /** Lo llama pc_save_android_open_backup() desde el menú F1. */
    public void openSaveBackupPicker() {
        runOnUiThread(() -> SaveTransfer.openBackupPicker(this));
    }

    /** Lo llama pc_save_android_open_restore() desde el menú F1. */
    public void openSaveRestorePicker() {
        runOnUiThread(() -> SaveTransfer.openRestorePicker(this));
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, android.content.Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == TexturePack.REQ_TEXTURE_PACK) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                TexturePack.nativeInstallFinished(false, "No file selected.");
            } else {
                final android.net.Uri uri = data.getData();
                // La extracción de cientos de MB no puede bloquear el hilo de UI; el
                // juego puede seguir corriendo mientras tanto.
                new Thread(() -> TexturePack.install(this, uri), "nectar-texture-pack").start();
            }
            return;
        }
        if (requestCode == TexturePack.REQ_MODEL_PACK) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                TexturePack.nativeInstallFinished(false, "No file selected.");
            } else {
                final android.net.Uri uri = data.getData();
                new Thread(() -> TexturePack.installModel(this, uri, TexturePack.pendingModelKind()),
                           "nectar-model-pack").start();
            }
            return;
        }
        if (requestCode == SaveTransfer.REQ_SAVE_BACKUP) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                SaveTransfer.nativeSaveTransferFinished(false, "No destination selected.");
            } else {
                final android.net.Uri uri = data.getData();
                new Thread(() -> SaveTransfer.backup(this, uri), "nectar-save-backup").start();
            }
            return;
        }
        if (requestCode == SaveTransfer.REQ_SAVE_RESTORE) {
            if (resultCode != RESULT_OK || data == null || data.getData() == null) {
                SaveTransfer.nativeSaveTransferFinished(false, "No file selected.");
            } else {
                final android.net.Uri uri = data.getData();
                new Thread(() -> SaveTransfer.restore(this, uri), "nectar-save-restore").start();
            }
        }
    }

    private android.net.wifi.WifiManager.WifiLock wifiLock;

    private void acquireLowLatencyWifi() {
        try {
            android.net.wifi.WifiManager wifi =
                (android.net.wifi.WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            if (wifi == null) return;
            wifiLock = wifi.createWifiLock(android.net.wifi.WifiManager.WIFI_MODE_FULL_LOW_LATENCY, "OpenNectar:netplay");
            wifiLock.setReferenceCounted(false);
            wifiLock.acquire();
        } catch (RuntimeException e) {
            wifiLock = null;
        }
    }

    @Override
    protected void onDestroy() {
        if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
        TexturePack.nativeUnregisterActivity();
        super.onDestroy();
    }

    @Override
    public void onWindowFocusChanged(boolean hasFocus) {
        super.onWindowFocusChanged(hasFocus);
        // Volver a ocultarla al recuperar el foco: el sistema la muestra al
        // salir y entrar, y con un deslizamiento desde el borde.
        if (hasFocus) hideSystemUi();
    }

    /** Modo inmersivo pegajoso: sin barra de estado ni de navegación; un
     *  deslizamiento desde el borde las enseña un momento y se vuelven a ir. */
    private void hideSystemUi() {
        // La ruta de SDL: además de ocultar, deja mFullscreenModeActive a
        // true, y con eso SDLActivity vuelve a esconder las barras cada vez
        // que el sistema las enseña.
        SDLActivity.setWindowStyle(true);
        Window window = getWindow();
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            window.setDecorFitsSystemWindows(false);
            WindowInsetsController controller = window.getInsetsController();
            if (controller != null) {
                controller.hide(WindowInsets.Type.statusBars() | WindowInsets.Type.navigationBars());
                controller.setSystemBarsBehavior(WindowInsetsController.BEHAVIOR_SHOW_TRANSIENT_BARS_BY_SWIPE);
            }
        } else {
            window.getDecorView().setSystemUiVisibility(
                View.SYSTEM_UI_FLAG_IMMERSIVE_STICKY
                | View.SYSTEM_UI_FLAG_LAYOUT_STABLE
                | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_LAYOUT_FULLSCREEN
                | View.SYSTEM_UI_FLAG_HIDE_NAVIGATION
                | View.SYSTEM_UI_FLAG_FULLSCREEN);
        }
    }
}
