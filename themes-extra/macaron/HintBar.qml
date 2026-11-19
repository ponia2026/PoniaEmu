// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// 下端操作提示：一整块米色圆角矩形，里面横排「图示 + 文字」
// icon 可为：dpad_lr / dpad_ud（十字键，左右或上下塗黑）、dot_up/right/down/left（四点键）、l1r1（两个肩键小方块）
import QtQuick 2.15

Rectangle {
    id: bar
    property var hints: []            // [{ icon: "dpad_lr", label: "..." }, ...]
    property string fontUi: "Noto Sans TC"

    height: 38
    radius: 20
    color: "transparent"
    width: row.width + 52

    Row {
        id: row
        anchors.centerIn: parent
        spacing: hints.length > 5 ? 20 : 26
        Repeater {
            model: bar.hints
            delegate: Row {
                spacing: 4
                anchors.verticalCenter: parent.verticalCenter

                // 肩键
                Row {
                    visible: modelData.icon === "l1r1"
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: 3
                    Repeater {
                        model: ["L1", "R1"]
                        delegate: Rectangle {
                            width: 24; height: 17
                            radius: 5
                            color: "#ffffff"
                            border.width: 1.3; border.color: "#565C65"
                            Text {
                                anchors.centerIn: parent
                                text: modelData
                                font.family: bar.fontUi; font.pixelSize: 10; font.bold: true
                                color: "#565C65"
                            }
                        }
                    }
                }
                // 其余图示
                Image {
                    visible: modelData.icon !== "l1r1"
                    anchors.verticalCenter: parent.verticalCenter
                    width: modelData.icon.indexOf("dpad") === 0 ? 20.6 : 20
                    height: width
                    sourceSize.width: 48; sourceSize.height: 48
                    source: modelData.icon !== "l1r1" ? "assets/images/" + modelData.icon + ".svg" : ""
                }
                Text {
                    anchors.verticalCenter: parent.verticalCenter
                    text: modelData.label
                    font.family: bar.fontUi; font.pixelSize: 14
                    color: "#3A3F44"
                }
            }
        }
    }
}
