// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_home_page.h"

#include <algorithm>

#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMenu>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "lichii/glass/glass_widgets.h"

namespace GlassUi {
namespace {

constexpr int EntryIndexRole = Qt::UserRole;
constexpr int SectionHeaderRole = Qt::UserRole + 1;

void ClearLayout(QLayout* layout) {
    if (!layout) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        } else if (QLayout* child = item->layout()) {
            ClearLayout(child);
        }
        delete item;
    }
}

} // namespace

GlassHomePage::GlassHomePage(QWidget* parent) : QWidget(parent), m_tokens(ActiveTokens()) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Left Game Sidebar ────────────────────────────────────────────────
    m_sidebar = new QWidget(this);
    m_sidebar->setObjectName(QStringLiteral("glassSidebar"));
    m_sidebar->setFixedWidth(268);
    auto* side_layout = new QVBoxLayout(m_sidebar);
    side_layout->setContentsMargins(14, 14, 14, 14);
    side_layout->setSpacing(10);

    m_filter_edit = new QLineEdit(m_sidebar);
    m_filter_edit->setPlaceholderText(tr("Search library…"));
    m_filter_edit->setClearButtonEnabled(true);
    side_layout->addWidget(m_filter_edit);

    m_game_list = new QListWidget(m_sidebar);
    m_game_list->setFrameShape(QFrame::NoFrame);
    m_game_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_game_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_game_list->setIconSize(QSize(34, 34));
    m_game_list->setSpacing(2);
    m_game_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; border: none; }"));
    side_layout->addWidget(m_game_list, 1);

    auto* side_div = new QFrame(m_sidebar);
    side_div->setObjectName(QStringLiteral("glassDivider"));
    side_layout->addWidget(side_div);

    m_add_folder_btn = new QPushButton(tr("Add Game Folder"), m_sidebar);
    m_add_folder_btn->setCursor(Qt::PointingHandCursor);
    side_layout->addWidget(m_add_folder_btn);

    m_footer_label = new QLabel(tr("Lichii"), m_sidebar);
    m_footer_label->setProperty("faint", true);
    QFont footer_font = m_footer_label->font();
    footer_font.setPointSizeF(std::max<qreal>(8.0, footer_font.pointSizeF() - 1.5));
    m_footer_label->setFont(footer_font);
    m_footer_label->setWordWrap(true);
    side_layout->addWidget(m_footer_label);

    root->addWidget(m_sidebar);

    // ── Right Hero Stage (Blurred Cover Backdrop) ────────────────────────
    m_backdrop = new GlassBackdrop(this);
    auto* backdrop_layout = new QVBoxLayout(m_backdrop);
    backdrop_layout->setContentsMargins(0, 0, 0, 0);

    m_stage_stack = new QStackedWidget(m_backdrop);
    m_stage_stack->setAttribute(Qt::WA_TranslucentBackground);
    m_stage_stack->setStyleSheet(QStringLiteral("QStackedWidget { background: transparent; }"));
    backdrop_layout->addWidget(m_stage_stack);

    // Page 0: Empty State
    auto* empty_page = new QWidget(m_stage_stack);
    empty_page->setObjectName(QStringLiteral("glassEmptyStage"));
    empty_page->setStyleSheet(QStringLiteral("QWidget#glassEmptyStage { background: transparent; }"));
    auto* empty_outer = new QVBoxLayout(empty_page);
    empty_outer->addStretch(1);
    auto* empty_row = new QHBoxLayout;
    empty_row->addStretch(1);
    m_empty_card = new GlassCard(empty_page, 20);
    m_empty_card->setMaximumWidth(500);
    auto* empty_card_layout = new QVBoxLayout(m_empty_card);
    empty_card_layout->setContentsMargins(32, 30, 32, 30);
    empty_card_layout->setSpacing(14);

    auto* welcome_title = new QLabel(tr("Welcome to Lichii"), m_empty_card);
    welcome_title->setProperty("glassHeroTitle", true);
    auto* welcome_desc = new QLabel(
        tr("Point Lichii to a folder containing your games to populate the library, auto-detect "
           "compatible mods, and launch directly from the Glass stage."),
        m_empty_card);
    welcome_desc->setProperty("muted", true);
    welcome_desc->setWordWrap(true);

    m_empty_add_btn = new QPushButton(tr("Add Game Folder"), m_empty_card);
    m_empty_add_btn->setProperty("glassRole", QStringLiteral("primary"));
    m_empty_add_btn->setMinimumHeight(40);
    m_empty_add_btn->setCursor(Qt::PointingHandCursor);

    auto* setup_row = new QHBoxLayout;
    setup_row->setSpacing(10);
    m_empty_keys_btn = new QPushButton(tr("Install Decryption Keys"), m_empty_card);
    m_empty_keys_btn->setCursor(Qt::PointingHandCursor);
    m_empty_fw_btn = new QPushButton(tr("Install Firmware"), m_empty_card);
    m_empty_fw_btn->setCursor(Qt::PointingHandCursor);
    setup_row->addWidget(m_empty_keys_btn, 1);
    setup_row->addWidget(m_empty_fw_btn, 1);

    empty_card_layout->addWidget(welcome_title);
    empty_card_layout->addWidget(welcome_desc);
    empty_card_layout->addSpacing(4);
    empty_card_layout->addWidget(m_empty_add_btn);
    empty_card_layout->addLayout(setup_row);

    empty_row->addWidget(m_empty_card);
    empty_row->addStretch(1);
    empty_outer->addLayout(empty_row);
    empty_outer->addStretch(1);
    m_stage_stack->addWidget(empty_page);

    // Page 1: Selected Game Hero + Overview Cards
    auto* hero_scroll = new QScrollArea(m_stage_stack);
    hero_scroll->setFrameShape(QFrame::NoFrame);
    hero_scroll->setWidgetResizable(true);
    hero_scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    hero_scroll->viewport()->setObjectName(QStringLiteral("glassHeroViewport"));
    hero_scroll->viewport()->setStyleSheet(
        QStringLiteral("QWidget#glassHeroViewport { background: transparent; }"));

    auto* hero_content = new QWidget(hero_scroll);
    hero_content->setObjectName(QStringLiteral("glassHeroContent"));
    hero_content->setStyleSheet(
        QStringLiteral("QWidget#glassHeroContent { background: transparent; }"));
    auto* hero_layout = new QVBoxLayout(hero_content);
    hero_layout->setContentsMargins(36, 32, 36, 32);
    hero_layout->setSpacing(22);

    // Hero Header (Cover + Title + Chips)
    auto* header_row = new QHBoxLayout;
    header_row->setSpacing(22);

    m_cover_label = new QLabel(hero_content);
    m_cover_label->setFixedSize(128, 128);
    m_cover_label->setAlignment(Qt::AlignCenter);
    header_row->addWidget(m_cover_label, 0, Qt::AlignTop);

    auto* title_col = new QVBoxLayout;
    title_col->setSpacing(8);

    m_eyebrow_label = new QLabel(hero_content);
    m_eyebrow_label->setProperty("muted", true);
    QFont mono = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    mono.setPointSizeF(std::max<qreal>(8.5, m_eyebrow_label->font().pointSizeF() - 0.5));
    m_eyebrow_label->setFont(mono);
    title_col->addWidget(m_eyebrow_label);

    m_title_label = new QLabel(hero_content);
    m_title_label->setProperty("glassHeroTitle", true);
    m_title_label->setWordWrap(true);
    title_col->addWidget(m_title_label);

    auto* chips_row = new QHBoxLayout;
    chips_row->setSpacing(8);
    m_chip_version = new QLabel(hero_content);
    m_chip_version->setProperty("glassChip", true);
    m_chip_playtime = new QLabel(hero_content);
    m_chip_playtime->setProperty("glassChip", true);
    m_chip_size = new QLabel(hero_content);
    m_chip_size->setProperty("glassChip", true);
    m_chip_mods = new QLabel(hero_content);
    m_chip_mods->setProperty("glassBadge", QStringLiteral("accent"));
    chips_row->addWidget(m_chip_version);
    chips_row->addWidget(m_chip_playtime);
    chips_row->addWidget(m_chip_size);
    chips_row->addWidget(m_chip_mods);
    chips_row->addStretch();
    title_col->addLayout(chips_row);

    // Primary Actions Row
    auto* actions_row = new QHBoxLayout;
    actions_row->setSpacing(10);

    m_play_btn = new QPushButton(tr("Play"), hero_content);
    m_play_btn->setProperty("glassRole", QStringLiteral("primary"));
    m_play_btn->setMinimumHeight(40);
    m_play_btn->setMinimumWidth(138);
    m_play_btn->setCursor(Qt::PointingHandCursor);

    m_pause_btn = new QPushButton(tr("Pause"), hero_content);
    m_pause_btn->setMinimumHeight(40);
    m_pause_btn->setCursor(Qt::PointingHandCursor);
    m_pause_btn->hide();

    m_mods_btn = new QPushButton(tr("Mods"), hero_content);
    m_mods_btn->setMinimumHeight(40);
    m_mods_btn->setCursor(Qt::PointingHandCursor);

    m_folders_btn = new QToolButton(hero_content);
    m_folders_btn->setProperty("glassRole", QStringLiteral("icon"));
    m_folders_btn->setToolTip(tr("Open Game Folders"));
    m_folders_btn->setPopupMode(QToolButton::InstantPopup);
    m_folders_btn->setFixedSize(40, 40);
    m_folders_btn->setCursor(Qt::PointingHandCursor);
    auto* folder_menu = new QMenu(m_folders_btn);
    folder_menu->addAction(tr("Open Save Data Folder"), this, [this] {
        if (const auto g = SelectedGame()) {
            emit OpenSaveFolderRequested(g->program_id, g->path);
        }
    });
    folder_menu->addAction(tr("Open Mod Folder"), this, [this] {
        if (const auto g = SelectedGame()) {
            emit OpenModFolderRequested(g->program_id, g->path);
        }
    });
    folder_menu->addAction(tr("Open Shader Cache Folder"), this, [this] {
        if (const auto g = SelectedGame()) {
            emit OpenShaderCacheRequested(g->program_id);
        }
    });
    m_folders_btn->setMenu(folder_menu);

    m_configure_btn = new QPushButton(hero_content);
    m_configure_btn->setProperty("glassRole", QStringLiteral("icon"));
    m_configure_btn->setToolTip(tr("Configure Game Properties"));
    m_configure_btn->setFixedSize(40, 40);
    m_configure_btn->setCursor(Qt::PointingHandCursor);

    m_favorite_btn = new QPushButton(hero_content);
    m_favorite_btn->setProperty("glassRole", QStringLiteral("icon"));
    m_favorite_btn->setToolTip(tr("Toggle Favorite"));
    m_favorite_btn->setFixedSize(40, 40);
    m_favorite_btn->setCursor(Qt::PointingHandCursor);

    m_more_btn = new QPushButton(QStringLiteral("⋯"), hero_content);
    m_more_btn->setProperty("glassRole", QStringLiteral("icon"));
    m_more_btn->setToolTip(tr("More Game Actions"));
    m_more_btn->setFixedSize(40, 40);
    m_more_btn->setCursor(Qt::PointingHandCursor);

    actions_row->addWidget(m_play_btn);
    actions_row->addWidget(m_pause_btn);
    actions_row->addWidget(m_mods_btn);
    actions_row->addWidget(m_folders_btn);
    actions_row->addWidget(m_configure_btn);
    actions_row->addWidget(m_favorite_btn);
    actions_row->addWidget(m_more_btn);
    actions_row->addStretch();

    title_col->addSpacing(6);
    title_col->addLayout(actions_row);
    header_row->addLayout(title_col, 1);
    hero_layout->addLayout(header_row);

    // Overview Cards Row (Installed Mods + Details)
    auto* cards_row = new QHBoxLayout;
    cards_row->setSpacing(16);

    // Card 1: Installed Mods & Add-ons
    m_addons_card = new GlassCard(hero_content, 16);
    auto* addons_card_layout = new QVBoxLayout(m_addons_card);
    addons_card_layout->setContentsMargins(20, 18, 20, 18);
    addons_card_layout->setSpacing(10);

    auto* addons_head = new QHBoxLayout;
    auto* addons_title = new QLabel(tr("Installed Mods & Add-ons"), m_addons_card);
    QFont card_title_font = addons_title->font();
    card_title_font.setBold(true);
    card_title_font.setPointSizeF(card_title_font.pointSizeF() + 1.0);
    addons_title->setFont(card_title_font);
    m_browse_mods_link = new QPushButton(tr("Browse Mods →"), m_addons_card);
    m_browse_mods_link->setProperty("glassRole", QStringLiteral("ghost"));
    m_browse_mods_link->setCursor(Qt::PointingHandCursor);
    addons_head->addWidget(addons_title);
    addons_head->addStretch();
    addons_head->addWidget(m_browse_mods_link);
    addons_card_layout->addLayout(addons_head);

    auto* addons_div = new QFrame(m_addons_card);
    addons_div->setObjectName(QStringLiteral("glassDivider"));
    addons_card_layout->addWidget(addons_div);

    m_addons_list_layout = new QVBoxLayout;
    m_addons_list_layout->setSpacing(6);
    addons_card_layout->addLayout(m_addons_list_layout);
    addons_card_layout->addStretch();

    cards_row->addWidget(m_addons_card, 3);

    // Card 2: Game Details & Quick Tools
    m_details_card = new GlassCard(hero_content, 16);
    auto* details_layout = new QVBoxLayout(m_details_card);
    details_layout->setContentsMargins(20, 18, 20, 18);
    details_layout->setSpacing(10);

    auto* details_title = new QLabel(tr("Game Details"), m_details_card);
    details_title->setFont(card_title_font);
    details_layout->addWidget(details_title);

    auto* details_div = new QFrame(m_details_card);
    details_div->setObjectName(QStringLiteral("glassDivider"));
    details_layout->addWidget(details_div);

    const auto add_kv = [&](const QString& key, QLabel*& value_out) {
        auto* row = new QHBoxLayout;
        auto* k = new QLabel(key, m_details_card);
        k->setProperty("muted", true);
        value_out = new QLabel(m_details_card);
        value_out->setTextInteractionFlags(Qt::TextSelectableByMouse);
        value_out->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        value_out->setWordWrap(true);
        row->addWidget(k);
        row->addWidget(value_out, 1);
        details_layout->addLayout(row);
    };
    add_kv(tr("Title ID"), m_detail_tid);
    m_detail_tid->setFont(mono);
    add_kv(tr("Format"), m_detail_type);
    add_kv(tr("Location"), m_detail_path);

    details_layout->addStretch();

    m_compat_btn = new QPushButton(tr("Report Compatibility…"), m_details_card);
    m_compat_btn->setCursor(Qt::PointingHandCursor);
    details_layout->addWidget(m_compat_btn);

    cards_row->addWidget(m_details_card, 2);
    hero_layout->addLayout(cards_row);
    hero_layout->addStretch(1);

    hero_scroll->setWidget(hero_content);
    m_stage_stack->addWidget(hero_scroll);

    root->addWidget(m_backdrop, 1);

    // ── Connections ──────────────────────────────────────────────────────
    connect(m_filter_edit, &QLineEdit::textChanged, this, [this] { RebuildSidebar(); });
    connect(m_game_list, &QListWidget::currentItemChanged, this,
            [this](QListWidgetItem* current) {
                if (!current || current->data(SectionHeaderRole).toBool()) {
                    return;
                }
                const int idx = current->data(EntryIndexRole).toInt();
                if (idx < 0 || idx >= m_games.size()) {
                    return;
                }
                const auto& g = m_games[idx];
                m_selected_pid = g.program_id;
                m_selected_path = g.path;
                UpdateHero();
                emit SelectedGameChanged(g.program_id, g.title_id_hex);
            });
    connect(m_game_list, &QListWidget::itemDoubleClicked, this, [this](QListWidgetItem* item) {
        if (!item || item->data(SectionHeaderRole).toBool()) {
            return;
        }
        const int idx = item->data(EntryIndexRole).toInt();
        if (idx >= 0 && idx < m_games.size()) {
            emit PlayRequested(m_games[idx].path, m_games[idx].program_id);
        }
    });

    connect(m_add_folder_btn, &QPushButton::clicked, this, &GlassHomePage::AddGameFolderRequested);
    connect(m_empty_add_btn, &QPushButton::clicked, this, &GlassHomePage::AddGameFolderRequested);
    connect(m_empty_keys_btn, &QPushButton::clicked, this, &GlassHomePage::InstallKeysRequested);
    connect(m_empty_fw_btn, &QPushButton::clicked, this, &GlassHomePage::InstallFirmwareRequested);

    connect(m_play_btn, &QPushButton::clicked, this, [this] {
        const auto g = SelectedGame();
        if (!g) {
            return;
        }
        if (m_running && (m_running_pid == 0 || m_running_pid == g->program_id)) {
            emit StopRequested();
        } else {
            emit PlayRequested(g->path, g->program_id);
        }
    });
    connect(m_pause_btn, &QPushButton::clicked, this, &GlassHomePage::PauseResumeRequested);
    connect(m_mods_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = SelectedGame()) {
            emit OpenModsRequested(g->program_id, g->title_id_hex);
        }
    });
    connect(m_browse_mods_link, &QPushButton::clicked, this, [this] {
        if (const auto g = SelectedGame()) {
            emit OpenModsRequested(g->program_id, g->title_id_hex);
        }
    });
    connect(m_configure_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = SelectedGame()) {
            emit ConfigureGameRequested(g->program_id, g->path);
        }
    });
    connect(m_favorite_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = SelectedGame()) {
            emit ToggleFavoriteRequested(g->program_id);
        }
    });
    connect(m_more_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = SelectedGame()) {
            emit ContextMenuRequested(g->program_id, g->path,
                                      m_more_btn->mapToGlobal(QPoint(0, m_more_btn->height())));
        }
    });
    connect(m_compat_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = SelectedGame()) {
            emit ReportCompatibilityRequested(g->program_id);
        }
    });

    UpdateIcons();
    UpdateHero();
}

