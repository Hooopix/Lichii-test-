// SPDX-FileCopyrightText: Copyright 2026 Hoochii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QSortFilterProxyModel>
#include <QStandardItemModel>
#include <QString>

#include "lichii/game/common.h"

class GameFilterProxyModel final : public QSortFilterProxyModel {
public:
    explicit GameFilterProxyModel(QObject* parent = nullptr) : QSortFilterProxyModel(parent) {}

    void SetFilterText(const QString& filter_text) {
        if (m_filterText == filter_text) {
            return;
        }
        m_filterText = filter_text;
        invalidateFilter();
    }

    [[nodiscard]] const QString& FilterText() const {
        return m_filterText;
    }

protected:
    bool filterAcceptsRow(int source_row, const QModelIndex& source_parent) const override {
        if (m_filterText.isEmpty() || source_parent.isValid()) {
            return true;
        }
        const auto* const model = qobject_cast<const QStandardItemModel*>(sourceModel());
        if (model == nullptr) {
            return true;
        }
        const QStandardItem* const item = model->item(source_row, 0);
        if (item == nullptr) {
            return false;
        }
        return Lichii::FilterMatches(m_filterText, item);
    }

private:
    QString m_filterText;
};
