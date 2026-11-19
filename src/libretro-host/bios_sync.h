// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Ponia.Roms/BIOS folder: the user (or the installer) puts BIOS/firmware into BIOS under Ponia.Roms, with the same structure as Libretro's system directory;
//   BIOS/fbneo/neogeo.zip    BIOS/dc/dc_boot.bin    BIOS/scph5501.bin    BIOS/applewin/*.rom ...
// when the player starts it "links" them into the system directory of the core (copying only if linking fails); what a core writes (saves, caches) stays in the app's own directory,
// so the BIOS can live on a read-only SD card. Top-level *.zip files (arcade BIOS such as neogeo.zip) are also linked into fbneo/ and mame2003-plus/.
#pragma once
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace ponia {
namespace bios_detail {

inline bool is_dir(const std::string& p) { struct stat st; return ::stat(p.c_str(), &st) == 0 && S_ISDIR(st.st_mode); }

inline void mkdir_p(const std::string& p) {
    for (size_t i = 1; i <= p.size(); ++i)
        if (i == p.size() || p[i] == '/') { const std::string d = p.substr(0, i); if (!d.empty()) ::mkdir(d.c_str(), 0775); }
}

inline std::string parent_of(const std::string& p) { const size_t s = p.find_last_of('/'); return s == std::string::npos ? std::string() : p.substr(0, s); }

inline bool copy_file(const std::string& src, const std::string& dst) {
    FILE* in = std::fopen(src.c_str(), "rb"); if (!in) return false;
    FILE* out = std::fopen(dst.c_str(), "wb"); if (!out) { std::fclose(in); return false; }
    char buf[1 << 16]; size_t n; bool ok = true;
    while ((n = std::fread(buf, 1, sizeof buf, in)) > 0) if (std::fwrite(buf, 1, n, out) != n) { ok = false; break; }
    std::fclose(in); std::fclose(out);
    if (!ok) ::unlink(dst.c_str());
    return ok;
}

// dst -> src. If dst is already a link to src it is left alone; a real file placed by the user is not overwritten; anything else (an old link) is replaced.
inline bool link_one(const std::string& src, const std::string& dst) {
    struct stat st;
    if (::lstat(dst.c_str(), &st) == 0) {
        if (S_ISLNK(st.st_mode)) {
            char b[4096]; const ssize_t n = ::readlink(dst.c_str(), b, sizeof b - 1);
            if (n > 0 && std::string(b, (size_t)n) == src) return true;
            ::unlink(dst.c_str());
        } else return true;
    }
    mkdir_p(parent_of(dst));
    if (::symlink(src.c_str(), dst.c_str()) == 0) return true;
    return copy_file(src, dst);
}

inline std::vector<std::string> list_dir(const std::string& d) {
    std::vector<std::string> r;
    if (DIR* h = ::opendir(d.c_str())) {
        while (const dirent* e = ::readdir(h)) { const std::string n = e->d_name; if (n != "." && n != "..") r.push_back(n); }
        ::closedir(h);
    }
    return r;
}

inline bool ends_with_ci(const std::string& s, const char* suf) {
    const size_t n = std::strlen(suf); if (s.size() < n) return false;
    for (size_t i = 0; i < n; ++i) { char a = s[s.size() - n + i], b = suf[i]; if (a >= 'A' && a <= 'Z') a = (char)(a + 32); if (b >= 'A' && b <= 'Z') b = (char)(b + 32); if (a != b) return false; }
    return true;
}

inline int link_tree(const std::string& src, const std::string& dst, int depth) {
    int n = 0;
    for (const std::string& name : list_dir(src)) {
        if (name[0] == '.' || ends_with_ci(name, ".md") || ends_with_ci(name, ".txt")) continue;   // README files and the like do not go into the system directory
        const std::string s = src + "/" + name, d = dst + "/" + name;
        if (is_dir(s)) { if (depth < 5) n += link_tree(s, d, depth + 1); }
        else if (link_one(s, d)) ++n;
    }
    return n;
}

} // namespace bios_detail

// Find the BIOS folder: an explicit --bios-dir wins; otherwise go up at most 6 levels from the directory of the game file and look for the level named Ponia.Roms,
// then use its BIOS subdirectory (Ponia.Roms/BIOS). Returns an empty string when not found.
inline std::string find_bios_dir(const std::string& content_path, const std::string& explicit_dir) {
    using namespace bios_detail;
    if (!explicit_dir.empty() && is_dir(explicit_dir)) return explicit_dir;
    std::string cur = parent_of(content_path);
    for (int i = 0; i < 6 && !cur.empty(); ++i, cur = parent_of(cur)) {
        const size_t s = cur.find_last_of('/');
        if (cur.substr(s == std::string::npos ? 0 : s + 1) == "Ponia.Roms" && is_dir(cur + "/BIOS")) return cur + "/BIOS";
    }
    return std::string();
}

// Link the whole bios_dir tree into system_dir; returns the number of files handled.
inline int sync_bios(const std::string& bios_dir, const std::string& system_dir) {
    using namespace bios_detail;
    if (bios_dir.empty() || system_dir.empty() || !is_dir(bios_dir)) return 0;
    mkdir_p(system_dir);
    int n = link_tree(bios_dir, system_dir, 0);
    // Top-level .zip files (neogeo.zip, stvbios.zip ...): each arcade core looks in its own subdirectory, so put a link into both fbneo/ and mame2003-plus/
    for (const std::string& name : list_dir(bios_dir)) {
        if (!ends_with_ci(name, ".zip")) continue;
        const std::string s = bios_dir + "/" + name;
        if (is_dir(s)) continue;
        n += link_one(s, system_dir + "/fbneo/" + name) ? 1 : 0;
        n += link_one(s, system_dir + "/mame2003-plus/" + name) ? 1 : 0;
    }
    return n;
}

// The BIOS of mame2003_plus must be in the same directory as the game zip (rompath = the directory of the game). Games are often on a read-only SD card where nothing can be added,
// so stage_dir gets "a link to the game + links to all BIOS zips", and the path of the game link is returned for the core. Returns an empty string on failure (the original path is used).
inline std::string stage_mame_content(const std::string& content, const std::string& bios_dir, const std::string& stage_dir) {
    using namespace bios_detail;
    if (bios_dir.empty() || !ends_with_ci(content, ".zip")) return std::string();
    mkdir_p(stage_dir);
    for (const std::string& name : list_dir(stage_dir)) ::unlink((stage_dir + "/" + name).c_str());   // rebuilt every time, so nothing of the previous game stays
    int n = 0;
    // First put in all zips of the directory of the game (clones of a merged romset need their parent in the same directory), then the BIOS (on a name clash the BIOS wins)
    for (const std::string& name : list_dir(parent_of(content))) {
        const std::string s = parent_of(content) + "/" + name;
        if (ends_with_ci(name, ".zip") && !is_dir(s)) n += link_one(s, stage_dir + "/" + name) ? 1 : 0;
    }
    for (const std::string& sub : {std::string(), std::string("/mame2003-plus"), std::string("/fbneo")})
        for (const std::string& name : list_dir(bios_dir + sub)) {
            const std::string s = bios_dir + sub + "/" + name;
            if (ends_with_ci(name, ".zip") && !is_dir(s)) n += link_one(s, stage_dir + "/" + name) ? 1 : 0;
        }
    const size_t sl = content.find_last_of('/');
    const std::string dst = stage_dir + "/" + content.substr(sl == std::string::npos ? 0 : sl + 1);
    return link_one(content, dst) ? dst : std::string();
}

} // namespace ponia
