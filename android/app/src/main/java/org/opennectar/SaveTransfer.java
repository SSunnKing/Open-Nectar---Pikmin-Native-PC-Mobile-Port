package org.opennectar;

import android.app.Activity;
import android.net.Uri;

import java.io.BufferedOutputStream;
import java.io.File;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;
import java.util.zip.ZipOutputStream;

/**
 * Copia de seguridad de la partida (issue #36).
 *
 * La tarjeta de memoria del port vive en <carpeta del juego>/save; en Android
 * esa carpeta queda en el almacenamiento privado de la app, que el jugador no puede alcanzar
 * con un gestor de archivos ni copiar a otro dispositivo. Esta clase exporta
 * e importa esa carpeta como un .zip a través del Storage Access Framework:
 * el jugador elige el destino (Descargas, Drive, SD...) y no hace falta ningún
 * permiso de almacenamiento.
 *
 * Los métodos native viven en libnectar*.so, ya cargada por NectarActivity.
 */
public final class SaveTransfer {
    private static final int MAX_FILES = 256;
    private static final long MAX_UNCOMPRESSED_BYTES = 64L * 1024L * 1024L;
    /** Códigos de startActivityForResult. Deben coincidir con la actividad. */
    public static final int REQ_SAVE_BACKUP = 2049;
    public static final int REQ_SAVE_RESTORE = 2050;

    public static native String nativeSaveDir();
    public static native void nativeSaveTransferFinished(boolean ok, String message);

