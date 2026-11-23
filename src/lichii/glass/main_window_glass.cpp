// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include <algorithm>
#include <filesystem>

#include <fmt/format.h>

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QShortcut>
#include <QStandardPaths>
#include <QStyleHints>
#include <QUrl>

#include "common/fs/path_util.h"
#include "common/scm_rev.h"
#include "common/settings.h"
#include "core/core.h"
#include "core/file_sys/content_archive.h"
#include "core/file_sys/patch_manager.h"
#include "core/hle/service/ns/game_list_applications.h"
#include "core/loader/loader.h"
#include "core/loader/nup.h"
#include "frontend_common/mod_manager.h"
#include "lichii/configuration/addon/mod_select_dialog.h"
#include "lichii/configuration/configuration_shared.h"
#include "lichii/configuration/configure_applets.h"
#include "lichii/configuration/configure_audio.h"
#include "lichii/configuration/configure_cpu.h"
#include "lichii/configuration/configure_debug_tab.h"
#include "lichii/configuration/configure_filesystem.h"
#include "lichii/configuration/configure_general.h"
#include "lichii/configuration/configure_graphics.h"
#include "lichii/configuration/configure_graphics_advanced.h"
#include "lichii/configuration/configure_graphics_extensions.h"
#include "lichii/configuration/configure_hotkeys.h"
#include "lichii/configuration/configure_input.h"
#include "lichii/configuration/configure_network.h"
#include "lichii/configuration/configure_profile_manager.h"
#include "lichii/configuration/configure_system.h"
#include "lichii/configuration/configure_ui.h"
#include "lichii/configuration/configure_web.h"
#include "lichii/configuration/shared_widget.h"
#include "lichii/game/game_list.h"
#include "lichii/glass/glass_command_palette.h"
#include "lichii/glass/glass_game_entry.h"
#include "lichii/glass/glass_home_page.h"
#include "lichii/glass/glass_mods_page.h"
#include "lichii/glass/glass_multiplayer_page.h"
#include "lichii/glass/glass_settings_page.h"
#include "lichii/glass/glass_shell.h"
#include "lichii/glass/glass_tokens.h"
#include "lichii/glass/settings_search.h"
#include "lichii/main_window.h"
#include "lichii/multiplayer/state.h"
#include "qt_common/abstract/frontend.h"
#include "qt_common/config/uisettings.h"
#include "qt_common/util/mod.h"
#include "qt_common/util/path.h"
#include "ui_main.h"

namespace {

u64 ParseHexTitleId(const QString& title_id) {
    if (title_id.size() != 16) {
        return 0;
    }
    bool ok = false;
    const u64 value = title_id.toULongLong(&ok, 16);
    return ok ? value : 0;
}

class LichiiSettingsTabsProvider final : public GlassUi::SettingsTabsProvider {
public:
    LichiiSettingsTabsProvider(QWidget* parent, Core::System& system_, HotkeyRegistry& registry_,
                               InputCommon::InputSubsystem* input_subsystem,
                               std::vector<VkDeviceInfo::Record>& vk_device_records,
                               bool enable_web_config,
                               std::function<void(const QString&)> on_language_changed,
                               std::function<void()> on_external_dirs_changed,
                               std::function<void()> on_reset_requested)
        : system(system_), registry(registry_),
          builder(std::make_unique<ConfigurationShared::Builder>(parent, !system_.IsPoweredOn())),
          applets_tab(std::make_unique<ConfigureApplets>(system_, nullptr, *builder, parent)),
          audio_tab(std::make_unique<ConfigureAudio>(system_, nullptr, *builder, parent)),
          cpu_tab(std::make_unique<ConfigureCpu>(system_, nullptr, *builder, parent)),
          debug_tab_tab(std::make_unique<ConfigureDebugTab>(system_, parent)),
          filesystem_tab(std::make_unique<ConfigureFilesystem>(parent)),
          general_tab(std::make_unique<ConfigureGeneral>(system_, nullptr, *builder, parent)),
          graphics_advanced_tab(
              std::make_unique<ConfigureGraphicsAdvanced>(system_, nullptr, *builder, parent)),
          graphics_extensions_tab(
              std::make_unique<ConfigureGraphicsExtensions>(system_, nullptr, *builder, parent)),
          ui_tab(std::make_unique<ConfigureUi>(system_, parent)),
          graphics_tab(std::make_unique<ConfigureGraphics>(
              system_, vk_device_records,
              [ga = graphics_advanced_tab.get()] { ga->ExposeComputeOption(); },
              [ui_ptr = ui_tab.get()](Settings::AspectRatio ratio, Settings::ResolutionSetup setup) {
                  ui_ptr->UpdateScreenshotInfo(ratio, setup);
              },
              nullptr, *builder, parent)),
          hotkeys_tab(std::make_unique<ConfigureHotkeys>(system_.HIDCore(), parent)),
          input_tab(std::make_unique<ConfigureInput>(system_, parent)),
          network_tab(std::make_unique<ConfigureNetwork>(system_, parent)),
          profile_tab(std::make_unique<ConfigureProfileManager>(system_, parent)),
          system_tab(std::make_unique<ConfigureSystem>(system_, nullptr, *builder, parent)),
          web_tab(std::make_unique<ConfigureWeb>(parent)) {
        Settings::SetConfiguringGlobal(true);
        web_tab->SetWebServiceConfigEnabled(enable_web_config);
        hotkeys_tab->Populate(registry);
        input_tab->Initialize(input_subsystem);

        if (on_reset_requested) {
            general_tab->SetResetCallback(std::move(on_reset_requested));
        }
        if (on_language_changed) {
            QObject::connect(ui_tab.get(), &ConfigureUi::LanguageChanged, parent,
                             std::move(on_language_changed));
        }
        if (on_external_dirs_changed) {
            QObject::connect(general_tab.get(), &ConfigureGeneral::ExternalContentDirsChanged,
                             parent, std::move(on_external_dirs_changed));
        }

        const std::array<std::pair<QWidget*, const char*>, 15> tab_icons{{
            {general_tab.get(), "sliders"},
            {hotkeys_tab.get(), "keyboard"},
            {ui_tab.get(), "list"},
            {web_tab.get(), "cloud"},
            {debug_tab_tab.get(), "bug"},
            {system_tab.get(), "system"},
            {profile_tab.get(), "user"},
            {network_tab.get(), "globe"},
            {filesystem_tab.get(), "drive"},
            {applets_tab.get(), "apps"},
            {cpu_tab.get(), "cpu"},
            {graphics_tab.get(), "monitor"},
            {graphics_advanced_tab.get(), "layers"},
            {graphics_extensions_tab.get(), "wand"},
            {audio_tab.get(), "audio"},
        }};
        for (const auto& [tab, icon] : tab_icons) {
            tab->setProperty("tabIcon", QString::fromLatin1(icon));
            ConfigurationShared::UseToggles(tab);
        }
        for (auto* const tab : input_tab->GetSubTabs()) {
            tab->setProperty("tabIcon", QStringLiteral("gamepad"));
        }
    }

