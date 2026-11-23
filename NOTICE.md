# NOTICE

## PoniaFE

Copyright (C) 2026 Equuleus Works.

PoniaFE is a derivative work of Pegasus Frontend (https://github.com/mmatyas/pegasus-frontend),
Copyright (C) 2017-2020 Mátyás Mustoha, and is distributed under the GNU General Public License,
version 3 or (at your option) any later version. Supplementary terms are in `LICENSE.md`.
This is an independent project. It is not affiliated with, or endorsed by, the authors of Pegasus Frontend.
Copyright notices in the original source files are retained as required by the GPL.

## PoniaPlay: built-in Libretro player (`src/libretro-host`, Android `PlayerActivity.java`)

- Written from scratch (no Pegasus code), Copyright (C) 2026 Equuleus Works, GPL-2.0-or-later
  (`SPDX-License-Identifier: GPL-2.0-or-later`). This lets it load GPLv2-only and GPLv3 Libretro cores alike.
  The frontend (Pegasus-derived, GPL-3.0-or-later) and PoniaPlay are separate programs that only exchange
  command-line arguments / Android Intents.
- `libretro.h`: MIT, Copyright (C) 2010-2020 The RetroArch team (license text in the header).
- Libretro cores are separate works with their own licenses. The installer bundles only cores whose licenses permit
  commercial distribution; the others are downloaded by the user on first run and are not part of this package.

## Bundled theme: Macaron (`themes-extra/macaron`)

- Source code (QML/JavaScript) and design: Copyright (C) 2026 Equuleus Works, GPL-3.0-or-later
  (`SPDX-License-Identifier: GPL-3.0-or-later` in each file).
- The name "Ponia" and the Ponia logo (`assets/images/ponia_mark.svg`) are the property of Equuleus Works.
  All rights reserved; see the trademark terms in `LICENSE.md`.
- System icons (`assets/icons/*.png`) were drawn with the help of an AI image generator and then edited.
  No copyright is claimed on them beyond the editing work, and they are provided as is.
  Each icon depicts a hardware product only for identification (see "Trademarks").
- Fonts, both under the SIL Open Font License 1.1 (license texts are next to the fonts):
  - Noto Sans TC, Copyright 2014-2021 Adobe (reserved font name "Source") (`assets/fonts/OFL-NotoSansTC.txt`)
    The bundled copy is a subset (about 7,400 characters: Latin, Cyrillic, Greek, kana, common Traditional Chinese,
    GB2312 level 1 and JIS level 1 characters) made with fontTools; glyph outlines are unchanged.
    Other characters fall back to the system fonts.
  - Noto Sans SC, Copyright 2014-2021 Adobe (reserved font name "Source") (`assets/fonts/OFL-NotoSansSC.txt`)
    Also a subset (about 10,200 characters, adding GB2312 level 2), instanced from the variable font at weights 400 and 700.
    The Simplified Chinese interface uses it as the main font, other languages use Noto Sans TC; missing characters fall back to the system fonts.
  - Varela Round, Copyright 2023 The Varela Round Project Authors (`assets/fonts/OFL-VarelaRound.txt`)

## Third-party components

- Qt Toolkit, LGPLv3, Copyright (C) The Qt Company Ltd.
- SDL2, zlib license. The Android build includes its Java sources in `src/app/platform/android/src/org/libsdl/app/`.
- Roboto fonts, Apache License 2.0, Copyright (C) Google Inc.

## Trademarks

All trademarks, service marks, trade names, trade dress, product names and logos are the property of their respective owners.
All company, product and service names used in this product are for identification purposes only.
Use of these names, logos and brands does not imply endorsement.

## Qt Multimedia video shaders

src/app/mm_shaders/*.qsb are compiled from the Qt 6.8.2 Qt Multimedia fragment shaders (alpha-texture variants), (C) The Qt Company, LGPL-3.0 / GPL-2.0. They are bundled because several distribution builds of Qt do not ship them.
