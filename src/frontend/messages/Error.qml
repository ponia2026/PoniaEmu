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


import QtQuick 2.0


Rectangle {
    anchors.fill: parent
    color: "#000"

    property alias title: txtTitle.text
    property string details: ""   // translations wrap the URL in <i>…</i> (some leave it unclosed and italicise the rest): drop italics, one style for the whole page
    property alias instruction: txtInstruction.text
    // translations carry "<pre>path</pre>" (monospace): show the path in the same font as the rest of the page instead
    property string logInfo: ""

    readonly property int titleTextSize: vpx(50)
    readonly property int detailsTextSize: vpx(20)       // one size for every paragraph (used to be 18 / 20 / 18)
    readonly property int instructionTextSize: detailsTextSize

    readonly property color titleColor: "#eee"
    readonly property color detailsColor: "#ccc"
    readonly property color instructionColor: "#fb5"


    Column {
        spacing: detailsTextSize * 1.5

        anchors {
            left: parent.left; leftMargin: vpx(50)
            right: parent.horizontalCenter
            verticalCenter: parent.verticalCenter
        }


        Text {
            id: txtTitle

            color: titleColor
            font.bold: true
            font.pixelSize: titleTextSize
            font.family: globalFonts.sans
        }

        Text {
            id: txtDescription

            text: details.replace(/<\/?i>|<\/>/g, "")   // "</>" = a translation's malformed closing tag
            textFormat: Text.RichText                   // keep <br> working now that the <i> tags are gone (auto-detect would fall back to plain text)
            width: parent.width
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft   // justified text stretched the gaps (worst with CJK)

            color: detailsColor
            font.pixelSize: detailsTextSize
            font.family: globalFonts.sans
        }

        Text {
            id: txtInstruction

            width: parent.width
            wrapMode: Text.WordWrap
            horizontalAlignment: Text.AlignLeft

            color: instructionColor
            font.pixelSize: instructionTextSize
            font.family: globalFonts.sans
        }

        Text {
            id: txtLogInfo

            text: logInfo.replace(/<pre>/g, "<br>").replace(/<\/pre>/g, "")
            textFormat: Text.RichText
            width: parent.width
            wrapMode: Text.WrapAtWordBoundaryOrAnywhere   // the path has no spaces
            horizontalAlignment: Text.AlignLeft

            color: detailsColor
            font.pixelSize: detailsTextSize
            font.family: globalFonts.sans
        }
    }
}
