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
        {"desmume2015", nullptr, "GPLv2", false, 700},   // NDS replacement on targets where melonds has no JIT (32-bit ARM)
        {"snes9x", nullptr, "Non-commercial", true, 870},
        {"genesis_plus_gx", nullptr, "Non-commercial", true, 1107},
        {"mednafen_pce_fast", nullptr, "GPLv2", false, 365},
        {"fbneo", nullptr, "Non-commercial", true, 16383},
        {"mame2003_plus", nullptr, "MAME Noncommercial", true, 9651},
        {"mame2010", nullptr, "MAME Noncommercial", true, 14930},
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

// ---------- build targets ----------
// The five targets PoniaEmu is built for. The target of a binary is fixed at compile time (current_target());
// the *_on(target, ...) functions below answer for any target, so packaging tools can ask about the targets they pack.
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

// Target name as used in PoniaCore asset names and by the packaging tools.
constexpr const char* target_name(Target t) {
    return t == Target::LinuxX64 ? "linux-x86_64"
         : t == Target::LinuxArm64 ? "linux-arm64"
         : t == Target::LinuxArm32 ? "linux-armhf"
         : t == Target::AndroidV8a ? "android-v8a" : "android-v7a";
}

// Returns false when the name is not a target.
inline bool parse_target(const char* name, Target& out) {
    for (Target t : {Target::LinuxX64, Target::LinuxArm64, Target::LinuxArm32, Target::AndroidV8a, Target::AndroidV7a})
        if (std::strcmp(name, target_name(t)) == 0) { out = t; return true; }
    return false;
}

// ---------- which core plays which system, per target ----------
// 0. Systems in unsupported_systems (Ports, PS2, Wii, Xbox, ...) are recognised but hidden on every target.
// 1. Each system has ONE default core (the table below). It is what every target uses unless a rule says otherwise.
// 2. A rule (target_rules) changes one system on one target: a replacement core, or "" = the system is not offered on that target
//    (nothing is downloaded or packed for it, and the frontend hides the system).
// 3. Where a core comes from on a target: the libretro buildbot, unless the core is listed in poniacore_cores (published by
//    https://github.com/ponia2026/PoniaCore, asset <core>_libretro_<target>.so.zip in the "cores-latest" release).
// Packaging asks the same functions (see etc/tools/list_cores.cpp), so a core that no system uses on a target is never packed for it.

// System key (the result of resolve_system in ponia_systems.h) -> default core id. Ports have no core and are not in this table.
struct SystemCore { const char* sys; const char* core; };

inline const SystemCore* system_table(int& n) {
    static const SystemCore T[] = {
        {"fc", "fceumm"}, {"gb", "gambatte"}, {"gbc", "gambatte"}, {"gba", "mgba"}, {"nds", "melonds"},
        {"sfc", "snes9x"}, {"genesis", "genesis_plus_gx"}, {"sms", "genesis_plus_gx"},
        {"pce", "mednafen_pce_fast"}, {"neogeo", "fbneo"}, {"cps1", "fbneo"}, {"cps2", "fbneo"}, {"fbneo", "fbneo"},
        {"mame", "mamearcade"}, {"n64", "mupen64plus_next"}, {"ps1", "pcsx_rearmed"}, {"dc", "flycast"},
        {"gc", "dolphin"}, {"psp", "ppsspp"}, {"saturn", "yabasanshiro"}, {"atari2600", "stella"}, {"atari5200", "a5200"},
        {"atari7800", "prosystem"}, {"pico8", "fake08"}, {"apple2", "applewin"}, {"dos", "dosbox_pure"},
    };
    n = (int)(sizeof T / sizeof T[0]);
    return T;
}

struct TargetRule { Target target; const char* sys; const char* core; const char* note; };

inline const TargetRule* target_rules(int& n) {
    static const TargetRule T[] = {
        // 32-bit ARM Linux. The reference is an RK3288 class board: systems that are not smooth there are not offered.
        {Target::LinuxArm32, "nds",       "desmume2015",   "melonds has no ARM32 JIT; desmume2015 comes from PoniaCore"},
        {Target::LinuxArm32, "mame",      "mame2003_plus", "full MAME does not fit the 32-bit address space; mame2003_plus comes from the buildbot"},
        {Target::LinuxArm32, "n64",       "",              "not smooth enough; decide after testing on the reference board"},
        {Target::LinuxArm32, "dc",        "",              "not smooth enough; decide after testing on the reference board"},
        {Target::LinuxArm32, "psp",       "",              "not smooth enough; decide after testing on the reference board"},
        {Target::LinuxArm32, "saturn",    "",              "not smooth enough"},
        {Target::LinuxArm32, "gc",        "",              "no ARM32 JIT"},
        // 32-bit Android: Dolphin has no ARM32 JIT
        {Target::AndroidV7a, "gc",        "",              "no ARM32 JIT"},
    };
    n = (int)(sizeof T / sizeof T[0]);
    return T;
}