    QList<QWidget*> TabsForSection(const QString& section_key) const override {
        if (section_key == QStringLiteral("general")) {
            return {general_tab.get(), hotkeys_tab.get(), ui_tab.get(), web_tab.get(),
                    debug_tab_tab.get()};
        }
        if (section_key == QStringLiteral("system")) {
            return {system_tab.get(), profile_tab.get(), network_tab.get(), filesystem_tab.get(),
                    applets_tab.get()};
        }
        if (section_key == QStringLiteral("cpu")) {
            return {cpu_tab.get()};
        }
        if (section_key == QStringLiteral("graphics")) {
            return {graphics_tab.get(), graphics_advanced_tab.get(), graphics_extensions_tab.get()};
        }
        if (section_key == QStringLiteral("audio")) {
            return {audio_tab.get()};
        }
        if (section_key == QStringLiteral("controls")) {
            return input_tab->GetSubTabs();
        }
        return {};
    }

    QIcon TabIcon(QWidget* tab) const override {
        return ConfigurationShared::PageIcon(tab->property("tabIcon").toString());
    }

    QScrollArea* WrapTab(QWidget* tab) override {
        return ConfigurationShared::ScrollTab(tab);
    }

    GlassUi::TabSearchResult SearchTab(QWidget* tab, const QString& query,
                                       QList<QWidget*>& highlighted_out) override {
        return GlassUi::SearchConfigTab(tab, query, highlighted_out);
    }

    void ClearHotkeysFilter() override {
        hotkeys_tab->SetSearchFilter({});
    }

    void ApplyAll() override {
        general_tab->ApplyConfiguration();
        ui_tab->ApplyConfiguration();
        system_tab->ApplyConfiguration();
        profile_tab->ApplyConfiguration();
        filesystem_tab->ApplyConfiguration();
        input_tab->ApplyConfiguration();
        hotkeys_tab->ApplyConfiguration(registry);
        cpu_tab->ApplyConfiguration();
        graphics_tab->ApplyConfiguration();
        graphics_advanced_tab->ApplyConfiguration();
        graphics_extensions_tab->ApplyConfiguration();
        audio_tab->ApplyConfiguration();
        debug_tab_tab->ApplyConfiguration();
        web_tab->ApplyConfiguration();
        network_tab->ApplyConfiguration();
        applets_tab->ApplyConfiguration();
        system.ApplySettings();
        Settings::LogSettings();
    }

    void RestoreInput() override {
        input_tab->RestoreConfiguration();
    }

private:
    Core::System& system;
    HotkeyRegistry& registry;
    std::unique_ptr<ConfigurationShared::Builder> builder;
    std::unique_ptr<ConfigureApplets> applets_tab;
    std::unique_ptr<ConfigureAudio> audio_tab;
    std::unique_ptr<ConfigureCpu> cpu_tab;
    std::unique_ptr<ConfigureDebugTab> debug_tab_tab;
    std::unique_ptr<ConfigureFilesystem> filesystem_tab;
    std::unique_ptr<ConfigureGeneral> general_tab;
    std::unique_ptr<ConfigureGraphicsAdvanced> graphics_advanced_tab;
    std::unique_ptr<ConfigureGraphicsExtensions> graphics_extensions_tab;
    std::unique_ptr<ConfigureUi> ui_tab;
    std::unique_ptr<ConfigureGraphics> graphics_tab;
    std::unique_ptr<ConfigureHotkeys> hotkeys_tab;
    std::unique_ptr<ConfigureInput> input_tab;
    std::unique_ptr<ConfigureNetwork> network_tab;
    std::unique_ptr<ConfigureProfileManager> profile_tab;
    std::unique_ptr<ConfigureSystem> system_tab;
    std::unique_ptr<ConfigureWeb> web_tab;
};

} // namespace

