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

// ---------- build targets and the per-target exclusion list ----------
// The five targets PoniaEmu is built for. The target is fixed at compile time.
enum class Target { LinuxX64, LinuxArm64, LinuxArm32, AndroidV8a, AndroidV7a };

constexpr Target current_target() {
#if defined(__ANDROID__)
#  if defined(__aarch64__)
    return Target::AndroidV8a;
#  else
    return Target::AndroidV7a;
#  endif
#elif defined(__aarch64__)
    return Target::LinuxArm64;
#elif defined(__arm__)
    return Target::LinuxArm32;
#else
    return Target::LinuxX64;
#endif
}

// ---------- cores downloaded from the PoniaCore project instead of the libretro buildbot ----------
// https://github.com/ponia2026/PoniaCore publishes these builds (GLES/EGL builds for GLES-only GPUs, and cores the buildbot
// does not provide). Asset name: <core>_libretro_<target>.so.zip in the "cores-latest" release.
struct PoniaCoreRow { Target target; const char* id; };

inline bool from_poniacore(const char* id) {
    static const PoniaCoreRow T[] = {
        {Target::LinuxArm64, "mupen64plus_next"},
        {Target::LinuxArm64, "yabasanshiro"},
        {Target::LinuxArm64, "ppsspp"},
        {Target::LinuxArm64, "dolphin"},
        {Target::LinuxArm64, "fake08"},
        {Target::LinuxX64,   "fake08"},
    };
    for (const PoniaCoreRow& r : T)
        if (r.target == current_target() && std::strcmp(r.id, id) == 0) return true;
    return false;
}

// Target name used in PoniaCore asset names; empty for Android (not served by PoniaCore).
constexpr const char* poniacore_target_name() {
    return current_target() == Target::LinuxX64 ? "linux-x86_64"
         : current_target() == Target::LinuxArm64 ? "linux-arm64"
         : current_target() == Target::LinuxArm32 ? "linux-armhf" : "";
}

// One row = one core id or one system key that is NOT offered on that target. An excluded core is never downloaded
// or started, and the frontend hides the systems that would need it (SearchContext::finalize_hide_unsupported).
// To bring one back, delete its row. The reason says what has to change before the row can go.
struct Exclusion { Target target; const char* what; const char* reason; };

inline const Exclusion* exclusion_table(int& n) {
    static const Exclusion T[] = {
        // 32-bit ARM Linux: no build on the libretro buildbot (linux/armv7-neon-hf)
        {Target::LinuxArm32, "melonds",          "no armhf build on the buildbot"},
        {Target::LinuxArm32, "mamearcade",       "no armhf build; the 32-bit address space is too small for full MAME"},
        {Target::LinuxArm32, "mupen64plus_next", "no armhf build on the buildbot"},
        {Target::LinuxArm32, "flycast",          "no armhf build on the buildbot"},
        {Target::LinuxArm32, "ppsspp",           "no armhf build on the buildbot"},
        {Target::LinuxArm32, "dolphin",          "no ARM32 JIT"},
        {Target::LinuxArm32, "yabasanshiro",     "no armhf build on the buildbot"},
        {Target::LinuxArm32, "stella",           "no armhf build on the buildbot"},
        {Target::LinuxArm32, "a5200",            "no armhf build on the buildbot"},
        {Target::LinuxArm32, "applewin",         "no armhf build on the buildbot"},
        {Target::LinuxArm32, "dosbox_pure",      "no armhf build on the buildbot"},
        {Target::LinuxArm32, "retro8",           "no armhf build; fake08 is shipped with the package instead"},
        // 64-bit ARM Linux: Nintendo 64, Saturn, PSP and GameCube come from PoniaCore (GLES/EGL builds), see from_poniacore()
        // 32-bit Android: Dolphin has no ARM32 JIT
        {Target::AndroidV7a, "dolphin",          "no ARM32 JIT"},
        // Ports are PC/Linux programs started through a launch command; there is nothing to run on Android
        {Target::AndroidV8a, "ports",            "Linux programs only"},
        {Target::AndroidV7a, "ports",            "Linux programs only"},
    };
    n = (int)(sizeof T / sizeof T[0]);
    return T;
}

// Is this core id or system key excluded on the target this binary was built for?
inline bool excluded(const char* what) {
    int n; const Exclusion* T = exclusion_table(n);
    for (int i = 0; i < n; ++i)
        if (T[i].target == current_target() && std::strcmp(T[i].what, what) == 0) return true;
    return false;
}

// System key (the result of resolve_system in ponia_systems.h) -> core id, ignoring the exclusion list; Ports are handled separately and are not in this table.
inline const char* table_core_for_system(const std::string& key) {
    struct M { const char* sys; const char* core; };
    static const M T[] = {
        {"fc", "fceumm"}, {"gb", "gambatte"}, {"gbc", "gambatte"}, {"gba", "mgba"}, {"nds", "melonds"},
        {"sfc", "snes9x"}, {"genesis", "genesis_plus_gx"}, {"sms", "genesis_plus_gx"},
        {"pce", "mednafen_pce_fast"}, {"neogeo", "fbneo"}, {"cps1", "fbneo"}, {"cps2", "fbneo"}, {"fbneo", "fbneo"},
        {"mame", "mamearcade"}, {"n64", "mupen64plus_next"}, {"ps1", "pcsx_rearmed"}, {"dc", "flycast"},
        {"gc", "dolphin"}, {"psp", "ppsspp"}, {"saturn", "yabasanshiro"}, {"atari2600", "stella"}, {"atari5200", "a5200"}, {"atari7800", "prosystem"}, {"pico8", "fake08"}, {"apple2", "applewin"}, {"dos", "dosbox_pure"},
    };
    for (const M& m : T) if (key == m.sys) return m.core;
    return nullptr;
}

// The core for a system on this target; empty when the system has no core or its core is excluded here.
inline std::string core_id_for_system(const std::string& key) {
    const char* core = table_core_for_system(key);
    return (core && !excluded(core)) ? std::string(core) : std::string();
}

// Should the frontend hide this system on this target: the system itself is excluded (such as Ports on Android) or its core is.
inline bool system_hidden(const std::string& key) {
    if (excluded(key.c_str())) return true;
    const char* core = table_core_for_system(key);
    return core && excluded(core);
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
