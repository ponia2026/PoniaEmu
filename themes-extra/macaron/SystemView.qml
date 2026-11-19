// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// System page (v0.2 design): static low-poly background + depth-of-field carousel. A large rounded frosted card in the centre; the cards on both sides shrink, fade and turn slightly with distance.
// Design coordinates 960x540; the top and bottom bars (44 / 56) are laid over it by theme.qml (translucent, blending into the background).
import QtQuick 2.15
import "systems.js" as Sys

Item {
    id: view
    width: 960
    height: 540

    property var items: []               // all system descriptions (sorted, search applied)
    property int pos: 0                  // currently selected position
    property int gamesCount: 0
    property int favCount: 0
    property int allCount: 0
    property var catTexts: ({})          // cat -> translated category name
    property string allText: ""
    property string favText: ""
    property string fontRound: "Varela Round"
    property string fontUi: "Noto Sans CJK TC"
    function hasCjk(t) { return /[⺀-鿿぀-ヿ가-힯＀-￯]/.test(t || ""); }

    readonly property int n: items.length

    // unwrapped (non-modulo) position: when pos jumps to the other end it takes the shortest path, so that the carousel slides smoothly
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

    // ---- background ----
    Image {
        anchors.fill: parent
        source: "assets/images/ponia_bg_light.png"
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        smooth: true
    }
    // ---- reflective floor: a horizontal bright band below the cards (design draft) ----
    Rectangle {
        x: 0; y: 370; width: 960; height: 120
        gradient: Gradient {
            GradientStop { position: 0.00; color: Qt.rgba(1, 1, 1, 0.0) }
            GradientStop { position: 0.45; color: Qt.rgba(1, 1, 1, 0.50) }
            GradientStop { position: 0.60; color: Qt.rgba(1, 1, 1, 0.40) }
            GradientStop { position: 1.00; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }

    // ---- carousel ----
    Repeater {
        model: view.n > 0 ? 5 : 0     // only the centre +-2 cards: further out they are almost invisible (opacity<0.15); saves GPU (TV boxes are tight on it)
        delegate: Item {
            id: card
            readonly property int k: Math.round(view.anim) + (index - 2)                 // unwrapped position
            readonly property real d: k - view.anim                                       // distance from the centre (continuous)
            readonly property real ad: Math.abs(d)
            readonly property var desc: view.items[((k % view.n) + view.n) % view.n]
            readonly property color accent: desc ? desc.base : "#888888"
            readonly property real foc: Math.max(0, 1 - ad)                             // 1 = exactly in the centre
            readonly property color accentC: Qt.color(card.accent)
            readonly property real lum: 0.299 * accentC.r + 0.587 * accentC.g + 0.114 * accentC.b
            readonly property real sat: Math.max(accentC.r, accentC.g, accentC.b) - Math.min(accentC.r, accentC.g, accentC.b)
            // the glow of grey-scale system colours (dark grey arcade, black-silver PSP, ...) is toned down to avoid a dirty grey blob; colours with saturation glow as usual
            readonly property real glowStrength: sat < 0.15 ? (lum < 0.45 ? 0.22 : 0.40) : 0.85

            width: 276; height: 360
            x: 480 - width / 2 + (d < 0 ? -1 : 1) * (Math.min(ad, 1) * 262 + Math.max(0, Math.min(ad, 2) - 1) * 170 + Math.max(0, ad - 2) * 60)
            y: 260 - height / 2 + (1 - foc) * 6
            scale: Math.max(0.4, ad <= 1 ? 1 - 0.22 * ad : 0.78 - 0.26 * (ad - 1))
            opacity: Math.max(0, ad <= 1 ? 1 - 0.25 * ad : 0.75 - 0.4 * (ad - 1))
            z: 100 - ad * 10
            visible: ad < 3.2 && desc

            // Perspective turn (design draft: the side card is taller on the side near the centre and shorter outside). Rotation has no perspective, so we build the 4x4 ourselves:
            // rotate by theta around the Y axis, do the perspective divide (view distance D), then move back to the card centre.
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

            // floor reflection: a soft glow in the system colour right below the card (the card is in front of it, only the lower edge shows).
            // The glow always uses a pre-baked PNG (assets/glow/<system key>.png) and is never redrawn with a Canvas at run time -
            // redrawing a Canvas when switching systems was the main cause of stutter.
            Image {
                x: -card.width * 0.10; y: card.height - 30; width: card.width * 1.2; height: 70
                source: card.desc ? "assets/glow/" + card.desc.icon + ".png" : ""
                opacity: 0.35 * (0.4 + card.foc * 0.6)
                smooth: true
            }
            // mirror-glass card (symmetric rounded corners)
            GlassCard { anchors.fill: parent; radius: 36; tint: card.accent; strength: 0.8 + 0.2 * card.foc }

            // system-colour glow behind the icon (replaces the old colour block), light centre slightly above the middle
            Image {
                x: (parent.width - width) / 2; y: 112 - height / 2; width: 230; height: 190
                source: card.desc ? "assets/glow/" + card.desc.icon + ".png" : ""
                smooth: true
            }
            // system line-art icon
            Image {
                x: (parent.width - width) / 2; y: 28 + (168 - height) / 2
                width: 196 * (card.desc && card.desc.iconScale ? card.desc.iconScale : 1) * (card.desc && card.desc.cicon ? 0.62 : 1); height: 168 * (card.desc && card.desc.iconScale ? card.desc.iconScale : 1) * (card.desc && card.desc.cicon ? 0.62 : 1)
                source: card.desc ? "assets/icons/" + (card.desc.cicon || card.desc.icon) + ".png" : ""
                fillMode: Image.PreserveAspectFit
                smooth: true
            }

            // system name (CJK text uses Noto Sans)
            Text {
                x: 8; y: 188; width: parent.width - 16; height: 44
                horizontalAlignment: Text.AlignHCenter; verticalAlignment: Text.AlignVCenter
                text: card.desc ? card.desc.name : ""
                font.family: view.hasCjk(text) ? view.fontUi : view.fontRound
                font.pixelSize: text.length > 14 ? 26 : 32; font.bold: true
                color: "#1F2329"
                elide: Text.ElideRight
            }

            // capsules: centre card = "category · year" on white with system-colour text and glow; side cards = category only on a light system-colour background
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
                Image {   // system-colour glow (stronger on the centre card)
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

            // statistics row (fades in only on the centre card): gamepad games of this system | star favourites | stack total of all systems
            StatsRow {
                x: (parent.width - width) / 2; y: 282
                visible: card.foc > 0.4     // the content is only built for the centre card
                opacity: Math.max(0, card.foc * 1.6 - 0.6)
                games: view.gamesCount; favs: view.favCount; total: view.allCount
                fontFamily: view.fontRound; pixel: 16; ink: "#6F757E"
            }
        }
    }
}