void MainWindow::SetUpGlassUi() {
    if (glass_shell != nullptr) {
        return;
    }

    glass_shell = new GlassUi::GlassShell(this);
    ui->horizontalLayout->addWidget(glass_shell);

    glass_shell->SetClassicMenus({ui->menu_File, ui->menu_Emulation, ui->menu_View,
                                  ui->menu_Multiplayer, ui->menu_Tools, ui->menu_Help});
    glass_shell->SetLibraryViewMode(UISettings::values.game_list_mode.GetValue());

    const QString version_str =
        Common::g_build_version[0] != '\0'
            ? QStringLiteral("Lichii %1").arg(QString::fromUtf8(Common::g_build_version))
            : QStringLiteral("Lichii");
    glass_shell->HomePage()->SetFooterText(version_str);

    // ── Home Page ────────────────────────────────────────────────────────
    auto* home = glass_shell->HomePage();
    home->SetAddonLoader([this](u64 pid, const QString& path) {
        return LoadGameAddonsForGlass(pid, path);
    });
    connect(home, &GlassUi::GlassHomePage::PlayRequested, this,
            [this](const QString& path, u64 pid) { OnGameListLoadFile(path, pid); });
    connect(home, &GlassUi::GlassHomePage::PauseResumeRequested, this,
            &MainWindow::OnPauseContinueGame);
    connect(home, &GlassUi::GlassHomePage::StopRequested, this, &MainWindow::OnStopGame);
    connect(home, &GlassUi::GlassHomePage::OpenSaveFolderRequested, this,
            [this](u64 pid, const QString& path) {
                OnGameListOpenFolder(pid, GameListOpenTarget::SaveData, path.toStdString());
            });
    connect(home, &GlassUi::GlassHomePage::OpenModFolderRequested, this,
            [this](u64 pid, const QString& path) {
                OnGameListOpenFolder(pid, GameListOpenTarget::ModData, path.toStdString());
            });
    connect(home, &GlassUi::GlassHomePage::OpenShaderCacheRequested, this,
            [this](u64 pid) { QtCommon::Path::OpenShaderCache(pid, this); });
    connect(home, &GlassUi::GlassHomePage::ConfigureGameRequested, this,
            [this](u64 pid, const QString& path) {
                OnGameListOpenPerGameProperties(pid, path.toStdString());
            });
    connect(home, &GlassUi::GlassHomePage::ToggleFavoriteRequested, this, [this](u64 pid) {
        game_list->ToggleFavorite(pid);
        RefreshGlassGames();
    });
    connect(home, &GlassUi::GlassHomePage::ContextMenuRequested, this,
            [this](u64 pid, const QString& path, const QPoint& global_pos) {
                QMenu menu(this);
                game_list->AddGamePopup(menu, pid, path.toStdString());
                menu.exec(global_pos);
                RefreshGlassGames();
            });
    connect(home, &GlassUi::GlassHomePage::ReportCompatibilityRequested, this, [this](u64 pid) {
        if (report_session && report_session->program_id == pid && !emulation_running &&
            !boot_in_progress && !close_pending) {
            CompatibilityReporting::Dialog dialog(*report_session, this);
            dialog.exec();
        } else {
            OnMenuReportCompatibility();
        }
    });
    connect(home, &GlassUi::GlassHomePage::AddGameFolderRequested, this,
            &MainWindow::OnGameListAddDirectory);
    connect(home, &GlassUi::GlassHomePage::InstallKeysRequested, this,
            &MainWindow::OnInstallDecryptionKeys);
    connect(home, &GlassUi::GlassHomePage::InstallFirmwareRequested, this,
            &MainWindow::OnInstallFirmwareFromZIP);
    connect(home, &GlassUi::GlassHomePage::AddonToggled, this,
            [this](u64 pid, const QString& key, bool enabled) {
                if (SetGameAddonEnabledForGlass(pid, key, enabled)) {
                    glass_shell->HomePage()->RefreshSelectedAddons();
                    glass_shell->ModsPage()->RefreshInstalled();
                }
            });

    // ── Dedicated Mods Page ("onglet Mods") ──────────────────────────────
    auto* mods = glass_shell->ModsPage();
    mods->SetStoreContextFactory([this](const QString& initial_tid) {
        return BuildModStoreContext(ParseHexTitleId(initial_tid), {}, nullptr);
    });
    mods->SetAddonLoader([this](u64 pid, const QString& path) {
        return LoadGameAddonsForGlass(pid, path);
    });
    connect(mods, &GlassUi::GlassModsPage::AddonToggled, this,
            [this](u64 pid, const QString& key, bool enabled) {
                if (SetGameAddonEnabledForGlass(pid, key, enabled)) {
                    glass_shell->HomePage()->RefreshSelectedAddons();
                    glass_shell->ModsPage()->RefreshInstalled();
                }
            });
    connect(mods, &GlassUi::GlassModsPage::InstallModZipRequested, this,
            &MainWindow::InstallModZipForGlass);
    connect(mods, &GlassUi::GlassModsPage::InstallModFolderRequested, this,
            &MainWindow::InstallModFolderForGlass);
    connect(mods, &GlassUi::GlassModsPage::OpenModFolderRequested, this,
            [this](u64 pid, const QString& path) {
                OnGameListOpenFolder(pid, GameListOpenTarget::ModData, path.toStdString());
            });
    connect(mods, &GlassUi::GlassModsPage::DeleteAddonRequested, this,
            &MainWindow::DeleteAddonForGlass);
    connect(mods, &GlassUi::GlassModsPage::OpenLocationRequested, this,
            [](const QString& loc) { QDesktopServices::openUrl(QUrl::fromLocalFile(loc)); });
    connect(mods, &GlassUi::GlassModsPage::ModsChanged, this, [this] {
        OnGameListRefresh();
        RefreshGlassGames();
    });

    // ── Multiplayer Page ─────────────────────────────────────────────────
    auto* mp = glass_shell->MultiplayerPage();
    connect(mp, &GlassUi::GlassMultiplayerPage::BrowseLobbyRequested, multiplayer_state,
            &MultiplayerState::OnViewLobby);
    connect(mp, &GlassUi::GlassMultiplayerPage::CreateRoomRequested, multiplayer_state,
            &MultiplayerState::OnCreateRoom);
    connect(mp, &GlassUi::GlassMultiplayerPage::DirectConnectRequested, multiplayer_state,
            &MultiplayerState::OnDirectConnectToRoom);
    connect(mp, &GlassUi::GlassMultiplayerPage::ShowRoomRequested, multiplayer_state,
            &MultiplayerState::OnOpenNetworkRoom);
    connect(mp, &GlassUi::GlassMultiplayerPage::LeaveRoomRequested, multiplayer_state,
            &MultiplayerState::OnCloseRoom);
    connect(multiplayer_state, &MultiplayerState::NetworkStateChanged, this,
            [this](const Network::RoomMember::State& state) {
                if (glass_shell != nullptr) {
                    glass_shell->MultiplayerPage()->SetConnectionState(state);
                }
            });

    // ── Settings Page ────────────────────────────────────────────────────
    auto* settings = glass_shell->SettingsPage();
    settings->SetTabsProviderFactory(
        [this](QWidget* parent) -> std::unique_ptr<GlassUi::SettingsTabsProvider> {
            return std::make_unique<LichiiSettingsTabsProvider>(
                parent, *QtCommon::system, hotkey_registry, input_subsystem.get(),
                vk_device_records, !multiplayer_state->IsHostingPublicRoom(),
                [this](const QString& locale) { OnLanguageChanged(locale); },
                [this] { OnGameListRefresh(); },
                [this] {
                    glass_shell->SetCurrentPage(GlassUi::GlassShell::Page::Play);
                    OnConfigure();
                });
        });
    connect(settings, &GlassUi::GlassSettingsPage::AppearanceChanged, this, [this] {
        UpdateUITheme();
        if (config) {
            config->SaveAllValues();
        }
    });
    connect(settings, &GlassUi::GlassSettingsPage::ShellModeChangeRequested, this,
            [this](bool /*use_glass*/) {
                UpdateUITheme();
                SyncGlassShellVisibility();
                if (config) {
                    config->SaveAllValues();
                }
            });
    connect(settings, &GlassUi::GlassSettingsPage::ConfigurationApplied, this, [this] {
        InitializeHotkeys();
        UpdateUITheme();
        SyncGlassShellVisibility();
        if (config) {
            config->SaveAllValues();
        }
        emit UpdateThemedIcons();
        if (UISettings::values.is_game_list_reload_pending.exchange(false)) {
            game_list->PopulateAsync(UISettings::values.game_dirs);
        }
    });

    // ── Shell Chrome & Shortcuts ─────────────────────────────────────────
    connect(glass_shell, &GlassUi::GlassShell::CommandPaletteRequested, this,
            &MainWindow::OpenGlassCommandPalette);
    connect(glass_shell, &GlassUi::GlassShell::AddGameFolderRequested, this,
            &MainWindow::OnGameListAddDirectory);
    connect(glass_shell, &GlassUi::GlassShell::LibraryViewModeRequested, this,
            &MainWindow::SetGameListMode);
    connect(glass_shell, &GlassUi::GlassShell::LibraryRefreshRequested, this,
            &MainWindow::OnGameListRefresh);
    connect(glass_shell, &GlassUi::GlassShell::LibraryFilterToggleRequested, this, [this] {
        ui->action_Show_Filter_Bar->trigger();
        game_list->SetFilterFocus();
    });

    connect(game_list, &GameList::PopulatingCompleted, this, &MainWindow::RefreshGlassGames);

    auto* palette_shortcut = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_K), this);
    palette_shortcut->setContext(Qt::WindowShortcut);
    connect(palette_shortcut, &QShortcut::activated, this, &MainWindow::OpenGlassCommandPalette);

