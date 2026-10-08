package org.opennectar;

import java.io.File;

/** Puente con libnectar_installer.so (pc_port/android/pc_installer_jni.cpp). */
public final class Installer {
    static { System.loadLibrary("nectar_installer"); }

    public interface Listener {
        void onProgress(int percent, String text);
    }

    /** Descripción del disco o null; errorOut[0] recibe el motivo. */
    public static native String nativeInspect(int fd, String[] errorOut);
    /** "nectar" / "nectar-pal" según el disco, o null. */
    public static native String nativeRequiredBuild(int fd);
    /** chdir del proceso a la carpeta del juego (ver pc_installer_jni.cpp). */
    public static native boolean nativeChdir(String dir);
    /** null si fue bien, o el mensaje de error. Bloquea: llamar desde un hilo. */
    public static native String nativeInstall(int fd, String gameDir, Listener listener);

    /** Carpeta del juego: la misma que elige pc_android_init() en el juego. */
    public static File gameDir(android.content.Context context) {
        return context.getFilesDir();
    }

    public static boolean assetsInstalled(android.content.Context context) {
        File assets = new File(gameDir(context), "assets");
        return new File(assets, ".pikmin-assets").isFile()
            && new File(assets, "dataDir/parms/gamePrms.bin").isFile();
    }

    private Installer() {}
}
