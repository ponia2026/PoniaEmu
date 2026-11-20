// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Lists the cores of one target from the same tables the program uses (src/libretro-host/ponia_cores.h), so packaging
// scripts never keep a second list.
//
//   list_cores <target> cores     one line per core that some system uses on that target: <core> <source> <license class> <approx kB>
//   list_cores <target> systems   one line per system: <system> <core or -> <source>; hidden systems show "-"
//
// <target>: linux-x86_64 linux-arm64 linux-armhf android-v8a android-v7a
// <source>: buildbot (libretro buildbot), poniacore (PoniaCore release), android-ndk (built with the NDK in CI)
//
// Build: g++ -std=c++17 -I src/libretro-host -o list_cores etc/tools/list_cores.cpp
#include "ponia_cores.h"

#include <cstdio>
#include <cstring>
#include <set>
#include <string>

namespace {

const char* source_of(ponia::Target t, const std::string& core)
{
    if (ponia::from_poniacore_on(t, core.c_str()))
        return "poniacore";
    if ((t == ponia::Target::AndroidV8a || t == ponia::Target::AndroidV7a) && core == "fake08")
        return "android-ndk";
    return "buildbot";
}

} // namespace

int main(int argc, char** argv)
{
    ponia::Target target;
    if (argc != 3 || !ponia::parse_target(argv[1], target)
        || (std::strcmp(argv[2], "cores") != 0 && std::strcmp(argv[2], "systems") != 0)) {
        std::fprintf(stderr, "usage: %s <linux-x86_64|linux-arm64|linux-armhf|android-v8a|android-v7a> <cores|systems>\n", argv[0]);
        return 2;
    }
    const bool list_cores = std::strcmp(argv[2], "cores") == 0;

    std::set<std::string> cores;
    int n = 0;
    const ponia::SystemCore* systems = ponia::system_table(n);
    for (int i = 0; i < n; ++i) {
        const std::string core = ponia::core_for_system_on(target, systems[i].sys);
        if (!list_cores)
            std::printf("%s %s %s\n", systems[i].sys, core.empty() ? "-" : core.c_str(), core.empty() ? "-" : source_of(target, core));
        if (!core.empty())
            cores.insert(core);
    }
    if (list_cores) {
        for (const std::string& core : cores) {
            const ponia::CoreInfo* info = ponia::core_info(core);
            std::printf("%s %s %s %u\n", core.c_str(), source_of(target, core),
                        info && info->noncommercial ? "noncommercial" : "commercial-ok", info ? info->approx_kb : 0u);
        }
    } else {
        int m = 0;
        const char* const* unsupported = ponia::unsupported_systems(m);
        for (int i = 0; i < m; ++i)
            std::printf("%s - unsupported\n", unsupported[i]);
    }
    return 0;
}
