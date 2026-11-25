// SPDX-License-Identifier: GPL-2.0-or-later
// Archive reading (zip / 7z): list, read, pick an entry, extract to a directory, zip-slip protection, corruption detection.

#include <QtTest/QtTest>

#include "archive.h"

#include <cstring>

#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include "fixtures.inc"

namespace {

std::vector<uint8_t> expected_rom()
{
    std::vector<uint8_t> v(3000);
    for (size_t i = 0; i < v.size(); ++i) v[i] = (uint8_t)((i * 31 + 7) & 0xFF);
    return v;
}
std::vector<uint8_t> expected_readme()
{
    std::vector<uint8_t> v;
    for (int i = 0; i < 20; ++i) for (const char* p = "hello ponia\n"; *p; ++p) v.push_back((uint8_t)*p);
    return v;
}
std::vector<uint8_t> expected_raw()
{
    std::vector<uint8_t> v(100);
    for (size_t i = 0; i < v.size(); ++i) v[i] = (uint8_t)((i * 3) & 0xFF);
    return v;
}

QString write_file(const QDir& dir, const QString& name, const unsigned char* data, unsigned int len)
{
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return QString();
    f.write(reinterpret_cast<const char*>(data), len);
    return path;
}

int index_of(const std::vector<ponia::ArchiveEntry>& e, const char* name)
{
    for (size_t i = 0; i < e.size(); ++i)
        if (e[i].name == name) return (int)i;
    return -1;
}

}  // namespace

class test_Archive : public QObject {
    Q_OBJECT

private slots:
    void kind_detection();
    void zip_list_and_read();
    void zip_limits_and_errors();
    void zip_corruption_is_detected();
    void sevenzip_list_and_read();
    void extract_all_zip_and_7z();
    void zip_slip_is_refused();
    void pick_by_extension();
    void remove_tree_removes();
};

void test_Archive::kind_detection()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QDir d(tmp.path());
    const QString z = write_file(d, "a.zip", kSampleZip, kSampleZip_len);
    const QString s = write_file(d, "a.7z", kSample7z, kSample7z_len);
    const unsigned char txt[] = "just text, not an archive";
    const QString t = write_file(d, "a.txt", txt, sizeof txt);
    QCOMPARE((int)ponia::archive_kind(z.toStdString()), (int)ponia::ArchiveKind::Zip);
    QCOMPARE((int)ponia::archive_kind(s.toStdString()), (int)ponia::ArchiveKind::SevenZip);
    QCOMPARE((int)ponia::archive_kind(t.toStdString()), (int)ponia::ArchiveKind::None);
    QCOMPARE((int)ponia::archive_kind(d.filePath("missing.zip").toStdString()), (int)ponia::ArchiveKind::None);
}

void test_Archive::zip_list_and_read()
{
    QTemporaryDir tmp;
    const QString z = write_file(QDir(tmp.path()), "a.zip", kSampleZip, kSampleZip_len);
    std::vector<ponia::ArchiveEntry> e;
    std::string err;
    QVERIFY2(ponia::archive_list(z.toStdString(), e, err), err.c_str());
    QCOMPARE((int)e.size(), 4);
    const int rom = index_of(e, "game/rom.nes"), raw = index_of(e, "raw.bin"), dir = index_of(e, "dir/");
    QVERIFY(rom >= 0 && raw >= 0 && dir >= 0 && index_of(e, "readme.txt") >= 0);
    QVERIFY(e[(size_t)dir].is_dir);
    QVERIFY(!e[(size_t)rom].is_dir);
    QCOMPARE((int)e[(size_t)rom].size, 3000);

    std::vector<uint8_t> out;
    QVERIFY2(ponia::archive_read(z.toStdString(), "game/rom.nes", out, err, 1 << 20), err.c_str());   // deflate
    QVERIFY(out == expected_rom());
    QVERIFY2(ponia::archive_read(z.toStdString(), "raw.bin", out, err, 1 << 20), err.c_str());          // stored
    QVERIFY(out == expected_raw());
    QVERIFY2(ponia::archive_read(z.toStdString(), "readme.txt", out, err, 1 << 20), err.c_str());
    QVERIFY(out == expected_readme());
}

