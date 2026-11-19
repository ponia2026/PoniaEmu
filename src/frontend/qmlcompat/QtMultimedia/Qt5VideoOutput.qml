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


// The Qt 5 VideoOutput API. In Qt 5 the output pointed to the player
// (`source: player`), in Qt 6 the player points to the output.
import QtMultimedia 6.0 as Q6

Q6.VideoOutput {
    id: root

    // same values as in Qt 5 and 6
    enum FillMode {
        Stretch,
        PreserveAspectFit,
        PreserveAspectCrop
    }
    // no equivalent in Qt 6
    enum FlushMode {
        EmptyFrame,
        FirstFrame,
        LastFrame
    }

    property var source: null
    property int flushMode: 0
    property bool autoOrientation: false

    onSourceChanged: {
        if (source && source.videoOutput !== undefined)
            source.videoOutput = root;
    }
}