#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    if (auto* hints = QGuiApplication::styleHints()) {
        connect(hints, &QStyleHints::colorSchemeChanged, this, [this] {
            if (UISettings::IsGlassShellEnabled() || UISettings::values.theme == "auto") {
                UpdateUITheme();
            }
        });
    }
#endif

    SyncGlassShellVisibility();
    RefreshGlassGames();
}

void MainWindow::SyncGlassShellVisibility() {
    if (glass_shell == nullptr) {
        return;
    }

    const bool use_glass = UISettings::IsGlassShellEnabled();
    const bool single_window_running =
        emulation_running && ui->action_Single_Window_Mode->isChecked();

    if (use_glass) {
        glass_shell->AttachGameList(game_list);
        game_list_placeholder->hide();
        if (single_window_running) {
            glass_shell->SettingsPage()->Deactivate(false);
            glass_shell->hide();
            ui->menubar->show();
        } else {
            ui->menubar->hide();
            glass_shell->show();
            glass_shell->SettingsPage()->SyncAppearanceFromSettings();
        }
    } else {
        glass_shell->SettingsPage()->Deactivate(false);
        glass_shell->hide();
        glass_shell->DetachGameList(game_list);
        if (ui->horizontalLayout->indexOf(game_list) < 0) {
            ui->horizontalLayout->insertWidget(0, game_list);
        }
        ui->menubar->show();
        if (!single_window_running) {
            const bool empty = game_list->IsEmpty();
            game_list->setVisible(!empty);
            game_list_placeholder->setVisible(empty);
        }
    }
}

