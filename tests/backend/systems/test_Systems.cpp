// SPDX-License-Identifier: GPL-2.0-or-later
// 机种别名 -> 机种键 -> 核心：锁住 Ponia.Roms 资料夹方案会用到的名称。

#include <QtTest/QtTest>

#include "ponia_cores.h"
#include "ponia_systems.h"

#include <string>

class test_Systems : public QObject {
    Q_OBJECT

private slots:
    void folder_and_collection_names_data();
    void folder_and_collection_names();
    void core_for_system();
    void standalone_systems_have_no_builtin_core();
};

void test_Systems::folder_and_collection_names_data()
{
    QTest::addColumn<QString>("name");
    QTest::addColumn<QString>("key");

    // Atari：三个资料夹各自一个机种；合集名 ATARI 仍代表 2600
    QTest::newRow("atari2600") << "ATARI2600" << "atari2600";
    QTest::newRow("atari5200") << "ATARI5200" << "atari5200";
    QTest::newRow("atari7800") << "ATARI7800" << "atari7800";
    QTest::newRow("atari collection") << "ATARI" << "atari2600";
    QTest::newRow("a7800 alias") << "A7800" << "atari7800";
    // 街机：按引擎分，含 hack 尾缀
    QTest::newRow("fbneo") << "FBNeo" << "fbneo";
    QTest::newRow("fbneo hack") << "FBNeo hack" << "fbneo";
    QTest::newRow("mame") << "MAME" << "mame";
    QTest::newRow("mame hack") << "MAME hack" << "mame";
    QTest::newRow("legacy FBNEO ACT") << "FBNEO ACT" << "fbneo";
    // 其他资料夹
    QTest::newRow("fc hack") << "FC hack" << "fc";
    QTest::newRow("sfc hack") << "SFC hack" << "sfc";
    QTest::newRow("md") << "MD" << "genesis";
    QTest::newRow("md hack") << "MD hack" << "genesis";
    QTest::newRow("dreamcast") << "Dreamcast" << "dc";
    QTest::newRow("gamecube") << "GameCube" << "gc";
    // 认不得的要回空字串（交给下一层判断）
    QTest::newRow("unknown folder") << "Roms" << "";
    QTest::newRow("saturn") << "Saturn" << "saturn";
    QTest::newRow("psp") << "PSP" << "psp";
}

void test_Systems::folder_and_collection_names()
{
    QFETCH(QString, name);
    QFETCH(QString, key);
    QCOMPARE(QString::fromStdString(ponia::resolve_system(name.toStdString())), key);
}

void test_Systems::core_for_system()
{
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("atari2600")), QStringLiteral("stella"));
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("atari5200")), QStringLiteral("a5200"));
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("atari7800")), QStringLiteral("prosystem"));
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("fbneo")), QStringLiteral("fbneo"));
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("mame")), QStringLiteral("mamearcade"));
    // 对应的核心都要登记在核心表里，首次下载才找得到
    for (const char* sys : { "atari5200", "atari7800" }) {
        const std::string cid = ponia::core_id_for_system(sys);
        QVERIFY2(ponia::core_info(cid) != nullptr, sys);
        QVERIFY(!ponia::core_info(cid)->noncommercial);
    }
}

void test_Systems::standalone_systems_have_no_builtin_core()
{
    // 1118 起没有机种走独立模拟器 App：PSP 走 libretro ppsspp、GameCube 走 dolphin、Saturn 走 yabasanshiro
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("psp")), QStringLiteral("ppsspp"));
    QVERIFY(ponia::standalone_launch_cmd("psp") == nullptr);
    QVERIFY(ponia::standalone_launch_cmd("gc") == nullptr);
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("gc")), QStringLiteral("dolphin"));
    QVERIFY(ponia::standalone_launch_cmd("fc") == nullptr);
    QVERIFY(ponia::standalone_launch_cmd("saturn") == nullptr);
    QCOMPARE(QString::fromStdString(ponia::core_id_for_system("saturn")), QStringLiteral("yabasanshiro"));
}

QTEST_MAIN(test_Systems)
#include "test_Systems.moc"
