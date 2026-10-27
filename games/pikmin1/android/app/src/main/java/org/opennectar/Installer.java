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
    /** Renombra a UTF-8 los nombres que no lo son bajo dir (Shift-JIS de
     *  instalaciones anteriores). Devuelve cuántos cambió. */
    public static native int nativeFixNames(String dir);
    /** null si fue bien, o el mensaje de error. Bloquea: llamar desde un hilo. */
    public static native String nativeInstall(int fd, String gameDir, Listener listener);

    /** Carpeta de Pikmin 1: la de datos elegida por el usuario (DataFolder). */
    public static File gameDir(android.content.Context context) {
        return DataFolder.current(context);
    }

    public static boolean assetsInstalled(android.content.Context context) {
        File assets = new File(gameDir(context), "assets");
        return new File(assets, ".pikmin-assets").isFile()
            && new File(assets, "dataDir/parms/gamePrms.bin").isFile();
    }

    private Installer() {}
}