void MainWindow::RefreshGlassGames() {
    if (glass_shell == nullptr || game_list == nullptr) {
        return;
    }
    auto entries = GlassUi::CollectGameEntries(game_list->GetModel());
    glass_shell->SetGames(std::move(entries));
}

void MainWindow::OpenGlassCommandPalette() {
    if (glass_shell == nullptr || emulation_running) {
        return;
    }

    const auto tokens = GlassUi::ActiveTokens();
    QList<GlassUi::CommandItem> items;

    // 1. Discovered library games
    for (const auto& game : glass_shell->HomePage()->Games()) {
        QIcon icon = !game.icon.isNull()
                         ? QIcon(GlassUi::RoundedPixmap(game.icon, 24, 6.0))
                         : GlassUi::TintedIcon(QStringLiteral("gamepad"), tokens.ink_muted, 18);
        const QString path = game.path;
        const u64 pid = game.program_id;
        items.push_back({
            .title = tr("Play %1").arg(game.title),
            .subtitle = game.title_id_hex.isEmpty() ? game.file_type : game.title_id_hex,
            .category = tr("Game"),
            .keywords = QStringLiteral("play launch boot %1 %2").arg(game.title, game.title_id_hex),
            .icon = std::move(icon),
            .action = [this, path, pid] { OnGameListLoadFile(path, pid); },
        });
    }

    // 2. Top-bar pages
    const auto add_page = [&](const QString& title, const QString& sub, const char* icon,
                              GlassUi::GlassShell::Page page) {
        items.push_back({
            .title = title,
            .subtitle = sub,
            .category = tr("Navigate"),
            .keywords = QStringLiteral("tab page go open %1").arg(title),
            .icon = GlassUi::TintedIcon(QString::fromLatin1(icon), tokens.accent_soft, 18),
            .action =
                [this, page] {
                    if (!UISettings::IsGlassShellEnabled()) {
                        UISettings::values.use_glass_shell = true;
                        UpdateUITheme();
                        SyncGlassShellVisibility();
                    }
                    glass_shell->SetCurrentPage(page);
                },
        });
    };
    add_page(tr("Play Stage"), tr("Open Hero Play & game sidebar"), "play",
             GlassUi::GlassShell::Page::Play);
    add_page(tr("Library"), tr("Browse full Tree, Grid, and Carousel game list"), "grid",
             GlassUi::GlassShell::Page::Library);
    add_page(tr("Mods"), tr("Auto-detecting Mod Store & installed add-ons"), "puzzle",
             GlassUi::GlassShell::Page::Mods);
    add_page(tr("Multiplayer"), tr("Public lobbies, hosting, and direct connect"), "users",
             GlassUi::GlassShell::Page::Multiplayer);
    add_page(tr("Settings"), tr("Appearance, graphics, controls, hotkeys, audio"), "sliders",
             GlassUi::GlassShell::Page::Settings);

    // 3. Settings sections
    const auto add_setting_section = [&](const QString& name, const QString& key,
                                         const char* icon) {
        items.push_back({
            .title = tr("Settings · %1").arg(name),
            .subtitle = tr("Jump directly to %1 settings").arg(name),
            .category = tr("Settings"),
            .keywords = QStringLiteral("configure preferences options %1").arg(name),
            .icon = GlassUi::TintedIcon(QString::fromLatin1(icon), tokens.ink_muted, 18),
            .action =
                [this, key] {
                    if (UISettings::IsGlassShellEnabled()) {
                        glass_shell->SetCurrentPage(GlassUi::GlassShell::Page::Settings);
                        glass_shell->SettingsPage()->SelectSection(key);
                    } else {
                        OnConfigure();
                    }
                },
        });
    };
    add_setting_section(tr("Appearance"), QStringLiteral("appearance"), "sparkles");
    add_setting_section(tr("General"), QStringLiteral("general"), "sliders");
    add_setting_section(tr("Hotkeys"), QStringLiteral("hotkeys"), "keyboard");
    add_setting_section(tr("System"), QStringLiteral("system"), "system");
    add_setting_section(tr("CPU"), QStringLiteral("cpu"), "cpu");
    add_setting_section(tr("Graphics"), QStringLiteral("graphics"), "monitor");
    add_setting_section(tr("Audio"), QStringLiteral("audio"), "audio");
    add_setting_section(tr("Controls"), QStringLiteral("controls"), "gamepad");
    add_setting_section(tr("Debug"), QStringLiteral("debug"), "bug");

    // 4. Folders
    const auto add_folder = [&](const QString& title, const char* icon, auto slot) {
        items.push_back({
            .title = title,
            .subtitle = tr("Open in system file manager"),
            .category = tr("Folder"),
            .keywords = QStringLiteral("directory path open folder %1").arg(title),
            .icon = GlassUi::TintedIcon(QString::fromLatin1(icon), tokens.ink_muted, 18),
            .action = [this, slot] { (this->*slot)(); },
        });
    };
    add_folder(tr("Open Mod Folder"), "puzzle", &MainWindow::OnOpenModFolder);
    add_folder(tr("Open NAND Folder"), "hdd", &MainWindow::OnOpenNANDFolder);
    add_folder(tr("Open SDMC Folder"), "sd", &MainWindow::OnOpenSDMCFolder);
    add_folder(tr("Open Log Folder"), "file", &MainWindow::OnOpenLogFolder);
    add_folder(tr("Open Root Data Folder"), "folder", &MainWindow::OnOpenRootDataFolder);

    // 5. Quick Actions
    items.push_back({
        .title = tr("Add Game Folder"),
        .subtitle = tr("Scan a new directory for games"),
        .category = tr("Action"),
        .keywords = QStringLiteral("add scan directory library"),
        .icon = GlassUi::TintedIcon(QStringLiteral("plus"), tokens.accent_soft, 18),
        .action = [this] { OnGameListAddDirectory(); },
    });
    items.push_back({
        .title = UISettings::IsGlassShellEnabled() ? tr("Switch to Classic UI")
                                                   : tr("Switch to Glass UI"),
        .subtitle = tr("Toggle between Horizon Glass shell and Classic interface"),
        .category = tr("Action"),
        .keywords = QStringLiteral("theme classic glass shell interface"),
        .icon = GlassUi::TintedIcon(QStringLiteral("sparkles"), tokens.ink_muted, 18),
        .action =
            [this] {
                UISettings::values.use_glass_shell = !UISettings::values.use_glass_shell.GetValue();
                UpdateUITheme();
                SyncGlassShellVisibility();
                if (config) {
                    config->SaveAllValues();
                }
            },
    });
    items.push_back({
        .title = UISettings::values.ui_performance_mode.GetValue()
                     ? tr("Disable Performance Mode (Enable Blur)")
                     : tr("Enable Performance Mode (Solid Plates)"),
        .subtitle = tr("Toggle backdrop blur and switch animations"),
        .category = tr("Action"),
        .keywords = QStringLiteral("blur animation performance mode glass"),
        .icon = GlassUi::TintedIcon(QStringLiteral("wand"), tokens.ink_muted, 18),
        .action =
            [this] {
                UISettings::values.ui_performance_mode =
                    !UISettings::values.ui_performance_mode.GetValue();
                UpdateUITheme();
                if (config) {
                    config->SaveAllValues();
                }
            },
    });
    items.push_back({
        .title = tr("Toggle Performance Overlay"),
        .subtitle = tr("Show or hide FPS, frame time, and FSR 4 / DLSS overlay"),
        .category = tr("Action"),
        .keywords = QStringLiteral("fps overlay hud stats"),
        .icon = GlassUi::TintedIcon(QStringLiteral("monitor"), tokens.ink_muted, 18),
        .action = [this] { ui->action_Show_Performance_Overlay->trigger(); },
    });
    items.push_back({
        .title = tr("Report a Bug"),
        .subtitle = tr("Create a diagnostic bug bundle"),
        .category = tr("Action"),
        .keywords = QStringLiteral("bug report log issue"),
        .icon = GlassUi::TintedIcon(QStringLiteral("bug"), tokens.ink_muted, 18),
        .action = [this] { OnReportBug(); },
    });
    items.push_back({
        .title = tr("About Lichii"),
        .subtitle = tr("Build version, commit, and copyable build info"),
        .category = tr("Action"),
        .keywords = QStringLiteral("about version info build"),
        .icon = GlassUi::TintedIcon(QStringLiteral("info"), tokens.ink_muted, 18),
        .action = [this] { OnAbout(); },
    });

    auto* palette = glass_shell->CommandPalette();
    palette->SetItems(std::move(items));
    palette->OpenCentered();
}