void test_Archive::zip_limits_and_errors()
{
    QTemporaryDir tmp;
    const QString z = write_file(QDir(tmp.path()), "a.zip", kSampleZip, kSampleZip_len);
    std::vector<uint8_t> out;
    std::string err;
    QVERIFY(!ponia::archive_read(z.toStdString(), "nope.bin", out, err, 1 << 20));
    QVERIFY(!ponia::archive_read(z.toStdString(), "game/rom.nes", out, err, 1000));   // over the size limit
    QVERIFY(!err.empty());
}

void test_Archive::zip_corruption_is_detected()
{
    QTemporaryDir tmp;
    std::vector<unsigned char> bad(kSampleZip, kSampleZip + kSampleZip_len);
    // 1) truncated (central directory missing)
    const QString cut = write_file(QDir(tmp.path()), "cut.zip", bad.data(), (unsigned)bad.size() / 2);
    std::vector<ponia::ArchiveEntry> e;
    std::string err;
    QVERIFY(!ponia::archive_list(cut.toStdString(), e, err));
    // 2) one byte of a stored entry changed -> CRC mismatch
    const std::vector<uint8_t> raw = expected_raw();
    size_t at = std::string::npos;
    for (size_t i = 0; i + raw.size() <= bad.size() && at == std::string::npos; ++i)
        if (std::equal(raw.begin(), raw.end(), bad.begin() + (long)i)) at = i;
    QVERIFY(at != std::string::npos);
    bad[at + 10] ^= 0xFF;
    const QString flipped = write_file(QDir(tmp.path()), "flip.zip", bad.data(), (unsigned)bad.size());
    std::vector<uint8_t> out;
    QVERIFY(!ponia::archive_read(flipped.toStdString(), "raw.bin", out, err, 1 << 20));
    QVERIFY(err.find("CRC") != std::string::npos);
}

void test_Archive::sevenzip_list_and_read()
{
    QTemporaryDir tmp;
    const QString s = write_file(QDir(tmp.path()), "a.7z", kSample7z, kSample7z_len);
    std::vector<ponia::ArchiveEntry> e;
    std::string err;
    QVERIFY2(ponia::archive_list(s.toStdString(), e, err), err.c_str());
    QCOMPARE((int)e.size(), 2);
    QVERIFY(index_of(e, "rom.nes") >= 0 && index_of(e, "readme.txt") >= 0);
    std::vector<uint8_t> out;
    QVERIFY2(ponia::archive_read(s.toStdString(), "rom.nes", out, err, 1 << 20), err.c_str());
    QVERIFY(out == expected_rom());
    QVERIFY2(ponia::archive_read(s.toStdString(), "readme.txt", out, err, 1 << 20), err.c_str());
    QVERIFY(out == expected_readme());
    QVERIFY(!ponia::archive_read(s.toStdString(), "rom.nes", out, err, 100));   // over the size limit
    QVERIFY(!ponia::archive_read(s.toStdString(), "missing", out, err, 1 << 20));
}

