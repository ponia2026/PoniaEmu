// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Ponia.Roms/BIOS 资料夹：使用者（或安装包）把 BIOS/韧体放在 Ponia.Roms 底下的 BIOS 里，结构照 Libretro 的 system 目录
//   BIOS/fbneo/neogeo.zip    BIOS/dc/dc_boot.bin    BIOS/scph5501.bin    BIOS/applewin/*.rom ...
// 播放器启动时把它「连结」进核心的 system 目录（连结失败才复制）；核心要写的东西（存档、快取）仍留在应用自己的目录，
// 所以 BIOS 放在唯读的 SD 卡上也行。根目录的 *.zip（街机 BIOS 如 neogeo.zip）另外连进 fbneo/ 与 mame2003-plus/。
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

// dst -> src。dst 已是指向 src 的连结就不动；是使用者自己放的实体档也不覆盖；其余（旧连结）换新。
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
        if (name[0] == '.' || ends_with_ci(name, ".md") || ends_with_ci(name, ".txt")) continue;   // README 之类不进 system 目录
        const std::string s = src + "/" + name, d = dst + "/" + name;
        if (is_dir(s)) { if (depth < 5) n += link_tree(s, d, depth + 1); }
        else if (link_one(s, d)) ++n;
    }
    return n;
}

} // namespace bios_detail

// 找 BIOS 资料夹：--bios-dir 明确指定优先；否则从游戏档所在目录往上最多 6 层，找名叫 Ponia.Roms 的那一层，
// 用它底下的 BIOS 子目录（Ponia.Roms/BIOS）。找不到回空字串。
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

// 把 bios_dir 整棵连进 system_dir；回传处理的档案数。
inline int sync_bios(const std::string& bios_dir, const std::string& system_dir) {
    using namespace bios_detail;
    if (bios_dir.empty() || system_dir.empty() || !is_dir(bios_dir)) return 0;
    mkdir_p(system_dir);
    int n = link_tree(bios_dir, system_dir, 0);
    // 根目录的 .zip（neogeo.zip、stvbios.zip…）街机核心各找各的子目录：fbneo/、mame2003-plus/ 都放一份连结
    for (const std::string& name : list_dir(bios_dir)) {
        if (!ends_with_ci(name, ".zip")) continue;
        const std::string s = bios_dir + "/" + name;
        if (is_dir(s)) continue;
        n += link_one(s, system_dir + "/fbneo/" + name) ? 1 : 0;
        n += link_one(s, system_dir + "/mame2003-plus/" + name) ? 1 : 0;
    }
    return n;
}

// mame2003_plus 的 BIOS 必须和游戏 zip 在同一个目录（rompath＝游戏所在目录）。游戏常在唯读的 SD 卡上、没法往里放，
// 所以在 stage_dir 里放「游戏的连结＋所有 BIOS zip 的连结」，回传游戏连结的路径给核心用。失败回空字串（照原路径）。
inline std::string stage_mame_content(const std::string& content, const std::string& bios_dir, const std::string& stage_dir) {
    using namespace bios_detail;
    if (bios_dir.empty() || !ends_with_ci(content, ".zip")) return std::string();
    mkdir_p(stage_dir);
    for (const std::string& name : list_dir(stage_dir)) ::unlink((stage_dir + "/" + name).c_str());   // 每次重建，别留上一款
    int n = 0;
    // 先放游戏所在目录的全部 zip（合并版 romset 的 clone 需要同目录的 parent），再放 BIOS（同名则 BIOS 为准）
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
