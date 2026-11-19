// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright (C) 2026 Equuleus Works
// Decide which system key a game belongs to (this decides which Libretro core is used automatically).
// Order: the x-system of the game -> name of the folder holding the ROM -> collection short name -> collection name.
// Reason: several folders can be merged into one collection (for example ATARI2600/5200/7800 -> ATARI),
// and the merged collection only keeps one short name, so it cannot tell which core each folder needs.
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
#include <utility>
#include <vector>

namespace ponia {

inline std::string system_key_from_parts(const QVariantMap& extra, const QFileInfo& fi,
                                         const std::vector<std::pair<QString, QString>>& collections)
{
    // 1. x-system written by the game itself (Pegasus ignores fields that start with x-, so both are compatible)
    const auto it = extra.constFind(QStringLiteral("system"));
    if (it != extra.cend()) {
        for (const QString& v : it.value().toStringList()) {
            const std::string k = resolve_system(v.trimmed().toStdString());
            if (!k.empty())
                return k;
        }
    }

    // 2. name of the folder that holds the ROM (FC, ATARI5200, FBNeo hack ...)
    if (fi.exists() || !fi.filePath().isEmpty()) {
        const std::string k = resolve_system(fi.absoluteDir().dirName().toStdString());
        if (!k.empty())
            return k;
    }

    // 3-4. collection short name, collection name
    for (const auto& col : collections) {
        const std::string k = resolve_system(col.first.toStdString(), col.second.toStdString());
        if (!k.empty())
            return k;
    }
    return std::string();
}

// file: the ROM file to open this time; nullptr uses the first file of the game.
inline std::string system_key_for_game(const model::Game& game, const QFileInfo* file = nullptr)
{
    QFileInfo fi;
    if (file) {
        fi = *file;
    }
    else {
        const auto& files = game.filesModel()->entries();
        if (!files.empty())
            fi = files.front()->fileinfo();
    }
    std::vector<std::pair<QString, QString>> collections;
    for (const model::Collection* col : game.collectionsModel()->entries())
        collections.emplace_back(col->shortName(), col->name());
    return system_key_from_parts(game.extraMap(), fi, collections);
}

} // namespace ponia