void GlassHomePage::SetAddonLoader(AddonLoader loader) {
    m_addon_loader = std::move(loader);
    RefreshSelectedAddons();
}

void GlassHomePage::SetGames(QList<GameEntry> games) {
    m_games = std::move(games);
    bool still_present = false;
    for (const auto& g : m_games) {
        if ((m_selected_pid != 0 && g.program_id == m_selected_pid) ||
            (!m_selected_path.isEmpty() && g.path == m_selected_path)) {
            m_selected_pid = g.program_id;
            m_selected_path = g.path;
            still_present = true;
            break;
        }
    }
    if (!still_present && !m_games.isEmpty()) {
        m_selected_pid = m_games.first().program_id;
        m_selected_path = m_games.first().path;
    } else if (m_games.isEmpty()) {
        m_selected_pid = 0;
        m_selected_path.clear();
    }
    RebuildSidebar();
    UpdateHero();
}

void GlassHomePage::SetSelectedProgramId(u64 program_id) {
    for (const auto& g : m_games) {
        if (g.program_id == program_id) {
            m_selected_pid = g.program_id;
            m_selected_path = g.path;
            RebuildSidebar();
            UpdateHero();
            return;
        }
    }
}

void GlassHomePage::SetRunningState(bool running, bool paused, u64 running_program_id) {
    m_running = running;
    m_paused = paused;
    m_running_pid = running_program_id;
    UpdatePlayButtons();
    RebuildSidebar();
}