ModStore::DialogContext MainWindow::BuildModStoreContext(u64 initial_title_id,
                                                         const std::string& file_name,
                                                         QSet<QString>* store_toggled) {
    auto detected = GlassUi::ToDetectedGames(GlassUi::CollectGameEntries(game_list->GetModel()));
    const auto lookup = [this, initial_title_id, file_name](const QString& title_id)
        -> ModStore::GameIdentity {
        auto& system = *QtCommon::system;
        const auto id = ParseHexTitleId(title_id);
        if (id == 0) {
            return {};
        }

        const auto open_game = [&](FileSys::VirtualFile file) -> std::unique_ptr<Loader::AppLoader> {
            auto loader = Loader::GetLoader(system, std::move(file), id);
            u64 actual_id{};
            return loader && loader->ReadProgramId(actual_id) == Loader::ResultStatus::Success &&
                           actual_id == id
                       ? std::move(loader)
                       : nullptr;
        };
        const auto open_path = [&](const std::string& path) {
            return open_game(Core::GetGameFileFromPath(QtCommon::vfs, path));
        };
        const bool has_selected_path = id == initial_title_id && !file_name.empty();
        std::unique_ptr<Loader::AppLoader> loader;
        if (has_selected_path) {
            loader = open_path(file_name);
        } else {
            const auto path = Service::NS::GetGameListApplicationPath(system, id);
            if (path) {
                loader = open_path(*path);
            }
            if (!loader) {
                const auto* const model = game_list->GetModel();
                const auto matches =
                    model->match(model->index(0, 0), GameListItemPath::ProgramIdRole,
                                 QVariant::fromValue(qulonglong{id}), -1,
                                 Qt::MatchExactly | Qt::MatchRecursive);
                for (const auto& match : matches) {
                    const auto candidate =
                        match.data(GameListItemPath::FullPathRole).toString().toStdString();
                    if (!candidate.empty() && (!path || *path != candidate)) {
                        loader = open_path(candidate);
                        if (loader) {
                            break;
                        }
                    }
                }
            }
            if (!loader) {
                loader = open_game(system.GetContentProvider().GetEntryUnparsed(
                    id, FileSys::ContentRecordType::Program));
            }
        }
        if (!loader) {
            return {};
        }

        ModStore::GameIdentity result{.installed = true};
        if (const auto build = loader->ReadMainBuildIdHex(system);
            build && build->find_first_not_of('0') != std::string::npos) {
            result.build_id = QString::fromStdString(*build);
        }
        const bool updatable = loader->IsRomFSUpdatable();
        const bool native_nx = updatable && loader->GetFileType() == Loader::FileType::NX;
        std::string version;
        if (!native_nx && loader->ReadDisplayVersion(version) == Loader::ResultStatus::Success) {
            result.display_version = QString::fromStdString(version).trimmed();
        }
        if (result.display_version.isEmpty() && updatable) {
            const FileSys::PatchManager patch{id, system.GetFileSystemController(),
                                             system.GetContentProvider()};
            if (const auto control = patch.GetControlMetadata().first) {
                result.display_version =
                    QString::fromStdString(control->GetVersionString()).trimmed();
            }
        }
        return result;
    };

    return ModStore::DialogContext{
        .base_url = QString::fromStdString(UISettings::values.mod_store_api_url.GetValue()),
        .initial_title_id =
            initial_title_id == 0
                ? QString{}
                : QString::fromStdString(fmt::format("{:016X}", initial_title_id)),
        .mods_root = QDir(QString::fromStdString(
                              Common::FS::GetEdenPathString(Common::FS::EdenPath::LoadDir)))
                         .absolutePath(),
        .detected_games = std::move(detected),
        .auto_load = true,
        .game_identity = lookup,
        .is_enabled = [](const QString& title_id, const QString& addon) {
            const auto id = ParseHexTitleId(title_id);
            if (id == 0 || addon.isEmpty()) {
                return false;
            }
            const auto& disabled = Settings::values.disabled_addons;
            const auto it = disabled.find(id);
            return it == disabled.end() ||
                   std::ranges::find(it->second, addon.toStdString()) == it->second.end();
        },
        .set_enabled = [this, initial_title_id, store_toggled](const QString& title_id,
                                                               const QString& addon, bool enabled) {
            const auto id = ParseHexTitleId(title_id);
            if (id == 0 || addon.isEmpty() || QtCommon::system->IsPoweredOn() ||
                !config->SetAddonEnabled(id, addon.toStdString(), enabled)) {
                return false;
            }
            if (store_toggled != nullptr && id == initial_title_id) {
                store_toggled->insert(addon);
            }
            return true;
        },
    };
}

