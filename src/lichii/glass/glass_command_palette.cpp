// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_command_palette.h"

#include <algorithm>

#include <QEvent>
#include <QFrame>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

namespace GlassUi {
namespace {

constexpr int MaxVisibleResults = 12;
constexpr int SubtitleRole = Qt::UserRole + 1;
constexpr int CategoryRole = Qt::UserRole + 2;

int ScoreItem(const CommandItem& item, const QStringList& tokens) {
    if (tokens.isEmpty()) {
        return 10;
    }
    const QString haystack =
        QStringLiteral("%1 %2 %3 %4").arg(item.title, item.subtitle, item.category, item.keywords);
    int score = 0;
    for (const QString& tok : tokens) {
        if (item.title.startsWith(tok, Qt::CaseInsensitive)) {
            score += 40;
        } else if (item.title.contains(tok, Qt::CaseInsensitive)) {
            score += 24;
        } else if (haystack.contains(tok, Qt::CaseInsensitive)) {
            score += 10;
        } else {
            return -1;
        }
    }
    return score;
}

class PaletteItemDelegate final : public QStyledItemDelegate {
public:
    PaletteItemDelegate(const PaletteTokens* tokens, QObject* parent = nullptr)
        : QStyledItemDelegate(parent), m_tokens(tokens) {}

    void paint(QPainter* painter, const QStyleOptionViewItem& option,
               const QModelIndex& index) const override {
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);

        const QRectF r = QRectF(option.rect).adjusted(4, 2, -4, -2);
        if (option.state & QStyle::State_Selected) {
            painter->setPen(QPen(m_tokens->accent, 1.0));
            painter->setBrush(m_tokens->plate_selected);
            painter->drawRoundedRect(r, 10, 10);
        } else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_tokens->plate_hover);
            painter->drawRoundedRect(r, 10, 10);
        }

        int left = static_cast<int>(r.left()) + 12;
        const QIcon icon = index.data(Qt::DecorationRole).value<QIcon>();
        if (!icon.isNull()) {
            const int icon_y = static_cast<int>(r.center().y()) - 12;
            icon.paint(painter, QRect(left, icon_y, 24, 24));
            left += 34;
        }

        const QString category = index.data(CategoryRole).toString();
        int right = static_cast<int>(r.right()) - 12;
        if (!category.isEmpty()) {
            QFont cf = option.font;
            cf.setPointSizeF(std::max<qreal>(8.0, cf.pointSizeF() - 1.5));
            cf.setBold(true);
            painter->setFont(cf);
            const QFontMetrics cfm(cf);
            const int badge_w = cfm.horizontalAdvance(category) + 16;
            const QRectF badge_rect(right - badge_w, r.center().y() - 10, badge_w, 20);
            painter->setPen(QPen(m_tokens->rule, 1.0));
            painter->setBrush(m_tokens->plate_bg);
            painter->drawRoundedRect(badge_rect, 8, 8);
            painter->setPen(m_tokens->ink_muted);
            painter->drawText(badge_rect, Qt::AlignCenter, category);
            right = static_cast<int>(badge_rect.left()) - 10;
        }

        const QString title = index.data(Qt::DisplayRole).toString();
        const QString subtitle = index.data(SubtitleRole).toString();
        const int text_w = std::max(40, right - left);

        QFont tf = option.font;
        tf.setBold(true);
        painter->setFont(tf);
        painter->setPen(m_tokens->ink_full);
        const QFontMetrics tfm(tf);
        if (subtitle.isEmpty()) {
            painter->drawText(QRect(left, static_cast<int>(r.top()), text_w, static_cast<int>(r.height())),
                              Qt::AlignVCenter | Qt::AlignLeft,
                              tfm.elidedText(title, Qt::ElideRight, text_w));
        } else {
            const int top = static_cast<int>(r.top()) + 6;
            painter->drawText(QRect(left, top, text_w, 20), Qt::AlignVCenter | Qt::AlignLeft,
                              tfm.elidedText(title, Qt::ElideRight, text_w));
            QFont sf = option.font;
            sf.setPointSizeF(std::max<qreal>(8.0, sf.pointSizeF() - 1.0));
            painter->setFont(sf);
            painter->setPen(m_tokens->ink_muted);
            const QFontMetrics sfm(sf);
            painter->drawText(QRect(left, top + 20, text_w, 16), Qt::AlignVCenter | Qt::AlignLeft,
                              sfm.elidedText(subtitle, Qt::ElideRight, text_w));
        }

        painter->restore();
    }

    QSize sizeHint(const QStyleOptionViewItem& /*option*/,
                   const QModelIndex& /*index*/) const override {
        return QSize(0, 50);
    }

private:
    const PaletteTokens* m_tokens;
};

} // namespace

