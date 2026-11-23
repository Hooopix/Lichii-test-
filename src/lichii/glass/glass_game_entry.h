// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <algorithm>

#include <QList>
#include <QPixmap>
#include <QSet>
#include <QStandardItem>
#include <QStandardItemModel>
#include <QString>

#include "common/common_types.h"
#include "lichii/mod_store/mod_store_dialog.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/game_list/game_list_p.h"
#include "qt_common/game_list/model.h"

namespace GlassUi {

struct GameEntry {
    u64 program_id{};
    QString title_id_hex;
    QString title;
    QString path;
    QString file_type;
    QString version;
    QString play_time;
    QString size_text;
    QString addons_summary;
    QPixmap icon;
    bool is_favorite{};
    int mod_count{};
};

struct InstalledAddon {
    QString key;
    QString name;
    QString version;
    QString category; // e.g. "Mod", "Update", "DLC", "Cheat"
    QString location; // non-empty for deletable disk mods
    bool enabled{true};
    bool toggleable{true};
    bool is_update{};
};

inline QString FormatTitleId(u64 program_id) {
    if (program_id == 0) {
        return {};
    }
    return QStringLiteral("%1").arg(program_id, 16, 16, QLatin1Char('0')).toUpper();
}

inline QList<GameEntry> CollectGameEntries(const QStandardItemModel* model) {
    QList<GameEntry> out;
    if (!model) {
        return out;
    }

    QSet<u64> seen_ids;
    QSet<QString> seen_paths;

    const auto try_add = [&](QStandardItem* name_item, QStandardItem* type_item,
                             QStandardItem* size_item, QStandardItem* time_item,
                             QStandardItem* addons_item) {
        if (!name_item ||
            name_item->data(GameListItem::TypeRole).toInt() !=
                static_cast<int>(GameListItemType::Game)) {
            return;
        }
        const u64 pid = name_item->data(GameListItemPath::ProgramIdRole).toULongLong();
        const QString full_path = name_item->data(GameListItemPath::FullPathRole).toString();
        if (pid != 0) {
            if (seen_ids.contains(pid)) {
                return;
            }
            seen_ids.insert(pid);
        } else if (!full_path.isEmpty()) {
            if (seen_paths.contains(full_path)) {
                return;
            }
            seen_paths.insert(full_path);
        }

        GameEntry entry;
        entry.program_id = pid;
        entry.title_id_hex = FormatTitleId(pid);
        entry.title = name_item->data(GameListItemPath::TitleRole).toString().trimmed();
        if (entry.title.isEmpty()) {
            entry.title = name_item->text().section(QLatin1Char('\n'), 0, 0).trimmed();
        }
        entry.path = full_path;
        entry.file_type = type_item && !type_item->text().isEmpty()
                              ? type_item->text().trimmed()
                              : name_item->data(GameListItemPath::FileTypeRole).toString().trimmed();
        entry.size_text = size_item ? size_item->text().trimmed() : QString{};
        entry.play_time = time_item ? time_item->text().trimmed() : QString{};
        entry.addons_summary = addons_item ? addons_item->text().trimmed() : QString{};
        entry.icon = name_item->data(Qt::DecorationRole).value<QPixmap>();
        entry.is_favorite = pid != 0 && UISettings::values.favorited_ids.contains(pid);

        // Extract version line from tooltip ("Play Time: ...\nVersion: 1.2.0").
        const QString tip = name_item->data(Qt::ToolTipRole).toString();
        const QStringList tip_lines = tip.split(QLatin1Char('\n'));
        for (const QString& line : tip_lines) {
            const int colon = line.indexOf(QLatin1Char(':'));
            if (colon > 0 && line.left(colon).contains(QStringLiteral("Version"), Qt::CaseInsensitive)) {
                entry.version = line.mid(colon + 1).trimmed();
                break;
            }
        }
        if (entry.version.isEmpty()) {
            entry.version = QStringLiteral("1.0.0");
        }
        if (!entry.addons_summary.isEmpty()) {
            const auto lines = entry.addons_summary.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
            entry.mod_count = static_cast<int>(lines.size());
        }
        out.push_back(std::move(entry));
    };

    for (int row = 0; row < model->rowCount(); ++row) {
        QStandardItem* top = model->item(row, GameListModel::COLUMN_NAME);
        if (!top) {
            continue;
        }
        const int type = top->data(GameListItem::TypeRole).toInt();
        if (type == static_cast<int>(GameListItemType::Game)) {
            try_add(top, model->item(row, GameListModel::COLUMN_FILE_TYPE),
                    model->item(row, GameListModel::COLUMN_SIZE),
                    model->item(row, GameListModel::COLUMN_PLAY_TIME),
                    model->item(row, GameListModel::COLUMN_ADD_ONS));
        } else if (type != static_cast<int>(GameListItemType::Favorites)) {
            for (int c = 0; c < top->rowCount(); ++c) {
                try_add(top->child(c, GameListModel::COLUMN_NAME),
                        top->child(c, GameListModel::COLUMN_FILE_TYPE),
                        top->child(c, GameListModel::COLUMN_SIZE),
                        top->child(c, GameListModel::COLUMN_PLAY_TIME),
                        top->child(c, GameListModel::COLUMN_ADD_ONS));
            }
        }
    }

    // Fallback pass for favorites-only entries if any weren't inside another folder.
    for (int row = 0; row < model->rowCount(); ++row) {
        QStandardItem* top = model->item(row, GameListModel::COLUMN_NAME);
        if (top &&
            top->data(GameListItem::TypeRole).toInt() ==
                static_cast<int>(GameListItemType::Favorites)) {
            for (int c = 0; c < top->rowCount(); ++c) {
                try_add(top->child(c, GameListModel::COLUMN_NAME),
                        top->child(c, GameListModel::COLUMN_FILE_TYPE),
                        top->child(c, GameListModel::COLUMN_SIZE),
                        top->child(c, GameListModel::COLUMN_PLAY_TIME),
                        top->child(c, GameListModel::COLUMN_ADD_ONS));
            }
        }
    }

    std::ranges::sort(out, [](const GameEntry& a, const GameEntry& b) {
        if (a.is_favorite != b.is_favorite) {
            return a.is_favorite > b.is_favorite;
        }
        return a.title.compare(b.title, Qt::CaseInsensitive) < 0;
    });
    return out;
}

inline QList<ModStore::DetectedGame> ToDetectedGames(const QList<GameEntry>& entries) {
    QList<ModStore::DetectedGame> detected;
    detected.reserve(entries.size());
    for (const auto& e : entries) {
        if (e.title_id_hex.isEmpty()) {
            continue;
        }
        detected.push_back(ModStore::DetectedGame{
            .title_id = e.title_id_hex,
            .name = e.title,
            .icon = e.icon,
            .installed_mods = e.mod_count,
        });
    }
    return detected;
}

} // namespace GlassUi