QList<GlassUi::InstalledAddon> MainWindow::LoadGameAddonsForGlass(u64 program_id,
                                                                  const QString& path) {
    QList<GlassUi::InstalledAddon> out;
    if (program_id == 0 || !QtCommon::system || !QtCommon::vfs) {
        return out;
    }

    auto& system = *QtCommon::system;
    const auto v_file = Core::GetGameFileFromPath(QtCommon::vfs, path.toStdString());
    FileSys::VirtualFile update_raw;
    if (v_file) {
        if (const auto loader = Loader::GetLoader(system, v_file, program_id)) {
            loader->ReadUpdateRaw(update_raw);
            if (loader->GetFileType() == Loader::FileType::NUP) {
                const auto& nup = static_cast<const Loader::AppLoader_NUP&>(*loader);
                if (const auto version = nup.ReadContainerUpdateVersion()) {
                    out.push_back({
                        .key = QStringLiteral("Update (NUP)"),
                        .name = tr("Update (NUP)"),
                        .version = QString::fromStdString(*version),
                        .category = tr("Update"),
                        .location = {},
                        .enabled = true,
                        .toggleable = false,
                        .is_update = true,
                    });
                }
            }
        }
    }

    const FileSys::PatchManager pm{program_id, system.GetFileSystemController(),
                                   system.GetContentProvider()};
    const auto& disabled = Settings::values.disabled_addons[program_id];

    for (const auto& patch : pm.GetPatches(update_raw)) {
        const QString name = QString::fromStdString(patch.name);
        const bool is_external_update = patch.type == FileSys::PatchType::Update &&
                                        patch.source == FileSys::PatchSource::External &&
                                        patch.numeric_version != 0;
        const QString key =
            is_external_update ? QStringLiteral("Update@%1").arg(patch.numeric_version) : name;

        bool patch_disabled = false;
        if (patch.off_by_default) {
            patch_disabled = !patch.enabled;
        } else {
            patch_disabled =
                std::ranges::find(disabled, key.toStdString()) != disabled.end();
        }

        const QString category =
            patch.type == FileSys::PatchType::Update ? tr("Update")
            : patch.type == FileSys::PatchType::DLC  ? tr("DLC")
                                                     : tr("Mod");

        out.push_back({
            .key = key,
            .name = name,
            .version = QString::fromStdString(patch.version),
            .category = category,
            .location = patch.type == FileSys::PatchType::Mod
                            ? QString::fromStdString(patch.location)
                            : QString{},
            .enabled = !patch_disabled,
            .toggleable = true,
            .is_update = patch.type == FileSys::PatchType::Update,
        });
    }

    return out;
}

