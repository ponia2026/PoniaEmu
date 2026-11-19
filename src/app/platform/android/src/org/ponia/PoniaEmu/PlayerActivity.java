// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Built-in Libretro player (replaces launching RetroArch). Runs in its own process (":player", see the manifest),
// so a crashing core cannot take the frontend down. The native side is libponia_player.so -> ponia_play().

package org.ponia.PoniaEmu;

import android.content.Intent;
import java.io.File;
import java.util.ArrayList;

public class PlayerActivity extends org.libsdl.app.SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "ponia_player" };
    }

    // Arguments come from the frontend as the same command-line options poniaplay takes (--system fc --content ...);
    // we add the directories that only this process knows about.
    @Override
    protected String[] getArguments() {
        final ArrayList<String> out = new ArrayList<String>();
        final Intent intent = getIntent();
        final String[] given = intent != null ? intent.getStringArrayExtra("args") : null;
        if (given != null)
            for (String a : given)
                out.add(a);

        final File files = getFilesDir();
        final File saves = new File(files, "saves");
        final File system = new File(files, "system");
        final File cores = new File(files, "cores");
        saves.mkdirs();
        system.mkdirs();
        out.add("--cores-dir");
        out.add(getApplicationInfo().nativeLibraryDir);   // bundled (commercial-safe) cores
        out.add("--cores-dir");
        out.add(cores.getAbsolutePath());                  // cores downloaded after installation
        if (!out.contains("--save-dir")) { out.add("--save-dir"); out.add(saves.getAbsolutePath()); }
        if (!out.contains("--system-dir")) { out.add("--system-dir"); out.add(system.getAbsolutePath()); }
        return out.toArray(new String[0]);
    }

    // ponia_play() keeps global state and may run once per process: end the :player process with the activity.
    @Override
    protected void onDestroy() {
        super.onDestroy();
        android.os.Process.killProcess(android.os.Process.myPid());
    }
}
