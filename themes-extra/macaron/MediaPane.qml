// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// 游戏页右上：有影片就播影片（停留 0.7 秒才开始，避免快速滚动时反复解码），没有就显示截图 / 封面 / 机种图标。
// 影片、截图一律拉伸填满框（版面比例由机种决定，混入别的比例就强制拉伸）；游戏 logo 叠在右下角。
import QtQuick 2.15
import QtMultimedia 5.15

Item {
    id: pane
    property var game: null
    property string fallbackIcon: ""
    property bool showLogo: true       // 游戏 logo 叠在右下角（新版游戏页改放文字区右上，这里关掉）
    property bool letterbox: false     // 宽屏机种(PSP/GC/DC)：保持原比例、框内上下留黑
    property bool suspended: false      // 要启动游戏了：先停掉影片，别跟游戏抢硬件解码器 / 画面

    readonly property var assets: game ? game.assets : null
    readonly property string videoUrl: (assets && assets.videos && assets.videos.length > 0) ? assets.videos[0].toString() : ""
    readonly property string shotUrl: {
        if (!assets) return "";
        if (assets.screenshots && assets.screenshots.length > 0) return assets.screenshots[0].toString();
        if (assets.titlescreen) return assets.titlescreen.toString();
        if (assets.background) return assets.background.toString();
        return "";
    }
    readonly property string boxUrl: assets && assets.boxFront ? assets.boxFront.toString() : ""
    readonly property string logoUrl: assets && assets.logo ? assets.logo.toString() : ""

    clip: true

    property string playUrl: ""
    property real vol: 0.6
    // 重新套用音量：先改成 0.59 再改回 0.6，强制把音量值再送一次给播放器
    Timer { id: kick; interval: 400; onTriggered: { if (pane.playUrl !== "" && !pane.suspended) { pane.vol = 0.59; pane.vol = 0.6; } } }
    Connections { target: video; function onPlaybackStateChanged() { if (video.playbackState === 1) kick.restart(); } }
    onVideoUrlChanged: { video.pause(); playUrl = ""; delay.restart(); }
    Timer { id: delay; interval: 500; onTriggered: { if (!pane.suspended) pane.playUrl = pane.videoUrl; } }
    onSuspendedChanged: { if (suspended) { delay.stop(); video.pause(); playUrl = ""; } else delay.restart(); }

    // 游戏（别的 App）在前景时不要在背景继续播放；回来后再开始
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) { if (!pane.suspended) delay.restart(); }
            else { delay.stop(); video.pause(); pane.playUrl = ""; }
        }
    }

    // 宽屏机种(PSP/GC/DC)只有在真的显示影片/截图时才用纯黑（框内上下黑边）；没有影片也没有截图（显示机种图标）时，
    // 和其他机种一样用半透明底色，各机种页看起来一致
    Rectangle { anchors.fill: parent; color: (pane.letterbox && (pane.shotUrl !== "" || video.visible)) ? "#000000" : Qt.rgba(0, 0, 0, 0.18) }

    // 兜底：机种图标
    Image {
        anchors.centerIn: parent
        width: parent.width * 0.4; height: parent.height * 0.5
        visible: pane.shotUrl === "" && !video.visible
        source: pane.fallbackIcon
        fillMode: Image.PreserveAspectFit
        opacity: 0.9
        mipmap: true
    }
    // 截图（影片开始前先显示）
    Image {
        anchors.fill: parent
        visible: pane.shotUrl !== ""
        source: pane.shotUrl
        fillMode: pane.letterbox ? Image.PreserveAspectFit : Image.Stretch
        asynchronous: true
    }
    // 影片
    Video {
        id: video
        anchors.fill: parent
        visible: pane.playUrl !== "" && video.playbackState === 1
        source: pane.playUrl
        autoPlay: true
        loops: MediaPlayer.Infinite
        volume: (pane.suspended || pane.playUrl === "") ? 0 : pane.vol   // 离开/切换时先把音量归零（再暂停、最后才清 source）；开始播放后 kick 再重新套用一次音量，避免从游戏返回后「在播但音量卡在 0」
        fillMode: pane.letterbox ? VideoOutput.PreserveAspectFit : VideoOutput.Stretch
    }
    // 游戏 logo
    Image {
        anchors.right: parent.right; anchors.rightMargin: 8
        anchors.bottom: parent.bottom; anchors.bottomMargin: 4
        width: Math.min(150, parent.width * 0.4); height: width * 0.45
        visible: pane.showLogo && pane.logoUrl !== ""
        source: pane.logoUrl
        fillMode: Image.PreserveAspectFit
        horizontalAlignment: Image.AlignRight
        verticalAlignment: Image.AlignBottom
        asynchronous: true
    }
}
