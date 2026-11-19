# Qt 5 theme compatibility modules

The PoniaFE frontend is based on Qt 6, but the existing Pegasus themes (including the Pegasus G themes) were all written for Qt 5.
The modules here are packed into `qrc:/qmlcompat` and put first in the QML import path,
so that these themes load under Qt 6 **without any change**.

## QtGraphicalEffects

Qt 6 renamed it to `Qt5Compat.GraphicalEffects`. The `qmldir` has a single `import` line that
forwards the old module name to the new one.

Do not write it as `import Qt5Compat.GraphicalEffects auto`: under Qt 6.4 the second
file that imports it again reports `version 1.0 is not installed`.

## QtMultimedia

For `import QtMultimedia 5.x`, the QtMultimedia of Qt 6 only exports `Video`;
`SoundEffect`, `MediaPlayer` and `VideoOutput` only exist as version 6.0; `Audio`,
`Playlist` and `PlaylistItem` have been removed.

A replacement `qmldir` takes the place of the system QtMultimedia:

- It still loads the real system plugin `quickmultimediaplugin`, so the Qt 6 types
  (`import QtMultimedia` or `6.x`) are not affected at all; the directory of the plugin is added by
  `FrontendLayer` through `addPluginPath()`
- `Video 6.0` points to the original `Video.qml` in the resources of the plugin
- It also provides the Qt 5 API as version 5.0, implemented with the `MediaPlayer` + `AudioOutput` of Qt 6:

| Qt 5 type | Implementation | Notes |
|---|---|---|
| `Audio`, `MediaPlayer` | `Qt5Player.qml` | In Qt 5 these are the same type; supports `playlist`, `autoPlay`, `loops` and more |
| `Playlist`, `PlaylistItem` | `Playlist.qml`, `PlaylistItem.qml` | Supports play modes such as Sequential/Loop/Random and `shuffle()` |
| `Video` | `Qt5Video.qml` | Adds `playlist`, `autoLoad` and more back on top of the Qt 6 `VideoOutput` |
| `VideoOutput` | `Qt5VideoOutput.qml` | Qt 5 writes `source: player`; here it is the other way round, the output of the player is set |
| `SoundEffect` | `Qt5SoundEffect.qml` | The API did not change, only version 5.0 is added |

Known differences:

- QML enums cannot be negative, so `MediaPlayer.Infinite`/`Audio.Infinite` use a large number instead,
  and a negative `loops` is always treated as an endless loop
- `audioRole` and `flushMode` have no counterpart in Qt 6; setting them has no effect

## Not covered

- `ShaderEffect` with inline GLSL in a theme: Qt 6 only accepts precompiled `.qsb` shaders,
  so such effects have to be changed theme by theme (example: `Components/Background.qml` of the Pegasus G theme Ginis)
