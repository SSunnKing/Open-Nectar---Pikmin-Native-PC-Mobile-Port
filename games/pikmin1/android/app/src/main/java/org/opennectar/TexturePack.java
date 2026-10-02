package org.opennectar;

import android.app.Activity;
import android.content.ContentResolver;
import android.database.Cursor;
import android.net.Uri;
import android.provider.OpenableColumns;

import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.Locale;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

/**
 * Puente Java de la fase 2 de docs/PLAN_TEXTURAS_HD.md: el inductor de packs
 * de texturas.
 *
 * El menú F1 nativo pide abrir el selector de archivos (Storage Access
 * Framework) y aquí se elige un .zip/.rar, se extrae su árbol
 * Load/Textures/ a la carpeta del juego y se avisa al menú con
 * nativeInstallFinished(). Nosotros no redistribuimos packs: solo leemos el
 * que el jugador descargue.
 *
 * Los métodos native viven en libnectar*.so (ya cargada por NectarActivity);
 * esta clase no tiene System.loadLibrary propio.
 */
public final class TexturePack {
    /** Código de startActivityForResult del selector. Debe coincidir entre
     *  la actividad y este helper. */
    public static final int REQ_TEXTURE_PACK = 2048;
    public static final int REQ_MODEL_PACK = 2049;

    public static native void nativeRegisterActivity(Activity activity);
    public static native void nativeUnregisterActivity();
    public static native String nativeGameDir();
    public static native void nativeInstallFinished(boolean ok, String message);
    /** Progreso de la extracción (ficheros escritos) para que el menú F1 lo
     *  pinte y no deje activar ni reiniciar con el zip a medias. */
    public static native void nativeInstallProgress(int files);

    /** Marca que hay una extracción en curso o cortada (reinicio, cierre de
     *  la app). Se borra al terminar bien; si sigue ahí al arrancar, el
     *  menú avisa de que el pack está incompleto. */
    public static final String INCOMPLETE_MARKER = ".incomplete";
    private static int sProgressFiles = 0;

    private static void progress() {
        sProgressFiles++;
        if ((sProgressFiles & 15) == 0) nativeInstallProgress(sProgressFiles);
    }

    /** Abre el selector del sistema en el hilo de UI (lo llama JNI desde el
     *  hilo del juego, que no puede tocar startActivityForResult). */
    public static void openTexturePackPicker(final Activity activity) {
        openPackPicker(activity, REQ_TEXTURE_PACK);
    }

    /** Modelo elegido en el submenú HD Models (HdModelConverter.KIND_*) para el picker en curso. */
    private static int sPendingModelKind = HdModelConverter.KIND_OLIMAR;

    public static int pendingModelKind() { return sPendingModelKind; }

    public static void openModelPackPicker(final Activity activity, int kind) {
        sPendingModelKind = kind;
        openPackPicker(activity, REQ_MODEL_PACK);
    }

    private static void openPackPicker(final Activity activity, int requestCode) {
        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(android.content.Intent.EXTRA_MIME_TYPES, new String[] {
            "application/zip",
            "application/x-zip-compressed",
            "application/octet-stream",
            "application/vnd.rar",
            "application/x-rar-compressed",
        });
        activity.startActivityForResult(intent, requestCode);
    }

    /** Pide reiniciar la app entera para que el pack activo se indexe al
     *  arrancar (el índice se construye una vez, en pc_texpack_init). Se
     *  llama desde JNI; el lanzador inicial (InstallerActivity) recompone; si
     *  los assets ya están, vuelve directo al juego. */
    public static void restartTexturePacks(final Activity activity) {
        activity.runOnUiThread(() -> {
            android.content.Intent intent =
                activity.getPackageManager().getLaunchIntentForPackage(activity.getPackageName());
            if (intent == null) {
                activity.finishAndRemoveTask();
                android.os.Process.killProcess(android.os.Process.myPid());
                return;
            }
            intent.addFlags(android.content.Intent.FLAG_ACTIVITY_NEW_TASK
                | android.content.Intent.FLAG_ACTIVITY_CLEAR_TASK
                | android.content.Intent.FLAG_ACTIVITY_CLEAR_TOP);
            activity.startActivity(intent);
            android.os.Process.killProcess(android.os.Process.myPid());
        });
    }

    // -----------------------------------------------------------------------
    // Instalación
    // -----------------------------------------------------------------------

