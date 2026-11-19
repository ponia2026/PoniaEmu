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


// The Qt 5 Playlist API, which was removed in Qt 6.
// Played by Qt5Player (Audio/MediaPlayer 5.x) and Qt5Video (Video 5.x).
import QtQml 2.15

QtObject {
    id: root

    enum PlaybackMode {
        CurrentItemOnce,
        CurrentItemInLoop,
        Sequential,
        Loop,
        Random
    }

    // PlaylistItem children
    default property list<QtObject> items
    // sources added with addItem() and friends
    property var extraSources: []

    property int playbackMode: 2 // Sequential
    property int currentIndex: itemCount > 0 ? 0 : -1
    readonly property int itemCount: items.length + extraSources.length
    readonly property url currentItemSource: itemSource(currentIndex)
    readonly property bool readOnly: false
    readonly property int error: 0
    readonly property string errorString: ""

    signal itemAboutToBeInserted(int start, int end)
    signal itemInserted(int start, int end)
    signal itemAboutToBeRemoved(int start, int end)
    signal itemRemoved(int start, int end)
    signal loaded()
    signal loadFailed()

    // shuffled play order, empty means the natural order
    property var _order: []

    function _rawSource(index) {
        if (index < items.length)
            return items[index].source;
        return extraSources[index - items.length];
    }

    function itemSource(index) {
        if (index < 0 || index >= itemCount)
            return "";
        const raw = _order.length === itemCount ? _order[index] : index;
        return _rawSource(raw);
    }

    function addItem(source) {
        const pos = itemCount;
        itemAboutToBeInserted(pos, pos);
        extraSources = extraSources.concat([source]);
        _order = [];
        if (currentIndex < 0)
            currentIndex = 0;
        itemInserted(pos, pos);
        return true;
    }

    function addItems(sources) {
        for (let i = 0; i < sources.length; i++)
            addItem(sources[i]);
        return true;
    }

    function insertItem(index, source) {
        if (index < items.length)
            return addItem(source); // children are fixed, append instead
        const list = extraSources.slice();
        list.splice(index - items.length, 0, source);
        itemAboutToBeInserted(index, index);
        extraSources = list;
        _order = [];
        itemInserted(index, index);
        return true;
    }

    function removeItem(index) {
        if (index < items.length || index >= itemCount)
            return false;
        itemAboutToBeRemoved(index, index);
        const list = extraSources.slice();
        list.splice(index - items.length, 1);
        extraSources = list;
        _order = [];
        if (currentIndex >= itemCount)
            currentIndex = itemCount - 1;
        itemRemoved(index, index);
        return true;
    }

    function clear() {
        const last = itemCount - 1;
        if (last >= 0)
            itemAboutToBeRemoved(0, last);
        extraSources = [];
        _order = [];
        currentIndex = itemCount > 0 ? 0 : -1;
        if (last >= 0)
            itemRemoved(0, last);
        return true;
    }

    function shuffle() {
        const order = [];
        for (let i = 0; i < itemCount; i++)
            order.push(i);
        for (let i = order.length - 1; i > 0; i--) {
            const j = Math.floor(Math.random() * (i + 1));
            const tmp = order[i];
            order[i] = order[j];
            order[j] = tmp;
        }
        _order = order;
    }

    function next() {
        if (itemCount > 0)
            currentIndex = (currentIndex + 1) % itemCount;
    }

    function previous() {
        if (itemCount > 0)
            currentIndex = (currentIndex - 1 + itemCount) % itemCount;
    }

    function nextIndex(steps) {
        return itemCount > 0 ? (currentIndex + (steps || 1)) % itemCount : -1;
    }

    function previousIndex(steps) {
        return itemCount > 0 ? (currentIndex - (steps || 1) % itemCount + itemCount) % itemCount : -1;
    }

    // Called by the player at the end of the current item.
    // Returns true if playback should continue.
    function _advance() {
        if (itemCount === 0)
            return false;

        switch (playbackMode) {
        case 0: // CurrentItemOnce
            return false;
        case 1: // CurrentItemInLoop
            return true;
        case 2: // Sequential
            if (currentIndex + 1 >= itemCount)
                return false;
            currentIndex++;
            return true;
        case 3: // Loop
            next();
            return true;
        case 4: // Random
            currentIndex = Math.floor(Math.random() * itemCount);
            return true;
        }
        return false;
    }
}
