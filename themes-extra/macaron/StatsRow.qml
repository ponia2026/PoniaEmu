// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Statistics row (no frame, no text labels): [gamepad] games of this system | [star] favourites of this system | [stack] games of all systems
// Numbers always use half-width digits (no thousands separator); icons are PNG.
import QtQuick 2.15

Row {
    id: sr
    property int games: 0
    property int favs: 0
    property int total: 0
    property string fontFamily: ""
    property real pixel: 18
    property color ink: "#8A8F98"
    property real iconH: pixel * 1.0

    spacing: pixel * 0.6

    component Stat: Row {
        property string icon: ""
        property int value: 0
        spacing: sr.pixel * 0.3
        Image {
            anchors.verticalCenter: parent.verticalCenter
            source: parent.icon
            sourceSize.width: 64; sourceSize.height: 64
            height: sr.iconH; width: sr.iconH * 1.15
            fillMode: Image.PreserveAspectFit
            smooth: true; mipmap: true
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: String(parent.value)
            font.family: sr.fontFamily; font.pixelSize: sr.pixel; font.weight: Font.Medium
            color: sr.ink
        }
    }
    component Sep: Rectangle {
        anchors.verticalCenter: parent.verticalCenter
        width: 1; height: sr.pixel * 0.9; color: Qt.rgba(0, 0, 0, 0.14)
    }

    Stat { icon: "assets/icons/ic_gamepad.png"; value: sr.games }
    Sep {}
    Stat { icon: "assets/icons/ic_star.png"; value: sr.favs }
    Sep {}
    Stat { icon: "assets/icons/ic_layers_total.png"; value: sr.total }
}
