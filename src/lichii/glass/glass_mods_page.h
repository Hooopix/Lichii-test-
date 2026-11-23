// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>

#include <QList>
#include <QWidget>

#include "common/common_types.h"
#include "lichii/glass/glass_game_entry.h"
#include "lichii/glass/glass_tokens.h"
#include "lichii/mod_store/mod_store_dialog.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QStackedWidget;
class QVBoxLayout;

namespace GlassUi {

class GlassCard;
class GlassSegmentedControl;

class GlassModsPage : public QWidget {
    Q_OBJECT

public:
    using StoreContextFactory = std::function<ModStore::DialogContext(const QString& initial_tid)>;
    using AddonLoader = std::function<QList<InstalledAddon>(u64 program_id, const QString& path)>;

    explicit GlassModsPage(QWidget* parent = nullptr);

    void SetStoreContextFactory(StoreContextFactory factory);
    void SetAddonLoader(AddonLoader loader);
    void SetGames(QList<GameEntry> games);
    void SetSelectedGame(u64 program_id);
    void SetEmulationRunning(bool running);
    void EnsureStoreLoaded();
    void RefreshInstalled();
    void RefreshTheme();

    [[nodiscard]] int CurrentSubTab() const;
    void SetCurrentSubTab(int index);
    [[nodiscard]] ModStore::Widget* StoreWidget() const {
        return m_store_widget;
    }

signals:
    void SelectedGameChanged(u64 program_id, const QString& title_id_hex);
    void AddonToggled(u64 program_id, const QString& addon_key, bool enabled);
    void InstallModZipRequested(u64 program_id);
    void InstallModFolderRequested(u64 program_id);
    void OpenModFolderRequested(u64 program_id, const QString& path);
    void DeleteAddonRequested(u64 program_id, const QString& addon_name, const QString& location);
    void OpenLocationRequested(const QString& location);
    void ModsChanged();

private:
    void RebuildGameSelector();
    void RebuildInstalledList();
    void UpdateDetectedBadge();
    void UpdateIcons();
    [[nodiscard]] std::optional<GameEntry> CurrentGame() const;

    PaletteTokens m_tokens;
    StoreContextFactory m_store_factory;
    AddonLoader m_addon_loader;
    QList<GameEntry> m_games;
    u64 m_selected_pid{};
    bool m_emulation_running{};

    // Header
    QLabel* m_title_label{nullptr};
    QLabel* m_detected_badge{nullptr};
    GlassSegmentedControl* m_sub_tabs{nullptr};
    QComboBox* m_game_selector{nullptr};

    // Content stack
    QStackedWidget* m_stack{nullptr};

    // Sub-tab 0: Browse (Auto-Detect)
    QWidget* m_browse_container{nullptr};
    QVBoxLayout* m_browse_layout{nullptr};
    ModStore::Widget* m_store_widget{nullptr};
    GlassCard* m_store_placeholder{nullptr};

    // Sub-tab 1: Installed
    GlassCard* m_installed_toolbar_card{nullptr};
    QLineEdit* m_installed_search{nullptr};
    QPushButton* m_install_zip_btn{nullptr};
    QPushButton* m_install_folder_btn{nullptr};
    QPushButton* m_open_folder_btn{nullptr};
    QPushButton* m_refresh_btn{nullptr};
    QVBoxLayout* m_installed_list_layout{nullptr};
};

} // namespace GlassUi
