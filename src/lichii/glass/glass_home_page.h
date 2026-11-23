// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <optional>

#include <QList>
#include <QPoint>
#include <QWidget>

#include "common/common_types.h"
#include "lichii/glass/glass_game_entry.h"
#include "lichii/glass/glass_tokens.h"

class QLabel;
class QLineEdit;
class QListWidget;
class QListWidgetItem;
class QPushButton;
class QStackedWidget;
class QToolButton;
class QVBoxLayout;

namespace GlassUi {

class GlassBackdrop;
class GlassCard;

class GlassHomePage : public QWidget {
    Q_OBJECT

public:
    using AddonLoader = std::function<QList<InstalledAddon>(u64 program_id, const QString& path)>;

    explicit GlassHomePage(QWidget* parent = nullptr);

    void SetAddonLoader(AddonLoader loader);
    void SetGames(QList<GameEntry> games);
    void SetSelectedProgramId(u64 program_id);
    void SetRunningState(bool running, bool paused, u64 running_program_id);
    void SetFooterText(const QString& text);
    void RefreshSelectedAddons();
    void RefreshTheme();

    [[nodiscard]] std::optional<GameEntry> SelectedGame() const;
    [[nodiscard]] const QList<GameEntry>& Games() const {
        return m_games;
    }

signals:
    void SelectedGameChanged(u64 program_id, const QString& title_id_hex);
    void PlayRequested(const QString& path, u64 program_id);
    void PauseResumeRequested();
    void StopRequested();
    void OpenModsRequested(u64 program_id, const QString& title_id_hex);
    void OpenSaveFolderRequested(u64 program_id, const QString& path);
    void OpenModFolderRequested(u64 program_id, const QString& path);
    void OpenShaderCacheRequested(u64 program_id);
    void ConfigureGameRequested(u64 program_id, const QString& path);
    void ToggleFavoriteRequested(u64 program_id);
    void ContextMenuRequested(u64 program_id, const QString& path, const QPoint& global_pos);
    void ReportCompatibilityRequested(u64 program_id);
    void AddGameFolderRequested();
    void InstallKeysRequested();
    void InstallFirmwareRequested();
    void AddonToggled(u64 program_id, const QString& addon_key, bool enabled);

private:
    void RebuildSidebar();
    void UpdateHero();
    void UpdatePlayButtons();
    void RebuildAddonsCard(const GameEntry& game);
    void UpdateIcons();

    PaletteTokens m_tokens;
    AddonLoader m_addon_loader;
    QList<GameEntry> m_games;
    u64 m_selected_pid{};
    QString m_selected_path;
    bool m_running{};
    bool m_paused{};
    u64 m_running_pid{};

    // Sidebar
    QWidget* m_sidebar{nullptr};
    QLineEdit* m_filter_edit{nullptr};
    QListWidget* m_game_list{nullptr};
    QPushButton* m_add_folder_btn{nullptr};
    QLabel* m_footer_label{nullptr};

    // Hero stage
    GlassBackdrop* m_backdrop{nullptr};
    QStackedWidget* m_stage_stack{nullptr};

    // Empty state
    GlassCard* m_empty_card{nullptr};
    QPushButton* m_empty_add_btn{nullptr};
    QPushButton* m_empty_keys_btn{nullptr};
    QPushButton* m_empty_fw_btn{nullptr};

    // Selected game view
    QLabel* m_cover_label{nullptr};
    QLabel* m_eyebrow_label{nullptr};
    QLabel* m_title_label{nullptr};
    QLabel* m_chip_version{nullptr};
    QLabel* m_chip_playtime{nullptr};
    QLabel* m_chip_size{nullptr};
    QLabel* m_chip_mods{nullptr};

    QPushButton* m_play_btn{nullptr};
    QPushButton* m_pause_btn{nullptr};
    QPushButton* m_mods_btn{nullptr};
    QToolButton* m_folders_btn{nullptr};
    QPushButton* m_configure_btn{nullptr};
    QPushButton* m_favorite_btn{nullptr};
    QPushButton* m_more_btn{nullptr};

    GlassCard* m_addons_card{nullptr};
    QVBoxLayout* m_addons_list_layout{nullptr};
    QPushButton* m_browse_mods_link{nullptr};

    GlassCard* m_details_card{nullptr};
    QLabel* m_detail_tid{nullptr};
    QLabel* m_detail_type{nullptr};
    QLabel* m_detail_path{nullptr};
    QPushButton* m_compat_btn{nullptr};
};

} // namespace GlassUi
