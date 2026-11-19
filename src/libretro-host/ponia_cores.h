// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// 机种 -> Libretro 核心对照，以及核心的授权分类、预估大小。poniaplay 与前端的「首次下载」共用。
// 授权资料来源：libretro-core-info 各核心 .info 的 license 栏（2026-11-15 核对）；正式发行前需再对一次各核心原始仓库。
#pragma once
#include <cstring>
#include <string>

namespace ponia {

struct CoreInfo {
    const char* id;          // 核心名（不含 _libretro.so），也是下载网址里的名字（Android n64 例外，见 dl_id）
    const char* dl_id;       // 下载用名字；nullptr＝同 id
    const char* license;
    bool noncommercial;      // true＝授权不允许商用，只能首次下载、不进安装包
    unsigned approx_kb;      // 压缩包约略大小（只用来在询问画面显示）
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
        {"mamearcade", nullptr, "GPLv2+", false, 256938},   // 完整版 MAME：联盟的街机 romset 是新版 MAME 的，mame2003_plus 认不得
        {"mupen64plus_next", "mupen64plus_next_gles3", "GPLv2", false, 2150},
        {"pcsx_rearmed", nullptr, "GPLv2", false, 520},
        {"flycast", nullptr, "GPLv2", false, 4795},
        {"ppsspp", nullptr, "GPLv2+", false, 9500},   // PSP：走内建播放器（GLES 零拷贝 + 缓冲 3 帧），不再用独立 PPSSPP App
        {"dolphin", nullptr, "GPLv2+", false, 6430},   // GameCube：需要 Sys 资料夹（内建在 APK assets，首次用时解开）
        {"yabasanshiro", nullptr, "GPLv2", false, 1526},   // Saturn（需要 saturn_bios.bin，放 Ponia.Roms/BIOS）
        {"stella", nullptr, "GPLv2", false, 1444},
        {"a5200", nullptr, "GPLv2", false, 300},
        {"prosystem", nullptr, "GPLv2", false, 200},
        {"fake08", nullptr, "MIT", false, 700},   // Pico-8 主力：支援压缩卡带（retro8 解不开）；buildbot 没有 Android 版，自行用 NDK 编译（见 fetch-bundled-cores.sh 注释）
        {"retro8", nullptr, "GPLv3", false, 248},   // Pico-8 备援
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

// 机种键（ponia_systems.h 的 resolve_system 结果）-> 核心 id；Ports 另行处理，不在此表。
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
    if (key == "gc") return std::string();   // 32 位 ARM 没有可用的 Dolphin 核心（无 ARM JIT）：不提供、也不会提示下载
#endif
    for (const M& m : T) if (key == m.sys) return m.core;
    return std::string();
}

// 走「独立模拟器 App」的机种：用 am start 带游戏档启动已安装的 App。回 nullptr＝不是独立模拟器机种。
// 目前没有机种走独立 App（PSP 已改走内建播放器：1118 实测 2X 比赛约 60fps，且热键与其他机种一致）；保留此机制备日后使用。
// 命令里可用 {file.uri}（前端的 FileProvider content:// 网址）与 {file.path}（原生路径）。
inline const char* standalone_launch_cmd(const std::string& key) {
    (void)key;
    return nullptr;
}

inline std::string core_file(const std::string& id) { return id + "_libretro.so"; }

} // namespace ponia
