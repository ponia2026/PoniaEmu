// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// 机种别名 -> 机种键（与主题 systems.js 的 icon 键一致）。前端（自动带默认启动命令）与 poniaplay 共用。
// 只列走内建 Libretro 播放器的机种（含 PSP/Saturn：走 libretro 核心；目前没有机种走独立模拟器 App）。
#pragma once
#include <cctype>
#include <string>

namespace ponia {

struct SystemAlias { const char* key; const char* aliases; };   // aliases：逗号分隔，已去除非字母数字并小写

inline const SystemAlias* system_aliases(int& count) {
    static const SystemAlias T[] = {
        {"fc", "fc,nes,famicom,fds,famicomdisksystem"},
        {"pce", "pce,pcengine,tg16,turbografx16,turbografx,pcenginecd,tgcd,supergrafx,sgx"},
        {"atari2600", "atari2600,a2600,atari,vcs"},
        {"atari5200", "atari5200,a5200,5200"},
        {"atari7800", "atari7800,a7800,7800,prosystem"},
        {"sms", "sms,mastersystem,segamastersystem,markiii"},
        {"genesis", "genesis,megadrive,md,segagenesis,segacd,megacd,sega32x,32x"},
        {"neogeo", "neogeo,neogeocd,ngcd,aes,mvs"},
        {"sfc", "sfc,snes,superfamicom,supernintendo,satellaview"},
        {"ps1", "ps1,psx,ps,psone,playstation"},
        {"n64", "n64,nintendo64"},
        {"dc", "dc,dreamcast,segadreamcast"},
        {"gc", "gc,gamecube,ngc,nintendogamecube"},
        {"fbneo", "fbneo,fba,fbalpha,finalburnneo,finalburnalpha"},
        {"cps1", "cps1,cps"},
        {"cps2", "cps2,cps3"},
        {"mame", "mame,arcade,mame2003,mame2010,mame2003plus"},
        {"gb", "gb,gameboy"},
        {"gbc", "gbc,gameboycolor"},
        {"gba", "gba,gameboyadvance"},
        {"nds", "nds,ds,nintendods"},
        {"apple2", "apple2,appleii,apple2gs"},
        {"pico8", "pico8,pico,fantasyconsole"},
        {"dos", "dos,msdos,pcdos,dosbox"},
        {"psp", "psp,playstationportable"},
        {"saturn", "saturn,segasaturn,ss"},
    };
    count = (int)(sizeof T / sizeof T[0]);
    return T;
}

inline std::string normalize(const std::string& s) {
    std::string r;
    for (unsigned char c : s) if (std::isalnum(c)) r += (char)std::tolower(c);
    return r;
}

inline bool alias_has(const char* list, const std::string& k) {
    std::string cur;
    for (const char* p = list;; ++p) {
        if (*p == ',' || *p == 0) { if (cur == k) return true; cur.clear(); if (*p == 0) break; }
        else cur += *p;
    }
    return false;
}

// 依集合简称、名称找机种键；找不到回空字串。支持 "xxx hack" 尾缀与较长前缀（如 "FBNEO ACT"）。
inline std::string resolve_system(const std::string& short_name, const std::string& name = std::string()) {
    int n; const SystemAlias* T = system_aliases(n);
    for (const std::string* src : {&short_name, &name}) {
        std::string k = normalize(*src);
        if (k.empty()) continue;
        for (int i = 0; i < n; ++i) if (alias_has(T[i].aliases, k)) return T[i].key;
        if (k.size() > 4 && k.compare(k.size() - 4, 4, "hack") == 0) {
            std::string k2 = k.substr(0, k.size() - 4);
            for (int i = 0; i < n; ++i) if (alias_has(T[i].aliases, k2)) return T[i].key;
        }
        std::string best_key; size_t best_len = 0;     // 前缀：只认长度 >= 4 的别名，取最长
        for (int i = 0; i < n; ++i) {
            std::string cur;
            for (const char* p = T[i].aliases;; ++p) {
                if (*p == ',' || *p == 0) {
                    if (cur.size() >= 4 && cur.size() > best_len && k.compare(0, cur.size(), cur) == 0) { best_len = cur.size(); best_key = T[i].key; }
                    cur.clear(); if (*p == 0) break;
                } else cur += *p;
            }
        }
        if (!best_key.empty()) return best_key;
    }
    return std::string();
}

} // namespace ponia
