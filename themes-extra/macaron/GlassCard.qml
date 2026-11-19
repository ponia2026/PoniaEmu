// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// 镜面玻璃卡（对照设计稿）：很透的玻璃体（看得到后面的底图）、一圈亮白细边、顶部一道高光、
// 底部淡淡的机种色反光。全部用原生圆角矩形叠出来：四角对称，不用 Canvas，也不用 GraphicalEffects。
import QtQuick 2.15

Item {
    id: glass
    property real radius: 36
    property color tint: "#ffffff"        // 机种色：只在底部反光里淡淡带一点
    property real strength: 1.0           // 1＝正常；侧边卡可略降

    readonly property color tintC: Qt.color(tint)

    // 1) 玻璃体：上亮、中间最透、下缘再亮一点（像玻璃厚边反光）
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
    // 2) 底部机种色反光
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        gradient: Gradient {
            GradientStop { position: 0.55; color: Qt.rgba(glass.tintC.r, glass.tintC.g, glass.tintC.b, 0.0) }
            GradientStop { position: 1.00; color: Qt.rgba(glass.tintC.r, glass.tintC.g, glass.tintC.b, 0.12) }
        }
    }
    // 3) 顶部高光带（贴着上缘，往下很快淡掉）
    Rectangle {
        x: 2; y: 2; width: parent.width - 4; height: parent.height * 0.22
        radius: glass.radius - 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: Qt.rgba(1, 1, 1, 0.55) }
            GradientStop { position: 1.0; color: Qt.rgba(1, 1, 1, 0.0) }
        }
    }
    // 4) 内缘细线（玻璃厚度感）
    Rectangle {
        anchors.fill: parent; anchors.margins: 3
        radius: glass.radius - 3
        color: "transparent"
        border.width: 1; border.color: Qt.rgba(1, 1, 1, 0.35)
    }
    // 5) 外缘亮白描边
    Rectangle {
        anchors.fill: parent
        radius: glass.radius
        color: "transparent"
        border.width: 2; border.color: Qt.rgba(1, 1, 1, 0.92)
    }
}
