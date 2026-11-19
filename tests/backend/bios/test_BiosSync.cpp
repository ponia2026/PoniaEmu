// SPDX-License-Identifier: GPL-2.0-or-later
// Ponia.Roms/BIOS：找资料夹、连结进 system 目录、根目录 zip 的分流、MAME 暂存目录。

#include <QtTest/QtTest>

#include "bios_sync.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

namespace {
void touch(const QString& path, const QByteArray& data = "x")
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    QVERIFY(f.open(QIODevice::WriteOnly));
    f.write(data);
}
std::string s(const QString& q) { return q.toStdString(); }
} // namespace

class test_BiosSync : public QObject {
    Q_OBJECT
private slots:
    void find_roms_bios_folder();
    void find_explicit_wins();
    void find_none();
    void sync_tree_and_root_zips();
    void sync_keeps_user_files();
    void mame_stage();
};

void test_BiosSync::find_roms_bios_folder()
{
    QTemporaryDir t;
    touch(t.path() + "/Ponia.Roms/FBNeo/kof95.zip");
    QDir().mkpath(t.path() + "/Ponia.Roms/BIOS");
    QCOMPARE(QString::fromStdString(ponia::find_bios_dir(s(t.path() + "/Ponia.Roms/FBNeo/kof95.zip"), "")), t.path() + "/Ponia.Roms/BIOS");
}

void test_BiosSync::find_explicit_wins()
{
    QTemporaryDir t, u;
    QDir().mkpath(t.path() + "/Ponia.Roms/BIOS");
    QCOMPARE(QString::fromStdString(ponia::find_bios_dir(s(t.path() + "/Ponia.Roms/a/b.zip"), s(u.path()))), u.path());
}

void test_BiosSync::find_none()
{
    QTemporaryDir t;
    touch(t.path() + "/Roms/FC/a.zip");
    QVERIFY(ponia::find_bios_dir(s(t.path() + "/Roms/FC/a.zip"), "").empty());
}

void test_BiosSync::sync_tree_and_root_zips()
{
    QTemporaryDir t;
    touch(t.path() + "/bios/scph5501.bin", "ps1");
    touch(t.path() + "/bios/dc/dc_boot.bin", "dc");
    touch(t.path() + "/bios/neogeo.zip", "ng");
    touch(t.path() + "/bios/README.md", "doc");
    const QString sys = t.path() + "/sys";
    const int n = ponia::sync_bios(s(t.path() + "/bios"), s(sys));
    QVERIFY(n >= 5);
    QCOMPARE(QFile(sys + "/scph5501.bin").size(), 3);
    QCOMPARE(QFile(sys + "/dc/dc_boot.bin").size(), 2);
    QVERIFY(QFile::exists(sys + "/neogeo.zip"));
    QVERIFY(QFile::exists(sys + "/fbneo/neogeo.zip"));
    QVERIFY(QFile::exists(sys + "/mame2003-plus/neogeo.zip"));
    QVERIFY(!QFile::exists(sys + "/README.md"));
    // 再跑一次不出错、结果相同
    QVERIFY(ponia::sync_bios(s(t.path() + "/bios"), s(sys)) >= 5);
    QCOMPARE(QFile(sys + "/dc/dc_boot.bin").size(), 2);
}

void test_BiosSync::sync_keeps_user_files()
{
    QTemporaryDir t;
    touch(t.path() + "/bios/scph5501.bin", "from-bios");
    touch(t.path() + "/sys/scph5501.bin", "mine");
    ponia::sync_bios(s(t.path() + "/bios"), s(t.path() + "/sys"));
    QFile f(t.path() + "/sys/scph5501.bin");
    QVERIFY(f.open(QIODevice::ReadOnly));
    QCOMPARE(f.readAll(), QByteArray("mine"));
}

void test_BiosSync::mame_stage()
{
    QTemporaryDir t;
    touch(t.path() + "/roms/sf2.zip", "parent");
    touch(t.path() + "/roms/sf2m4.zip", "clone");
    touch(t.path() + "/bios/neogeo.zip", "ng");
    const std::string out = ponia::stage_mame_content(s(t.path() + "/roms/sf2m4.zip"), s(t.path() + "/bios"), s(t.path() + "/stage"));
    QCOMPARE(QString::fromStdString(out), t.path() + "/stage/sf2m4.zip");
    QVERIFY(QFile::exists(t.path() + "/stage/sf2m4.zip"));
    QVERIFY(QFile::exists(t.path() + "/stage/sf2.zip"));       // parent 一起带过去
    QVERIFY(QFile::exists(t.path() + "/stage/neogeo.zip"));    // BIOS 也在
    // 换一款游戏再暂存：上一款的连结要清掉
    touch(t.path() + "/roms2/other.zip", "o");
    ponia::stage_mame_content(s(t.path() + "/roms2/other.zip"), s(t.path() + "/bios"), s(t.path() + "/stage"));
    QVERIFY(!QFile::exists(t.path() + "/stage/sf2m4.zip"));
    QVERIFY(QFile::exists(t.path() + "/stage/other.zip"));
}

QTEST_MAIN(test_BiosSync)
#include "test_BiosSync.moc"