    /** Pide crear un documento .zip (exportar). Se llama desde JNI. */
    public static void openBackupPicker(final Activity activity) {
        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_CREATE_DOCUMENT);
        intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        intent.setType("application/zip");
        intent.putExtra(android.content.Intent.EXTRA_TITLE, "OpenNectar-save.zip");
        activity.startActivityForResult(intent, REQ_SAVE_BACKUP);
    }

    /** Pide abrir un documento .zip (importar). Se llama desde JNI. */
    public static void openRestorePicker(final Activity activity) {
        android.content.Intent intent = new android.content.Intent(android.content.Intent.ACTION_OPEN_DOCUMENT);
        intent.addCategory(android.content.Intent.CATEGORY_OPENABLE);
        intent.setType("*/*");
        intent.putExtra(android.content.Intent.EXTRA_MIME_TYPES, new String[] {
            "application/zip",
            "application/x-zip-compressed",
            "application/octet-stream",
        });
        activity.startActivityForResult(intent, REQ_SAVE_RESTORE);
    }

    /** Escribe un .zip con el contenido de la carpeta de guardado. Bloquea. */
    public static void backup(Activity activity, Uri uri) {
        final String saveDir = nativeSaveDir();
        if (saveDir == null || saveDir.isEmpty()) {
            nativeSaveTransferFinished(false, "The save folder is not ready.");
            return;
        }
        final File root = new File(saveDir);
        if (!root.isDirectory()) {
            nativeSaveTransferFinished(false, "There is no save data to export yet.");
            return;
        }
        int files = 0;
        try (OutputStream raw = activity.getContentResolver().openOutputStream(uri, "wt")) {
            if (raw == null) throw new Exception("Could not open the selected file.");
            try (ZipOutputStream zip = new ZipOutputStream(new BufferedOutputStream(raw))) {
                // save/ also contains the regenerable shader cache. A backup
                // is only the two emulated memory cards; exporting the cache
                // made our own ZIP fail the restore validator.
                files += addDirectory(root, new File(root, "card0"), zip);
                files += addDirectory(root, new File(root, "card1"), zip);
            }
        } catch (Exception e) {
            nativeSaveTransferFinished(false,
                e.getMessage() == null ? "Could not export the save." : e.getMessage());
            return;
        }
        if (files <= 0) {
            nativeSaveTransferFinished(false, "There is no save data to export yet.");
            return;
        }
        nativeSaveTransferFinished(true, "Exported " + files + " save file(s). Keep the .zip somewhere safe.");
    }

    /** Extrae y valida el .zip en una carpeta temporal antes de sustituir la
     *  tarjeta. Un archivo truncado nunca deja el guardado actual a medias. */
    public static void restore(Activity activity, Uri uri) {
        final String saveDir = nativeSaveDir();
        if (saveDir == null || saveDir.isEmpty()) {
            nativeSaveTransferFinished(false, "The save folder is not ready.");
            return;
        }
        final File root = new File(saveDir);
        final File parent = root.getParentFile();
        if (parent == null || (!parent.isDirectory() && !parent.mkdirs())) {
            nativeSaveTransferFinished(false, "Could not access the game folder.");
            return;
        }
        final File staging = new File(parent, root.getName() + ".importing");
        final File previous = new File(parent, root.getName() + ".previous");
        deleteTree(staging);
        deleteTree(previous);
        if (!staging.mkdirs()) {
            nativeSaveTransferFinished(false, "Could not prepare the save import.");
            return;
        }
        int files = 0;
        long bytes = 0;
        try (InputStream raw = activity.getContentResolver().openInputStream(uri)) {
            if (raw == null) throw new Exception("Could not open the selected file.");
            try (ZipInputStream zip = new ZipInputStream(raw)) {
                ZipEntry entry;
                byte[] buffer = new byte[64 * 1024];
                while ((entry = zip.getNextEntry()) != null) {
                    if (entry.isDirectory()) continue;
                    final String normalized = entry.getName().replace('\\', '/');
                    // Backups made by the first implementation also included
                    // save/shader_cache. Ignore any unrelated entries so those
                    // already-exported ZIPs remain usable, but never extract
                    // them. At least one actual card file is still required.
                    if (!(normalized.startsWith("card0/") || normalized.startsWith("card1/"))) {
                        zip.closeEntry();
                        continue;
                    }
                    final File out = safeSaveFile(staging, normalized);
                    if (out == null) throw new Exception("The archive contains an unsafe save path.");
                    if (++files > MAX_FILES) throw new Exception("The save archive contains too many files.");
                    mkdirs(out.getParentFile());
                    try (OutputStream os = new FileOutputStream(out)) {
                        int n;
                        while ((n = zip.read(buffer)) > 0) {
                            bytes += n;
                            if (bytes > MAX_UNCOMPRESSED_BYTES)
                                throw new Exception("The save archive is unexpectedly large.");
                            os.write(buffer, 0, n);
                        }
                    }
                    zip.closeEntry();
                }
            }
        } catch (Exception e) {
            deleteTree(staging);
            nativeSaveTransferFinished(false,
                e.getMessage() == null ? "Could not import the save." : e.getMessage());
            return;
        }
        if (files <= 0) {
            deleteTree(staging);
            nativeSaveTransferFinished(false, "That archive has no save files in it.");
            return;
        }
        if (root.exists() && !root.renameTo(previous)) {
            deleteTree(staging);
            nativeSaveTransferFinished(false, "Could not replace the current save.");
            return;
        }
        if (!staging.renameTo(root)) {
            if (previous.exists()) previous.renameTo(root);
            deleteTree(staging);
            nativeSaveTransferFinished(false, "Could not finish the save import; the old save was kept.");
            return;
        }
        deleteTree(previous);
        nativeSaveTransferFinished(true, "Imported " + files + " save file(s). Restart to load it.");
    }

    // ── Helpers ─────────────────────────────────────────────────────────────

    private static int addDirectory(File root, File dir, ZipOutputStream zip) throws Exception {
        int count = 0;
        File[] children = dir.listFiles();
        if (children == null) return 0;
        for (File child : children) {
            if (child.isDirectory()) {
                count += addDirectory(root, child, zip);
            } else {
                final String rel = relativePath(root, child);
                if (rel == null) continue;
                zip.putNextEntry(new ZipEntry(rel));
                try (InputStream in = new java.io.FileInputStream(child)) {
                    byte[] buffer = new byte[64 * 1024];
                    int n;
                    while ((n = in.read(buffer)) > 0) zip.write(buffer, 0, n);
                }
                zip.closeEntry();
                count++;
            }
        }
        return count;
    }

    private static String relativePath(File root, File file) {
        try {
            final String base = root.getCanonicalPath();
            final String path = file.getCanonicalPath();
            if (!path.startsWith(base + File.separator)) return null;
            return path.substring(base.length() + 1).replace(File.separatorChar, '/');
        } catch (Exception e) {
            return null;
        }
    }

    /** Resuelve una ruta del zip bajo root sin dejar escapar de él ("../"). */
    private static File safeFile(File root, String name) {
        if (name == null) return null;
        String normalized = name.replace('\\', '/');
        while (normalized.startsWith("/")) normalized = normalized.substring(1);
        if (normalized.isEmpty()) return null;
        File candidate = new File(root, normalized);
        try {
            if (!candidate.getCanonicalPath().startsWith(root.getCanonicalPath() + File.separator)) return null;
        } catch (Exception e) {
            return null;
        }
        return candidate;
    }

    /** Backups only contain the two emulated memory-card directories. */
    private static File safeSaveFile(File root, String name) {
        if (name == null) return null;
        final String normalized = name.replace('\\', '/');
        if (!(normalized.startsWith("card0/") || normalized.startsWith("card1/"))) return null;
        return safeFile(root, normalized);
    }

    private static void mkdirs(File dir) {
        if (dir != null) dir.mkdirs();
    }

    private static void deleteTree(File file) {
        if (file == null || !file.exists()) return;
        if (file.isDirectory()) {
            File[] children = file.listFiles();
            if (children != null) for (File child : children) deleteTree(child);
        }
        file.delete();
    }

    private SaveTransfer() {}
}