// Systems that are recognised (ponia_systems.h) but supported on NO target: they are hidden everywhere and nothing is
// downloaded or packed for them. To start supporting one, move it to system_table with a default core.
// Ports are PC/Linux programs started through a launch command; the others have no core that runs on the boards PoniaEmu targets.
inline const char* const* unsupported_systems(int& n) {
    static const char* const T[] = {"ports", "ps2", "ps3", "vita", "wii", "wiiu", "3ds", "switch", "xbox", "xbox360"};
    n = (int)(sizeof T / sizeof T[0]);
    return T;
}

inline bool system_unsupported(const std::string& key) {
    int n; const char* const* T = unsupported_systems(n);
    for (int i = 0; i < n; ++i)
        if (key == T[i]) return true;
    return false;
}

struct PoniaCoreRow { Target target; const char* id; };

inline const PoniaCoreRow* poniacore_cores(int& n) {
    static const PoniaCoreRow T[] = {
        {Target::LinuxArm64, "mupen64plus_next"},
        {Target::LinuxArm64, "yabasanshiro"},
        {Target::LinuxArm64, "ppsspp"},
        {Target::LinuxX64,   "ppsspp"},   // desktop GL built with GLEW_EGL=1: the buildbot (GLX) core fails glewInit() on KMS/DRM
        {Target::LinuxArm64, "dolphin"},
        {Target::LinuxArm64, "fake08"},
        {Target::LinuxX64,   "fake08"},
        {Target::LinuxArm32, "fake08"},
        {Target::LinuxArm32, "a5200"},
        {Target::LinuxArm32, "stella"},   // built from the last upstream commit that gcc 12 can compile (before std::format)
        {Target::LinuxArm32, "dosbox_pure"},
        {Target::LinuxArm32, "applewin"},
        {Target::LinuxArm32, "desmume2015"},   // the buildbot has no ARMv7 build of it
    };
    n = (int)(sizeof T / sizeof T[0]);
    return T;
}

inline const TargetRule* rule_for(Target t, const std::string& key) {
    int n; const TargetRule* T = target_rules(n);
    for (int i = 0; i < n; ++i)
        if (T[i].target == t && key == T[i].sys) return &T[i];
    return nullptr;
}

// The core for a system on a target: the rule's replacement, else the default. Empty when the system has no core
// there (a rule with "", or a system without a default core).
inline std::string core_for_system_on(Target t, const std::string& key) {
    if (system_unsupported(key))
        return std::string();
    if (const TargetRule* r = rule_for(t, key))
        return r->core;
    int n; const SystemCore* T = system_table(n);
    for (int i = 0; i < n; ++i)
        if (key == T[i].sys) return T[i].core;
    return std::string();
}

// Is the system hidden on that target (a rule says it is not offered there)?
inline bool system_hidden_on(Target t, const std::string& key) {
    if (system_unsupported(key))
        return true;
    const TargetRule* r = rule_for(t, key);
    return r && r->core[0] == '\0';
}

// Is the core published by PoniaCore (not by the libretro buildbot) on that target?
inline bool from_poniacore_on(Target t, const char* id) {
    int n; const PoniaCoreRow* T = poniacore_cores(n);
    for (int i = 0; i < n; ++i)
        if (T[i].target == t && std::strcmp(T[i].id, id) == 0) return true;
    return false;
}

// The same questions for the target this binary was built for.
inline std::string core_id_for_system(const std::string& key) { return core_for_system_on(current_target(), key); }
inline bool system_hidden(const std::string& key) { return system_hidden_on(current_target(), key); }
inline bool from_poniacore(const char* id) { return from_poniacore_on(current_target(), id); }

// Target name used in PoniaCore asset names; empty for Android (not served by PoniaCore).
constexpr const char* poniacore_target_name() {
    return current_target() == Target::AndroidV8a || current_target() == Target::AndroidV7a ? "" : target_name(current_target());
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