void GlassHomePage::SetFooterText(const QString& text) {
    m_footer_label->setText(text);
}

void GlassHomePage::RefreshSelectedAddons() {
    if (const auto g = SelectedGame()) {
        RebuildAddonsCard(*g);
    }
}

void GlassHomePage::RefreshTheme() {
    m_tokens = ActiveTokens();
    m_backdrop->SetTokens(m_tokens);
    m_empty_card->SetTokens(m_tokens);
    m_addons_card->SetTokens(m_tokens);
    m_details_card->SetTokens(m_tokens);
    UpdateIcons();
    RebuildSidebar();
    UpdateHero();
}

std::optional<GameEntry> GlassHomePage::SelectedGame() const {
    for (const auto& g : m_games) {
        if ((m_selected_pid != 0 && g.program_id == m_selected_pid) ||
            (!m_selected_path.isEmpty() && g.path == m_selected_path)) {
            return g;
        }
    }
    return std::nullopt;
}

void GlassHomePage::RebuildSidebar() {
    const QSignalBlocker block(m_game_list);
    m_game_list->clear();

    const QString filter = m_filter_edit->text().trimmed();
    QList<int> fav_indices;
    QList<int> other_indices;
    for (int i = 0; i < m_games.size(); ++i) {
        const auto& g = m_games[i];
        if (!filter.isEmpty() && !g.title.contains(filter, Qt::CaseInsensitive) &&
            !g.title_id_hex.contains(filter, Qt::CaseInsensitive)) {
            continue;
        }
        if (g.is_favorite) {
            fav_indices.push_back(i);
        } else {
            other_indices.push_back(i);
        }
    }

    const auto add_header = [&](const QString& label) {
        auto* hdr = new QListWidgetItem(label, m_game_list);
        hdr->setFlags(Qt::NoItemFlags);
        hdr->setData(SectionHeaderRole, true);
        QFont f = hdr->font();
        f.setBold(true);
        f.setPointSizeF(std::max<qreal>(8.0, f.pointSizeF() - 1.5));
        hdr->setFont(f);
        hdr->setForeground(m_tokens.ink_faint);
        hdr->setSizeHint(QSize(0, 26));
    };

    const auto add_entries = [&](const QList<int>& indices) {
        for (int idx : indices) {
            const auto& g = m_games[idx];
            const bool is_running_game = m_running && m_running_pid != 0 && m_running_pid == g.program_id;
            const QString sub = is_running_game
                                    ? tr("● Playing now")
                                    : (!g.play_time.isEmpty() ? g.play_time : g.version);
            auto* item = new QListWidgetItem(m_game_list);
            item->setText(QStringLiteral("%1\n%2").arg(g.title, sub));
            item->setData(EntryIndexRole, idx);
            item->setData(SectionHeaderRole, false);
            item->setSizeHint(QSize(0, 50));
            if (!g.icon.isNull()) {
                item->setIcon(QIcon(RoundedPixmap(g.icon, 34, 8.0)));
            } else {
                item->setIcon(TintedIcon(QStringLiteral("gamepad"), m_tokens.ink_muted, 24));
            }
            if ((m_selected_pid != 0 && g.program_id == m_selected_pid) ||
                (!m_selected_path.isEmpty() && g.path == m_selected_path)) {
                m_game_list->setCurrentItem(item);
            }
        }
    };

    if (!fav_indices.isEmpty()) {
        add_header(tr("FAVORITES"));
        add_entries(fav_indices);
    }
    if (!other_indices.isEmpty()) {
        add_header(fav_indices.isEmpty() ? tr("LIBRARY (%1)").arg(other_indices.size())
                                         : tr("ALL GAMES (%1)").arg(other_indices.size()));
        add_entries(other_indices);
    }
}