GlassCommandPalette::GlassCommandPalette(QWidget* parent)
    : QDialog(parent, Qt::Popup | Qt::FramelessWindowHint), m_tokens(ActiveTokens()) {
    setAttribute(Qt::WA_TranslucentBackground, true);
    setModal(false);
    setFixedWidth(620);

    auto* outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);

    m_frame = new QFrame(this);
    m_frame->setObjectName(QStringLiteral("glassCommandPaletteFrame"));
    auto* lay = new QVBoxLayout(m_frame);
    lay->setContentsMargins(14, 14, 14, 12);
    lay->setSpacing(10);

    m_input = new QLineEdit(m_frame);
    m_input->setPlaceholderText(tr("Jump to a game, tab, setting, folder, or action…"));
    m_input->setClearButtonEnabled(true);
    m_input->setMinimumHeight(38);
    QFont inf = m_input->font();
    inf.setPointSizeF(inf.pointSizeF() + 1.0);
    m_input->setFont(inf);
    m_input->installEventFilter(this);
    lay->addWidget(m_input);

    m_list = new QListWidget(m_frame);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    m_list->setItemDelegate(new PaletteItemDelegate(&m_tokens, m_list));
    m_list->setMaximumHeight(340);
    m_list->setMinimumHeight(180);
    lay->addWidget(m_list, 1);

    auto* div = new QFrame(m_frame);
    div->setObjectName(QStringLiteral("glassDivider"));
    lay->addWidget(div);

    m_footer = new QLabel(tr("↑↓ Navigate   ·   Enter Open   ·   Esc Close"), m_frame);
    m_footer->setProperty("faint", true);
    m_footer->setAlignment(Qt::AlignRight);
    lay->addWidget(m_footer);

    outer->addWidget(m_frame);

    connect(m_input, &QLineEdit::textChanged, this, &GlassCommandPalette::RebuildResults);
    connect(m_list, &QListWidget::itemActivated, this, [this] { ActivateSelected(); });
    connect(m_list, &QListWidget::itemClicked, this, [this] { ActivateSelected(); });
}

void GlassCommandPalette::SetItems(QList<CommandItem> items) {
    m_all_items = std::move(items);
    RebuildResults(m_input->text());
}

void GlassCommandPalette::OpenCentered() {
    RefreshTheme();
    m_input->clear();
    RebuildResults({});
    adjustSize();
    if (QWidget* p = parentWidget()) {
        const QPoint center = p->mapToGlobal(p->rect().center());
        move(center.x() - width() / 2, p->mapToGlobal(QPoint(0, 0)).y() + 84);
    }
    show();
    raise();
    activateWindow();
    m_input->setFocus();
}

void GlassCommandPalette::RefreshTheme() {
    m_tokens = ActiveTokens();
    m_list->viewport()->update();
}

int GlassCommandPalette::VisibleResultCount() const {
    return m_list->count();
}

void GlassCommandPalette::SetQuery(const QString& text) {
    m_input->setText(text);
}

bool GlassCommandPalette::ActivateSelected() {
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_visible_indices.size()) {
        return false;
    }
    const auto action = m_all_items[m_visible_indices[row]].action;
    hide();
    if (action) {
        action();
    }
    return true;
}

bool GlassCommandPalette::eventFilter(QObject* obj, QEvent* event) {
    if (obj == m_input && event->type() == QEvent::KeyPress) {
        const auto* ke = static_cast<QKeyEvent*>(event);
        if (ke->key() == Qt::Key_Escape) {
            hide();
            return true;
        }
        if (ke->key() == Qt::Key_Down && m_list->count() > 0) {
            m_list->setCurrentRow((m_list->currentRow() + 1) % m_list->count());
            return true;
        }
        if (ke->key() == Qt::Key_Up && m_list->count() > 0) {
            const int prev = m_list->currentRow() - 1;
            m_list->setCurrentRow(prev < 0 ? m_list->count() - 1 : prev);
            return true;
        }
        if (ke->key() == Qt::Key_Return || ke->key() == Qt::Key_Enter) {
            ActivateSelected();
            return true;
        }
    }
    return QDialog::eventFilter(obj, event);
}

void GlassCommandPalette::RebuildResults(const QString& query) {
    m_list->clear();
    m_visible_indices.clear();

    const QStringList tokens = query.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    struct Scored {
        int index;
        int score;
    };
    QList<Scored> scored;
    scored.reserve(m_all_items.size());
    for (int i = 0; i < m_all_items.size(); ++i) {
        const int s = ScoreItem(m_all_items[i], tokens);
        if (s >= 0) {
            scored.push_back({i, s});
        }
    }
    std::ranges::stable_sort(scored, [](const Scored& a, const Scored& b) {
        return a.score > b.score;
    });

    const int limit = std::min<int>(static_cast<int>(scored.size()), MaxVisibleResults);
    for (int i = 0; i < limit; ++i) {
        const auto& item = m_all_items[scored[i].index];
        m_visible_indices.push_back(scored[i].index);
        auto* row = new QListWidgetItem(item.icon, item.title, m_list);
        row->setData(SubtitleRole, item.subtitle);
        row->setData(CategoryRole, item.category);
    }

    if (m_list->count() > 0) {
        m_list->setCurrentRow(0);
    }
}

} // namespace GlassUi
