// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>

#include <QDialog>
#include <QIcon>
#include <QList>
#include <QString>

#include "lichii/glass/glass_tokens.h"

class QFrame;
class QLabel;
class QLineEdit;
class QListWidget;

namespace GlassUi {

struct CommandItem {
    QString title;
    QString subtitle;
    QString category;
    QString keywords;
    QIcon icon;
    std::function<void()> action;
};

class GlassCommandPalette : public QDialog {
    Q_OBJECT

public:
    explicit GlassCommandPalette(QWidget* parent = nullptr);

    void SetItems(QList<CommandItem> items);
    void OpenCentered();
    void RefreshTheme();

    [[nodiscard]] int VisibleResultCount() const;
    void SetQuery(const QString& text);
    bool ActivateSelected();

protected:
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    void RebuildResults(const QString& query);

    PaletteTokens m_tokens;
    QList<CommandItem> m_all_items;
    QList<int> m_visible_indices;

    QFrame* m_frame{nullptr};
    QLineEdit* m_input{nullptr};
    QListWidget* m_list{nullptr};
    QLabel* m_footer{nullptr};
};

} // namespace GlassUi
