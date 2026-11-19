# LZMA SDK（只含 7z 解码端）

- 版本：LZMA SDK 19.00（2019-02-02），作者 Igor Pavlov，**公有领域（Public domain）**。
- 来源：libretro-common 随附的副本（`libretro-common/src/7zip/`，型别已改为 stdint），取自 ES4All 的建置树。
- 只保留解码所需档案（7zArcIn/7zDec/7zFile/7zStream、LzmaDec/Lzma2Dec、Bcj2/Bra/Delta、CRC）；不含压缩端（LzFind/LzmaEnc）与 PPMd。
- 使用处：`../../archive.cpp`（7z 读取）。编译定义 `_7ZIP_ST`（单执行绪）。
- 不要在这里修改程式；要升级就整批换。