void GlassHomePage::UpdateHero() {
    const auto selected = SelectedGame();
    if (!selected) {
        m_backdrop->ClearArtwork();
        m_stage_stack->setCurrentIndex(0);
        return;
    }

    m_stage_stack->setCurrentIndex(1);
    const auto& g = *selected;

    if (!g.icon.isNull()) {
        m_backdrop->SetArtwork(g.icon);
        m_cover_label->setPixmap(RoundedPixmap(g.icon, 124, 22.0));
    } else {
        m_backdrop->ClearArtwork();
        m_cover_label->setPixmap(
            TintedIcon(QStringLiteral("gamepad"), m_tokens.ink_muted, 64).pixmap(64, 64));
    }

    const QString eyebrow =
        !g.title_id_hex.isEmpty()
            ? QStringLiteral("%1 · %2").arg(g.file_type.isEmpty() ? tr("Title") : g.file_type,
                                            g.title_id_hex)
            : g.file_type;
    m_eyebrow_label->setText(eyebrow);
    m_title_label->setText(g.title);

    m_chip_version->setText(tr("v%1").arg(g.version.isEmpty() ? QStringLiteral("1.0.0") : g.version));
    m_chip_playtime->setText(g.play_time.isEmpty() ? tr("Never Played") : g.play_time);
    m_chip_size->setText(g.size_text);
    m_chip_size->setVisible(!g.size_text.isEmpty());

    m_detail_tid->setText(g.title_id_hex.isEmpty() ? tr("—") : g.title_id_hex);
    m_detail_type->setText(g.file_type.isEmpty() ? tr("—") : g.file_type);
    m_detail_path->setText(g.path.isEmpty() ? tr("—") : g.path);

    m_favorite_btn->setIcon(TintedIcon(QStringLiteral("star"),
                                       g.is_favorite ? m_tokens.accent : m_tokens.ink_muted, 18));
    UpdatePlayButtons();
    RebuildAddonsCard(g);
}

