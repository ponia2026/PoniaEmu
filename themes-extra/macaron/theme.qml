// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Ponia theme (written for PoniaEmu; uses only APIs that exist in both Pegasus and Ponia, so the original Pegasus can load it too)
// System page: five-panel macaron background + fixed layout; systems are sorted by "Favorites (virtual) -> home consoles -> arcade -> handhelds -> multi-platform", then by year.
// Game page: list on the left + video/description/cover on the right; the layout is 4:3 / 16:9 depending on the system.
// Design drafts and parameters: see ponia-theme-samples (system_view_v3 / gamelist_gba_*_v3).
import QtQuick 2.15
import "systems.js" as Sys

FocusScope {
    id: root
    focus: true

    // ---- system list state ----
    property var overrides: ({})
    property var descs: []             // system descriptions, in the same order as api.collections
    property var order: []             // all entries: -1 = the virtual "Favorites" system, the others = collection index (sorted)
    property var shown: []             // entries after the "system search" is applied
    property int pos: 0                // position in shown
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

    // ---- game page state ----
    property bool inGames: false
    property int gameIndex: 0
    property string gameQuery: ""
    property var favGames: []          // all favourite games (the content of the virtual "Favorites" system)
    property var rowMap: null          // source indices after search filtering; null = no filtering
    property int rowCount: 0
    property int favInSys: 0           // number of favourites in the current system (for the count box of the system page)
    property int gamesInSys: 0

    // ---- fonts: Noto Sans CJK (Regular + Bold, Traditional/Simplified Chinese and Japanese) for the bars and the list, Varela Round for the middle area ----
    FontLoader { id: fontCjkR; source: "assets/fonts/NotoSansCJK-Regular.otf" }
    FontLoader { id: fontCjkB; source: "assets/fonts/NotoSansCJK-Bold.otf" }
    FontLoader { id: fontRound; source: "assets/fonts/VarelaRound-Regular.ttf" }
    readonly property string uiFamily: fontCjkR.name

    // ---- texts (system names are not translated; the rest goes through translation) ----
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
    readonly property string tFavName: "Favorites"   // not translated, keep the original text
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

    // ---- first download of game cores (api.cores is Ponia-only; treated as absent when missing) ----
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

    // ================= system list =================
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
        // remember the last system (api.memory is stored by Pegasus, so the original Pegasus works the same)
        var want = cnt > 0 ? 0 : -1;
        var saved = api.memory.get("macaron.sys");
        if (saved === "@fav") want = -1;
        else if (saved) for (var j = 0; j < cnt; j++) if (api.collections.get(j).name === saved) { want = j; break; }
        applySysFilter(want);
    }

    // keep = the entry that should stay selected
    function applySysFilter(keep) {
        var q = sysQuery.toLowerCase();
        var res = [];
        for (var i = 0; i < order.length; i++) {
            var e = order[i];
            var nm = entryDesc(e).name + " " + (e >= 0 ? api.collections.get(e).name : "");
            if (q === "" || nm.toLowerCase().indexOf(q) >= 0) res.push(e);
        }
        sysNoMatch = (res.length === 0);
        if (res.length === 0) return;           // no match: keep the original list and only light up the "no match" hint
        shown = res;
        var p = shown.indexOf(keep);
        pos = p >= 0 ? p : 0;
        refreshSysStats();
    }

    // optional: overrides.json in the theme directory, shaped like {"pico8":{"y":2015,"c":"other","n":"PICO-8","r":"16:9"}}
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

    Component.onCompleted: {
        buildFavs(); rebuild(); loadOverrides();
        // Coming back from a game (the frontend was rebuilt): open the game list of the last system, not the system list
        if (api.memory.get("macaron.resume") === "games") {
            api.memory.set("macaron.resume", "");
            if (shown.length > 0) enterGames();
        }
    }

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

    // ================= game list =================
    function srcCount() { return curE < 0 ? favGames.length : (sysCol ? sysCol.games.count : 0); }
    function srcGet(i) { return curE < 0 ? favGames[i] : sysCol.games.get(i); }
    function rowGame(r) {
        if (r < 0 || r >= rowCount) return null;
        return srcGet(rowMap ? rowMap[r] : r);
    }
    function currentGame() { return inGames ? rowGame(gameIndex) : null; }

    // keepSel=true: when the search condition changes, try to keep the previously selected game
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
    // switch system within the game page (L1/R1)
    function switchSystem(delta) {
        setPos(pos + delta);
        enterGames();
    }
    function toggleFavorite() {
        var g = currentGame();
        if (!g) return;
        g.favorite = !g.favorite;
        buildFavs();
        if (curE < 0) applyGameFilter();       // unfavourite in the favourites list -> disappears at once
        refreshSysStats();
    }
    // Before launching, stop the video and wait a moment: launching a game while a video is playing (the hardware decoder is busy) crashes it outright on Android
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
            // On Linux the frontend is rebuilt after the game ends: remember that we left from the game list, so it opens there again
            if (g) { api.memory.set("macaron.resume", "games"); g.launch(); }
        }
    }
    onLaunchingChanged: { /* the pause of the video is bound by GameView.mediaPaused */ }

    // ================= stage: scaled proportionally from the 960x540 design draft =================
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

        // system search condition (system page)
        Chip {
            visible: !root.inGames && root.sysQuery !== ""
            x: 480 - width / 2; y: 52
            z: 3
            text: root.sysQuery + (root.sysNoMatch ? "  ·  " + root.tNoMatch : "")
            ink: root.curDesc.ink; fontUi: root.uiFamily
        }

        // top white bar: Ponia logo on the left, network + time on the right
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

        // bottom white bar: hints (one beige rounded rectangle)
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

        // ---- first download of game cores (api.cores only exists in Ponia; the original Pegasus has no such object, so this whole block never appears) ----
        Item {
            id: coreDlg
            anchors.fill: parent
            z: 20
            visible: root.coreDlgOpen
            Rectangle { anchors.fill: parent; color: Qt.rgba(0.12, 0.13, 0.15, 0.45) }
            MouseArea { anchors.fill: parent }   // swallow clicks

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
                Rectangle {   // progress bar
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

    // ================= keys =================
    // Pegasus key semantics: A=isAccept, B=isCancel, X=isDetails (search), Y=isFilters (add to favourites), L1/R1=isPrevPage/isNextPage
    Keys.onPressed: (event) => {
        if (coreDlg.visible) {   // first-download dialog for cores: accept = download / close / retry, cancel = later / close
            event.accepted = true;
            if (event.isAutoRepeat) return;
            var cs = cores.state;
            if (api.keys.isAccept(event)) {
                if (cs === "downloading") return;
                if (cs === "done") { coreDlgClosed = true; cores.acknowledge(); }
                else { coreDlgClosed = false; cores.start(); }
            } else if (api.keys.isCancel(event)) {
                if (cs === "downloading") return;
                if (cs === "idle") cores.dismiss();
                else { coreDlgClosed = true; cores.acknowledge(); }
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
            // everything else (including cancel/menu keys) is handed to the main program to open the main menu
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
