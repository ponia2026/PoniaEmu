// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Reads ZIP (zlib) and 7z (the bundled LZMA SDK decoder).
// Many Libretro cores (need_fullpath=false) cannot unpack archives themselves; as with RetroArch, the frontend unpacks first and hands the result to the core.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace ponia {

enum class ArchiveKind { None, Zip, SevenZip };

struct ArchiveEntry {
    std::string name;       // path inside the archive (for zip the raw bytes, usually UTF-8; for 7z already converted to UTF-8)
    uint64_t size = 0;      // size after unpacking
    bool is_dir = false;
};

// Decided from the magic bytes at the start of the file (not only from the extension); returns None when it is not an archive or cannot be opened.
ArchiveKind archive_kind(const std::string& path);

bool archive_list(const std::string& path, std::vector<ArchiveEntry>& out, std::string& err);

// Read one file of the archive completely into memory. Fails when it exceeds max_bytes or the CRC does not match.
bool archive_read(const std::string& path, const std::string& entry_name, std::vector<uint8_t>& out,
                  std::string& err, uint64_t max_bytes);

// Unpack the whole archive into dest_dir (creates subdirectories; entries containing ".." or absolute paths are rejected). max_total limits the total unpacked size.
bool archive_extract_all(const std::string& path, const std::string& dest_dir, std::string& err, uint64_t max_total);

// Pick the first suitable file by the valid_extensions of the core (for example "nes|fds|unf") and return its index in entries; -1 when none is found.
// When there are "description files" such as m3u/cue/gdi they are preferred (multi-file disc images must be loaded from the description file).
// When valid_extensions is empty the first entry that is not a directory is chosen.
int archive_pick(const std::vector<ArchiveEntry>& entries, const char* valid_extensions);

// Normalise a path inside an archive: backslashes become '/', a leading "./" is removed; absolute paths or paths containing ".." return false (prevents unpacking outside the directory).
// This is the normalised name that archive_extract_all writes.
bool archive_safe_path(std::string& name);

// rm -rf (used to clean the staging directory of the unpacked files). A path that does not exist counts as success.
bool remove_tree(const std::string& path);

}  // namespace ponia
