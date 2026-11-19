// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// System -> Libretro core mapping, plus the license class and the estimated size of each core. Shared by poniaplay and the "first download" of the frontend.
// License source: the license field of each core's .info in libretro-core-info (checked on 2026-11-15); check each core's original repository once more before an official release.
#pragma once
#include <cstring>
#include <string>

namespace ponia {

struct CoreInfo {
    const char* id;          // core name (without _libretro.so), also the name in the download URL (Android n64 is an exception, see dl_id)
    const char* dl_id;       // name used for the download; nullptr = same as id
    const char* license;
    bool noncommercial;      // true = the license does not allow commercial use, so it can only be downloaded on first use and is not in the installer
    unsigned approx_kb;      // approximate size of the compressed package (only shown in the prompt)
};

inline const CoreInfo* core_table(int& n) {
    static const CoreInfo T[] = {
        {"fceumm", nullptr, "GPLv2", false, 441},
        {"gambatte", nullptr, "GPLv2", false, 424},
        {"mgba", nullptr, "MPLv2.0", false, 410},
        {"melonds", nullptr, "GPLv3", false, 674},
        {"snes9x", nullptr, "Non-commercial", true, 870},
        {"genesis_plus_gx", nullptr, "Non-commercial", true, 1107},
        {"mednafen_pce_fast", nullptr, "GPLv2", false, 365},
        {"fbneo", nullptr, "Non-commercial", true, 16383},
        {"mame2003_plus", nullptr, "MAME Noncommercial", true, 9651},
        {"mamearcade", nullptr, "GPLv2+", false, 256938},   // full MAME: the arcade romsets of the community are from recent MAME, which mame2003_plus does not recognise
        {"mupen64plus_next", "mupen64plus_next_gles3", "GPLv2", false, 2150},
        {"pcsx_rearmed", nullptr, "GPLv2", false, 520},
        {"flycast", nullptr, "GPLv2", false, 4795},
        {"ppsspp", nullptr, "GPLv2+", false, 9500},   // PSP: goes through the built-in player (GLES zero-copy + 3-frame buffering), no standalone PPSSPP app any more
        {"dolphin", nullptr, "GPLv2+", false, 6430},   // GameCube: needs the Sys folder (bundled in the APK assets, unpacked on first use)
        {"yabasanshiro", nullptr, "GPLv2", false, 1526},   // Saturn (needs saturn_bios.bin, put it in Ponia.Roms/BIOS)
        {"stella", nullptr, "GPLv2", false, 1444},
        {"a5200", nullptr, "GPLv2", false, 300},
        {"prosystem", nullptr, "GPLv2", false, 200},
        {"fake08", nullptr, "MIT", false, 700},   // main PICO-8 core: supports compressed cartridges (retro8 cannot open them); the buildbot has no Android build, so it is built with the NDK (see the comments in fetch-bundled-cores.sh)
        {"retro8", nullptr, "GPLv3", false, 248},   // PICO-8 fallback
        {"applewin", nullptr, "GPLv2", false, 1541},
        {"dosbox_pure", nullptr, "GPLv2", false, 5500},
    };
    n = (int)(sizeof T / sizeof T[0]);
    return T;
}

inline const CoreInfo* core_info(const std::string& id) {
    int n; const CoreInfo* T = core_table(n);
    for (int i = 0; i < n; ++i) if (id == T[i].id) return &T[i];
    return nullptr;
}

// System key (the result of resolve_system in ponia_systems.h) -> core id; Ports are handled separately and are not in this table.
inline std::string core_id_for_system(const std::string& key) {
    struct M { const char* sys; const char* core; };
    static const M T[] = {
        {"fc", "fceumm"}, {"gb", "gambatte"}, {"gbc", "gambatte"}, {"gba", "mgba"}, {"nds", "melonds"},
        {"sfc", "snes9x"}, {"genesis", "genesis_plus_gx"}, {"sms", "genesis_plus_gx"},
        {"pce", "mednafen_pce_fast"}, {"neogeo", "fbneo"}, {"cps1", "fbneo"}, {"cps2", "fbneo"}, {"fbneo", "fbneo"},
        {"mame", "mamearcade"}, {"n64", "mupen64plus_next"}, {"ps1", "pcsx_rearmed"}, {"dc", "flycast"},
        {"gc", "dolphin"}, {"psp", "ppsspp"}, {"saturn", "yabasanshiro"}, {"atari2600", "stella"}, {"atari5200", "a5200"}, {"atari7800", "prosystem"}, {"pico8", "fake08"}, {"apple2", "applewin"}, {"dos", "dosbox_pure"},
    };
#if defined(__arm__) && !defined(__aarch64__)
    if (key == "gc") return std::string();   // 32-bit ARM has no usable Dolphin core (no ARM JIT): not offered, and no download prompt either
#endif
    for (const M& m : T) if (key == m.sys) return m.core;
    return std::string();
}

// Systems that go through a "standalone emulator app": the installed app is started with am start and the game file. Returns nullptr when the system is not a standalone-emulator system.
// Currently no system goes through a standalone app (PSP now uses the built-in player: it runs at about 60 fps in 2X on a mid-range Mali-G31 board, and the hotkeys match the other systems); the mechanism is kept for later use.
// The command can use {file.uri} (the FileProvider content:// URL of the frontend) and {file.path} (the native path).
inline const char* standalone_launch_cmd(const std::string& key) {
    (void)key;
    return nullptr;
}

inline std::string core_file(const std::string& id) { return id + "_libretro.so"; }

} // namespace ponia
