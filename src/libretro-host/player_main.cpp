// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Android entry point: SDLActivity (PlayerActivity) loads libponia_player.so and calls SDL_main.
// (SDLActivity creates its "SDLThread" with a 64 MB stack — see SDLActivity.java — so big cores (N64, Dreamcast) do not overflow.)
#include "ponia_play.h"
#include <SDL.h>

extern "C" __attribute__((visibility("default"))) int SDL_main(int argc, char** argv) { return ponia_play(argc, argv); }