    /** Descomprime el pack elegido en <carpeta del juego>/Load/Textures/.
     *  Bloquea; llamar en un hilo. Al terminar informa a nativeInstallFinished
     *  (el menú F1 lo pinta durante unos segundos). */
    public static void install(Activity activity, Uri uri) {
        installPack(activity, uri, "Textures", false);
    }

    public static void installModel(Activity activity, Uri uri, int kind) {
        installPack(activity, uri, "Models", true, kind);
    }

    private static void installPack(Activity activity, Uri uri, String category, boolean modelPack) {
        installPack(activity, uri, category, modelPack, -1);
    }

    private static void installPack(Activity activity, Uri uri, String category, boolean modelPack, int modelKind) {
        final String name = displayName(activity, uri);
        final String lower = name == null ? "" : name.toLowerCase(Locale.ROOT);
        final String label = name == null ? "pack" : name;
        final String gameDir = nativeGameDir();
        if (gameDir == null || gameDir.isEmpty()) {
            nativeInstallFinished(false, "Game directory is not ready.");
            return;
        }
        File targetRoot = new File(gameDir, "Load");
        mkdirs(targetRoot);
        File categoryRoot = new File(targetRoot, category);
        mkdirs(categoryRoot);
        File marker = new File(categoryRoot, INCOMPLETE_MARKER);
        try { new FileOutputStream(marker).close(); } catch (Exception ignored) {}
        sProgressFiles = 0;
        nativeInstallProgress(0);

        final File cacheCopy;
        try {
            if (lower.endsWith(".rar")) {
                cacheCopy = copyToCache(activity, uri);
                installRar(cacheCopy, targetRoot, category, modelPack);
                deleteQuietly(cacheCopy);
            } else if (modelPack) {
                // Los rips públicos de Pikmin 3 (Collada + PNG) se convierten
                // aquí mismo a NHM; cualquier otro zip se trata como pack .nhm.
                cacheCopy = copyToCache(activity, uri);
                HdModelConverter.Result converted;
                try {
                    converted = HdModelConverter.convert(cacheCopy, categoryRoot, modelKind);
                } finally {
                    deleteQuietly(cacheCopy);
                }
                if (converted != null) {
                    deleteQuietly(marker);
                    nativeInstallFinished(true, "Converted " + label + " into " + converted.pack
                        + " (" + converted.files + " model files). Restart to use the HD model.");
                    return;
                }
                installZip(activity.getContentResolver(), uri, targetRoot, category, modelPack);
            } else {
                installZip(activity.getContentResolver(), uri, targetRoot, category, modelPack);
            }
        } catch (Exception e) {
            nativeInstallFinished(false, e.getMessage() == null ? "Could not install the pack." : e.getMessage());
            return;
        }

        int assets = countPackFiles(categoryRoot, modelPack);
        if (assets <= 0) {
            nativeInstallFinished(false,
                '"' + label + "\" has no supported Load/" + category + " files.");
            return;
        }
        deleteQuietly(marker);
        nativeInstallFinished(true,
            "Installed " + label + " (" + assets + (modelPack ? " model files). Restart to use the HD model."
                : " textures). Activate the pack and restart."));
    }

    private static String displayName(Activity activity, Uri uri) {
        try {
            Cursor cursor = activity.getContentResolver().query(uri,
                new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null);
            if (cursor != null) {
                try {
                    if (cursor.moveToFirst()) {
                        int col = cursor.getColumnIndex(OpenableColumns.DISPLAY_NAME);
                        if (col >= 0) return cursor.getString(col);
                    }
                } finally {
                    cursor.close();
                }
            }
        } catch (Exception ignored) {}
        return null;
    }

    // ── ZIP ────────────────────────────────────────────────────────────────

    private static void installZip(ContentResolver resolver, Uri uri, File targetRoot,
                                   String category, boolean modelPack)
            throws Exception {
        int extracted = 0;
        try (InputStream in = resolver.openInputStream(uri);
             ZipInputStream zip = new ZipInputStream(in)) {
            ZipEntry entry;
            while ((entry = zip.getNextEntry()) != null) {
                String rel = loadPackRelativePath(entry.getName(), category);
                if (rel == null) continue;
                File out = safeFile(targetRoot, rel);
                if (entry.isDirectory()) {
                    mkdirs(out);
                } else if (out != null) {
                    mkdirs(out.getParentFile());
                    try (OutputStream os = new FileOutputStream(out)) {
                        byte[] buffer = new byte[64 * 1024];
                        int n;
                        while ((n = zip.read(buffer)) > 0) os.write(buffer, 0, n);
                    }
                    if (isSupported(out, modelPack)) {
                        extracted++;
                    }
                    progress();
                }
                zip.closeEntry();
            }
        }
        if (extracted <= 0) throw new Exception("The archive contains no supported Load/" + category + " files.");
    }

