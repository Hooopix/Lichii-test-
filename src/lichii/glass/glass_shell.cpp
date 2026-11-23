// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_shell.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QPushButton>
#include <QStackedWidget>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include "lichii/glass/glass_command_palette.h"
#include "lichii/glass/glass_home_page.h"
#include "lichii/glass/glass_mods_page.h"
#include "lichii/glass/glass_multiplayer_page.h"
#include "lichii/glass/glass_settings_page.h"
#include "lichii/glass/glass_widgets.h"

namespace GlassUi {
namespace {

QPixmap MakeBrandBadge(const PaletteTokens& tokens) {
    constexpr int logical = 28;
    QImage img(logical * 2, logical * 2, QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(2.0);
    img.fill(Qt::transparent);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(tokens.accent);
    p.drawRoundedRect(QRectF(1.0, 1.0, logical - 2.0, logical - 2.0), 7.5, 7.5);
    const QIcon glyph = TintedIcon(QStringLiteral("sparkles"), tokens.on_accent, 16);
    if (!glyph.isNull()) {
        glyph.paint(&p, QRect(6, 6, 16, 16));
    }
    p.end();
    return QPixmap::fromImage(img);
}

} // namespace

GlassShell::GlassShell(QWidget* parent) : QWidget(parent), m_tokens(ActiveTokens()) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Top Navigation Bar ───────────────────────────────────────────────
    m_top_bar = new QWidget(this);
    m_top_bar->setObjectName(QStringLiteral("glassTopBar"));
    m_top_bar->setFixedHeight(56);
    auto* top_layout = new QHBoxLayout(m_top_bar);
    top_layout->setContentsMargins(16, 8, 16, 8);
    top_layout->setSpacing(8);

    m_brand_icon = new QLabel(m_top_bar);
    m_brand_icon->setFixedSize(28, 28);
    m_brand_title = new QLabel(QStringLiteral("Lichii"), m_top_bar);
    QFont brand_font = m_brand_title->font();
    brand_font.setBold(true);
    brand_font.setPointSizeF(brand_font.pointSizeF() + 3.0);
    m_brand_title->setFont(brand_font);

    top_layout->addWidget(m_brand_icon);
    top_layout->addWidget(m_brand_title);
    top_layout->addSpacing(18);

