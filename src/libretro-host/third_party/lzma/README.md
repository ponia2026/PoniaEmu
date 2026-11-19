# LZMA SDK (7z decoder only)

- Version: LZMA SDK 19.00 (2019-02-02) by Igor Pavlov, **public domain**.
- Source: the copy shipped with libretro-common (`libretro-common/src/7zip/`, types changed to stdint), taken from the build tree of ES4All.
- Only the files needed for decoding are kept (7zArcIn/7zDec/7zFile/7zStream, LzmaDec/Lzma2Dec, Bcj2/Bra/Delta, CRC); the encoder side (LzFind/LzmaEnc) and PPMd are not included.
- Used by: `../../archive.cpp` (7z reading). Compile definition `_7ZIP_ST` (single thread).
- Do not edit the code here; to upgrade, replace the whole set.
