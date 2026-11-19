// PoniaEmu built-in fallback theme: shows a hint instead of an empty screen when no theme is available
import QtQuick 2.0

FocusScope {
    focus: true
    Rectangle {
        anchors.fill: parent
        color: "#202020"
    }
    Column {
        anchors.centerIn: parent
        spacing: parent.height * 0.03
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "white"
            font.pixelSize: parent.parent.height * 0.06
            text: "PoniaEmu"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#cccccc"
            font.pixelSize: parent.parent.height * 0.035
            text: qsTr("No theme installed. Put a theme into the `themes` folder.")
        }
    }
}
