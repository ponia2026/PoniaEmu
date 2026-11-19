// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Right side of the top white bar: connection state (wired / wireless / offline), battery (only on machines with a battery), time.
// Only reads properties of api.device that may be missing on either side: when a property is missing it falls back to "wireless icon + time", so the original Pegasus works too.
import QtQuick 2.15

Row {
    id: area
    property string fontUi: "Noto Sans CJK TC"
    property color ink: "#4A4A52"
    spacing: 12
    layoutDirection: Qt.LeftToRight

    readonly property var dev: (typeof api !== "undefined" && api.device) ? api.device : null
    // DeviceInfo::NetworkType: 0 unknown 1 offline 2 wired 3 wireless 4 mobile 5 other; -1 when the property is missing
    readonly property int net: (dev && dev.networkType !== undefined) ? dev.networkType : -1
    // DeviceInfo::BatteryStatus: 0 unknown 1 no battery 2 discharging 3 charging 4 full
    readonly property int batt: (dev && dev.batteryStatus !== undefined) ? dev.batteryStatus : 0
    readonly property bool hasBattery: batt >= 2 && dev.batteryPercent === dev.batteryPercent
    readonly property real pct: hasBattery ? Math.max(0, Math.min(1, dev.batteryPercent)) : 0

    // connection icon
    Image {
        anchors.verticalCenter: parent.verticalCenter
        width: 22; height: 22
        source: area.net === 2 ? "assets/images/network_ethernet.svg"
              : area.net === 1 ? "assets/images/network_offline.svg"
              :                  "assets/images/network_wifi.svg"
        sourceSize.width: 66; sourceSize.height: 66
    }

    // battery
    Row {
        visible: area.hasBattery
        anchors.verticalCenter: parent.verticalCenter
        spacing: 5
        Item {
            width: 26; height: 14
            anchors.verticalCenter: parent.verticalCenter
            Rectangle {
                x: 0; y: 0; width: 23; height: 14; radius: 3
                color: "transparent"; border.width: 1.6; border.color: area.ink
                Rectangle {
                    x: 2.5; y: 2.5; height: parent.height - 5; radius: 1.5
                    width: Math.max(2, (parent.width - 5) * area.pct)
                    color: (area.batt === 3 || area.batt === 4) ? "#3FA34D" : (area.pct < 0.2 ? "#C0392B" : area.ink)
                }
            }
            Rectangle { x: 23.5; y: 4; width: 2.5; height: 6; radius: 1; color: area.ink }
        }
        Text {
            anchors.verticalCenter: parent.verticalCenter
            text: Math.round(area.pct * 100) + "%"
            font.family: area.fontUi; font.pixelSize: 12
            color: area.ink
        }
    }

    // time
    Text {
        id: clock
        anchors.verticalCenter: parent.verticalCenter
        font.family: area.fontUi; font.pixelSize: 14; font.bold: true
        color: area.ink
        text: Qt.formatTime(new Date(), "HH:mm")
        Timer { interval: 15000; running: true; repeat: true; onTriggered: clock.text = Qt.formatTime(new Date(), "HH:mm") }
    }
}
