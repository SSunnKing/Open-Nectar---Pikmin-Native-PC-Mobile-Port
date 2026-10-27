package org.opennectar;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Color;
import android.graphics.Typeface;
import android.net.Uri;
import android.os.Build;
import android.os.Bundle;
import android.provider.DocumentsContract;
import android.provider.Settings;
import android.view.Gravity;
import android.view.View;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.File;
import java.util.List;

/**
 * Entrada de la app (issue #176). Con la carpeta de datos lista pasa directo
 * al launcher. Si no: pide el permiso de acceso a todos los archivos, deja
 * elegir la carpeta (por defecto Almacenamiento interno/OpenNectar) y
 * traslada a ella lo que hubiera de versiones anteriores, que guardaban todo
 * en la carpeta privada de la app.
 */
public class StartActivity extends Activity {
    /** Desde el botón Change folder del launcher: elegir otra carpeta y
     *  trasladar a ella todo lo de la actual. */
    public static final String EXTRA_CHANGE = "org.opennectar.CHANGE_FOLDER";

    private static final int REQ_ALL_FILES = 1;
    private static final int REQ_PICK_FOLDER = 2;
    private static final int REQ_LEGACY_PERMISSION = 3;

    private File chosen;
    /** Modo cambiar: la carpeta en uso, de la que sale todo. */
    private File current;
    private TextView intro;
    private TextView folderText;
    private TextView status;
    private ProgressBar progress;
    private Button grant;
    private Button pick;
    private Button useDefault;
    private Button confirm;
    private Thread worker;

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        boolean change = getIntent() != null && getIntent().getBooleanExtra(EXTRA_CHANGE, false);
        current = change ? DataFolder.ready(this) : null;
        if (!change && DataFolder.ready(this) != null) {
            openLauncher();
            return;
        }
        File previous = DataFolder.configured(this);
        chosen = previous != null ? previous : DataFolder.defaultFolder();
        buildUi(previous != null && current == null);
        refresh();
    }

    @Override
    protected void onResume() {
        super.onResume();
        // Vuelta de la pantalla de ajustes del permiso.
        if (grant != null && worker == null) refresh();
    }

    private void buildUi(boolean lostAccess) {
        int pad = dp(24);
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setGravity(Gravity.CENTER_HORIZONTAL);
        root.setPadding(pad, pad, pad, pad);

        TextView title = new TextView(this);
        title.setText(current != null ? "Change data folder" : "Open Nectar data folder");
        title.setTextSize(26);
        title.setTypeface(Typeface.DEFAULT_BOLD);
        title.setTextColor(Color.WHITE);
        title.setGravity(Gravity.CENTER);
        root.addView(title);

        intro = new TextView(this);
        intro.setText(current != null
                      ? "Everything in the current folder (installed games, saves, settings, texture packs, "
                        + "models and maps) is moved to the folder you choose.\n\nCurrent folder: "
                        + current.getAbsolutePath()
                      : (lostAccess ? "The data folder can no longer be used (access was removed or the folder is gone). "
                                     + "Grant access again or choose another folder.\n\n" : "")
                      + "Open Nectar keeps the installed games, saves, settings, texture packs, models and maps "
                      + "in a folder on your device storage, so you can reach them with any file manager.\n\n"
                      + "It needs permission to access all files (the games open their files directly). "
                      + "Anything from a previous version is moved into the new folder.");
        intro.setTextColor(Color.rgb(210, 220, 210));
        intro.setTextSize(15);
        intro.setPadding(0, dp(12), 0, dp(16));
        root.addView(intro);

        grant = button("1. Allow access to all files", v -> requestPermission());
        root.addView(grant);

        folderText = new TextView(this);
        folderText.setTextColor(Color.WHITE);
        folderText.setTextSize(15);
        folderText.setPadding(0, dp(16), 0, dp(8));
        root.addView(folderText);

        LinearLayout row = new LinearLayout(this);
        row.setOrientation(LinearLayout.HORIZONTAL);
        row.setGravity(Gravity.CENTER);
        pick = button("Choose another folder", v -> pickFolder());
        useDefault = button("Use default", v -> {
            chosen = DataFolder.defaultFolder();
            refresh();
        });
        row.addView(pick);
        row.addView(useDefault);
        root.addView(row);

        confirm = button(current != null ? "Move everything here" : "2. Use this folder", v -> startMigration());
        root.addView(confirm);
        if (current != null) root.addView(button("Cancel", v -> openLauncher()));

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000);
        progress.setVisibility(View.GONE);
        progress.setPadding(0, dp(16), 0, 0);
        root.addView(progress);

        status = new TextView(this);
        status.setTextColor(Color.rgb(255, 200, 120));
        status.setTextSize(14);
        status.setGravity(Gravity.CENTER);
        status.setPadding(0, dp(12), 0, 0);
        root.addView(status);

        ScrollView scroll = new ScrollView(this);
        scroll.setBackgroundColor(Color.rgb(18, 26, 18));
        scroll.setFillViewport(true);
        scroll.addView(root);
        setContentView(scroll);
    }

    private Button button(String text, View.OnClickListener listener) {
        Button b = new Button(this);
        b.setText(text);
        b.setOnClickListener(listener);
        return b;
    }

    private void refresh() {
        boolean permission = DataFolder.hasPermission(this);
        grant.setEnabled(!permission);
        grant.setText(permission ? "1. Access to all files: allowed" : "1. Allow access to all files");
        folderText.setText("Folder: " + chosen.getAbsolutePath());
        pick.setEnabled(permission);
        useDefault.setEnabled(permission);
        confirm.setEnabled(permission);
    }

    private void requestPermission() {
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.R) {
            try {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_APP_ALL_FILES_ACCESS_PERMISSION,
                                                  Uri.parse("package:" + getPackageName())),
                                       REQ_ALL_FILES);
            } catch (Exception e) {
                startActivityForResult(new Intent(Settings.ACTION_MANAGE_ALL_FILES_ACCESS_PERMISSION), REQ_ALL_FILES);
            }
        } else {
            requestPermissions(new String[] { android.Manifest.permission.READ_EXTERNAL_STORAGE,
                                              android.Manifest.permission.WRITE_EXTERNAL_STORAGE },
                               REQ_LEGACY_PERMISSION);
        }
    }

    @Override
    public void onRequestPermissionsResult(int requestCode, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(requestCode, permissions, results);
        refresh();
    }

    private void pickFolder() {
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
        // Abrir el selector en la raíz del almacenamiento interno.
        intent.putExtra(DocumentsContract.EXTRA_INITIAL_URI,
                        DocumentsContract.buildDocumentUri("com.android.externalstorage.documents", "primary:"));
        startActivityForResult(intent, REQ_PICK_FOLDER);
    }

    @Override
    protected void onActivityResult(int requestCode, int resultCode, Intent data) {
        super.onActivityResult(requestCode, resultCode, data);
        if (requestCode == REQ_PICK_FOLDER && resultCode == RESULT_OK && data != null) {
            File dir = DataFolder.pathFromTree(data.getData());
            if (dir == null) {
                status.setText("Choose a folder on the device storage (internal storage or SD card).");
                return;
            }
            chosen = dir;
            status.setText("");
        }
        refresh();
    }

    private void startMigration() {
        final File target = chosen;
        if (DataFolder.isInside(target, getFilesDir())
            || DataFolder.isInside(target, new File(android.os.Environment.getExternalStorageDirectory(), "Android"))) {
            status.setText("Choose a folder outside Android/data: that one is deleted with the app.");
            return;
        }
        if (!DataFolder.usable(target)) {
            status.setText("Cannot write to " + target.getAbsolutePath() + ". Choose another folder.");
            return;
        }
        if (current != null && DataFolder.sameFile(target, current)) {
            openLauncher(); // la misma: nada que mover
            return;
        }
        if (current != null && DataFolder.isInside(target, current)) {
            status.setText("Choose a folder that is not inside the current one.");
            return;
        }
        final List<File> sources = DataFolder.legacySources(this, target);
        if (current != null) sources.add(0, current);
        grant.setEnabled(false);
        pick.setEnabled(false);
        useDefault.setEnabled(false);
        confirm.setEnabled(false);
        if (sources.isEmpty()) {
            finishSetup(target);
            return;
        }
        progress.setProgress(0);
        progress.setVisibility(View.VISIBLE);
        status.setText("Moving your data…");
        worker = new Thread(() -> {
            String failure = null;
            try {
                DataFolder.migrate(sources, target, (done, total, file) -> runOnUiThread(() -> {
                    progress.setProgress(total > 0 ? (int) (done * 1000 / total) : 1000);
                    status.setText("Moving your data… " + (done >> 20) + " / " + (total >> 20) + " MB\n" + file);
                }));
            } catch (Exception e) {
                failure = e.getMessage() != null ? e.getMessage() : e.toString();
            }
            final String error = failure;
            runOnUiThread(() -> {
                worker = null;
                if (error != null) {
                    progress.setVisibility(View.GONE);
                    status.setText("The data could not be moved: " + error
                                   + "\nNothing was lost: what was not moved is still in its old place. Try again.");
                    refresh();
                    return;
                }
                finishSetup(target);
            });
        }, "nectar-data-move");
        worker.start();
    }

    private void finishSetup(File target) {
        if (current != null) current.delete(); // la carpeta vieja, si ha quedado vacía
        DataFolder.save(this, target);
        openLauncher();
    }

    private void openLauncher() {
        startActivity(new Intent(this, LauncherActivity.class));
        finish();
    }

    @Override
    public void onBackPressed() {
        if (worker != null) return; // no salir a medias del traslado
        if (current != null) openLauncher(); // cambiar carpeta: atrás es cancelar
        else super.onBackPressed();
    }

    private int dp(int value) {
        return Math.round(value * getResources().getDisplayMetrics().density);
    }
}
