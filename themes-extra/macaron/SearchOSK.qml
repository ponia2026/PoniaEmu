// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// 搜寻用画面键盘（A–Z、0–9、空白、退格、清除、完成）。手把用方向键 + 确认；实体键盘直接打字（由 theme.qml 转交）。
import QtQuick 2.15

Item {
    id: osk
    width: 960
    height: 540
    visible: false

    property string query: ""
    property string title: ""
    property string hintText: ""
    property color ink: "#8A2F33"
    property color accent: "#EE8D87"
    property string fontUi: "Noto Sans TC"
    property string okText: "OK"
    property string clearText: "Clear"
    property string spaceText: "Space"

    signal textEdited(string text)    // 内容变动（即时过滤）
    signal closed()

    readonly property var rows: [
        ["A","B","C","D","E","F","G","H","I","J"],
        ["K","L","M","N","O","P","Q","R","S","T"],
        ["U","V","W","X","Y","Z","0","1","2","3"],
        ["4","5","6","7","8","9","-","'","&","."],
        ["@space","@back","@clear","@ok"]
    ]
    property int cr: 0
    property int cc: 0

    function open(text) { query = text || ""; cr = 0; cc = 0; visible = true; }
    function close() { visible = false; closed(); }
    function _set(t) { query = t; textEdited(t); }
    function typeChar(ch) { _set(query + ch); }
    function backspace() { if (query.length > 0) _set(query.substring(0, query.length - 1)); }
    function press() {
        var k = rows[cr][cc];
        if (k === "@space") typeChar(" ");
        else if (k === "@back") backspace();
        else if (k === "@clear") _set("");
        else if (k === "@ok") close();
        else typeChar(k);
    }

    // 返回 true = 已处理
    function handleKey(event, keys) {
        if (!visible) return false;
        if (event.key === Qt.Key_Left) { cc = (cc + rows[cr].length - 1) % rows[cr].length; return true; }
        if (event.key === Qt.Key_Right) { cc = (cc + 1) % rows[cr].length; return true; }
        if (event.key === Qt.Key_Up) { cr = (cr + rows.length - 1) % rows.length; cc = Math.min(cc, rows[cr].length - 1); return true; }
        if (event.key === Qt.Key_Down) { cr = (cr + 1) % rows.length; cc = Math.min(cc, rows[cr].length - 1); return true; }
        if (event.isAutoRepeat) return true;
        // 实体键盘的退格键同时也是「取消」键之一：在这里优先当退格；手把 B 键送来的是 Esc，仍然会关闭键盘
        if (event.key === Qt.Key_Backspace) { backspace(); return true; }
        if (keys.isAccept(event)) { press(); return true; }
        if (keys.isCancel(event)) { close(); return true; }
        if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) { close(); return true; }
        if (event.text && event.text.length === 1 && event.text >= " " && event.key !== Qt.Key_Escape) { typeChar(event.text); return true; }
        return true;      // 打开时吞掉其余按键，避免操作到后面的画面
    }

    Rectangle { anchors.fill: parent; color: Qt.rgba(0, 0, 0, 0.45) }

    Rectangle {
        id: panel
        width: 560; height: 352
        anchors.centerIn: parent
        radius: 22
        color: "#FCF8F1"

        Text {
            x: 24; y: 14; width: parent.width - 48
            text: osk.title
            font.family: osk.fontUi; font.pixelSize: 14; font.bold: true
            color: "#5A3A2A"
        }
        Rectangle {
            x: 24; y: 40; width: parent.width - 48; height: 40; radius: 12
            color: "#ffffff"
            border.width: 2; border.color: osk.accent
            Text {
                anchors.verticalCenter: parent.verticalCenter
                x: 12; width: parent.width - 24
                text: osk.query.length > 0 ? osk.query : osk.hintText
                elide: Text.ElideLeft
                font.family: osk.fontUi; font.pixelSize: 18
                color: osk.query.length > 0 ? osk.ink : "#b9a89a"
            }
        }

        Column {
            x: 24; y: 94
            spacing: 6
            Repeater {
                model: osk.rows
                delegate: Row {
                    readonly property int r: index
                    spacing: 6
                    Repeater {
                        model: modelData
                        delegate: Rectangle {
                            readonly property bool special: modelData.charAt(0) === "@"
                            readonly property bool sel: osk.cr === r && osk.cc === index
                            width: special ? 122 : 45
                            height: 42
                            radius: 10
                            color: sel ? osk.ink : "#F4E8CF"
                            Text {
                                anchors.centerIn: parent
                                text: !special ? modelData
                                    : modelData === "@space" ? osk.spaceText
                                    : modelData === "@back" ? "←"
                                    : modelData === "@clear" ? osk.clearText
                                    : osk.okText
                                font.family: osk.fontUi; font.pixelSize: special ? 15 : 18; font.bold: true
                                color: sel ? "#ffffff" : "#5A3A2A"
                            }
                        }
                    }
                }
            }
        }
    }
}
