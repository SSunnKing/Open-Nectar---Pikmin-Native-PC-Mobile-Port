package org.opennectar;

import android.content.Context;
import android.content.SharedPreferences;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.FileUtils;
import android.provider.DocumentsContract;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.ArrayList;
import java.util.List;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Future;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Carpeta de datos de la app (issue #176): los dos juegos instalados, sus
 * partidas, ajustes, mods y mapas viven en una carpeta del almacenamiento
 * interno que elige el usuario (por defecto /storage/emulated/0/OpenNectar),
 * visible desde cualquier gestor de archivos. Requiere el permiso de acceso a
 * todos los archivos: el juego y el launcher abren sus ficheros con fopen.
 *
 * Antes todo vivía en getFilesDir() (o, si se copió a mano, en la carpeta
 * externa privada). migrate() lo traslada a la carpeta nueva.
 */
public final class DataFolder {
    private static final String PREFS = "data_folder";
    private static final String KEY_PATH = "path";
    /** Nombre por defecto, en la raíz del almacenamiento interno. */
    public static final String DEFAULT_NAME = "OpenNectar";

    public interface Progress {
        void onProgress(long done, long total, String file);
    }

    private DataFolder() {}

    /** Lo que guardó la última configuración, exista o no; null si nunca. */
    public static File configured(Context context) {
        String path = prefs(context).getString(KEY_PATH, null);
        return path != null && !path.isEmpty() ? new File(path) : null;
    }

    public static File defaultFolder() {
        return new File(Environment.getExternalStorageDirectory(), DEFAULT_NAME);
    }

    /** La carpeta configurada si se puede usar ahora mismo; si no, null. */
    public static File ready(Context context) {
        File dir = configured(context);
        return dir != null && hasPermission(context) && usable(dir) ? dir : null;
    }

    /** Carpeta de datos en uso: la configurada o, mientras no lo esté, la
     *  interna de siempre (StartActivity no deja llegar al launcher sin ella). */
    public static File current(Context context) {
        File dir = ready(context);
        return dir != null ? dir : context.getFilesDir();
    }

    static void save(Context context, File dir) {
        prefs(context).edit().putString(KEY_PATH, dir.getAbsolutePath()).apply();
    }

    public static boolean hasPermission(Context context) {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) return Environment.isExternalStorageManager();
        return context.checkSelfPermission(android.Manifest.permission.WRITE_EXTERNAL_STORAGE)
            == android.content.pm.PackageManager.PERMISSION_GRANTED;
    }

    /** Existe (o se puede crear) y se puede escribir en ella. */
    public static boolean usable(File dir) {
        if (!dir.isDirectory() && !dir.mkdirs()) return false;
        File probe = new File(dir, ".nectar-write-test");
        try (OutputStream out = new FileOutputStream(probe)) {
            out.write(1);
        } catch (IOException e) {
            return false;
        } finally {
            probe.delete();
        }
        return true;
    }

    /**
     * Ruta de una carpeta elegida con ACTION_OPEN_DOCUMENT_TREE. Solo el
     * almacenamiento del dispositivo (interno o tarjeta SD): "primary:x" es
     * /storage/emulated/0/x y "ABCD-1234:x" es /storage/ABCD-1234/x. Null si
     * es de otro proveedor (Drive, Descargas...), que no tiene ruta.
     */
    public static File pathFromTree(Uri tree) {
        if (tree == null || !"com.android.externalstorage.documents".equals(tree.getAuthority())) return null;
        String id = DocumentsContract.getTreeDocumentId(tree);
        int colon = id.indexOf(':');
        if (colon < 0) return null;
        String volume = id.substring(0, colon);
        String rel = id.substring(colon + 1);
        File base = "primary".equalsIgnoreCase(volume) ? Environment.getExternalStorageDirectory()
                                                       : new File("/storage", volume);
        return rel.isEmpty() ? base : new File(base, rel);
    }

    /** Carpetas de donde trasladar datos anteriores, por orden de prioridad. */
    public static List<File> legacySources(Context context, File target) {
        List<File> out = new ArrayList<>();
        addSource(out, context.getFilesDir(), target);
        addSource(out, context.getExternalFilesDir(null), target);
        return out;
    }

    private static void addSource(List<File> out, File dir, File target) {
        if (dir == null || !dir.isDirectory() || sameFile(dir, target)) return;
        String[] names = dir.list();
        if (names != null && names.length > 0) out.add(dir);
    }

    public static long sizeOf(File file) {
        if (file.isFile()) return file.length();
        long total = 0;
        File[] children = file.listFiles();
        if (children != null) for (File child : children) total += sizeOf(child);
        return total;
    }

    /**
     * Traslada el contenido de `sources` a `target`, fichero a fichero: cada
     * uno se borra del origen cuando ya está copiado, así que si se corta a
     * medias, la próxima vez sigue donde iba. Lo que ya existe en `target` se
     * conserva y su original se queda donde estaba (una carpeta reutilizada
     * tras reinstalar la app no pierde nada); de varias fuentes gana la
     * primera. Bloquea: llamar desde un hilo.
     */
    public static void migrate(List<File> sources, File target, Progress progress) throws IOException {
        // Las instalaciones anteriores dejaban los nombres japoneses de Pikmin 2
        // en Shift-JIS: Java no puede abrirlos y el almacenamiento compartido no
        // los admite. Se pasan a UTF-8 en el origen, como en Windows y macOS.
        for (File source : sources) Installer.nativeFixNames(source.getAbsolutePath());
        // En el mismo almacenamiento (cambiar de carpeta), renombrar es
        // instantáneo y no ocupa más; entre /data y /storage falla y se copia.
        for (File source : sources) {
            File[] children = source.listFiles();
            if (children == null) continue;
            for (File child : children) {
                File to = new File(target, child.getName());
                if (!to.exists()) child.renameTo(to);
            }
        }
        // Lo que queda se copia: a /storage se escribe a través de FUSE, donde
        // cada fichero cuesta varias idas y vueltas (crear, escribir, renombrar)
        // y los juegos son miles de ficheros pequeños. Se copian varios a la vez
        // y cada uno con FileUtils.copy (sendfile/splice en el núcleo, sin pasar
        // por un búfer de Java).
        List<File[]> files = new ArrayList<>();
        List<File[]> dirs = new ArrayList<>();
        long total = 0;
        for (File source : sources) {
            File[] children = source.listFiles();
            if (children == null) continue;
            for (File child : children) total += collect(child, new File(target, child.getName()), files, dirs);
        }
        long free = target.getUsableSpace();
        if (total > free) {
            throw new IOException("Not enough free space: " + (total >> 20) + " MB needed, " + (free >> 20)
                                  + " MB available.");
        }
        for (File[] dir : dirs) {
            if (!dir[1].isDirectory() && !dir[1].mkdirs()) throw new IOException("Could not create " + dir[1]);
        }

        final long all = total;
        final AtomicLong done = new AtomicLong();
        final AtomicLong lastReport = new AtomicLong();
        ExecutorService pool = Executors.newFixedThreadPool(COPY_THREADS);
        try {
            List<Future<?>> jobs = new ArrayList<>();
            for (File[] pair : files) {
                jobs.add(pool.submit(() -> {
                    long size = pair[0].length();
                    moveFile(pair[0], pair[1]);
                    long now = System.nanoTime();
                    long bytes = done.addAndGet(size);
                    long last = lastReport.get();
                    if (now - last > 100_000_000L && lastReport.compareAndSet(last, now)) {
                        progress.onProgress(bytes, all, pair[0].getName());
                    }
                    return null;
                }));
            }
            for (Future<?> job : jobs) {
                try {
                    job.get();
                } catch (ExecutionException e) {
                    Throwable cause = e.getCause();
                    throw cause instanceof IOException ? (IOException) cause : new IOException(cause);
                } catch (InterruptedException e) {
                    Thread.currentThread().interrupt();
                    throw new IOException("Interrupted", e);
                }
            }
        } finally {
            pool.shutdownNow();
        }
        progress.onProgress(all, all, "");
        // Carpetas de origen vacías, de dentro afuera.
        for (int i = dirs.size() - 1; i >= 0; i--) dirs.get(i)[0].delete();
    }

    private static final int COPY_THREADS = 4;

    /** Recorre `from`: anota las carpetas ({origen, destino}) y los ficheros
     *  por copiar. Devuelve los bytes por copiar. */
    private static long collect(File from, File to, List<File[]> files, List<File[]> dirs) {
        if (from.isDirectory()) {
            dirs.add(new File[] { from, to });
            long bytes = 0;
            File[] children = from.listFiles();
            if (children != null) {
                for (File child : children) bytes += collect(child, new File(to, child.getName()), files, dirs);
            }
            return bytes;
        }
        files.add(new File[] { from, to });
        return from.length();
    }

    private static void moveFile(File from, File to) throws IOException {
        if (to.exists()) return; // ya estaba en el destino: se conserva, y el original también
        File partial = new File(to.getPath() + ".part");
        try (FileInputStream in = new FileInputStream(from); FileOutputStream out = new FileOutputStream(partial)) {
            FileUtils.copy(in, out);
        } catch (IOException e) {
            partial.delete();
            throw new IOException("Could not copy " + from + ": " + e.getMessage(), e);
        }
        if (!partial.renameTo(to)) {
            partial.delete();
            throw new IOException("Could not write " + to);
        }
        from.delete();
    }

    static boolean sameFile(File a, File b) {
        try {
            return a.getCanonicalPath().equals(b.getCanonicalPath());
        } catch (IOException e) {
            return a.getAbsolutePath().equals(b.getAbsolutePath());
        }
    }

    /** `inner` está dentro de `outer` (o es la misma). */
    static boolean isInside(File inner, File outer) {
        try {
            String in = inner.getCanonicalPath() + "/";
            String out = outer.getCanonicalPath() + "/";
            return in.startsWith(out);
        } catch (IOException e) {
            return false;
        }
    }

    private static SharedPreferences prefs(Context context) {
        return context.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
    }
}
