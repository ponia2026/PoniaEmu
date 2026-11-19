// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Small tag of a search condition: magnifier icon (drawn by ourselves, independent of font glyphs) + keyword
import QtQuick 2.15

Rectangle {
    id: chip
    property string text: ""
    property color ink: "#8A2F33"
    property string fontUi: "Noto Sans CJK TC"
    height: 26; radius: 13
    width: row.width + 22
    color: Qt.rgba(1, 1, 1, 0.92)

    Row {
        id: row
        anchors.centerIn: parent
        spacing: 6
        Item {
            width: 12; height: 12
            anchors.verticalCenter: parent.verticalCenter
            Rectangle { x: 0; y: 0; width: 8; height: 8; radius: 4; color: "transparent"; border.width: 1.6; border.color: chip.ink }
            Rectangle { x: 7; y: 8; width: 5.5; height: 1.8; radius: 1; color: chip.ink; rotation: 45; transformOrigin: Item.Left }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: chip.text
            font.family: chip.fontUi; font.pixelSize: 13; font.bold: true
            color: chip.ink
        }
    }
}