    const std::array<QString, 5> labels{
        tr("Play"), tr("Library"), tr("Mods"), tr("Multiplayer"), tr("Settings"),
    };
    for (int i = 0; i < 5; ++i) {
        auto* btn = new QPushButton(labels[static_cast<size_t>(i)], m_top_bar);
        btn->setProperty("glassRole", QStringLiteral("navTab"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(15, 15));
        connect(btn, &QPushButton::clicked, this,
                [this, i] { SetCurrentPage(static_cast<Page>(i)); });
        m_nav_buttons[static_cast<size_t>(i)] = btn;
        top_layout->addWidget(btn);
    }

    top_layout->addStretch(1);

    m_search_pill = new QPushButton(tr("Quick Jump…   Ctrl+K"), m_top_bar);
    m_search_pill->setProperty("glassRole", QStringLiteral("searchPill"));
    m_search_pill->setCursor(Qt::PointingHandCursor);
    m_search_pill->setToolTip(tr("Open Command Palette (Ctrl+K)"));
    top_layout->addWidget(m_search_pill);

    m_add_btn = new QPushButton(m_top_bar);
    m_add_btn->setProperty("glassRole", QStringLiteral("icon"));
    m_add_btn->setFixedSize(34, 34);
    m_add_btn->setToolTip(tr("Add Game Folder"));
    m_add_btn->setCursor(Qt::PointingHandCursor);
    top_layout->addWidget(m_add_btn);

    m_menu_btn = new QToolButton(m_top_bar);
    m_menu_btn->setProperty("glassRole", QStringLiteral("icon"));
    m_menu_btn->setFixedSize(34, 34);
    m_menu_btn->setToolTip(tr("Classic Menus (File, Emulation, View, Tools, Help)"));
    m_menu_btn->setPopupMode(QToolButton::InstantPopup);
    m_menu_btn->setCursor(Qt::PointingHandCursor);
    m_classic_menu = new QMenu(m_menu_btn);
    m_menu_btn->setMenu(m_classic_menu);
    top_layout->addWidget(m_menu_btn);

    root->addWidget(m_top_bar);

    // ── Stacked Pages ────────────────────────────────────────────────────
    m_stack = new QStackedWidget(this);

    // 0: Play
    m_home_page = new GlassHomePage(m_stack);
    m_stack->addWidget(m_home_page);

    // 1: Library (wraps existing GameList)
    m_library_page = new QWidget(m_stack);
    auto* lib_root = new QVBoxLayout(m_library_page);
    lib_root->setContentsMargins(0, 0, 0, 0);
    lib_root->setSpacing(0);

    auto* lib_bar = new QWidget(m_library_page);
    lib_bar->setObjectName(QStringLiteral("glassLibraryBar"));
    auto* lib_bar_layout = new QHBoxLayout(lib_bar);
    lib_bar_layout->setContentsMargins(20, 10, 20, 10);
    lib_bar_layout->setSpacing(10);

    auto* lib_title = new QLabel(tr("Library"), lib_bar);
    QFont ltf = lib_title->font();
    ltf.setBold(true);
    ltf.setPointSizeF(ltf.pointSizeF() + 2.5);
    lib_title->setFont(ltf);
    lib_bar_layout->addWidget(lib_title);
    lib_bar_layout->addSpacing(8);

    m_library_mode_control = new GlassSegmentedControl(lib_bar);
    m_library_mode_control->SetSegments({tr("List"), tr("Grid"), tr("Carousel")});
    lib_bar_layout->addWidget(m_library_mode_control);
    lib_bar_layout->addStretch(1);

    m_library_filter_btn = new QPushButton(tr("Filter"), lib_bar);
    m_library_filter_btn->setCursor(Qt::PointingHandCursor);
    m_library_refresh_btn = new QPushButton(tr("Refresh"), lib_bar);
    m_library_refresh_btn->setCursor(Qt::PointingHandCursor);
    lib_bar_layout->addWidget(m_library_filter_btn);
    lib_bar_layout->addWidget(m_library_refresh_btn);
    lib_root->addWidget(lib_bar);

    auto* lib_host = new QWidget(m_library_page);
    m_library_host_layout = new QVBoxLayout(lib_host);
    m_library_host_layout->setContentsMargins(16, 12, 16, 12);
    m_library_host_layout->setSpacing(0);
    lib_root->addWidget(lib_host, 1);
    m_stack->addWidget(m_library_page);

    // 2: Mods
    m_mods_page = new GlassModsPage(m_stack);
    m_stack->addWidget(m_mods_page);

    // 3: Multiplayer
    m_multiplayer_page = new GlassMultiplayerPage(m_stack);
    m_stack->addWidget(m_multiplayer_page);

    // 4: Settings
    m_settings_page = new GlassSettingsPage(m_stack);
    m_stack->addWidget(m_settings_page);

    root->addWidget(m_stack, 1);

    m_palette = new GlassCommandPalette(this);

    // ── Connections ──────────────────────────────────────────────────────
    connect(m_search_pill, &QPushButton::clicked, this, &GlassShell::CommandPaletteRequested);
    connect(m_add_btn, &QPushButton::clicked, this, &GlassShell::AddGameFolderRequested);
    connect(m_library_filter_btn, &QPushButton::clicked, this,
            &GlassShell::LibraryFilterToggleRequested);
    connect(m_library_refresh_btn, &QPushButton::clicked, this,
            &GlassShell::LibraryRefreshRequested);
    connect(m_library_mode_control, &GlassSegmentedControl::CurrentIndexChanged, this,
            [this](int idx) {
                if (m_syncing_view_mode) {
                    return;
                }
                const auto mode = idx == 1 ? Settings::GameListMode::GridView
                                  : idx == 2 ? Settings::GameListMode::CarouselView
                                             : Settings::GameListMode::TreeView;
                emit LibraryViewModeRequested(mode);
            });

    // Keep selected game synced between Play and Mods tabs.
    connect(m_home_page, &GlassHomePage::SelectedGameChanged, this,
            [this](u64 pid, const QString&) { m_mods_page->SetSelectedGame(pid); });
    connect(m_home_page, &GlassHomePage::OpenModsRequested, this,
            [this](u64 pid, const QString&) {
                m_mods_page->SetSelectedGame(pid);
                SetCurrentPage(Page::Mods);
            });
    connect(m_mods_page, &GlassModsPage::SelectedGameChanged, this,
            [this](u64 pid, const QString&) { m_home_page->SetSelectedProgramId(pid); });
    connect(m_mods_page, &GlassModsPage::ModsChanged, this,
            [this] { m_home_page->RefreshSelectedAddons(); });

    UpdateNavStyles();
    UpdateIcons();
}

GlassShell::~GlassShell() = default;

void GlassShell::SetCurrentPage(Page page) {
    if (m_current_page == Page::Settings && page != Page::Settings) {
        m_settings_page->Deactivate(false);
    }

    m_current_page = page;
    m_stack->setCurrentIndex(static_cast<int>(page));

    if (page == Page::Mods) {
        m_mods_page->EnsureStoreLoaded();
    } else if (page == Page::Settings) {
        m_settings_page->Activate();
    }

    UpdateNavStyles();
    emit PageChanged(page);
}

void GlassShell::AttachGameList(QWidget* game_list) {
    if (!game_list) {
        return;
    }
    if (m_library_host_layout->indexOf(game_list) < 0) {
        m_library_host_layout->addWidget(game_list, 1);
    }
    game_list->show();
}

void GlassShell::DetachGameList(QWidget* game_list) {
    if (!game_list) {
        return;
    }
    m_library_host_layout->removeWidget(game_list);
}

void GlassShell::SetClassicMenus(const QList<QMenu*>& menus) {
    m_classic_menu->clear();
    for (QMenu* menu : menus) {
        if (menu) {
            m_classic_menu->addAction(menu->menuAction());
        }
    }
}

void GlassShell::SetLibraryViewMode(Settings::GameListMode mode) {
    m_syncing_view_mode = true;
    const int idx = mode == Settings::GameListMode::GridView      ? 1
                    : mode == Settings::GameListMode::CarouselView ? 2
                                                                   : 0;
    m_library_mode_control->SetCurrentIndex(idx);
    m_syncing_view_mode = false;
}

void GlassShell::SetGames(QList<GameEntry> games) {
    m_home_page->SetGames(games);
    m_mods_page->SetGames(std::move(games));
}

void GlassShell::SetRunningState(bool running, bool paused, u64 program_id) {
    m_home_page->SetRunningState(running, paused, program_id);
    m_mods_page->SetEmulationRunning(running);
}

void GlassShell::RefreshTheme() {
    m_tokens = ActiveTokens();
    m_library_mode_control->SetTokens(m_tokens);
    m_home_page->RefreshTheme();
    m_mods_page->RefreshTheme();
    m_multiplayer_page->RefreshTheme();
    m_settings_page->RefreshTheme();
    m_palette->RefreshTheme();
    UpdateNavStyles();
    UpdateIcons();
}

void GlassShell::UpdateNavStyles() {
    const std::array<const char*, 5> icons{"play", "grid", "puzzle", "users", "sliders"};
    for (int i = 0; i < 5; ++i) {
        auto* btn = m_nav_buttons[static_cast<size_t>(i)];
        const bool active = static_cast<int>(m_current_page) == i;
        btn->setProperty("active", active);
        btn->setIcon(TintedIcon(QString::fromLatin1(icons[static_cast<size_t>(i)]),
                                active ? m_tokens.accent_soft : m_tokens.ink_muted, 15));
        if (auto* st = btn->style()) {
            st->unpolish(btn);
            st->polish(btn);
        }
    }
}

void GlassShell::UpdateIcons() {
    m_brand_icon->setPixmap(MakeBrandBadge(m_tokens));
    m_add_btn->setIcon(TintedIcon(QStringLiteral("plus"), m_tokens.ink_full, 16));
    m_menu_btn->setIcon(TintedIcon(QStringLiteral("list"), m_tokens.ink_full, 16));
    m_library_filter_btn->setIcon(TintedIcon(QStringLiteral("sliders"), m_tokens.ink_full, 14));
    m_library_refresh_btn->setIcon(TintedIcon(QStringLiteral("refresh"), m_tokens.ink_full, 14));
}

} // namespace GlassUi
