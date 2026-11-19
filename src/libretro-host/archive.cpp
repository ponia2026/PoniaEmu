// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works

#include "archive.h"

#include <zlib.h>

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <mutex>

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#include "third_party/lzma/7z.h"
#include "third_party/lzma/7zCrc.h"
#include "third_party/lzma/7zFile.h"

namespace ponia {
namespace {

std::string lower(std::string s)
{
    for (char& c : s)
        c = (char)std::tolower((unsigned char)c);
    return s;
}

std::string ext_of(const std::string& name)
{
    const size_t dot = name.find_last_of('.');
    const size_t sep = name.find_last_of("/\\");
    if (dot == std::string::npos || (sep != std::string::npos && dot < sep))
        return std::string();
    return lower(name.substr(dot + 1));
}

uint16_t rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
uint32_t rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }
uint64_t rd64(const uint8_t* p) { return (uint64_t)rd32(p) | ((uint64_t)rd32(p + 4) << 32); }

struct CFile {
    FILE* f = nullptr;
    explicit CFile(const std::string& path) { f = std::fopen(path.c_str(), "rb"); }
    ~CFile() { if (f) std::fclose(f); }
    CFile(const CFile&) = delete;
    CFile& operator=(const CFile&) = delete;
};

bool read_at(FILE* f, uint64_t off, void* buf, size_t n)
{
    if (fseeko(f, (off_t)off, SEEK_SET) != 0)
        return false;
    return std::fread(buf, 1, n, f) == n;
}

bool mkdirs(const std::string& path)   // mkdir -p
{
    std::string cur;
    for (size_t i = 0; i <= path.size(); ++i) {
        if (i == path.size() || path[i] == '/') {
            if (!cur.empty() && ::mkdir(cur.c_str(), 0700) != 0 && errno != EEXIST)
                return false;
        }
        if (i < path.size())
            cur += path[i];
    }
    return true;
}

// Relative path inside the archive: unified to '/', absolute paths and ".." are rejected (zip-slip).
bool safe_rel(std::string& name)
{
    for (char& c : name)
        if (c == '\\')
            c = '/';
    while (name.compare(0, 2, "./") == 0)
        name.erase(0, 2);
    if (name.empty() || name[0] == '/')
        return false;
    size_t i = 0;
    while (i <= name.size()) {
        size_t j = name.find('/', i);
        if (j == std::string::npos)
            j = name.size();
        if (name.compare(i, j - i, "..") == 0)
            return false;
        i = j + 1;
    }
    return true;
}

std::string parent_dir(const std::string& path)
{
    const size_t s = path.find_last_of('/');
    return s == std::string::npos ? std::string() : path.substr(0, s);
}

using Sink = std::function<bool(const uint8_t*, size_t)>;

// ---------------------------------------------------------------- ZIP

struct ZipEnt {
    std::string name;
    uint16_t method = 0, flags = 0;
    uint32_t crc = 0;
    uint64_t csize = 0, usize = 0, lho = 0;   // lho = local header offset
};

bool zip_read_dir(FILE* f, std::vector<ZipEnt>& out, std::string& err)
{
    if (fseeko(f, 0, SEEK_END) != 0) { err = "seek failed"; return false; }
    const uint64_t fsz = (uint64_t)ftello(f);
    if (fsz < 22) { err = "file too small for a zip"; return false; }

    const size_t tail_n = (size_t)std::min<uint64_t>(fsz, 22 + 65535 + 20);
    std::vector<uint8_t> tail(tail_n);
    if (!read_at(f, fsz - tail_n, tail.data(), tail_n)) { err = "read failed"; return false; }

    int64_t eocd = -1;
    for (int64_t i = (int64_t)tail_n - 22; i >= 0; --i) {
        if (rd32(&tail[(size_t)i]) == 0x06054b50u && i + 22 + rd16(&tail[(size_t)i + 20]) <= (int64_t)tail_n) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) { err = "end of central directory not found"; return false; }

    const uint8_t* e = &tail[(size_t)eocd];
    uint64_t total = rd16(e + 10), cdsize = rd32(e + 12), cdoff = rd32(e + 16);
    if (total == 0xFFFFu || cdsize == 0xFFFFFFFFu || cdoff == 0xFFFFFFFFu) {   // ZIP64
        if (eocd < 20 || rd32(&tail[(size_t)eocd - 20]) != 0x07064b50u) { err = "zip64 locator missing"; return false; }
        const uint64_t z64 = rd64(&tail[(size_t)eocd - 20 + 8]);
        uint8_t z[56];
        if (!read_at(f, z64, z, sizeof z) || rd32(z) != 0x06064b50u) { err = "bad zip64 directory"; return false; }
        total = rd64(z + 32);
        cdsize = rd64(z + 40);
        cdoff = rd64(z + 48);
    }
    if (cdsize > (64u << 20) || cdoff + cdsize > fsz) { err = "bad central directory"; return false; }

    std::vector<uint8_t> cd((size_t)cdsize);
    if (!read_at(f, cdoff, cd.data(), cd.size())) { err = "cannot read central directory"; return false; }

    size_t p = 0;
    for (uint64_t i = 0; i < total; ++i) {
        if (p + 46 > cd.size() || rd32(&cd[p]) != 0x02014b50u) { err = "bad central directory entry"; return false; }
        const uint8_t* h = &cd[p];
        const size_t nl = rd16(h + 28), el = rd16(h + 30), cl = rd16(h + 32);
        if (p + 46 + nl + el + cl > cd.size()) { err = "truncated central directory"; return false; }

        ZipEnt z;
        z.flags = rd16(h + 8);
        z.method = rd16(h + 10);
        z.crc = rd32(h + 16);
        z.csize = rd32(h + 20);
        z.usize = rd32(h + 24);
        z.lho = rd32(h + 42);
        z.name.assign(reinterpret_cast<const char*>(h + 46), nl);

        const uint8_t* x = h + 46 + nl;
        for (size_t xp = 0; xp + 4 <= el;) {   // ZIP64 extra field: only fills in the fields that do not fit in 32 bits (0xFFFFFFFF)
            const uint16_t tag = rd16(x + xp), sz = rd16(x + xp + 2);
            if (xp + 4 + sz > el) break;
            if (tag == 0x0001) {
                size_t q = xp + 4;
                const size_t end = xp + 4 + sz;
                if (rd32(h + 24) == 0xFFFFFFFFu && q + 8 <= end) { z.usize = rd64(x + q); q += 8; }
                if (rd32(h + 20) == 0xFFFFFFFFu && q + 8 <= end) { z.csize = rd64(x + q); q += 8; }
                if (rd32(h + 42) == 0xFFFFFFFFu && q + 8 <= end) { z.lho = rd64(x + q); q += 8; }
            }
            xp += 4 + sz;
        }
        out.push_back(std::move(z));
        p += 46 + nl + el + cl;
    }
    return true;
}

bool zip_stream(FILE* f, const ZipEnt& z, const Sink& sink, std::string& err)
{
    if (z.flags & 1) { err = "encrypted entry"; return false; }
    uint8_t lh[30];
    if (!read_at(f, z.lho, lh, sizeof lh) || rd32(lh) != 0x04034b50u) { err = "bad local header"; return false; }
    const uint64_t data_off = z.lho + 30 + rd16(lh + 26) + rd16(lh + 28);
    if (fseeko(f, (off_t)data_off, SEEK_SET) != 0) { err = "seek failed"; return false; }

    uLong crc = crc32(0L, Z_NULL, 0);
    uint64_t left = z.csize, produced = 0;
    std::vector<uint8_t> in(1 << 16), out(1 << 16);

    if (z.method == 0) {   // stored
        while (left) {
            const size_t n = (size_t)std::min<uint64_t>(left, in.size());
            if (std::fread(in.data(), 1, n, f) != n) { err = "truncated entry"; return false; }
            crc = crc32(crc, in.data(), (uInt)n);
            produced += n;
            left -= n;
            if (!sink(in.data(), n)) { err = "write failed"; return false; }
        }
    }
    else if (z.method == 8) {   // deflate
        z_stream zs;
        std::memset(&zs, 0, sizeof zs);
        if (inflateInit2(&zs, -15) != Z_OK) { err = "inflateInit failed"; return false; }
        int rc = Z_OK;
        while (rc != Z_STREAM_END) {
            if (zs.avail_in == 0) {
                const size_t n = (size_t)std::min<uint64_t>(left, in.size());
                if (n == 0) { err = "truncated deflate stream"; inflateEnd(&zs); return false; }
                if (std::fread(in.data(), 1, n, f) != n) { err = "truncated entry"; inflateEnd(&zs); return false; }
                left -= n;
                zs.next_in = in.data();
                zs.avail_in = (uInt)n;
            }
            zs.next_out = out.data();
            zs.avail_out = (uInt)out.size();
            rc = inflate(&zs, Z_NO_FLUSH);
            if (rc != Z_OK && rc != Z_STREAM_END) { err = "inflate error"; inflateEnd(&zs); return false; }
            const size_t got = out.size() - zs.avail_out;
            if (got) {
                crc = crc32(crc, out.data(), (uInt)got);
                produced += got;
                if (!sink(out.data(), got)) { err = "write failed"; inflateEnd(&zs); return false; }
            }
        }
        inflateEnd(&zs);
    }
    else {
        err = "unsupported zip compression method " + std::to_string(z.method);
        return false;
    }
    if (produced != z.usize) { err = "size mismatch"; return false; }
    if ((uint32_t)crc != z.crc) { err = "CRC mismatch"; return false; }
    return true;
}

// ---------------------------------------------------------------- 7z

void* sz_alloc(ISzAllocPtr, size_t n) { return std::malloc(n); }
void sz_free(ISzAllocPtr, void* p) { std::free(p); }

std::string sz_error(SRes r)
{
    switch (r) {
    case SZ_ERROR_UNSUPPORTED: return "unsupported 7z feature (encrypted, or an uncommon filter)";
    case SZ_ERROR_MEM: return "out of memory";
    case SZ_ERROR_CRC: return "CRC mismatch";
    case SZ_ERROR_INPUT_EOF: return "truncated archive";
    case SZ_ERROR_NO_ARCHIVE: return "not a 7z archive";
    case SZ_ERROR_ARCHIVE: case SZ_ERROR_DATA: return "corrupt 7z archive";
    default: return "7z error " + std::to_string((int)r);
    }
}

std::string utf16_to_utf8(const uint16_t* w, size_t n)
{
    std::string s;
    for (size_t i = 0; i < n; ++i) {
        uint32_t c = w[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n && w[i + 1] >= 0xDC00 && w[i + 1] < 0xE000) {
            c = 0x10000 + ((c - 0xD800) << 10) + (w[i + 1] - 0xDC00);
            ++i;
        }
        if (c < 0x80) s += (char)c;
        else if (c < 0x800) { s += (char)(0xC0 | (c >> 6)); s += (char)(0x80 | (c & 0x3F)); }
        else if (c < 0x10000) { s += (char)(0xE0 | (c >> 12)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
        else { s += (char)(0xF0 | (c >> 18)); s += (char)(0x80 | ((c >> 12) & 0x3F)); s += (char)(0x80 | ((c >> 6) & 0x3F)); s += (char)(0x80 | (c & 0x3F)); }
    }
    return s;
}

class SevenZip {
public:
    SevenZip() { std::memset(&file_, 0, sizeof file_); std::memset(&look_, 0, sizeof look_); std::memset(&db_, 0, sizeof db_); }
    ~SevenZip()
    {
        if (out_) alloc_.Free(&alloc_, out_);
        if (db_init_) SzArEx_Free(&db_, &alloc_);
        if (look_.buf) std::free(look_.buf);
        if (file_open_) File_Close(&file_.file);
    }
    SevenZip(const SevenZip&) = delete;
    SevenZip& operator=(const SevenZip&) = delete;

    bool open(const std::string& path, std::string& err)
    {
        static std::once_flag once;
        std::call_once(once, [] { CrcGenerateTable(); });

        FileInStream_CreateVTable(&file_);
        if (InFile_Open(&file_.file, path.c_str()) != 0) { err = "cannot open file"; return false; }
        file_open_ = true;

        const size_t buf_size = 1 << 18;
        LookToRead2_CreateVTable(&look_, 0);
        look_.buf = static_cast<Byte*>(std::malloc(buf_size));
        if (!look_.buf) { err = "out of memory"; return false; }
        look_.bufSize = buf_size;
        LookToRead2_Init(&look_);
        look_.realStream = &file_.vt;

        SzArEx_Init(&db_);
        db_init_ = true;
        const SRes r = SzArEx_Open(&db_, &look_.vt, &alloc_, &alloc_);
        if (r != SZ_OK) { err = sz_error(r); return false; }
        return true;
    }

    uint32_t count() const { return (uint32_t)db_.NumFiles; }

    ArchiveEntry entry(uint32_t i) const
    {
        const size_t len = SzArEx_GetFileNameUtf16(&db_, i, nullptr);   // includes the terminating NUL
        std::vector<uint16_t> w(len ? len : 1, 0);
        SzArEx_GetFileNameUtf16(&db_, i, w.data());
        ArchiveEntry e;
        e.name = utf16_to_utf8(w.data(), len ? len - 1 : 0);
        e.size = SzArEx_GetFileSize(&db_, i);
        e.is_dir = SzArEx_IsDir(&db_, i) != 0;
        return e;
    }

    // data points into an internal cache (solid block), valid until the next extract or until the object is destroyed.
    bool extract(uint32_t i, const uint8_t*& data, size_t& size, std::string& err)
    {
        size_t off = 0, processed = 0;
        const SRes r = SzArEx_Extract(&db_, &look_.vt, i, &block_, &out_, &out_size_, &off, &processed, &alloc_, &alloc_);
        if (r != SZ_OK) { err = sz_error(r); return false; }
        data = out_ + off;
        size = processed;
        return true;
    }

private:
    ISzAlloc alloc_ = {sz_alloc, sz_free};
    CFileInStream file_;
    CLookToRead2 look_;
    CSzArEx db_;
    bool file_open_ = false, db_init_ = false;
    uint32_t block_ = 0xFFFFFFFFu;
    Byte* out_ = nullptr;
    size_t out_size_ = 0;
};

}  // namespace

// ================================================================ public interface

ArchiveKind archive_kind(const std::string& path)
{
    CFile f(path);
    if (!f.f) return ArchiveKind::None;
    uint8_t h[6] = {0};
    const size_t n = std::fread(h, 1, sizeof h, f.f);
    if (n >= 4 && h[0] == 'P' && h[1] == 'K' && ((h[2] == 3 && h[3] == 4) || (h[2] == 5 && h[3] == 6)))
        return ArchiveKind::Zip;
    if (n >= 6 && std::memcmp(h, "7z\xBC\xAF\x27\x1C", 6) == 0)
        return ArchiveKind::SevenZip;
    return ArchiveKind::None;
}

bool archive_list(const std::string& path, std::vector<ArchiveEntry>& out, std::string& err)
{
    out.clear();
    switch (archive_kind(path)) {
    case ArchiveKind::Zip: {
        CFile f(path);
        std::vector<ZipEnt> zs;
        if (!f.f || !zip_read_dir(f.f, zs, err)) { if (err.empty()) err = "cannot open file"; return false; }
        for (const ZipEnt& z : zs) {
            ArchiveEntry e;
            e.name = z.name;
            e.size = z.usize;
            e.is_dir = !z.name.empty() && z.name.back() == '/';
            out.push_back(std::move(e));
        }
        return true;
    }
    case ArchiveKind::SevenZip: {
        SevenZip sz;
        if (!sz.open(path, err)) return false;
        for (uint32_t i = 0; i < sz.count(); ++i)
            out.push_back(sz.entry(i));
        return true;
    }
    default:
        err = "not a zip or 7z archive";
        return false;
    }
}

bool archive_read(const std::string& path, const std::string& entry_name, std::vector<uint8_t>& out,
                  std::string& err, uint64_t max_bytes)
{
    out.clear();
    switch (archive_kind(path)) {
    case ArchiveKind::Zip: {
        CFile f(path);
        std::vector<ZipEnt> zs;
        if (!f.f || !zip_read_dir(f.f, zs, err)) { if (err.empty()) err = "cannot open file"; return false; }
        for (const ZipEnt& z : zs) {
            if (z.name != entry_name) continue;
            if (z.usize > max_bytes) { err = "entry too large"; return false; }
            out.reserve((size_t)z.usize);
            return zip_stream(f.f, z, [&out](const uint8_t* d, size_t n) { out.insert(out.end(), d, d + n); return true; }, err);
        }
        err = "entry not found";
        return false;
    }
    case ArchiveKind::SevenZip: {
        SevenZip sz;
        if (!sz.open(path, err)) return false;
        for (uint32_t i = 0; i < sz.count(); ++i) {
            const ArchiveEntry e = sz.entry(i);
            if (e.is_dir || e.name != entry_name) continue;
            if (e.size > max_bytes) { err = "entry too large"; return false; }
            const uint8_t* d = nullptr;
            size_t n = 0;
            if (!sz.extract(i, d, n, err)) return false;
            out.assign(d, d + n);
            return true;
        }
        err = "entry not found";
        return false;
    }
    default:
        err = "not a zip or 7z archive";
        return false;
    }
}

bool archive_extract_all(const std::string& path, const std::string& dest_dir, std::string& err, uint64_t max_total)
{
    if (!mkdirs(dest_dir)) { err = "cannot create " + dest_dir; return false; }
    uint64_t total = 0;

    // Write out one file (rel has passed safe_rel)
    const auto open_out = [&](const std::string& rel, std::string& full) -> FILE* {
        full = dest_dir + "/" + rel;
        const std::string dir = parent_dir(full);
        if (!dir.empty() && !mkdirs(dir)) return nullptr;
        return std::fopen(full.c_str(), "wb");
    };

    switch (archive_kind(path)) {
    case ArchiveKind::Zip: {
        CFile f(path);
        std::vector<ZipEnt> zs;
        if (!f.f || !zip_read_dir(f.f, zs, err)) { if (err.empty()) err = "cannot open file"; return false; }
        for (const ZipEnt& z : zs) {
            if (!z.name.empty() && z.name.back() == '/') continue;
            std::string rel = z.name;
            if (!safe_rel(rel)) { err = "unsafe path in archive: " + z.name; return false; }
            total += z.usize;
            if (total > max_total) { err = "archive too large"; return false; }
            std::string full;
            FILE* o = open_out(rel, full);
            if (!o) { err = "cannot write " + full; return false; }
            const bool ok = zip_stream(f.f, z, [o](const uint8_t* d, size_t n) { return std::fwrite(d, 1, n, o) == n; }, err);
            std::fclose(o);
            if (!ok) return false;
        }
        return true;
    }
    case ArchiveKind::SevenZip: {
        SevenZip sz;
        if (!sz.open(path, err)) return false;
        for (uint32_t i = 0; i < sz.count(); ++i) {
            const ArchiveEntry e = sz.entry(i);
            if (e.is_dir) continue;
            std::string rel = e.name;
            if (!safe_rel(rel)) { err = "unsafe path in archive: " + e.name; return false; }
            total += e.size;
            if (total > max_total) { err = "archive too large"; return false; }
            const uint8_t* d = nullptr;
            size_t n = 0;
            if (!sz.extract(i, d, n, err)) return false;
            std::string full;
            FILE* o = open_out(rel, full);
            if (!o) { err = "cannot write " + full; return false; }
            const bool ok = n == 0 || std::fwrite(d, 1, n, o) == n;
            std::fclose(o);
            if (!ok) { err = "write failed"; return false; }
        }
        return true;
    }
    default:
        err = "not a zip or 7z archive";
        return false;
    }
}

int archive_pick(const std::vector<ArchiveEntry>& entries, const char* valid_extensions)
{
    std::vector<std::string> exts;
    if (valid_extensions) {
        std::string cur;
        for (const char* p = valid_extensions;; ++p) {
            if (*p == '|' || *p == 0) {
                if (!cur.empty()) exts.push_back(lower(cur));
                cur.clear();
                if (*p == 0) break;
            }
            else cur += *p;
        }
    }
    int first_file = -1, only_file = -1, files = 0;
    for (size_t i = 0; i < entries.size(); ++i) {
        if (entries[i].is_dir) continue;
        ++files;
        only_file = (int)i;
        if (first_file < 0) first_file = (int)i;
    }
    if (files == 0) return -1;
    if (exts.empty()) return first_file;

    const auto listed = [&](const std::string& e) { return std::find(exts.begin(), exts.end(), e) != exts.end(); };
    for (const char* desc : { "m3u", "cue", "gdi" }) {   // description files first
        for (size_t i = 0; i < entries.size(); ++i)
            if (!entries[i].is_dir && ext_of(entries[i].name) == desc && listed(desc))
                return (int)i;
    }
    for (size_t i = 0; i < entries.size(); ++i)
        if (!entries[i].is_dir && listed(ext_of(entries[i].name)))
            return (int)i;
    return files == 1 ? only_file : -1;   // when there is only one file, hand it to the core even if the extension does not match
}

bool archive_safe_path(std::string& name) { return safe_rel(name); }

bool remove_tree(const std::string& path)
{
    struct stat st;
    if (::lstat(path.c_str(), &st) != 0)
        return errno == ENOENT;
    if (S_ISDIR(st.st_mode)) {
        if (DIR* d = ::opendir(path.c_str())) {
            while (const dirent* e = ::readdir(d)) {
                if (!std::strcmp(e->d_name, ".") || !std::strcmp(e->d_name, ".."))
                    continue;
                remove_tree(path + "/" + e->d_name);
            }
            ::closedir(d);
        }
        return ::rmdir(path.c_str()) == 0;
    }
    return ::unlink(path.c_str()) == 0;
}

}  // namespace ponia
