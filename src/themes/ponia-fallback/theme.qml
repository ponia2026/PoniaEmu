// PoniaFE 内置兜底主题：没有任何主题可用时显示说明，避免界面空白
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
            text: "PoniaFE"
        }
        Text {
            anchors.horizontalCenter: parent.horizontalCenter
            color: "#cccccc"
            font.pixelSize: parent.parent.height * 0.035
            text: qsTr("No theme installed. Put a theme into the `themes` folder.")
        }
    }
}
