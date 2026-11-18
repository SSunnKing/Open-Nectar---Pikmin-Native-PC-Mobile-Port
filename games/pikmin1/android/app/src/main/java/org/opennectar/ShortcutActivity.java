package org.opennectar;

import android.app.Activity;
import android.content.Intent;
import android.os.Bundle;

/**
 * Target of the Pikmin and Pikmin 2 shortcuts (static ones and the ones
 * Create shortcut pins). Not an SDL activity: it only forwards the game
 * choice, then finishes.
 *
 * The data folder is chosen in StartActivity before the launcher can run.
 * A shortcut that arrives first goes there and the choice is kept. Once the
 * launcher is up (singleTask), this delivers the choice to it with
 * onNewIntent instead of starting a second one.
 */
public class ShortcutActivity extends Activity {
    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        try {
            forward(getIntent());
        } finally {
            finish();
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        try {
            forward(intent);
        } finally {
            finish();
        }
    }

    private void forward(Intent source) {
        boolean ready = DataFolder.ready(this) != null;
        Intent intent = new Intent(this, ready ? LauncherActivity.class : StartActivity.class);
        GameLaunch.copy(source, intent);
        intent.addFlags(Intent.FLAG_ACTIVITY_CLEAR_TOP | Intent.FLAG_ACTIVITY_SINGLE_TOP);
        startActivity(intent);
    }
}
