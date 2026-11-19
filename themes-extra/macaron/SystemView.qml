// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// 系统页（v0.2 设计）：静态低多边形底图 + 景深轮播。中央一张大圆角磨砂卡，左右依距离缩小、变淡、微转。
// 设计稿坐标 960×540；上下栏（44 / 56）由 theme.qml 叠在上面（半透明，融入底图）。
import QtQuick 2.15
import "systems.js" as Sys

Item {
    id: view
    width: 960
    height: 540

    property var items: []               // 全部机种描述（已排序、已套用搜寻）
    property int pos: 0                  // 目前选中的位置
    property int gamesCount: 0
    property int favCount: 0
    property int allCount: 0
    property var catTexts: ({})          // cat -> 已翻译类别名
    property string allText: ""
    property string favText: ""
    property string fontRound: "Varela Round"
    property string fontUi: "Noto Sans SC"
    function hasCjk(t) { return /[⺀-鿿぀-ヿ가-힯＀-￯]/.test(t || ""); }

    readonly property int n: items.length

    // 展开（不取模）的位置：pos 换到另一端时走最短路径，轮播才会平顺滑过去
    property real track: 0
    property int _lastPos: 0
    onPosChanged: {
        if (n === 0) { _lastPos = pos; return; }
        var d = pos - _lastPos;
        if (d > n / 2) d -= n; else if (d < -n / 2) d += n;
        track += d;
        _lastPos = pos;
    }
    Component.onCompleted: { track = pos; _lastPos = pos; }
    property real anim: track
    Behavior on anim { NumberAnimation { duration: 250; easing.type: Easing.OutCubic } }

    // ---- 底图 ----
    Image {
        anchors.fill: parent
        source: "assets/images/ponia_bg_light.png"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
    }
    // ---- 反光地板：卡片下方一条横向亮带（设计稿）----
    Rectangle {
        x: 0; y: 370; width: 960; height: 120
        gradient: Gradient {
            GradientStop { position: 0.00; color: Qt.rgba(1, 1, 1, 0.0) }
            GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.50) }
            GradientStop { position: 0.60; color: Qt.rgba(1, 1, 1, 0.40) }
            GradientStop { position: 1.00; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    // ---- 轮播 ----
    Repeater {
        model: view.n > 0 ? 5 : 0     // 只做中央±2 张：再外面几乎看不到（opacity<0.15），省 GPU（电视盒很吃紧）
        delegate: Item {
            id: card
            readonly property int k: Math.round(view.anim) + (index - 2)                 // 展开位置
            readonly property real d: k - view.anim                                       // 离中心的距离（连续）
            readonly property real ad: Math.abs(d)
            readonly property var desc: view.items[((k % view.n) + view.n) % view.n]
            readonly property color accent: desc ? desc.base : "#888888"
            readonly property real foc: Math.max(0, 1 - ad)                             // 1＝正中
            readonly property color accentC: Qt.color(card.accent)
            readonly property real lum: 0.299 * accentC.r + 0.587 * accentC.g + 0.114 * accentC.b
            readonly property real sat: Math.max(accentC.r, accentC.g, accentC.b) - Math.min(accentC.r, accentC.g, accentC.b)
            // 灰阶的机种色（街机深灰、PSP 黑银等）光晕收敛，避免脏灰一团；有彩度的照常发光
            readonly property real glowStrength: sat < 0.15 ? (lum < 0.45 ? 0.22 : 0.40) : 0.85

            width: 276; height: 360
            x: 480 - width / 2 + (d < 0 ? -1 : 1) * (Math.min(ad, 1) * 262 + Math.max(0, Math.min(ad, 2) - 1) * 170 + Math.max(0, ad - 2) * 60)
            y: 260 - height / 2 + (1 - foc) * 6
            scale: Math.max(0.4, ad <= 1 ? 1 - 0.22 * ad : 0.78 - 0.26 * (ad - 1))
            opacity: Math.max(0, ad <= 1 ? 1 - 0.25 * ad : 0.75 - 0.4 * (ad - 1))
            z: 100 - ad * 10
            visible: ad < 3.2 && desc

            // 透视转向（设计稿：侧卡靠中央那一边较高、外侧较矮）。Rotation 没有透视，这里自己组 4x4：
            // 绕 Y 轴转 θ 后做透视除法（视距 D），再移回卡片中心。
            readonly property real theta: Math.max(-1, Math.min(1, d)) * 22 * Math.PI / 180
            transform: Matrix4x4 {
                readonly property real s: Math.sin(card.theta)
                readonly property real c: Math.cos(card.theta)
                readonly property real dist: 900
                readonly property real cx: card.width / 2
                readonly property real cy: card.height / 2
                matrix: Qt.matrix4x4(c + cx * s / dist, 0, 0, cx - cx * c - cx * cx * s / dist,
                                     cy * s / dist,     1, 0, -cy * cx * s / dist,
                                     0,              0, 1, 0,
                                     s / dist,          0, 0, 1 - cx * s / dist)
            }

            // 地板反光：卡片正下方一圈带机种色的柔光（卡片在它前面，只露出下缘）。
            // 光晕一律用预先烘好的 PNG（assets/glow/<机种键>.png），不在执行期用 Canvas 重画——
            // 切换机种时重画 Canvas 是卡顿的主因。
            Image {
                x: -card.width * 0.10; y: card.height - 30; width: card.width * 1.2; height: 70
                source: card.desc ? "assets/glow/" + card.desc.icon + ".png" : ""
                opacity: 0.35 * (0.4 + card.foc * 0.6)
                smooth: true
            }
            // 阴影（很淡：设计稿的卡片几乎是浮在地板上）
            Repeater {
                model: 3
                Rectangle {
                    x: -index * 4; y: 12 + index * 3; width: card.width + index * 8; height: card.height + index * 3
                    radius: 36 + index * 4; color: Qt.rgba(0.24, 0.22, 0.2, (0.035 - index * 0.009) * (0.4 + card.foc * 0.6))
                }
            }
            // 镜面玻璃卡（四角对称圆角）
            GlassCard { anchors.fill: parent; radius: 36; tint: card.accent; strength: 0.8 + 0.2 * card.foc }

            // 机种色渲染光晕：在图标后面（取代原本的色块），光心偏上
            Image {
                x: (parent.width - width) / 2; y: 112 - height / 2; width: 230; height: 190
                source: card.desc ? "assets/glow/" + card.desc.icon + ".png" : ""
                smooth: true
            }
            // 机种线稿图标
            Image {
                x: (parent.width - width) / 2; y: 28 + (168 - height) / 2
                width: 196 * (card.desc && card.desc.iconScale ? card.desc.iconScale : 1) * (card.desc && card.desc.cicon ? 0.62 : 1); height: 168 * (card.desc && card.desc.iconScale ? card.desc.iconScale : 1) * (card.desc && card.desc.cicon ? 0.62 : 1)
                source: card.desc ? "assets/icons/" + (card.desc.cicon || card.desc.icon) + ".png" : ""
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            // 机种名（含中日韩字改用 Noto Sans）
            Text {
                x: 8; y: 188; width: parent.width - 16; height: 44
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                text: card.desc ? card.desc.name : ""
                font.family: view.hasCjk(text) ? view.fontUi : view.fontRound
                font.pixelSize: text.length > 14 ? 26 : 32; font.bold: true
                color: "#1F2329"
                elide: Text.ElideRight
            }

            // 膠囊：中央卡＝「类别 · 年份」白底 + 机种色字 + 机种色光晕；侧卡＝只写类别、机种色淡底
            Rectangle {
                id: pill
                readonly property bool center: card.foc > 0.5
                readonly property string label: {
                    if (!card.desc) return "";
                    var parts = [];
                    if (!card.desc.virtual && view.catTexts[card.desc.cat]) parts.push(view.catTexts[card.desc.cat]);
                    if (center && card.desc.year > 0) parts.push(card.desc.year);
                    return parts.join("  ·  ");
                }
                visible: label !== ""
                x: (parent.width - width) / 2; y: center ? 242 : 250
                width: pillText.width + 34; height: 30; radius: 15
                color: center ? Qt.tint("#ffffff", Qt.rgba(card.accentC.r, card.accentC.g, card.accentC.b, 0.20))
                              : Qt.tint(Qt.rgba(1, 1, 1, 0.85), Qt.rgba(card.accentC.r, card.accentC.g, card.accentC.b, 0.30))
                border.width: 1
                border.color: Qt.rgba(card.accentC.r, card.accentC.g, card.accentC.b, center ? 0.40 : 0.22)
                Image {   // 机种色光晕（中央卡较明显）
                    z: -1; x: -24; y: -14; width: parent.width + 48; height: parent.height + 28
                    source: card.desc ? "assets/glow/" + card.desc.icon + ".png" : ""
                    opacity: pill.center ? 0.5 : 0.2
                    smooth: true
                }
                Text {
                    id: pillText; anchors.centerIn: parent
                    text: pill.label
                    font.family: view.fontUi; font.pixelSize: 15
                    color: card.desc.ink
                }
            }

            // 统计数字列（只在中央卡淡入）：手柄 本机种游戏数 | 星 最爱数 | 叠层 全部机种总数
            StatsRow {
                x: (parent.width - width) / 2; y: 282
                visible: card.foc > 0.4     // 只有中央卡才建立画面
                opacity: Math.max(0, card.foc * 1.6 - 0.6)
                games: view.gamesCount; favs: view.favCount; total: view.allCount
                fontFamily: view.fontRound; pixel: 16; ink: "#6F757E"
            }
        }
    }
}
