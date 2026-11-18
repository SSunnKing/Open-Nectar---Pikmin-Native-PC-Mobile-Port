package org.opennectar;

import android.content.Context;
import android.content.Intent;
import android.net.Uri;

/**
 * How a shortcut names a game. The strings match launch_request.h
 * (kAndroidLaunchExtra, kAndroidLaunchUriPrefix): an extra
 * org.opennectar.LAUNCH = pikmin1|pikmin2, and/or the data URI
 * opennectar://launch/pikmin1. The native launcher parses both.
 */
public final class GameLaunch {
    public static final String EXTRA = "org.opennectar.LAUNCH";
    public static final String URI_PREFIX = "opennectar://launch/";

    private GameLaunch() {}

    /** Explicit VIEW intent for ShortcutActivity. Static shortcuts use the same shape. */
    public static Intent view(Context context, String gameId) {
        Intent intent = new Intent(Intent.ACTION_VIEW, Uri.parse(URI_PREFIX + gameId));
        intent.setClass(context, ShortcutActivity.class);
        intent.addCategory(Intent.CATEGORY_DEFAULT);
        intent.putExtra(EXTRA, gameId);
        return intent;
    }

    /** Copies the game choice onto `to`. Leaves `to` unchanged when `from` has none. */
    public static void copy(Intent from, Intent to) {
        if (from == null || to == null) return;
        String extra = from.getStringExtra(EXTRA);
        if (extra != null) to.putExtra(EXTRA, extra);
        if (from.getData() != null) to.setData(from.getData());
    }

    public static String extra(Intent intent) {
        if (intent == null) return null;
        return intent.getStringExtra(EXTRA);
    }

    public static String data(Intent intent) {
        if (intent == null || intent.getData() == null) return null;
        return intent.getData().toString();
    }
}
