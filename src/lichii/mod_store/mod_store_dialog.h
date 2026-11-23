// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>

#include <QList>
#include <QPixmap>
#include <QString>
#include <QWidget>

class QTreeWidget;

namespace ModStore {

struct Game;

struct GameIdentity {
    bool installed{};
    QString display_version;
    QString build_id;
};

struct DetectedGame {
    QString title_id;
    QString name;
    QPixmap icon;
    int installed_mods{};
};

struct DialogContext {
    QString base_url;
    QString initial_title_id;
    QString mods_root;
    QList<DetectedGame> detected_games;
    bool auto_load{true};
    std::function<GameIdentity(const QString&)> game_identity;
    std::function<bool(const QString&, const QString&)> is_enabled;
    std::function<bool(const QString&, const QString&, bool)> set_enabled;
};

class Widget : public QWidget {
    Q_OBJECT

public:
    explicit Widget(DialogContext context, QWidget* parent = nullptr, bool embedded = true);
    ~Widget() override;

    [[nodiscard]] bool Changed() const;
    [[nodiscard]] bool CanClose() const;
    void CancelIfBusy();

    void SetDetectedGames(QList<DetectedGame> games);
    void SetSelectedTitleId(const QString& title_id);
    void SetMyGamesOnly(bool my_games_only);
    void SetCatalogGames(QList<Game> catalog_games);
    void RefreshTheme();
    void RefreshAll();

    [[nodiscard]] QString SelectedTitleId() const;
    [[nodiscard]] bool IsMyGamesOnly() const;
    [[nodiscard]] int DetectedGameCount() const;
    [[nodiscard]] int MatchedCatalogGameCount() const;
    [[nodiscard]] QTreeWidget* GameTree() const;

signals:
    void ModsChanged();
    void SelectedGameChanged(const QString& title_id);
    void CloseRequested();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

bool ShowDialog(QWidget* parent, DialogContext context);

} // namespace ModStore
