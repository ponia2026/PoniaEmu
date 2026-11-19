// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Libretro host entry point shared by all platforms. argv uses the same options as the `poniaplay` command line.
// Returns 0 on normal exit. Call at most once per process (global state); on Android it runs in the :player process.
#pragma once
#ifdef __cplusplus
extern "C" {
#endif
int ponia_play(int argc, char** argv);
#ifdef __cplusplus
}
#endif