void GlassHomePage::UpdatePlayButtons() {
    const auto g = SelectedGame();
    const bool this_running = g && m_running && (m_running_pid == 0 || m_running_pid == g->program_id);
    if (this_running) {
        m_play_btn->setText(tr("Stop"));
        m_play_btn->setProperty("glassRole", QStringLiteral("danger"));
        m_play_btn->setIcon(TintedIcon(QStringLiteral("stop"), m_tokens.danger, 16));
        m_pause_btn->setText(m_paused ? tr("Resume") : tr("Pause"));
        m_pause_btn->setIcon(
            TintedIcon(m_paused ? QStringLiteral("play") : QStringLiteral("pause"),
                       m_tokens.ink_full, 15));
        m_pause_btn->show();
    } else {
        m_play_btn->setText(tr("Play"));
        m_play_btn->setProperty("glassRole", QStringLiteral("primary"));
        m_play_btn->setIcon(TintedIcon(QStringLiteral("play"), m_tokens.on_accent, 16));
        m_pause_btn->hide();
    }
    if (auto* st = m_play_btn->style()) {
        st->unpolish(m_play_btn);
        st->polish(m_play_btn);
    }
}

void GlassHomePage::RebuildAddonsCard(const GameEntry& game) {
    ClearLayout(m_addons_list_layout);

    QList<InstalledAddon> addons;
    if (m_addon_loader && game.program_id != 0) {
        addons = m_addon_loader(game.program_id, game.path);
    }

    int enabled_count = 0;
    for (const auto& a : addons) {
        if (a.enabled) {
            ++enabled_count;
        }
    }

    if (addons.isEmpty()) {
        m_chip_mods->setText(tr("No add-ons"));
        auto* empty_lbl = new QLabel(
            tr("No mods, updates, or DLCs installed for this title yet. Open the Mods tab to "
               "browse auto-detected mods from the Mod Store."),
            m_addons_card);
        empty_lbl->setProperty("muted", true);
        empty_lbl->setWordWrap(true);
        m_addons_list_layout->addWidget(empty_lbl);
        return;
    }

    m_chip_mods->setText(tr("%1 / %2 Add-ons active").arg(enabled_count).arg(addons.size()));

    const int max_rows = std::min<int>(static_cast<int>(addons.size()), 5);
    for (int i = 0; i < max_rows; ++i) {
        const auto& addon = addons[i];
        auto* row_widget = new QWidget(m_addons_card);
        auto* row = new QHBoxLayout(row_widget);
        row->setContentsMargins(0, 4, 0, 4);
        row->setSpacing(10);

        auto* name_lbl = new QLabel(addon.name, row_widget);
        QFont nf = name_lbl->font();
        nf.setBold(true);
        name_lbl->setFont(nf);

        auto* badge = new QLabel(addon.category, row_widget);
        badge->setProperty("glassChip", true);

        row->addWidget(name_lbl, 1);
        row->addWidget(badge);

        if (addon.toggleable) {
            auto* sw = new GlassSwitch(row_widget);
            sw->SetTokens(m_tokens);
            sw->setChecked(addon.enabled);
            sw->setEnabled(!m_running);
            const u64 pid = game.program_id;
            const QString key = addon.key;
            connect(sw, &QAbstractButton::toggled, this,
                    [this, pid, key](bool checked) { emit AddonToggled(pid, key, checked); });
            row->addWidget(sw);
        }
        m_addons_list_layout->addWidget(row_widget);
    }

    if (addons.size() > max_rows) {
        auto* more_lbl = new QLabel(
            tr("+ %1 more in the Mods tab…").arg(addons.size() - max_rows), m_addons_card);
        more_lbl->setProperty("faint", true);
        m_addons_list_layout->addWidget(more_lbl);
    }
}

void GlassHomePage::UpdateIcons() {
    m_add_folder_btn->setIcon(TintedIcon(QStringLiteral("plus"), m_tokens.ink_full, 15));
    m_empty_add_btn->setIcon(TintedIcon(QStringLiteral("plus"), m_tokens.on_accent, 16));
    m_empty_keys_btn->setIcon(TintedIcon(QStringLiteral("key"), m_tokens.ink_full, 15));
    m_empty_fw_btn->setIcon(TintedIcon(QStringLiteral("archive"), m_tokens.ink_full, 15));
    m_mods_btn->setIcon(TintedIcon(QStringLiteral("puzzle"), m_tokens.ink_full, 16));
    m_folders_btn->setIcon(TintedIcon(QStringLiteral("folder"), m_tokens.ink_full, 18));
    m_configure_btn->setIcon(TintedIcon(QStringLiteral("sliders"), m_tokens.ink_full, 18));
    m_compat_btn->setIcon(TintedIcon(QStringLiteral("bug"), m_tokens.ink_full, 15));
    UpdatePlayButtons();
}

} // namespace GlassUi
