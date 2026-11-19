// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Mirror-glass card (after the design draft): a very transparent glass body (the background shows through), a thin bright white rim, a highlight band at the top,
// and a faint system-colour reflection at the bottom. Built only from native rounded rectangles: symmetric corners, no Canvas, no GraphicalEffects.
import QtQuick 2.15

Item {
    id: glass
    property real radius: 36
    property color tint: "#ffffff"        // system colour: only a faint trace in the bottom reflection
    property real strength: 1.0           // 1 = normal; side cards may be a little lower

    readonly property color tintC: Qt.color(tint)

    // 1) glass body: bright at the top, most transparent in the middle, a bit brighter again at the bottom edge (like the reflection on the thick edge of glass)
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.00; color: Qt.rgba(1, 1, 1, 0.58 * glass.strength) }
            GradientStop { position: 0.35; color: Qt.rgba(1, 1, 1, 0.30 * glass.strength) }
            GradientStop { position: 0.80; color: Qt.rgba(1, 1, 1, 0.26 * glass.strength) }
            GradientStop { position: 1.00; color: Qt.rgba(1, 1, 1, 0.42 * glass.strength) }
        }
    }
    // 2) system-colour reflection at the bottom
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.55; color: Qt.rgba(glass.tintC.r, glass.tintC.g, glass.tintC.b, 0.0) }
            GradientStop { position: 1.00; color: Qt.rgba(glass.tintC.r, glass.tintC.g, glass.tintC.b, 0.12) }
        }
    }
    // 3) highlight band at the top (along the top edge, fading out quickly downward)
    Rectangle {
        x: 2; y: 2; width: parent.width - 4; height: parent.height * 0.22
        radius: glass.radius - 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.55) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }
    // 4) thin inner edge line (gives a sense of glass thickness)
    Rectangle {
        anchors.fill: parent; anchors.margins: 3
        radius: glass.radius - 3
        color: "transparent"
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.35)
    }
    // 5) bright white outline
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: 2; border.color: Qt.rgba(1, 1, 1, 0.92)
    }
}
