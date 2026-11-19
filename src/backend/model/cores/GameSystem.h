// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// 判断一款游戏属于哪个机种键（决定自动用哪颗 Libretro 核心）。
// 顺序：游戏的 x-system → ROM 所在资料夹名 → 集合简称 → 集合名。
// 原因：多个资料夹可以合并成同一个集合（例：ATARI2600/5200/7800 → ATARI），
// 合并后集合只剩一个简称，分不出各资料夹该用哪颗核心。
#pragma once

#include "model/gaming/Collection.h"
#include "model/gaming/Game.h"
#include "model/gaming/GameFile.h"
#include "ponia_systems.h"

#include <QDir>
#include <QFileInfo>
#include <QStringList>
#include <QVariantMap>

#include <string>

namespace ponia {

// file：这次要开的那个 ROM 文件；传 nullptr 则取游戏的第一个文件。
inline std::string system_key_for_game(const model::Game& game, const QFileInfo* file = nullptr)
{
    // 1. 游戏自己写的 x-system（Pegasus 会忽略 x- 开头的栏位，所以两边都相容）
    const QVariantMap& extra = game.extraMap();
    const auto it = extra.constFind(QStringLiteral("system"));
    if (it != extra.cend()) {
        for (const QString& v : it.value().toStringList()) {
            const std::string k = resolve_system(v.trimmed().toStdString());
            if (!k.empty())
                return k;
        }
    }

    // 2. ROM 所在资料夹名（FC、ATARI5200、FBNeo hack …）
    QFileInfo fi;
    if (file) {
        fi = *file;
    }
    else {
        const auto& files = game.filesModel()->entries();
        if (!files.empty())
            fi = files.front()->fileinfo();
    }
    if (fi.exists() || !fi.filePath().isEmpty()) {
        const std::string k = resolve_system(fi.absoluteDir().dirName().toStdString());
        if (!k.empty())
            return k;
    }

    // 3~4. 集合简称、集合名
    for (const model::Collection* col : game.collectionsModel()->entries()) {
        const std::string k = resolve_system(col->shortName().toStdString(), col->name().toStdString());
        if (!k.empty())
            return k;
    }
    return std::string();
}

} // namespace ponia
