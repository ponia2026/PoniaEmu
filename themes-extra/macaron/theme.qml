// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Ponia 主题（PoniaFE 自创；只用天马/Ponia 两边都有的 API，原版天马也能载入）
// 系统页：五格马卡龙底图 + 固定版位；机种按「最爱(虚拟) → 家用机 → 街机 → 掌机 → 多平台」再依年份排序。
// 游戏页：左列表 + 右影片/简介/封面；版面依机种分 4:3 / 16:9。
// 设计稿与参数见 ponia-theme-samples（system_view_v3 / gamelist_gba_*_v3）。
import QtQuick 2.15
import "systems.js" as Sys

FocusScope {
    id: root
    focus: true

    // ---- 机种清单状态 ----
    property var overrides: ({})
    property var descs: []             // 与 api.collections 同序的机种描述
    property var order: []             // 全部条目：-1＝「最爱」虚拟机种，其余＝集合索引（已排序）
    property var shown: []             // 套用「机种搜寻」后的条目
    property int pos: 0                // 在 shown 里的位置
    property string sysQuery: ""
    property bool sysNoMatch: false

    readonly property var favDesc: Sys.favDesc(tFavName)
    readonly property int n: shown.length
    readonly property int curE: n > 0 ? shown[Math.min(pos, n - 1)] : -1
    readonly property bool isFav: curE < 0
    readonly property var sysCol: curE >= 0 ? api.collections.get(curE) : null
    readonly property var curDesc: n > 0 ? entryDesc(curE) : favDesc
    readonly property var prevDesc: n > 0 ? entryDesc(shown[(pos - 1 + n) % n]) : favDesc
    readonly property var nextDesc: n > 0 ? entryDesc(shown[(pos + 1) % n]) : favDesc
    readonly property var shownDescs: { var a = []; for (var i = 0; i < shown.length; i++) a.push(entryDesc(shown[i])); return a; }

    // ---- 游戏页状态 ----
    property bool inGames: false
    property int gameIndex: 0
    property string gameQuery: ""
    property var favGames: []          // 所有最爱游戏（「最爱」虚拟机种的内容）
    property var rowMap: null          // 搜寻过滤后的来源索引；null＝不过滤
    property int rowCount: 0
    property int favInSys: 0           // 当前机种里的最爱数（系统页数量框用）
    property int gamesInSys: 0

    // ---- 字体：上下白条与列表用 Noto Sans TC，中间区域用 Varela Round ----
    FontLoader { id: fontTcR; source: "assets/fonts/NotoSansTC-Regular.ttf" }
    FontLoader { id: fontTcB; source: "assets/fonts/NotoSansTC-Bold.ttf" }
    FontLoader { id: fontScR; source: "assets/fonts/NotoSansSC-Regular.ttf" }
    FontLoader { id: fontScB; source: "assets/fonts/NotoSansSC-Bold.ttf" }
    FontLoader { id: fontRound; source: "assets/fonts/VarelaRound-Regular.ttf" }
    // 翻译文件里只有简体中文把这条译成 "CJK:SC"，借此知道目前是简体界面
    readonly property bool useSC: (qsTr("CJK:TC") + api.tr) === "CJK:SC"
    readonly property string uiFamily: useSC ? fontScR.name : fontTcR.name

    // ---- 文案（机种名不翻译；其余走翻译）----
    readonly property string tBrowse: (qsTr("Browse") + api.tr)
    readonly property string tConfirm: (qsTr("Confirm") + api.tr)
    readonly property string tExit: (qsTr("Exit") + api.tr)
    readonly property string tBack: (qsTr("Back") + api.tr)
    readonly property string tSearch: (qsTr("Search") + api.tr)
    readonly property string tAddFav: (qsTr("Add to favorites") + api.tr)
    readonly property string tSwitch: (qsTr("Switch system") + api.tr)
    readonly property string tPage: (qsTr("Page") + api.tr)
    readonly property string tAll: (qsTr("All games") + api.tr)
    readonly property string tFavGames: (qsTr("Favorite games") + api.tr)
    readonly property string tFavName: "Favorites"   // 不翻译，维持原文
    readonly property string tEmptyFav: (qsTr("No favorites yet. Press the favorite button on a game to add it here.") + api.tr)
    readonly property string tNoMatch: (qsTr("No match") + api.tr)
    readonly property string tSearchSys: (qsTr("Search systems") + api.tr)
    readonly property string tSearchGames: (qsTr("Search games") + api.tr)
    readonly property string tOk: (qsTr("OK") + api.tr)
    readonly property string tClear: (qsTr("Clear") + api.tr)
    readonly property string tSpace: (qsTr("Space") + api.tr)
    readonly property var tCat: ({
        fav: "Favorites",
        home: (qsTr("Home console") + api.tr), arcade: (qsTr("Arcade") + api.tr),
        hand: (qsTr("Handheld") + api.tr), computer: (qsTr("Computer") + api.tr),
        other: (qsTr("Others") + api.tr), multi: (qsTr("Others") + api.tr)
    })

    // ---- 首次下载游戏核心（Ponia 专有 api.cores；没有就当不存在）----
    readonly property var cores: (typeof api.cores !== "undefined") ? api.cores : null
    property bool coreDlgClosed: false
    readonly property bool coreDlgOpen: cores !== null && !inGames && !osk.visible
        && (cores.needed || cores.state === "downloading"
            || ((cores.state === "done" || cores.state === "error") && !coreDlgClosed))
    readonly property string tCoreTitle: !cores ? "" :
        cores.state === "downloading" ? (qsTr("Downloading game cores…") + api.tr) :
        cores.state === "done" ? (qsTr("Game cores are ready") + api.tr) :
        cores.state === "error" ? (qsTr("Download failed") + api.tr) : (qsTr("Download game cores") + api.tr)
    readonly property string coreBody: !cores ? "" :
        cores.state === "downloading" ? (qsTr("Now downloading: %1") + api.tr).arg(cores.current) :
        cores.state === "done" ? (qsTr("All set. You can start playing now.") + api.tr) :
        cores.state === "error" ? cores.errorText :
        (qsTr("%1 emulator cores are needed for your games (about %2 MB). Download them now?") + api.tr)
            .arg(cores.missingCount).arg(Math.max(1, Math.round(cores.totalKb / 1024)))
    readonly property var coreHints: !cores ? [] :
        cores.state === "downloading" ? [] :
        cores.state === "done" ? [ { icon: "dot_down", label: (qsTr("OK") + api.tr) } ] :
        cores.state === "error" ? [ { icon: "dot_down", label: (qsTr("Retry") + api.tr) }, { icon: "dot_right", label: (qsTr("Close") + api.tr) } ] :
        [ { icon: "dot_down", label: (qsTr("Download") + api.tr) }, { icon: "dot_right", label: (qsTr("Later") + api.tr) } ]

    // ================= 机种清单 =================
    function entryDesc(e) { return e < 0 ? favDesc : descs[e]; }

    function rebuild() {
        var cnt = api.collections.count;
        var d = [];
        for (var i = 0; i < cnt; i++) {
            var c = api.collections.get(i);
            d.push(Sys.describe(c.shortName, c.name, overrides));
        }
        descs = d;
        order = [-1].concat(Sys.sortedOrder(d));
        // 记住上次的机种（api.memory 由天马保存，原版天马同样可用）
        var want = cnt > 0 ? 0 : -1;
        var saved = api.memory.get("macaron.sys");
        if (saved === "@fav") want = -1;
        else if (saved) for (var j = 0; j < cnt; j++) if (api.collections.get(j).name === saved) { want = j; break; }
        applySysFilter(want);
    }

    // keep＝要保持选中的条目
    function applySysFilter(keep) {
        var q = sysQuery.toLowerCase();
        var res = [];
        for (var i = 0; i < order.length; i++) {
            var e = order[i];
            var nm = entryDesc(e).name + " " + (e >= 0 ? api.collections.get(e).name : "");
            if (q === "" || nm.toLowerCase().indexOf(q) >= 0) res.push(e);
        }
        sysNoMatch = (res.length === 0);
        if (res.length === 0) return;           // 没有符合就保持原清单，只亮「无符合」提示
        shown = res;
        var p = shown.indexOf(keep);
        pos = p >= 0 ? p : 0;
        refreshSysStats();
    }

    // 选用：theme 目录下 overrides.json，形如 {"pico8":{"y":2015,"c":"other","n":"PICO-8","r":"16:9"}}
    function loadOverrides() {
        var x = new XMLHttpRequest();
        x.open("GET", Qt.resolvedUrl("overrides.json"));
        x.onreadystatechange = function () {
            if (x.readyState !== 4) return;
            var o = {};
            if (x.responseText) { try { o = JSON.parse(x.responseText); } catch (e) { o = {}; } }
            overrides = o;
            rebuild();
        };
        x.send();
    }

    Component.onCompleted: { buildFavs(); rebuild(); loadOverrides(); }

    function buildFavs() {
        var a = [];
        var total = api.allGames.count;
        for (var i = 0; i < total; i++) {
            var g = api.allGames.get(i);
            if (g.favorite) a.push(g);
        }
        favGames = a;
    }

    function refreshSysStats() {
        if (curE < 0) { gamesInSys = favGames.length; favInSys = favGames.length; return; }
        var col = api.collections.get(curE);
        gamesInSys = col.games.count;
        var k = 0;
        for (var i = 0; i < col.games.count; i++) if (col.games.get(i).favorite) k++;
        favInSys = k;
    }

    function entryKey(e) { return e < 0 ? "@fav" : api.collections.get(e).name; }
    function rememberSys() { if (n > 0) api.memory.set("macaron.sys", entryKey(curE)); }

    function setPos(p) {
        if (n === 0) return;
        pos = ((p % n) + n) % n;
        rememberSys();
        refreshSysStats();
    }

    // ================= 游戏列表 =================
    function srcCount() { return curE < 0 ? favGames.length : (sysCol ? sysCol.games.count : 0); }
    function srcGet(i) { return curE < 0 ? favGames[i] : sysCol.games.get(i); }
    function rowGame(r) {
        if (r < 0 || r >= rowCount) return null;
        return srcGet(rowMap ? rowMap[r] : r);
    }
    function currentGame() { return inGames ? rowGame(gameIndex) : null; }

    // keepSel=true：搜寻条件改变时尽量保持原本选中的那款游戏
    function applyGameFilter(keepSel) {
        var keep = keepSel ? currentGame() : null;
        var q = gameQuery.toLowerCase();
        if (q === "") { rowMap = null; rowCount = srcCount(); }
        else {
            var m = [], c = srcCount();
            for (var i = 0; i < c; i++) {
                var g = srcGet(i);
                if (g.title.toLowerCase().indexOf(q) >= 0) m.push(i);
            }
            rowMap = m; rowCount = m.length;
        }
        gameIndex = Math.max(0, Math.min(rowCount - 1, gameIndex));
        if (keep) {
            for (var r = 0; r < rowCount; r++) { if (rowGame(r) === keep) { gameIndex = r; break; } }
        }
    }

    function enterGames() {
        if (curE < 0) buildFavs();
        gameQuery = "";
        gameIndex = 0;
        inGames = true;
        applyGameFilter();
        var last = api.memory.get("macaron.g." + entryKey(curE));
        if (rowCount > 0 && typeof last === "number") gameIndex = Math.max(0, Math.min(rowCount - 1, last));
    }
    function leaveGames() { inGames = false; gameQuery = ""; refreshSysStats(); }

    function setGame(i) {
        if (rowCount === 0) return;
        gameIndex = Math.max(0, Math.min(rowCount - 1, i));
        if (!rowMap) api.memory.set("macaron.g." + entryKey(curE), gameIndex);
    }
    // 游戏页内切机种（L1/R1）
    function switchSystem(delta) {
        setPos(pos + delta);
        enterGames();
    }
    function toggleFavorite() {
        var g = currentGame();
        if (!g) return;
        g.favorite = !g.favorite;
        buildFavs();
        if (curE < 0) applyGameFilter();       // 最爱列表里取消 → 当场消失
        refreshSysStats();
    }
    // 启动前先停掉影片并稍等一下：影片播放中（硬件解码器占着）启动 RetroArch 会让它在安卓上直接当掉
    property bool launching: false
    property var pendingGame: null
    function launchCurrent() {
        if (launching) return;
        var g = currentGame();
        if (!g) return;
        pendingGame = g;
        launching = true;
        launchTimer.start();
    }
    Timer {
        id: launchTimer
        interval: 450
        onTriggered: {
            var g = root.pendingGame;
            root.pendingGame = null;
            root.launching = false;
            if (g) g.launch();
        }
    }
    onLaunchingChanged: { /* 影片的暂停由 GameView.mediaPaused 绑定 */ }

    // ================= 舞台：按 960×540 设计稿等比缩放 =================
    Rectangle { anchors.fill: parent; color: "#ffffff" }

    Item {
        id: stage
        width: 960
        height: 540
        anchors.centerIn: parent
        scale: Math.min(root.width / 960, root.height / 540)
        clip: true

        SystemView {
            anchors.fill: parent
            visible: !root.inGames
            items: root.shownDescs
            pos: root.pos
            gamesCount: root.gamesInSys
            favCount: root.favInSys
            allCount: api.allGames.count
            catTexts: root.tCat
            allText: root.tAll; favText: root.tFavGames
            fontRound: fontRound.name; fontUi: root.uiFamily
        }
        GameView {
            anchors.fill: parent
            visible: root.inGames
            cur: root.curDesc
            catText: (root.curDesc && !root.curDesc.virtual) ? root.tCat[root.curDesc.cat] : ""
            count: root.rowCount
            gameAt: root.rowGame
            gameIndex: root.gameIndex
            mediaPaused: root.launching || !root.inGames
            totalCount: root.srcCount()
            favCount: root.favInSys
            allCount: api.allGames.count
            allText: root.tAll
            emptyText: (root.isFav && root.gameQuery === "") ? root.tEmptyFav : root.tNoMatch
            queryText: root.gameQuery
            fontUi: root.uiFamily; fontRound: fontRound.name
        }

        // 机种搜寻条件（系统页）
        Chip {
            visible: !root.inGames && root.sysQuery !== ""
            x: 480 - width / 2; y: 52
            z: 3
            text: root.sysQuery + (root.sysNoMatch ? "  ·  " + root.tNoMatch : "")
            ink: root.curDesc.ink; fontUi: root.uiFamily
        }

        // 上白条：左 Ponia 标志，右网络 + 时间
        Rectangle {
            x: 0; y: 0; width: 960; height: 44; color: Qt.rgba(1, 1, 1, 0.25)
            z: 5
            Image {
                x: 22; y: 6; width: 32; height: 32
                source: "assets/images/ponia_mark.svg"
                sourceSize.width: 96; sourceSize.height: 96
            }
            Text {
                x: 60; anchors.verticalCenter: parent.verticalCenter
                text: "Ponia"
                font.family: root.uiFamily; font.pixelSize: 22; font.bold: true
                color: "#3A3A3A"
            }
            StatusArea {
                anchors.right: parent.right; anchors.rightMargin: 22
                anchors.verticalCenter: parent.verticalCenter
                fontUi: root.uiFamily
            }
        }

        // 下白条：操作提示（一整块米色圆角矩形）
        Rectangle {
            x: 0; y: 484; width: 960; height: 56; color: Qt.rgba(1, 1, 1, 0.25)
            z: 5
            HintBar {
                anchors.centerIn: parent
                fontUi: root.uiFamily
                hints: root.inGames
                    ? [ { icon: "l1r1", label: root.tSwitch },
                        { icon: "dpad_ud", label: root.tBrowse },
                        { icon: "dpad_lr", label: root.tPage },
                        { icon: "dot_down", label: root.tConfirm },
                        { icon: "dot_right", label: root.tExit },
                        { icon: "dot_left", label: root.tSearch },
                        { icon: "dot_up", label: root.tAddFav } ]
                    : [ { icon: "dpad_lr", label: root.tBrowse },
                        { icon: "dot_down", label: root.tConfirm },
                        { icon: "dot_right", label: root.tExit },
                        { icon: "dot_left", label: root.tSearch } ]
            }
        }

        // ---- 首次下载游戏核心（api.cores 只有 Ponia 有；原版天马没有这个物件，整段不会出现）----
        Item {
            id: coreDlg
            anchors.fill: parent
            z: 20
            visible: root.coreDlgOpen
            Rectangle { anchors.fill: parent; color: Qt.rgba(0.12, 0.13, 0.15, 0.45) }
            MouseArea { anchors.fill: parent }   // 吃掉点击

            Rectangle {
                width: 560; height: 276; radius: 28
                anchors.centerIn: parent
                color: Qt.rgba(1, 1, 1, 0.985)
                border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.9)

                Text {
                    x: 32; y: 24; width: parent.width - 64; height: 34
                    text: root.tCoreTitle
                    font.family: root.uiFamily; font.pixelSize: 24; font.bold: true
                    color: "#1F2329"; verticalAlignment: Text.AlignVCenter
                }
                Text {
                    x: 32; y: 66; width: parent.width - 64; height: 80
                    wrapMode: Text.WordWrap
                    text: root.coreBody
                    font.family: root.uiFamily; font.pixelSize: 16; lineHeight: 1.4
                    color: "#4A4F57"
                }
                Text {
                    visible: root.cores && root.cores.state === "idle"
                    x: 32; y: 170; width: parent.width - 64; height: 20
                    elide: Text.ElideRight
                    text: root.cores ? root.cores.missingNames.join("  ·  ") : ""
                    font.family: root.uiFamily; font.pixelSize: 13; color: "#7A7F87"
                }
                Rectangle {   // 进度条
                    visible: root.cores && root.cores.state === "downloading"
                    x: 32; y: 172; width: parent.width - 64; height: 10; radius: 5
                    color: Qt.rgba(0.5, 0.5, 0.55, 0.2)
                    Rectangle {
                        width: parent.width * (root.cores ? root.cores.progress : 0); height: parent.height; radius: 5
                        color: root.curDesc.base
                    }
                }
                HintBar {
                    anchors.horizontalCenter: parent.horizontalCenter
                    y: parent.height - 62
                    fontUi: root.uiFamily
                    hints: root.coreHints
                }
            }
        }

        SearchOSK {
            id: osk
            z: 10
            ink: root.curDesc.ink; accent: root.curDesc.base
            fontUi: root.uiFamily
            okText: root.tOk; clearText: root.tClear; spaceText: root.tSpace
            hintText: root.inGames ? root.tSearchGames : root.tSearchSys
            onTextEdited: (text) => {
                if (root.inGames) { root.gameQuery = text; root.applyGameFilter(true); }
                else { root.sysQuery = text; root.applySysFilter(root.curE); }
            }
        }
    }

    // ================= 按键 =================
    // 天马按键语义：A=isAccept、B=isCancel、X=isDetails（搜寻）、Y=isFilters（加入最爱）、L1/R1=isPrevPage/isNextPage
    Keys.onPressed: (event) => {
        if (coreDlg.visible) {   // 首次下载核心对话框：确认＝下载／关闭／重试，取消＝以后再说／关闭
            event.accepted = true;
            if (event.isAutoRepeat) return;
            var cs = cores.state;
            if (api.keys.isAccept(event)) {
                if (cs === "downloading") return;
                if (cs === "done") coreDlgClosed = true;
                else { coreDlgClosed = false; cores.start(); }
            } else if (api.keys.isCancel(event)) {
                if (cs === "downloading") return;
                if (cs === "idle") cores.dismiss(); else coreDlgClosed = true;
            }
            return;
        }
        if (osk.visible) { event.accepted = osk.handleKey(event, api.keys); return; }

        if (!root.inGames) {
            if (event.key === Qt.Key_Right) { event.accepted = true; setPos(pos + 1); }
            else if (event.key === Qt.Key_Left) { event.accepted = true; setPos(pos - 1); }
            else if (api.keys.isNextPage(event)) { event.accepted = true; setPos(pos + 5); }
            else if (api.keys.isPrevPage(event)) { event.accepted = true; setPos(pos - 5); }
            else if (event.isAutoRepeat) { return; }
            else if (api.keys.isAccept(event)) { event.accepted = true; enterGames(); }
            else if (api.keys.isDetails(event)) { event.accepted = true; osk.title = tSearchSys; osk.open(sysQuery); }
            else if (api.keys.isCancel(event) && sysQuery !== "") { event.accepted = true; sysQuery = ""; applySysFilter(curE); }
            // 其余（含取消/菜单键）交给主程序打开主菜单
        } else {
            if (event.key === Qt.Key_Down) { event.accepted = true; setGame(gameIndex + 1); }
            else if (event.key === Qt.Key_Up) { event.accepted = true; setGame(gameIndex - 1); }
            else if (event.key === Qt.Key_Right) { event.accepted = true; setGame(gameIndex + 10); }
            else if (event.key === Qt.Key_Left) { event.accepted = true; setGame(gameIndex - 10); }
            else if (api.keys.isNextPage(event)) { event.accepted = true; switchSystem(1); }
            else if (api.keys.isPrevPage(event)) { event.accepted = true; switchSystem(-1); }
            else if (event.isAutoRepeat) { return; }
            else if (api.keys.isFilters(event)) { event.accepted = true; toggleFavorite(); }
            else if (api.keys.isDetails(event)) { event.accepted = true; osk.title = tSearchGames; osk.open(gameQuery); }
            else if (api.keys.isCancel(event)) {
                event.accepted = true;
                if (gameQuery !== "") { gameQuery = ""; applyGameFilter(true); }
                else leaveGames();
            }
            else if (api.keys.isAccept(event)) { event.accepted = true; launchCurrent(); }
        }
    }
}
