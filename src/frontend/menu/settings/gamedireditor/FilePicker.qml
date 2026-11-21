// Pegasus Frontend
// Copyright (C) 2017-2018  Mátyás Mustoha
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

// Ponia: folder picker with multi-select. Only folders are listed; A ticks / unticks a folder, Right opens it,
// Keys: D-pad Right opens the highlighted folder, D-pad Left goes up (same as the ".." entry); Down button on ".." goes up, on an unticked folder ticks it, on a ticked
// folder confirms and adds every ticked folder; Right button unticks (the highlighted folder if it is ticked, otherwise the
// most recently ticked one) and, when nothing is ticked, closes the picker.

import QtQuick 2.8
import Pegasus.FolderListModel 1.0


FocusScope {
    id: root

    signal cancel
    signal pick(var dir_paths)

    anchors.fill: parent

    property var picked: []   // absolute paths ticked so far (kept while browsing into other folders)

    function join(dir, name) {
        return dir.endsWith("/") ? dir + name : dir + "/" + name;
    }
    function isPicked(path) {
        return picked.indexOf(path) >= 0;
    }
    function togglePath(path) {
        var list = picked.slice();
        var i = list.indexOf(path);
        if (i >= 0)
            list.splice(i, 1);
        else
            list.push(path);
        picked = list;
    }
    function undoTick() {
        if (picked.length === 0) {
            root.cancel();
            return;
        }
        var it = list.currentItem;
        if (it && it.ticked)
            togglePath(it.fullPath);     // highlighted folder is ticked: untick that one
        else
            picked = picked.slice(0, picked.length - 1);   // otherwise untick the most recently ticked folder
    }
    function confirm() {
        root.pick(picked.length > 0 ? picked : [folderModel.folder]);
        picked = [];
    }

    Keys.onPressed: (event) => {
        if (event.isAutoRepeat)
            return;
        if (api.keys.isCancel(event)) {   // Right button
            event.accepted = true;
            root.undoTick();
        }
    }


    FolderListModel {
        id: folderModel
        // no collection files are listed: only folders (and the ".." entry) are shown
        files: [ "\u0000" ]
        extensions: [ "\u0000" ]
    }


    Rectangle {
        id: shade
        anchors.fill: parent
        color: "#000"

        opacity: 0.3

        MouseArea {
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: { root.picked = []; root.cancel(); }
        }
    }


    Rectangle {
        height: parent.height * 0.9
        width: height * 1.5
        color: "#444"
        radius: vpx(8)

        anchors.centerIn: parent


        MouseArea {
            anchors.fill: parent
        }

        Text {
            id: info
            text: qsTr("Choose your ROM folders - D-pad Right: open the folder, D-pad Left: go up.\n"
                     + "Down button: tick a folder (on \"..\": go up); press it again on a ticked folder to confirm and add all ticked folders. Right button: untick (when nothing is ticked: close).") + api.tr
            color: "#ee4"
            font.family: globalFonts.sans
            font.pixelSize: vpx(18)
            lineHeight: 1.15

            anchors.top: parent.top
            width: parent.width
            padding: font.pixelSize

            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignVCenter
            wrapMode: Text.WordWrap
        }
        Item {
            anchors.top: info.bottom
            anchors.bottom: parent.bottom
            width: parent.width - vpx(30)
            anchors.bottomMargin: vpx(15)
            anchors.horizontalCenter: parent.horizontalCenter

            Rectangle {
                id: path
                width: parent.width
                height: pathText.height
                color: "#222"

                Text {
                    id: pathText
                    text: folderModel.folder
                    width: parent.width - countText.width

                    color: "#bbb"
                    font.family: globalFonts.sans
                    font.pixelSize: vpx(18)

                    lineHeight: 2.5
                    verticalAlignment: Text.AlignVCenter
                    leftPadding: font.pixelSize; rightPadding: leftPadding
                    elide: Text.ElideMiddle
                }
                Text {
                    id: countText
                    visible: root.picked.length > 0
                    text: qsTr("%1 selected").arg(root.picked.length) + api.tr
                    anchors.right: parent.right
                    height: parent.height

                    color: "#6d6"
                    font.bold: true
                    font.family: globalFonts.sans
                    font.pixelSize: vpx(18)
                    verticalAlignment: Text.AlignVCenter
                    rightPadding: font.pixelSize
                    width: visible ? implicitWidth + rightPadding : 0
                }
            }
            Rectangle {
                width: parent.width
                anchors.top: path.bottom
                anchors.bottom: parent.bottom
                color: "#333"

                ListView {
                    id: list
                    anchors.fill: parent
                    clip: true

                    model: folderModel
                    delegate: fileDelegate

                    focus: true
                    highlightRangeMode: ListView.ApplyRange
                    preferredHighlightBegin: height * 0.5 - vpx(18) * 1.25
                    preferredHighlightEnd: height * 0.5 + vpx(18) * 1.25
                    highlightMoveDuration: 0

                    // The folder list is filled after the dialog opens, and the list then has no current item, so no cursor is shown
                    // until a key is pressed. Put the cursor on the first real folder (the row after "..") as soon as there is one.
                    Connections {
                        target: folderModel
                        function onFolderChanged() { list.currentIndex = -1; }
                        function onCountChanged() {
                            if (list.currentIndex < 0 && folderModel.count > 0)
                                list.currentIndex = folderModel.count > 1 ? 1 : 0;
                        }
                    }
                }
            }
        }
    }

    Component {
        id: fileDelegate

        Rectangle {
            readonly property bool highlighted: ListView.isCurrentItem || mouseArea.containsMouse
            readonly property bool isParent: name === ".."
            readonly property string fullPath: isParent ? "" : root.join(folderModel.folder, name)
            readonly property bool ticked: fullPath !== "" && root.isPicked(fullPath)

            readonly property real rowH: Math.round(vpx(18) * 2.5 * 4 / 3)   // fixed: children size themselves from this, not from label.height (circular on the first row)

            color: highlighted ? "#566" : "transparent"
            width: ListView.view.width
            height: rowH

            Keys.onPressed: (event) => {
                if (event.isAutoRepeat)
                    return;

                if (api.keys.isAccept(event)) {
                    event.accepted = true;
                    if (isParent)
                        folderModel.cd("..");
                    else if (ticked)
                        root.confirm();   // second press on a ticked folder = done: add everything ticked
                    else
                        root.togglePath(fullPath);
                }
                else if (api.keys.isRight(event)) {
                    event.accepted = true;
                    if (!isParent)
                        folderModel.cd(name);
                }
                else if (api.keys.isLeft(event)) {   // D-pad Left: up one level, same as the ".." entry
                    event.accepted = true;
                    folderModel.cd("..");
                }
            }


            // tick box (not shown for the ".." entry)
            Rectangle {
                id: box
                visible: !isParent
                width: parent.height * 0.6
                height: width
                radius: width * 0.2

                anchors.left: parent.left
                anchors.leftMargin: parent.height * 0.4
                anchors.verticalCenter: parent.verticalCenter

                color: ticked ? "#4c5" : "transparent"
                border.width: 2
                border.color: ticked ? "#4c5" : "#aaa"

                Text {
                    anchors.centerIn: parent
                    visible: ticked
                    text: "■"   // filled square: the check mark U+2713 is not in the bundled font
                    color: "#fff"
                    font.bold: true
                    font.pixelSize: parent.height * 0.8
                }
            }

            Image {
                id: icon
                source: "qrc:/frontend/assets/dir-icon.png"

                fillMode: Image.PreserveAspectFit
                height: parent.height * 0.75
                width: height

                anchors.left: parent.left
                anchors.leftMargin: parent.height * 1.2
                anchors.top: parent.top
                anchors.topMargin: parent.height * 0.08
            }

            Text {
                id: label
                text: name
                height: parent.rowH
                verticalAlignment: Text.AlignVCenter
                lineHeight: 2.5

                color: ticked ? "#9f9" : "#eee"
                font.bold: ticked
                font.family: globalFonts.sans
                font.pixelSize: vpx(18)

                anchors.left: icon.right
                anchors.leftMargin: icon.height * 0.3
                anchors.right: parent.right
                elide: Text.ElideRight
            }

            MouseArea {
                id: mouseArea
                anchors.fill: parent
                hoverEnabled: true
                onClicked: {
                    list.currentIndex = index;
                    if (isParent)
                        folderModel.cd("..");
                    else
                        root.togglePath(fullPath);
                }
                onDoubleClicked: {
                    if (!isParent)
                        folderModel.cd(name);
                }
            }
        }
    }
}
