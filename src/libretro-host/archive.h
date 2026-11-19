// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// ZIP（zlib）与 7z（随附的 LZMA SDK 解码端）读取。
// 很多 Libretro 核心（need_fullpath=false）自己不会解压，和 RetroArch 一样由前端先解开再交给核心。
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ponia {

enum class ArchiveKind { None, Zip, SevenZip };

struct ArchiveEntry {
    std::string name;       // 压缩包内的路径（zip 为原始位元组，通常是 UTF-8；7z 已转成 UTF-8）
    uint64_t size = 0;      // 解压后大小
    bool is_dir = false;
};

// 看文件开头的魔术位元组判断（不只看副档名）；不是压缩包或打不开回 None。
ArchiveKind archive_kind(const std::string& path);

bool archive_list(const std::string& path, std::vector<ArchiveEntry>& out, std::string& err);

// 把压缩包里的一个档案整个读进内存。超过 max_bytes 或 CRC 不符会失败。
bool archive_read(const std::string& path, const std::string& entry_name, std::vector<uint8_t>& out,
                  std::string& err, uint64_t max_bytes);

// 把整个压缩包解到 dest_dir（会建立子目录；拒绝含 ".." 或绝对路径的条目）。max_total 为解压总量上限。
bool archive_extract_all(const std::string& path, const std::string& dest_dir, std::string& err, uint64_t max_total);

// 依核心的 valid_extensions（例 "nes|fds|unf"）挑第一个合适的档案，回传 entries 的索引；找不到回 -1。
// 有 m3u/cue/gdi 这类「描述档」时优先选它们（多档案光碟映像要从描述档载入）。
// valid_extensions 为空时选第一个非目录条目。
int archive_pick(const std::vector<ArchiveEntry>& entries, const char* valid_extensions);

// 压缩包内路径正规化：反斜线改 '/'、去掉开头的 "./"；绝对路径或含 ".." 回 false（防止解到目录外）。
// archive_extract_all 写出的路径就是这样正规化后的名字。
bool archive_safe_path(std::string& name);

// rm -rf（用来清掉暂存的解压目录）。路径不存在算成功。
bool remove_tree(const std::string& path);

}  // namespace ponia