void test_Archive::extract_all_zip_and_7z()
{
    QTemporaryDir tmp;
    const QDir d(tmp.path());
    const QString z = write_file(d, "a.zip", kSampleZip, kSampleZip_len);
    const QString s = write_file(d, "a.7z", kSample7z, kSample7z_len);
    std::string err;

    const QString zo = d.filePath("out_zip");
    QVERIFY2(ponia::archive_extract_all(z.toStdString(), zo.toStdString(), err, 1 << 20), err.c_str());
    QFile rom(zo + "/game/rom.nes");
    QVERIFY(rom.open(QIODevice::ReadOnly));
    QCOMPARE((int)rom.size(), 3000);
    QVERIFY(QFile::exists(zo + "/raw.bin") && QFile::exists(zo + "/readme.txt"));

    const QString so = d.filePath("out_7z");
    QVERIFY2(ponia::archive_extract_all(s.toStdString(), so.toStdString(), err, 1 << 20), err.c_str());
    QFile rom7(so + "/rom.nes");
    QVERIFY(rom7.open(QIODevice::ReadOnly));
    const QByteArray got = rom7.readAll();
    const std::vector<uint8_t> want = expected_rom();
    QVERIFY(got.size() == (int)want.size() && std::memcmp(got.constData(), want.data(), want.size()) == 0);   // note: char and uint8_t cannot be compared directly with std::equal

    // total extracted size limit
    QVERIFY(!ponia::archive_extract_all(z.toStdString(), d.filePath("out_small").toStdString(), err, 100));
}

void test_Archive::zip_slip_is_refused()
{
    QTemporaryDir tmp;
    const QDir d(tmp.path());
    const QString z = write_file(d, "evil.zip", kEvilZip, kEvilZip_len);
    std::string err;
    QVERIFY(!ponia::archive_extract_all(z.toStdString(), d.filePath("inner/dest").toStdString(), err, 1 << 20));
    QVERIFY(!QFile::exists(d.filePath("inner/evil.txt")));
    QVERIFY(!QFile::exists(d.filePath("evil.txt")));

    std::string n = "../x";
    QVERIFY(!ponia::archive_safe_path(n));
    n = "/abs/x";
    QVERIFY(!ponia::archive_safe_path(n));
    n = "a\\b\\c.bin";
    QVERIFY(ponia::archive_safe_path(n));
    QCOMPARE(QString::fromStdString(n), QStringLiteral("a/b/c.bin"));
    n = "./game/x.bin";
    QVERIFY(ponia::archive_safe_path(n));
    QCOMPARE(QString::fromStdString(n), QStringLiteral("game/x.bin"));
}

void test_Archive::pick_by_extension()
{
    std::vector<ponia::ArchiveEntry> e(4);
    e[0].name = "readme.txt";
    e[1].name = "game/rom.NES";   // extension match is case-insensitive
    e[2].name = "raw.bin";
    e[3].name = "dir/"; e[3].is_dir = true;
    QCOMPARE(ponia::archive_pick(e, "nes|fds|unf"), 1);
    QCOMPARE(ponia::archive_pick(e, "bin"), 2);
    QCOMPARE(ponia::archive_pick(e, nullptr), 0);          // no hint list: first non-directory entry
    QCOMPARE(ponia::archive_pick(e, "xyz"), -1);           // several files, none matches
    e.resize(1);
    QCOMPARE(ponia::archive_pick(e, "xyz"), 0);            // only one file: let the core try it

    std::vector<ponia::ArchiveEntry> disc(3);
    disc[0].name = "game (Track 1).bin";
    disc[1].name = "game.cue";
    disc[2].name = "game.m3u";
    QCOMPARE(ponia::archive_pick(disc, "bin|cue|m3u"), 2);   // m3u first
    disc.pop_back();
    QCOMPARE(ponia::archive_pick(disc, "bin|cue"), 1);       // then cue (never .bin)
    QCOMPARE(ponia::archive_pick(std::vector<ponia::ArchiveEntry>(), "nes"), -1);
}

void test_Archive::remove_tree_removes()
{
    QTemporaryDir tmp;
    const QDir d(tmp.path());
    QVERIFY(d.mkpath("t/a/b"));
    QFile f(d.filePath("t/a/b/x.bin"));
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.close();
    QVERIFY(ponia::remove_tree(d.filePath("t").toStdString()));
    QVERIFY(!QFileInfo::exists(d.filePath("t")));
    QVERIFY(ponia::remove_tree(d.filePath("never-existed").toStdString()));   // a missing path counts as success
}

QTEST_MAIN(test_Archive)
#include "test_Archive.moc"