bool MainWindow::SetGameAddonEnabledForGlass(u64 program_id, const QString& addon_key,
                                             bool enabled) {
    if (program_id == 0 || addon_key.isEmpty() || QtCommon::system->IsPoweredOn() || !config) {
        return false;
    }
    if (!config->SetAddonEnabled(program_id, addon_key.toStdString(), enabled)) {
        return false;
    }
    UISettings::values.is_game_list_reload_pending.exchange(true);
    return true;
}

void MainWindow::InstallModZipForGlass(u64 program_id) {
    if (program_id == 0 || QtCommon::system->IsPoweredOn()) {
        return;
    }
    const auto path = QtCommon::Frontend::GetOpenFileName(
        tr("Zipped Mod Location"),
        QStandardPaths::writableLocation(QStandardPaths::DownloadLocation),
        tr("Zipped Archives (*.zip)"));
    if (path.isEmpty()) {
        return;
    }
    const QString extracted = QtCommon::Mod::ExtractMod(path);
    if (extracted.isEmpty()) {
        return;
    }
    const auto mods = QtCommon::Mod::GetModFolders(extracted, QFileInfo(path).baseName());
    auto install_list = [this, program_id](const QStringList& list) {
        QStringList failed;
        for (const auto& mod : list) {
            if (FrontendCommon::InstallMod(mod.toStdString(), program_id, true) ==
                FrontendCommon::Failed) {
                failed << QFileInfo(mod).baseName();
            }
        }
        if (failed.empty()) {
            QtCommon::Frontend::Information(tr("Mod Install Succeeded"),
                                            tr("Successfully installed all selected mods."));
            glass_shell->ModsPage()->RefreshInstalled();
            glass_shell->HomePage()->RefreshSelectedAddons();
            OnGameListRefresh();
        } else {
            QtCommon::Frontend::Critical(
                tr("Mod Install Failed"),
                tr("Failed to install the following mods:\n\t%1").arg(failed.join(QStringLiteral("\n\t"))));
        }
    };
    if (mods.size() > 1) {
        auto* dialog = new ModSelectDialog(mods, this);
        connect(dialog, &ModSelectDialog::modsSelected, this, install_list);
        dialog->show();
    } else if (!mods.isEmpty()) {
        install_list(mods);
    }
}

void MainWindow::InstallModFolderForGlass(u64 program_id) {
    if (program_id == 0 || QtCommon::system->IsPoweredOn()) {
        return;
    }
    const auto path = QtCommon::Frontend::GetExistingDirectory(
        tr("Mod Folder"), QStandardPaths::writableLocation(QStandardPaths::DownloadLocation));
    if (path.isEmpty()) {
        return;
    }
    const auto mods = QtCommon::Mod::GetModFolders(path, QFileInfo(path).fileName());
    auto install_list = [this, program_id](const QStringList& list) {
        QStringList failed;
        for (const auto& mod : list) {
            if (FrontendCommon::InstallMod(mod.toStdString(), program_id, true) ==
                FrontendCommon::Failed) {
                failed << QFileInfo(mod).baseName();
            }
        }
        if (failed.empty()) {
            QtCommon::Frontend::Information(tr("Mod Install Succeeded"),
                                            tr("Successfully installed all selected mods."));
            glass_shell->ModsPage()->RefreshInstalled();
            glass_shell->HomePage()->RefreshSelectedAddons();
            OnGameListRefresh();
        } else {
            QtCommon::Frontend::Critical(
                tr("Mod Install Failed"),
                tr("Failed to install the following mods:\n\t%1").arg(failed.join(QStringLiteral("\n\t"))));
        }
    };
    if (mods.size() > 1) {
        auto* dialog = new ModSelectDialog(mods, this);
        connect(dialog, &ModSelectDialog::modsSelected, this, install_list);
        dialog->show();
    } else if (!mods.isEmpty()) {
        install_list(mods);
    }
}

void MainWindow::DeleteAddonForGlass(u64 program_id, const QString& addon_name,
                                     const QString& location) {
    if (program_id == 0 || location.isEmpty() || QtCommon::system->IsPoweredOn()) {
        return;
    }
    const auto choice = QMessageBox::warning(
        this, tr("Delete Mod"),
        tr("Delete \"%1\" permanently from disk? This cannot be undone.").arg(addon_name),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
    if (choice != QMessageBox::Yes) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove_all(std::filesystem::path(location.toStdU16String()), ec);
    if (ec) {
        QMessageBox::critical(this, tr("Delete Mod"),
                              tr("Could not delete \"%1\".").arg(addon_name));
        return;
    }
    glass_shell->ModsPage()->RefreshInstalled();
    glass_shell->HomePage()->RefreshSelectedAddons();
    OnGameListRefresh();
}
