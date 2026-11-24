// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QListView>
#include <QString>

class GameCard;
class GameFilterProxyModel;
class GameListModel;
class QResizeEvent;

class GameCarousel : public QListView {
    Q_OBJECT

public:
    explicit GameCarousel(QWidget* parent = nullptr);

    void SetModel(GameListModel* model);
    void ApplyFilter(const QString& edit_filter_text, GameListModel* model);
    [[nodiscard]] QString GetLastFilterResultItem() const;
    void UpdateIconSize();

    QModelIndex indexAt(const QPoint& point) const override;

signals:
    void FilterResultReady(int visible, int total);

private:
    GameCard* m_gameCard = nullptr;
    GameFilterProxyModel* m_filterProxy = nullptr;
};
