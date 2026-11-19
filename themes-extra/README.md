# Bundled theme

PoniaFE ships one theme, **Macaron** (`themes-extra/macaron`). It is installed to `<datadir>/poniafe/themes/`
(Android: packed into the APK `assets/themes/` and unpacked to the config directory on first launch) and is the default theme.
A tiny fallback theme (`src/themes/ponia-fallback`) is built into the program and is only used when no other theme is found.

## Macaron

- Pastel five-panel system view. Systems are sorted as Favorites (virtual), Home consoles, Arcade, Handhelds, Computers, Others, and by release year inside each group.
- Game list with video, game logo, scrolling description and box art. The layout follows the system: 4:3 or 16:9.
- Controls: L1/R1 switch system, X search, Y favorite, A confirm, B back.
- Systems that are not in the built-in table use the generic controller icon, keep the collection name, and are filed under Others.
  An optional `overrides.json` in the theme folder can set year, category, icon, name or ratio per collection:
  `{"pico8": {"y": 2015, "c": "other", "n": "PICO-8", "r": "16:9"}}` (key = collection short name, lower case). Categories: `home`, `arcade`, `hand`, `computer`, `other` (`multi` is the old name of `other`).
- Works with both the original Pegasus Frontend and PoniaFE: it uses only APIs available in both
  (`api.collections`, `api.allGames`, `game.assets.*`).
- Status area (top right): wired / Wi-Fi / offline icon from `api.device.networkType`, battery only on devices that have one, and the clock. Without these properties (original Pegasus) it falls back to a Wi-Fi icon.
- License and credits: see `NOTICE.md` in the repository root.
