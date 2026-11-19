// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// 上白条右侧：连线状态（有线 / 无线 / 离线）、电量（有电池的机器才显示）、时间。
// 只读 api.device 里两边都可能没有的属性：缺属性时退回「无线图标 + 时间」，原版天马也能用。
import QtQuick 2.15

Row {
    id: area
    property string fontUi: "Noto Sans SC"
    property color ink: "#4A4A52"
    spacing: 12
    layoutDirection: Qt.LeftToRight

    readonly property var dev: (typeof api !== "undefined" && api.device) ? api.device : null
    // DeviceInfo::NetworkType：0 未知 1 离线 2 有线 3 无线 4 行动网络 5 其他；没有这个属性＝-1
    readonly property int net: (dev && dev.networkType !== undefined) ? dev.networkType : -1
    // DeviceInfo::BatteryStatus：0 未知 1 无电池 2 放电 3 充电 4 已充满
    readonly property int batt: (dev && dev.batteryStatus !== undefined) ? dev.batteryStatus : 0
    readonly property bool hasBattery: batt >= 2 && dev.batteryPercent === dev.batteryPercent
    readonly property real pct: hasBattery ? Math.max(0, Math.min(1, dev.batteryPercent)) : 0

    // 连线图示
    Image {
        anchors.verticalCenter: parent.verticalCenter
        width: 22; height: 22
        source: area.net === 2 ? "assets/images/network_ethernet.svg"
              : area.net === 1 ? "assets/images/network_offline.svg"
              :                  "assets/images/network_wifi.svg"
        sourceSize.width: 66; sourceSize.height: 66
    }

    // 电量
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

    // 时间
    Text {
        id: clock
        anchors.verticalCenter: parent.verticalCenter
        font.family: area.fontUi; font.pixelSize: 14; font.bold: true
        color: area.ink
        text: Qt.formatTime(new Date(), "HH:mm")
        Timer { interval: 15000; running: true; repeat: true; onTriggered: clock.text = Qt.formatTime(new Date(), "HH:mm") }
    }
}
