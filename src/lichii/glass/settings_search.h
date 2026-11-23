// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QList>
#include <QString>

class QWidget;

namespace GlassUi {

struct TabSearchResult {
    bool matched{};
    QWidget* first_match{};
    int match_count{};
};

void ClearSearchHighlights(QList<QWidget*>& highlighted);

TabSearchResult SearchConfigTab(QWidget* tab, const QString& query,
                                QList<QWidget*>& highlighted_out);

} // namespace GlassUi
