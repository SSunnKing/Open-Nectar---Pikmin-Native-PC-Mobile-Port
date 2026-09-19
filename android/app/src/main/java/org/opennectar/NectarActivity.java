package org.opennectar;

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
    @Override
    public void loadLibraries() {
        // Antes de cargar el juego: sus inicializadores estáticos leen
        // pikmin_settings.conf del directorio actual (idioma del disco PAL).
        Installer.nativeChdir(Installer.gameDir(this).getAbsolutePath());
        super.loadLibraries();
    }

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", gameLibrary() };
    }

    /** "nectar" (USA) o "nectar-pal" (Europa), según el disco instalado: el
     *  instalador lo deja en assets/.pikmin-build. */
    private String gameLibrary() {
        java.io.File marker = new java.io.File(Installer.gameDir(this), "assets/.pikmin-build");
        try (java.io.BufferedReader in = new java.io.BufferedReader(new java.io.FileReader(marker))) {
            String line = in.readLine();
            if (line != null) {
                line = line.trim();
                if (line.equals("nectar") || line.equals("nectar-pal")) return line;
            }
        } catch (java.io.IOException ignored) {}
        return "nectar";
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // El menú F1 (nativo) llama a JNI para abrir el selector de packs o
        // reiniciar la app; necesita la actividad. Se registra aquí, después
        // de super.onCreate(): SDK libnectar*.so ya está cargada.
        TexturePack.nativeRegisterActivity(this);
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

    @Override
    protected void onDestroy() {
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
