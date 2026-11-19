// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Equuleus Works
// Top right of the game page: play the video if there is one (it only starts after 0.7 s, to avoid decoding over and over while scrolling fast), otherwise show the screenshot / cover / system icon.
// Videos and screenshots are always stretched to fill the frame (the layout ratio is decided by the system; any other ratio is stretched by force); the game logo is overlaid at the bottom right.
import QtQuick 2.15
import QtMultimedia 5.15

Item {
    id: pane
    property var game: null
    property string fallbackIcon: ""
    property bool showLogo: true       // game logo overlaid at the bottom right (the new game page puts it at the top right of the text area, so it is off here)
    property bool letterbox: false     // widescreen systems (PSP/GC/DC): keep the original ratio, black bars above and below inside the frame
    property bool suspended: false      // a game is about to start: stop the video first, so it does not compete with the game for the hardware decoder / screen

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
    // Re-apply the volume: set 0.59 first and then 0.6 again, which forces the volume value to be sent to the player once more
    Timer { id: kick; interval: 400; onTriggered: { if (pane.playUrl !== "" && !pane.suspended) { pane.vol = 0.59; pane.vol = 0.6; } } }
    Connections { target: video; function onPlaybackStateChanged() { if (video.playbackState === 1) kick.restart(); } }
    onVideoUrlChanged: { video.pause(); playUrl = ""; delay.restart(); }
    Timer { id: delay; interval: 500; onTriggered: { if (!pane.suspended) pane.playUrl = pane.videoUrl; } }
    onSuspendedChanged: { if (suspended) { delay.stop(); video.pause(); playUrl = ""; } else delay.restart(); }

    // Do not keep playing in the background while a game (another app) is in the foreground; start again when back
    Connections {
        target: Qt.application
        function onStateChanged() {
            if (Qt.application.state === Qt.ApplicationActive) { if (!pane.suspended) delay.restart(); }
            else { delay.stop(); video.pause(); pane.playUrl = ""; }
        }
    }

    // Widescreen systems (PSP/GC/DC) only use pure black (black bars inside the frame) when a video/screenshot is really shown; with no video and no screenshot (the system icon is shown),
    // use the same translucent background as the other systems, so that the system pages look consistent
    Rectangle { anchors.fill: parent; color: (pane.letterbox && (pane.shotUrl !== "" || video.visible)) ? "#000000" : Qt.rgba(0, 0, 0, 0.18) }

    // fallback: system icon
    Image {
        anchors.centerIn: parent
        width: parent.width * 0.4; height: parent.height * 0.5
        visible: pane.shotUrl === "" && !video.visible
        source: pane.fallbackIcon
        fillMode: Image.PreserveAspectFit
        opacity: 0.9
        mipmap: true
    }
    // screenshot (shown before the video starts)
    Image {
        anchors.fill: parent
        visible: pane.shotUrl !== ""
        source: pane.shotUrl
        fillMode: pane.letterbox ? Image.PreserveAspectFit : Image.Stretch
        asynchronous: true
    }
    // video
    Video {
        id: video
        anchors.fill: parent
        visible: pane.playUrl !== "" && video.playbackState === 1
        source: pane.playUrl
        autoPlay: true
        loops: MediaPlayer.Infinite
        volume: (pane.suspended || pane.playUrl === "") ? 0 : pane.vol   // when leaving/switching, set the volume to zero first (then pause, and clear source last); after playback starts, kick re-applies the volume once, so that "playing but the volume stuck at 0" does not happen after returning from a game
        fillMode: pane.letterbox ? VideoOutput.PreserveAspectFit : VideoOutput.Stretch
    }
    // game logo
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
