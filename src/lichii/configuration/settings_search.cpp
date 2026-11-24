// SPDX-FileCopyrightText: Copyright 2026 Hoochii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/configuration/settings_search.h"

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QRadioButton>
#include <QStringList>
#include <QStyle>
#include <QWidget>

#include "lichii/configuration/configure_cpu_debug.h"
#include "lichii/configuration/configure_debug.h"
#include "lichii/configuration/configure_debug_tab.h"
#include "lichii/configuration/configure_hotkeys.h"

namespace SettingsSearch {
namespace {

bool MatchesAllTokens(const QString& haystack, const QStringList& tokens) {
    if (haystack.isEmpty() || tokens.isEmpty()) {
        return false;
    }
    for (const QString& token : tokens) {
        if (!haystack.contains(token, Qt::CaseInsensitive)) {
            return false;
        }
    }
    return true;
}

void MarkHighlight(QWidget* widget, QList<QWidget*>& highlighted) {
    if (!widget) {
        return;
    }
    if (!widget->property("searchMatch").toBool()) {
        widget->setProperty("searchMatch", true);
        if (auto* style = widget->style()) {
            style->unpolish(widget);
            style->polish(widget);
        }
        widget->update();
        highlighted.push_back(widget);
    }
}

TabSearchResult ScanSubtree(QWidget* root, const QStringList& tokens,
                            QList<QWidget*>& highlighted_out) {
    TabSearchResult result{};
    if (!root) {
        return result;
    }

    const QString tab_name = root->accessibleName();
    const bool tab_name_matched = MatchesAllTokens(tab_name, tokens);

    const auto widgets = root->findChildren<QWidget*>();
    for (QWidget* w : widgets) {
        if (!w || w->isHidden()) {
            continue;
        }
        QString searchable;
        if (const auto* label = qobject_cast<const QLabel*>(w)) {
            searchable = label->text();
        } else if (const auto* box = qobject_cast<const QGroupBox*>(w)) {
            searchable = box->title();
        } else if (const auto* btn = qobject_cast<const QAbstractButton*>(w)) {
            searchable = btn->text();
        } else if (const auto* combo = qobject_cast<const QComboBox*>(w)) {
            QStringList items;
            for (int i = 0; i < combo->count(); ++i) {
                items.push_back(combo->itemText(i));
            }
            searchable = items.join(QLatin1Char(' '));
        } else {
            continue;
        }

        const QString tip = w->toolTip();
        if (!tip.isEmpty()) {
            searchable.append(QLatin1Char(' '));
            searchable.append(tip);
        }

        if (MatchesAllTokens(searchable, tokens)) {
            result.matched = true;
            ++result.match_count;
            if (!result.first_match) {
                result.first_match = w;
            }
            // Only visually highlight leaf controls / labels, not whole QGroupBoxes.
            if (!qobject_cast<QGroupBox*>(w)) {
                MarkHighlight(w, highlighted_out);
            }
        }
    }

    if (!result.matched && tab_name_matched) {
        result.matched = true;
        result.match_count = 1;
    }
    return result;
}

} // namespace

void ClearSearchHighlights(QList<QWidget*>& highlighted) {
    for (QWidget* w : highlighted) {
        if (!w) {
            continue;
        }
        w->setProperty("searchMatch", false);
        if (auto* style = w->style()) {
            style->unpolish(w);
            style->polish(w);
        }
        w->update();
    }
    highlighted.clear();
}

TabSearchResult SearchConfigTab(QWidget* tab, const QString& raw_query,
                                QList<QWidget*>& highlighted_out) {
    const QString trimmed = raw_query.trimmed();
    if (!tab) {
        return {};
    }

    if (auto* hotkeys = qobject_cast<ConfigureHotkeys*>(tab)) {
        const bool matched = hotkeys->SetSearchFilter(trimmed);
        return {.matched = matched, .first_match = nullptr, .match_count = matched ? 1 : 0};
    }

    if (trimmed.isEmpty()) {
        return {.matched = true, .first_match = nullptr, .match_count = 0};
    }

    const QStringList tokens = trimmed.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return {.matched = true, .first_match = nullptr, .match_count = 0};
    }

    if (auto* debug_tab = qobject_cast<ConfigureDebugTab*>(tab)) {
        const auto d_res = ScanSubtree(debug_tab->DebugSubTab(), tokens, highlighted_out);
        const auto c_res = ScanSubtree(debug_tab->CpuDebugSubTab(), tokens, highlighted_out);
        TabSearchResult combined{};
        combined.matched = d_res.matched || c_res.matched ||
                           MatchesAllTokens(debug_tab->accessibleName(), tokens);
        combined.match_count = d_res.match_count + c_res.match_count;
        if (d_res.matched && !c_res.matched) {
            debug_tab->SetCurrentIndex(0);
            combined.first_match = d_res.first_match;
        } else if (c_res.matched && !d_res.matched) {
            debug_tab->SetCurrentIndex(1);
            combined.first_match = c_res.first_match;
        } else {
            combined.first_match = d_res.first_match ? d_res.first_match : c_res.first_match;
        }
        return combined;
    }

    return ScanSubtree(tab, tokens, highlighted_out);
}

} // namespace SettingsSearch
