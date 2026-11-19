// Pegasus Frontend
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


// The Qt 5 Video API (including playlist support), implemented with
// the Qt 6 VideoOutput and the Qt 5 style player of this module.
import QtQuick 2.15
import QtMultimedia 6.0 as Q6

Item {
    id: root

    enum PlaybackState {
        StoppedState,
        PlayingState,
        PausedState
    }

    property alias source: core.source
    property alias playlist: core.playlist
    property alias autoPlay: core.autoPlay
    property alias autoLoad: core.autoLoad
    property alias loops: core.loops
    property alias volume: core.volume
    property alias muted: core.muted
    property alias playbackRate: core.playbackRate
    property alias notifyInterval: core.notifyInterval
    property alias fillMode: output.fillMode
    property alias orientation: output.orientation
    property int flushMode: 0 // no equivalent in Qt 6

    readonly property alias playbackState: core.playbackState
    readonly property alias status: core.status
    readonly property alias position: core.position
    readonly property alias duration: core.duration
    readonly property alias hasAudio: core.hasAudio
    readonly property alias hasVideo: core.hasVideo
    readonly property alias seekable: core.seekable
    readonly property alias bufferProgress: core.bufferProgress
    readonly property alias error: core.error
    readonly property alias errorString: core.errorString
    readonly property alias metaData: core.metaData
    readonly property alias contentRect: output.contentRect
    readonly property alias sourceRect: output.sourceRect

    signal playing()
    signal paused()
    signal stopped()

    function play() { core.play(); }
    function pause() { core.pause(); }
    function stop() { core.stop(); }
    function seek(offset) { core.seek(offset); }

    implicitWidth: output.implicitWidth
    implicitHeight: output.implicitHeight

    Q6.VideoOutput {
        id: output
        anchors.fill: parent
    }

    Qt5Player {
        id: core
        videoOutput: output

        onPlaying: root.playing()
        onPaused: root.paused()
        onStopped: root.stopped()
    }
}
