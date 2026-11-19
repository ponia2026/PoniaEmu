// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// System alias -> system key (matches the icon key of systems.js in the theme). Shared by the frontend (which adds a default launch command automatically) and poniaplay.
// Only the systems that go through the built-in Libretro player are listed (including PSP/Saturn: via libretro cores; currently no system uses a standalone emulator app).
#pragma once
#include <cctype>
#include <string>

namespace ponia {

struct SystemAlias { const char* key; const char* aliases; };   // aliases: comma separated, with non-alphanumerics removed and lower-cased

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

// Find the system key by collection short name or name; returns an empty string when not found. Supports a trailing "xxx hack" and longer prefixes (such as "FBNEO ACT").
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
        std::string best_key; size_t best_len = 0;     // prefix: only aliases of length >= 4 count, the longest wins
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