    // ── RAR ────────────────────────────────────────────────────────────────

    private static void installRar(File rarFile, File targetRoot, String category,
                                   boolean modelPack) throws Exception {
        // junrar (com.github.junrar) es una implementación Java de UnRAR:
        // soporta RAR4 y RAR5 en lectura. El recurso SAF no es un fichero
        // reabrible por ruta, así que primero se copia a la caché.
        int extracted = 0;
        try (com.github.junrar.Archive archive = new com.github.junrar.Archive(rarFile)) {
            com.github.junrar.rarfile.FileHeader header;
            while ((header = archive.nextFileHeader()) != null) {
                if (header.isDirectory()) continue;
                String rel = loadPackRelativePath(header.getFileName(), category);
                if (rel == null) continue;
                File out = safeFile(targetRoot, rel);
                if (out == null) continue;
                mkdirs(out.getParentFile());
                try (OutputStream os = new FileOutputStream(out)) {
                    archive.extractFile(header, os);
                }
                if (isSupported(out, modelPack)) {
                    extracted++;
                }
                progress();
            }
        }
        if (extracted <= 0) throw new Exception("The RAR contains no supported Load/" + category + " files.");
    }

    private static File copyToCache(Activity activity, Uri uri) throws Exception {
        File cache = new File(activity.getCacheDir(), "texture-pack.rar");
        try (InputStream in = activity.getContentResolver().openInputStream(uri);
             OutputStream os = new FileOutputStream(cache)) {
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) os.write(buffer, 0, n);
        }
        return cache;
    }

    // ── Extracción de rutas ─────────────────────────────────────────────────

    /** Busca el segmento "Load/Textures" en una ruta de entrada (packs de
     *  Dolphin: "NombrePack/Load/Textures/GPI/..."). Devuelve el resto de la
     *  ruta a partir de la carpeta de GameID, o null si la entrada no toca
     *  texturas. */
    private static String loadPackRelativePath(String name, String category) {
        if (name == null) return null;
        String normalized = name.replace('\\', '/');
        String lower = normalized.toLowerCase(Locale.ROOT);
        String segment = "/load/" + category.toLowerCase(Locale.ROOT);
        int idx = lower.indexOf(segment);
        int segmentLength = segment.length();
        if (idx < 0) return null;
        int sub = idx + segmentLength;
        if (sub < normalized.length() && (normalized.charAt(sub) == '/' || normalized.charAt(sub) == '\\')) sub++;
        String rel = normalized.substring(sub);
        if (rel.isEmpty()) return null;
        return category + "/" + rel;
    }

    /** Resuelve una ruta relativa bajo root sin dejar escapar de él: los
     *  paquetes de texturas no deben poder sobrescribir nada fuera de
     *  Load/Textures/. */
    private static File safeFile(File root, String relative) {
        if (relative == null || relative.isEmpty()) return null;
        File candidate = new File(root, relative);
        String canonRoot;
        try {
            canonRoot = root.getCanonicalPath();
            if (!candidate.getCanonicalPath().startsWith(canonRoot + File.separator)) return null;
        } catch (Exception e) {
            return null;
        }
        return candidate;
    }

    private static void mkdirs(File dir) {
        if (dir != null) dir.mkdirs();
    }

    private static boolean isSupported(File file, boolean modelPack) {
        String lower = file.getName().toLowerCase(Locale.ROOT);
        return modelPack ? lower.endsWith(".nhm")
            : lower.endsWith(".dds") || lower.endsWith(".png");
    }

    private static int countPackFiles(File root, boolean modelPack) {
        if (root == null || !root.isDirectory()) return 0;
        int count = 0;
        File[] files = root.listFiles();
        if (files == null) return 0;
        for (File file : files) {
            if (file.isDirectory()) {
                count += countPackFiles(file, modelPack);
            } else {
                if (isSupported(file, modelPack)) count++;
            }
        }
        return count;
    }

    private static void deleteQuietly(File file) {
        if (file != null) file.delete();
    }

    private TexturePack() {}
}
