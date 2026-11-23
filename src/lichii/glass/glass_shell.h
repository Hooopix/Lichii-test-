// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <QList>
#include <QWidget>

#include "common/common_types.h"
#include "common/settings_enums.h"
#include "lichii/glass/glass_game_entry.h"
#include "lichii/glass/glass_tokens.h"

class QLabel;
class QMenu;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace GlassUi {

class GlassCommandPalette;
class GlassHomePage;
class GlassModsPage;
class GlassMultiplayerPage;
class GlassSegmentedControl;
class GlassSettingsPage;

class GlassShell : public QWidget {
    Q_OBJECT

public:
    enum class Page : int {
        Play = 0,
        Library = 1,
        Mods = 2,
        Multiplayer = 3,
        Settings = 4,
    };

    explicit GlassShell(QWidget* parent = nullptr);
    ~GlassShell() override;

    void SetCurrentPage(Page page);
    [[nodiscard]] Page CurrentPage() const {
        return m_current_page;
    }

    void AttachGameList(QWidget* game_list);
    void DetachGameList(QWidget* game_list);
    void SetClassicMenus(const QList<QMenu*>& menus);
    void SetLibraryViewMode(Settings::GameListMode mode);
    void SetGames(QList<GameEntry> games);
    void SetRunningState(bool running, bool paused, u64 program_id);
    void RefreshTheme();

    [[nodiscard]] GlassHomePage* HomePage() const {
        return m_home_page;
    }
    [[nodiscard]] GlassModsPage* ModsPage() const {
        return m_mods_page;
    }
    [[nodiscard]] GlassMultiplayerPage* MultiplayerPage() const {
        return m_multiplayer_page;
    }
    [[nodiscard]] GlassSettingsPage* SettingsPage() const {
        return m_settings_page;
    }
    [[nodiscard]] GlassCommandPalette* CommandPalette() const {
        return m_palette;
    }

signals:
    void PageChanged(Page page);
    void CommandPaletteRequested();
    void AddGameFolderRequested();
    void LibraryViewModeRequested(Settings::GameListMode mode);
    void LibraryRefreshRequested();
    void LibraryFilterToggleRequested();

private:
    void UpdateNavStyles();
    void UpdateIcons();

    PaletteTokens m_tokens;
    Page m_current_page{Page::Play};
    bool m_syncing_view_mode{false};

    // Top bar
    QWidget* m_top_bar{nullptr};
    QLabel* m_brand_icon{nullptr};
    QLabel* m_brand_title{nullptr};
    std::array<QPushButton*, 5> m_nav_buttons{};
    QPushButton* m_search_pill{nullptr};
    QPushButton* m_add_btn{nullptr};
    QToolButton* m_menu_btn{nullptr};
    QMenu* m_classic_menu{nullptr};

    // Pages
    QStackedWidget* m_stack{nullptr};
    GlassHomePage* m_home_page{nullptr};
    QWidget* m_library_page{nullptr};
    GlassSegmentedControl* m_library_mode_control{nullptr};
    QPushButton* m_library_filter_btn{nullptr};
    QPushButton* m_library_refresh_btn{nullptr};
    QVBoxLayout* m_library_host_layout{nullptr};
    GlassModsPage* m_mods_page{nullptr};
    GlassMultiplayerPage* m_multiplayer_page{nullptr};
    GlassSettingsPage* m_settings_page{nullptr};
    GlassCommandPalette* m_palette{nullptr};
};

} // namespace GlassUi
