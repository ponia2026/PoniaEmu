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


// The Qt 5 Audio/MediaPlayer API (they were the same type in Qt 5),
// implemented with the Qt 6 MediaPlayer and AudioOutput.
import QtQml 2.15
import QtMultimedia 6.0 as Q6

QtObject {
    id: root

    enum PlaybackState {
        StoppedState,
        PlayingState,
        PausedState
    }
    // NOTE: QML enums can't be negative (Qt 5 used -1), any negative
    // `loops` value is treated as infinite as well
    enum Loops {
        Infinite = 2147483647
    }
    enum Status {
        UnknownStatus,
        NoMedia,
        Loading,
        Loaded,
        Stalled,
        Buffering,
        Buffered,
        EndOfMedia,
        InvalidMedia
    }
    enum Error {
        NoError,
        ResourceError,
        FormatError,
        NetworkError,
        AccessDenied,
        ServiceMissing
    }
    enum AudioRole {
        UnknownRole,
        AccessibilityRole,
        AlarmRole,
        GameRole,
        MusicRole,
        NotificationRole,
        RingtoneRole,
        SonificationRole,
        VideoRole,
        VoiceCommunicationRole
    }

    property url source
    property var playlist: null
    property bool autoPlay: false
    property bool autoLoad: true
    property int loops: 1
    property real volume: 1.0
    property bool muted: false
    property real playbackRate: 1.0
    property int audioRole: 0 // no effect in Qt 6
    property string customAudioRole
    property int notifyInterval: 1000
    // set by Qt5Video
    property var videoOutput: null

    readonly property int playbackState: player.playbackState // same values in Qt 5 and 6
    readonly property int status: player.mediaStatus + 1 // Qt 5 had UnknownStatus first
    readonly property int position: player.position
    readonly property int duration: player.duration
    readonly property bool hasAudio: player.hasAudio
    readonly property bool hasVideo: player.hasVideo
    readonly property bool seekable: player.seekable
    readonly property real bufferProgress: player.bufferProgress
    readonly property int error: player.error
    readonly property string errorString: player.errorString
    readonly property var metaData: player.metaData
    readonly property bool availability: true

    signal playing()
    signal paused()
    signal stopped()

    function play() {
        player.play();
    }
    function pause() {
        player.pause();
    }
    function stop() {
        player.stop();
    }
    function seek(offset) {
        player.position = offset;
    }

    property Q6.AudioOutput _output: Q6.AudioOutput {
        volume: root.volume
        muted: root.muted
    }

    property Q6.MediaPlayer _player: Q6.MediaPlayer {
        id: player

        audioOutput: root._output
        videoOutput: root.videoOutput
        source: root.playlist ? root.playlist.currentItemSource : root.source
        // with a playlist, the playback mode of the playlist decides what happens at the end
        loops: {
            if (root.playlist)
                return 1;
            if (root.loops < 0 || root.loops === 2147483647)
                return Q6.MediaPlayer.Infinite;
            return root.loops;
        }
        playbackRate: root.playbackRate

        onSourceChanged: {
            if (root.autoPlay && source.toString() !== "")
                play();
        }

        onPlaybackStateChanged: {
            switch (playbackState) {
            case Q6.MediaPlayer.PlayingState: root.playing(); break;
            case Q6.MediaPlayer.PausedState: root.paused(); break;
            case Q6.MediaPlayer.StoppedState: root.stopped(); break;
            }
        }

        onMediaStatusChanged: {
            if (mediaStatus === Q6.MediaPlayer.EndOfMedia && root.playlist && root.playlist._advance())
                play();
        }
    }

    Component.onCompleted: {
        if (autoPlay && player.source.toString() !== "")
            player.play();
    }
}
