# Qt 5 主题兼容模块

PoniaFE 基于 Qt 6，但现有的 Pegasus 主题（包括天马G）都是为 Qt 5 写的。
这里的模块打包进 `qrc:/qmlcompat`，并加在 QML 导入路径的最前面，
让这些主题**不用修改**就能在 Qt 6 下加载。

## QtGraphicalEffects

Qt 6 把它改名为 `Qt5Compat.GraphicalEffects`。`qmldir` 只有一行 `import`，
把旧模块名转发过去。

注意不能写成 `import Qt5Compat.GraphicalEffects auto`：Qt 6.4 下第二个
文件再导入时会报 `version 1.0 is not installed`。

## QtMultimedia

Qt 6 的 QtMultimedia 对 `import QtMultimedia 5.x` 只导出 `Video`，
`SoundEffect`、`MediaPlayer`、`VideoOutput` 只有 6.0 版本；`Audio`、
`Playlist`、`PlaylistItem` 则已经删除。

这里用一份替代的 `qmldir` 顶替系统的 QtMultimedia：

- 仍然加载系统真正的插件 `quickmultimediaplugin`，所以 Qt 6 的类型
  （`import QtMultimedia` 或 `6.x`）完全不受影响；插件所在目录由
  `FrontendLayer` 通过 `addPluginPath()` 加入
- `Video 6.0` 指向插件自带资源里的原版 `Video.qml`
- 额外提供 5.0 版本的 Qt 5 API，用 Qt 6 的 `MediaPlayer` + `AudioOutput` 实现：

| Qt 5 类型 | 实现文件 | 说明 |
|---|---|---|
| `Audio`、`MediaPlayer` | `Qt5Player.qml` | Qt 5 里两者是同一个类型；支持 `playlist`、`autoPlay`、`loops` 等 |
| `Playlist`、`PlaylistItem` | `Playlist.qml`、`PlaylistItem.qml` | 支持 Sequential/Loop/Random 等播放模式与 `shuffle()` |
| `Video` | `Qt5Video.qml` | 在 Qt 6 `VideoOutput` 上加回 `playlist`、`autoLoad` 等 |
| `VideoOutput` | `Qt5VideoOutput.qml` | Qt 5 写法是 `source: player`，这里反过来设置播放器的输出 |
| `SoundEffect` | `Qt5SoundEffect.qml` | API 没变，只是补上 5.0 版本 |

已知差异：

- QML 枚举不能为负数，`MediaPlayer.Infinite`/`Audio.Infinite` 用一个大数代替，
  `loops` 为负数也一律当作无限循环
- `audioRole`、`flushMode` 在 Qt 6 没有对应功能，设置后不起作用

## 不在兼容范围内

- 主题里内联 GLSL 的 `ShaderEffect`：Qt 6 只接受预编译的 `.qsb` 着色器，
  这类效果需要逐个主题修改（例：天马G Ginis 的 `Components/Background.qml`）
