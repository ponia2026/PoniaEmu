// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Game list page (v0.2 design): static background + a single large rounded frosted-white card; inside the card, left = system title + plain-text list, right = video + cover + description.
// A blank placeholder frame for "previous / next" shows at each side edge (no real cover is drawn, to save GPU). The video frame is always 4:3; widescreen systems get black bars above and below inside the frame.
import QtQuick 2.15

Item {
    id: gv
    width: 960
    height: 540

    property var cur: null              // system description (colour, icon, name, letterbox)
    property int count: 0               // current number of list rows (search filter applied)
    property var gameAt: function (i) { return null; }
    property int gameIndex: 0
    property string allText: ""
    property string catText: ""         // translated category name (Home console ...)
    property string emptyText: ""
    property bool mediaPaused: false     // a game is launching
    property string queryText: ""       // the search condition is shown when not empty
    property int totalCount: 0
    property int favCount: 0
    property int allCount: 0
    property string fontUi: "Noto Sans TC"
    property string fontRound: "Varela Round"
    function hasCjk(t) { return /[⺀-鿿぀-ヿ가-힯＀-￯]/.test(t || ""); }

    readonly property var game: (count > 0 && gameIndex >= 0 && gameIndex < count) ? gameAt(gameIndex) : null
    readonly property color ink: cur ? cur.ink : "#8A2F33"
    readonly property color accent: cur ? cur.base : "#EE8D87"
    readonly property color dark: cur ? cur.dark : "#D96A66"
    readonly property color textMain: "#1F2329"
    readonly property color textSub: "#7A7F87"
    readonly property int rowH: 35

    // ---- background ----
    Image {
        anchors.fill: parent
        source: "assets/images/ponia_bg_light.png"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
    }

    // ---- left/right edges: blank placeholder frames of the previous / next game ----
    // The placeholder must be visible: glass card + a white underlay + a little shadow
    Item {
        x: -39; y: 158; width: 100; height: 247; visible: gv.count > 1
        Rectangle { x: 2; y: 6; width: parent.width; height: parent.height; radius: 30; color: Qt.rgba(0.24, 0.2, 0.16, 0.05) }
        Rectangle { anchors.fill: parent; radius: 30; color: Qt.rgba(1, 1, 1, 0.34) }
        GlassCard { anchors.fill: parent; radius: 30; tint: gv.accent }
    }
    Item {
        x: 899; y: 158; width: 100; height: 247; visible: gv.count > 1
        Rectangle { x: -2; y: 6; width: parent.width; height: parent.height; radius: 30; color: Qt.rgba(0.24, 0.2, 0.16, 0.05) }
        Rectangle { anchors.fill: parent; radius: 30; color: Qt.rgba(1, 1, 1, 0.34) }
        GlassCard { anchors.fill: parent; radius: 30; tint: gv.accent }
    }

    // ---- the single large card ----
    Item {
        id: card
        x: 77; y: 52; width: 806; height: 432

        // The glass base only shrinks at the top and bottom (top +14, bottom -11); the content inside keeps its position
        Item {
            id: plate
            x: 0; y: 14; width: card.width; height: card.height - 25
            Repeater {   // soft shadow
                model: 4
                Rectangle {
                    x: -index * 6; y: 12 + index * 3; width: plate.width + index * 12; height: plate.height + index * 4
                    radius: 32 + index * 4; color: Qt.rgba(0.24, 0.2, 0.16, 0.04 - index * 0.007)
                }
            }
            GlassCard { anchors.fill: parent; radius: 32; tint: gv.accent }
        }

        // ---- header: system icon chip + name + two capsules ----
        Rectangle {
            x: 24; y: 32; width: 76; height: 76; radius: 20
            color: Qt.rgba(Qt.color(gv.accent).r, Qt.color(gv.accent).g, Qt.color(gv.accent).b, 0.22)
            Image {
                anchors.centerIn: parent; width: 62 * (gv.cur && gv.cur.iconScale ? gv.cur.iconScale : 1); height: 53 * (gv.cur && gv.cur.iconScale ? gv.cur.iconScale : 1)
                source: gv.cur ? "assets/icons/" + (gv.cur.cicon || gv.cur.icon) + ".png" : ""
                fillMode: Image.PreserveAspectFit; smooth: true; mipmap: true
            }
        }
        Text {
            x: 114; y: 26; width: 224; height: 36
            text: gv.cur ? gv.cur.name : ""
            font.family: gv.hasCjk(text) ? gv.fontUi : gv.fontRound
            font.pixelSize: 26; font.bold: true
            verticalAlignment: Text.AlignVCenter
            color: gv.textMain
            fontSizeMode: Text.HorizontalFit; minimumPixelSize: 14
            elide: Text.ElideRight
        }
        Row {
            x: 114; y: 63; spacing: 8
            Repeater {
                model: [
                    (function () {
                        var p = [];
                        if (gv.cur && !gv.cur.virtual && gv.catText) p.push(gv.catText);
                        if (gv.cur && gv.cur.year > 0) p.push(gv.cur.year);
                        return p.join(" · ");
                    })()
                ]
                delegate: Rectangle {
                    visible: modelData !== ""
                    width: pt.width + 22; height: 24; radius: 12
                    color: Qt.tint("#ffffff", Qt.rgba(Qt.color(gv.accent).r, Qt.color(gv.accent).g, Qt.color(gv.accent).b, 0.16))
                    border.width: 1; border.color: Qt.rgba(Qt.color(gv.accent).r, Qt.color(gv.accent).g, Qt.color(gv.accent).b, 0.34)
                    Text {
                        id: pt; anchors.centerIn: parent; text: modelData
                        font.family: gv.fontUi; font.pixelSize: 12; color: gv.ink
                    }
                }
            }
        }
        StatsRow {
            x: 114; y: 90
            games: gv.totalCount; favs: gv.favCount; total: gv.allCount
            fontFamily: gv.fontRound; pixel: 13; ink: "#737A84"
        }
        // search condition
        Chip {
            visible: gv.queryText !== ""
            x: 24 + 290 - width; y: 100
            text: gv.queryText; ink: gv.ink; fontUi: gv.fontUi
        }

        // ---- left: plain-text game list (selected = capsule in a light system colour) ----
        Item {
            id: listBox
            x: 24; y: 120; width: 300; height: 280
            clip: true
            ListView {
                id: list
                anchors.fill: parent
                model: gv.count
                interactive: false
                boundsBehavior: Flickable.StopAtBounds
                delegate: Rectangle {
                    readonly property var g: gv.gameAt(index)
                    readonly property bool sel: index === gv.gameIndex
                    width: ListView.view.width; height: gv.rowH
                    radius: 17
                    color: sel ? Qt.rgba(Qt.color(gv.accent).r, Qt.color(gv.accent).g, Qt.color(gv.accent).b, 0.30) : "transparent"
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        x: 20; width: parent.width - 56
                        text: g ? g.title : ""
                        font.family: gv.fontUi; font.pixelSize: 16; font.bold: sel
                        fontSizeMode: Text.HorizontalFit; minimumPixelSize: 13
                        elide: Text.ElideRight
                        color: sel ? gv.ink : gv.textSub
                    }
                    Text {
                        anchors.verticalCenter: parent.verticalCenter
                        anchors.right: parent.right; anchors.rightMargin: 16
                        visible: g ? g.favorite : false
                        text: "★"; font.family: gv.fontUi; font.pixelSize: 15
                        color: "#FF4B4B"; opacity: 0.9   // the red of the Ponia horse-head logo
                    }
                }
            }
            NumberAnimation { id: scrollAnim; target: list; property: "contentY"; duration: 120; easing.type: Easing.OutQuad }
            function ensure() {
                var h = gv.rowH, vh = height;
                var maxY = Math.max(0, gv.count * h - vh);
                scrollAnim.to = Math.max(0, Math.min(maxY, gv.gameIndex * h - (vh - h) / 2));
                scrollAnim.restart();
            }
            Connections { target: gv; function onGameIndexChanged() { listBox.ensure(); } function onCountChanged() { listBox.ensure(); } }
            Text {
                visible: gv.count === 0
                anchors.fill: parent; anchors.margins: 16
                text: gv.emptyText; wrapMode: Text.WordWrap
                font.family: gv.fontUi; font.pixelSize: 16; color: gv.textSub
            }
        }

        // ---- top right: video (4:3) + cover ----
        Rectangle {
            x: 346; y: 32; width: 290; height: 218; radius: 18
            color: Qt.rgba(1, 1, 1, 0.55)
            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.9)
            visible: gv.count > 0
            MediaPane {
                anchors.fill: parent; anchors.margins: 6
                game: gv.game
                suspended: gv.mediaPaused
                letterbox: gv.cur ? gv.cur.letterbox : false
                showLogo: false
                fallbackIcon: gv.cur ? "assets/icons/" + (gv.cur.cicon || gv.cur.icon) + ".png" : ""
            }
        }
        Rectangle {
            x: 650; y: 32; width: 132; height: 218; radius: 18
            color: Qt.rgba(1, 1, 1, 0.55)
            border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.9)
            visible: gv.count > 0 && cover.source.toString() !== ""
            Image {
                id: cover
                // same top edge as the video (6px inset of MediaPane); the image itself is top-aligned
                anchors.top: parent.top; anchors.topMargin: 6; anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 12; height: parent.height - 12
                verticalAlignment: Image.AlignTop
                source: gv.game && gv.game.assets.boxFront ? gv.game.assets.boxFront : ""
                fillMode: Image.PreserveAspectFit      // scale proportionally, no stretching
                asynchronous: true; mipmap: true
            }
        }

        // ---- bottom right: game name + logo (small, top right) + description (scrolls down slowly when too long) ----
        Item {
            id: info
            x: 346; y: 262; width: 436; height: 141
            visible: gv.count > 0
            clip: true

            readonly property string bodyText: {
                var g = gv.game;
                if (!g) return "";
                var t = g.description || g.summary;
                if (t) return t;
                var parts = [];
                if (g.developer) parts.push(g.developer);
                if (g.publisher && g.publisher !== g.developer) parts.push(g.publisher);
                if (g.genre) parts.push(g.genre);
                return parts.join("  ·  ");
            }

            TextMetrics {   // natural width of the game name: the logo sits right next to the name instead of being pushed to the far right
                id: ttlMetrics
                font: ttl.font; text: ttl.text
            }
            Text {
                id: ttl
                width: Math.min(ttlMetrics.advanceWidth + 4, parent.width - (logo.source.toString() !== "" ? 126 : 0)); height: 32
                text: gv.game ? gv.game.title : ""
                font.family: gv.hasCjk(text) ? gv.fontUi : gv.fontRound; font.pixelSize: 24; font.bold: true
                verticalAlignment: Text.AlignVCenter
                color: gv.textMain
                fontSizeMode: Text.HorizontalFit; minimumPixelSize: 14
                elide: Text.ElideRight
            }
            Image {
                id: logo
                x: ttl.width + 14; y: -2; width: Math.min(110, parent.width - x); height: 36
                source: gv.game && gv.game.assets.logo ? gv.game.assets.logo : ""
                fillMode: Image.PreserveAspectFit; asynchronous: true; mipmap: true
                horizontalAlignment: Image.AlignLeft
            }
            Item {
                id: body
                y: 40; width: parent.width; height: parent.height - y
                clip: true
                Text {
                    id: bodyTxt
                    width: parent.width - 12
                    text: info.bodyText
                    wrapMode: Text.WordWrap
                    font.family: gv.fontUi; font.pixelSize: 14; lineHeight: 1.3
                    color: gv.textSub
                    readonly property real overflow: Math.max(0, height - body.height)
                    function rearm() { scroller.stop(); y = 0; if (overflow > 0) scroller.start(); }
                    onTextChanged: rearm()
                    onOverflowChanged: rearm()
                    SequentialAnimation {
                        id: scroller
                        running: false
                        loops: Animation.Infinite
                        PauseAnimation { duration: 2500 }
                        NumberAnimation { target: bodyTxt; property: "y"; to: -bodyTxt.overflow; duration: Math.max(1, bodyTxt.overflow * 55) }
                        PauseAnimation { duration: 2000 }
                        NumberAnimation { target: bodyTxt; property: "y"; to: 0; duration: 300 }
                        onStopped: bodyTxt.y = 0
                    }
                    Component.onCompleted: rearm()
                }
                Rectangle {   // thin scroll bar (hints that there is more content; no white fade at the bottom any more)
                    visible: bodyTxt.overflow > 0
                    x: parent.width - 4; width: 3; radius: 2
                    color: Qt.rgba(0.5, 0.5, 0.55, 0.5)
                    height: Math.max(18, body.height * body.height / (body.height + bodyTxt.overflow))
                    y: bodyTxt.overflow > 0 ? (-bodyTxt.y / bodyTxt.overflow) * (body.height - height) : 0
                }
            }
        }
    }
}
