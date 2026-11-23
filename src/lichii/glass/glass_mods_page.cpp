// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_mods_page.h"

#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QVBoxLayout>

#include "lichii/glass/glass_widgets.h"

namespace GlassUi {
namespace {

void ClearWidgets(QLayout* layout) {
    if (!layout) {
        return;
    }
    while (QLayoutItem* item = layout->takeAt(0)) {
        if (QWidget* w = item->widget()) {
            w->deleteLater();
        } else if (QLayout* child = item->layout()) {
            ClearWidgets(child);
        }
        delete item;
    }
}

} // namespace

GlassModsPage::GlassModsPage(QWidget* parent) : QWidget(parent), m_tokens(ActiveTokens()) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(24, 18, 24, 18);
    root->setSpacing(14);

    // ── Header Bar ───────────────────────────────────────────────────────
    auto* header = new QHBoxLayout;
    header->setSpacing(12);

    m_title_label = new QLabel(tr("Mods"), this);
    m_title_label->setProperty("glassPageTitle", true);
    header->addWidget(m_title_label);

    m_detected_badge = new QLabel(this);
    m_detected_badge->setProperty("glassBadge", QStringLiteral("live"));
    header->addWidget(m_detected_badge);

    header->addSpacing(8);

    m_sub_tabs = new GlassSegmentedControl(this);
    m_sub_tabs->SetSegments({tr("Browse (Auto-Detect)"), tr("Installed")});
    header->addWidget(m_sub_tabs);

    header->addStretch(1);

    auto* game_lbl = new QLabel(tr("Library Game:"), this);
    game_lbl->setProperty("muted", true);
    m_game_selector = new QComboBox(this);
    m_game_selector->setMinimumWidth(220);
    m_game_selector->setSizeAdjustPolicy(QComboBox::AdjustToMinimumContentsLengthWithIcon);
    m_game_selector->setMinimumContentsLength(20);
    header->addWidget(game_lbl);
    header->addWidget(m_game_selector);

    root->addLayout(header);

    // ── Stacked Sub-Views ────────────────────────────────────────────────
    m_stack = new QStackedWidget(this);

    // Sub-tab 0: Browse (Auto-Detect)
    m_browse_container = new QWidget(m_stack);
    m_browse_layout = new QVBoxLayout(m_browse_container);
    m_browse_layout->setContentsMargins(0, 0, 0, 0);
    m_browse_layout->setSpacing(0);

    m_store_placeholder = new GlassCard(m_browse_container, 16);
    auto* ph_layout = new QVBoxLayout(m_store_placeholder);
    ph_layout->setContentsMargins(28, 28, 28, 28);
    auto* ph_lbl = new QLabel(tr("Opening Mod Store…"), m_store_placeholder);
    ph_lbl->setProperty("muted", true);
    ph_lbl->setAlignment(Qt::AlignCenter);
    ph_layout->addWidget(ph_lbl);
    m_browse_layout->addWidget(m_store_placeholder, 1);

    m_stack->addWidget(m_browse_container);

    // Sub-tab 1: Installed Mods & Add-ons
    auto* installed_page = new QWidget(m_stack);
    auto* installed_root = new QVBoxLayout(installed_page);
    installed_root->setContentsMargins(0, 0, 0, 0);
    installed_root->setSpacing(12);

    m_installed_toolbar_card = new GlassCard(installed_page, 14);
    auto* tb_layout = new QHBoxLayout(m_installed_toolbar_card);
    tb_layout->setContentsMargins(14, 10, 14, 10);
    tb_layout->setSpacing(8);

    m_installed_search = new QLineEdit(m_installed_toolbar_card);
    m_installed_search->setPlaceholderText(tr("Filter installed mods, patches, updates, DLCs…"));
    m_installed_search->setClearButtonEnabled(true);

    m_install_zip_btn = new QPushButton(tr("Install .zip…"), m_installed_toolbar_card);
    m_install_zip_btn->setProperty("glassRole", QStringLiteral("primary"));
    m_install_zip_btn->setCursor(Qt::PointingHandCursor);

    m_install_folder_btn = new QPushButton(tr("Install Folder…"), m_installed_toolbar_card);
    m_install_folder_btn->setCursor(Qt::PointingHandCursor);

    m_open_folder_btn = new QPushButton(tr("Open Mod Folder"), m_installed_toolbar_card);
    m_open_folder_btn->setCursor(Qt::PointingHandCursor);

    m_refresh_btn = new QPushButton(tr("Refresh"), m_installed_toolbar_card);
    m_refresh_btn->setCursor(Qt::PointingHandCursor);

    tb_layout->addWidget(m_installed_search, 1);
    tb_layout->addWidget(m_install_zip_btn);
    tb_layout->addWidget(m_install_folder_btn);
    tb_layout->addWidget(m_open_folder_btn);
    tb_layout->addWidget(m_refresh_btn);
    installed_root->addWidget(m_installed_toolbar_card);

    auto* scroll = new QScrollArea(installed_page);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setObjectName(QStringLiteral("glassModsViewport"));
    scroll->viewport()->setStyleSheet(
        QStringLiteral("QWidget#glassModsViewport { background: transparent; }"));

    auto* list_host = new QWidget(scroll);
    list_host->setObjectName(QStringLiteral("glassModsHost"));
    list_host->setStyleSheet(
        QStringLiteral("QWidget#glassModsHost { background: transparent; }"));
    m_installed_list_layout = new QVBoxLayout(list_host);
    m_installed_list_layout->setContentsMargins(0, 0, 4, 0);
    m_installed_list_layout->setSpacing(8);
    m_installed_list_layout->addStretch();
    scroll->setWidget(list_host);

    installed_root->addWidget(scroll, 1);
    m_stack->addWidget(installed_page);

    root->addWidget(m_stack, 1);

    // ── Connections ──────────────────────────────────────────────────────
    connect(m_sub_tabs, &GlassSegmentedControl::CurrentIndexChanged, this, [this](int index) {
        m_stack->setCurrentIndex(index);
        if (index == 0) {
            EnsureStoreLoaded();
        } else {
            RefreshInstalled();
        }
    });

    connect(m_game_selector, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (index < 0) {
            return;
        }
        const u64 pid = m_game_selector->currentData().toULongLong();
        if (pid == m_selected_pid) {
            return;
        }
        m_selected_pid = pid;
        const QString tid = FormatTitleId(pid);
        if (m_store_widget && !tid.isEmpty()) {
            m_store_widget->SetSelectedTitleId(tid);
        }
        RebuildInstalledList();
        emit SelectedGameChanged(pid, tid);
    });

    connect(m_installed_search, &QLineEdit::textChanged, this,
            [this] { RebuildInstalledList(); });
    connect(m_install_zip_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = CurrentGame()) {
            emit InstallModZipRequested(g->program_id);
        }
    });
    connect(m_install_folder_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = CurrentGame()) {
            emit InstallModFolderRequested(g->program_id);
        }
    });
    connect(m_open_folder_btn, &QPushButton::clicked, this, [this] {
        if (const auto g = CurrentGame()) {
            emit OpenModFolderRequested(g->program_id, g->path);
        }
    });
    connect(m_refresh_btn, &QPushButton::clicked, this, [this] {
        RefreshInstalled();
        if (m_store_widget) {
            m_store_widget->RefreshAll();
        }
    });

    UpdateDetectedBadge();
    UpdateIcons();
    RebuildInstalledList();
}

void GlassModsPage::SetStoreContextFactory(StoreContextFactory factory) {
    m_store_factory = std::move(factory);
}

void GlassModsPage::SetAddonLoader(AddonLoader loader) {
    m_addon_loader = std::move(loader);
    RebuildInstalledList();
}

void GlassModsPage::SetGames(QList<GameEntry> games) {
    m_games = std::move(games);
    bool still_valid = false;
    for (const auto& g : m_games) {
        if (m_selected_pid != 0 && g.program_id == m_selected_pid) {
            still_valid = true;
            break;
        }
    }
    if (!still_valid) {
        m_selected_pid = m_games.isEmpty() ? 0 : m_games.first().program_id;
    }

    RebuildGameSelector();
    UpdateDetectedBadge();
    if (m_store_widget) {
        m_store_widget->SetDetectedGames(ToDetectedGames(m_games));
    }
    RebuildInstalledList();
}

void GlassModsPage::SetSelectedGame(u64 program_id) {
    if (program_id == 0) {
        return;
    }
    m_selected_pid = program_id;
    const QSignalBlocker block(m_game_selector);
    const int idx = m_game_selector->findData(qulonglong{program_id});
    if (idx >= 0) {
        m_game_selector->setCurrentIndex(idx);
    }
    if (m_store_widget) {
        m_store_widget->SetSelectedTitleId(FormatTitleId(program_id));
    }
    RebuildInstalledList();
}

void GlassModsPage::SetEmulationRunning(bool running) {
    m_emulation_running = running;
    m_install_zip_btn->setEnabled(!running && CurrentGame().has_value());
    m_install_folder_btn->setEnabled(!running && CurrentGame().has_value());
    RebuildInstalledList();
}

void GlassModsPage::EnsureStoreLoaded() {
    if (m_store_widget || !m_store_factory) {
        return;
    }
    auto ctx = m_store_factory(FormatTitleId(m_selected_pid));
    if (ctx.detected_games.isEmpty()) {
        ctx.detected_games = ToDetectedGames(m_games);
    }
    if (m_store_placeholder) {
        m_browse_layout->removeWidget(m_store_placeholder);
        m_store_placeholder->deleteLater();
        m_store_placeholder = nullptr;
    }
    m_store_widget = new ModStore::Widget(std::move(ctx), m_browse_container, true);
    m_browse_layout->addWidget(m_store_widget, 1);

    connect(m_store_widget, &ModStore::Widget::ModsChanged, this, [this] {
        RebuildInstalledList();
        emit ModsChanged();
    });
    connect(m_store_widget, &ModStore::Widget::SelectedGameChanged, this,
            [this](const QString& title_id_hex) {
                if (title_id_hex.isEmpty()) {
                    return;
                }
                for (const auto& g : m_games) {
                    if (g.title_id_hex.compare(title_id_hex, Qt::CaseInsensitive) == 0) {
                        if (m_selected_pid != g.program_id) {
                            m_selected_pid = g.program_id;
                            const QSignalBlocker block(m_game_selector);
                            const int idx = m_game_selector->findData(qulonglong{g.program_id});
                            if (idx >= 0) {
                                m_game_selector->setCurrentIndex(idx);
                            }
                            RebuildInstalledList();
                            emit SelectedGameChanged(g.program_id, g.title_id_hex);
                        }
                        break;
                    }
                }
            });
}

void GlassModsPage::RefreshInstalled() {
    RebuildInstalledList();
}

void GlassModsPage::RefreshTheme() {
    m_tokens = ActiveTokens();
    m_sub_tabs->SetTokens(m_tokens);
    m_installed_toolbar_card->SetTokens(m_tokens);
    if (m_store_placeholder) {
        m_store_placeholder->SetTokens(m_tokens);
    }
    if (m_store_widget) {
        m_store_widget->RefreshTheme();
    }
    UpdateIcons();
    RebuildInstalledList();
}

int GlassModsPage::CurrentSubTab() const {
    return m_sub_tabs->CurrentIndex();
}

void GlassModsPage::SetCurrentSubTab(int index) {
    m_sub_tabs->SetCurrentIndex(index);
}

std::optional<GameEntry> GlassModsPage::CurrentGame() const {
    for (const auto& g : m_games) {
        if (g.program_id == m_selected_pid && m_selected_pid != 0) {
            return g;
        }
    }
    if (!m_games.isEmpty()) {
        return m_games.first();
    }
    return std::nullopt;
}

void GlassModsPage::RebuildGameSelector() {
    const QSignalBlocker block(m_game_selector);
    m_game_selector->clear();
    for (const auto& g : m_games) {
        QIcon icon;
        if (!g.icon.isNull()) {
            icon = QIcon(RoundedPixmap(g.icon, 20, 4.0));
        }
        m_game_selector->addItem(icon, g.title, qulonglong{g.program_id});
    }
    const int idx = m_game_selector->findData(qulonglong{m_selected_pid});
    if (idx >= 0) {
        m_game_selector->setCurrentIndex(idx);
    } else if (m_game_selector->count() > 0) {
        m_game_selector->setCurrentIndex(0);
        m_selected_pid = m_game_selector->currentData().toULongLong();
    }
    const bool has_game = CurrentGame().has_value();
    m_install_zip_btn->setEnabled(!m_emulation_running && has_game);
    m_install_folder_btn->setEnabled(!m_emulation_running && has_game);
    m_open_folder_btn->setEnabled(has_game);
}

void GlassModsPage::UpdateDetectedBadge() {
    if (m_games.isEmpty()) {
        m_detected_badge->setText(tr("No library games"));
        m_detected_badge->setProperty("glassBadge", QStringLiteral("accent"));
    } else {
        m_detected_badge->setText(
            tr("● %n game(s) auto-detected", "", static_cast<int>(m_games.size())));
        m_detected_badge->setProperty("glassBadge", QStringLiteral("live"));
    }
    if (auto* st = m_detected_badge->style()) {
        st->unpolish(m_detected_badge);
        st->polish(m_detected_badge);
    }
}

void GlassModsPage::RebuildInstalledList() {
    ClearWidgets(m_installed_list_layout);

    const auto game = CurrentGame();
    if (!game) {
        auto* card = new GlassCard(this, 16);
        card->SetTokens(m_tokens);
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(24, 24, 24, 24);
        lay->setSpacing(10);
        auto* title = new QLabel(tr("No games in your library yet"), card);
        QFont f = title->font();
        f.setBold(true);
        title->setFont(f);
        auto* desc = new QLabel(
            tr("Add a game directory first so Lichii can auto-detect installed mods, updates, "
               "DLCs, and matching Mod Store releases."),
            card);
        desc->setProperty("muted", true);
        desc->setWordWrap(true);
        lay->addWidget(title);
        lay->addWidget(desc);
        m_installed_list_layout->addWidget(card);
        m_installed_list_layout->addStretch();
        return;
    }

    QList<InstalledAddon> addons;
    if (m_addon_loader && game->program_id != 0) {
        addons = m_addon_loader(game->program_id, game->path);
    }

    const QString query = m_installed_search->text().trimmed();
    QList<InstalledAddon> visible;
    for (const auto& a : addons) {
        if (!query.isEmpty() && !a.name.contains(query, Qt::CaseInsensitive) &&
            !a.category.contains(query, Qt::CaseInsensitive) &&
            !a.version.contains(query, Qt::CaseInsensitive)) {
            continue;
        }
        visible.push_back(a);
    }

    if (visible.isEmpty()) {
        auto* card = new GlassCard(this, 16);
        card->SetTokens(m_tokens);
        auto* lay = new QVBoxLayout(card);
        lay->setContentsMargins(24, 24, 24, 24);
        lay->setSpacing(10);
        auto* title = new QLabel(
            addons.isEmpty() ? tr("No mods or add-ons installed for %1").arg(game->title)
                             : tr("No installed add-ons match \"%1\"").arg(query),
            card);
        QFont f = title->font();
        f.setBold(true);
        title->setFont(f);
        auto* desc = new QLabel(
            tr("Browse auto-detected mods from the Mod Store or install a local .zip / folder."),
            card);
        desc->setProperty("muted", true);
        desc->setWordWrap(true);

        auto* btn_row = new QHBoxLayout;
        btn_row->setSpacing(10);
        auto* browse_btn = new QPushButton(tr("Browse Auto-Detected Mods →"), card);
        browse_btn->setProperty("glassRole", QStringLiteral("primary"));
        browse_btn->setCursor(Qt::PointingHandCursor);
        connect(browse_btn, &QPushButton::clicked, this, [this] { SetCurrentSubTab(0); });

        auto* zip_btn = new QPushButton(tr("Install from .zip…"), card);
        zip_btn->setCursor(Qt::PointingHandCursor);
        zip_btn->setEnabled(!m_emulation_running);
        const u64 pid = game->program_id;
        connect(zip_btn, &QPushButton::clicked, this,
                [this, pid] { emit InstallModZipRequested(pid); });

        btn_row->addWidget(browse_btn);
        btn_row->addWidget(zip_btn);
        btn_row->addStretch();

        lay->addWidget(title);
        lay->addWidget(desc);
        lay->addLayout(btn_row);
        m_installed_list_layout->addWidget(card);
        m_installed_list_layout->addStretch();
        return;
    }

    for (const auto& addon : visible) {
        auto* card = new GlassCard(this, 13);
        card->SetTokens(m_tokens);
        auto* row = new QHBoxLayout(card);
        row->setContentsMargins(16, 12, 16, 12);
        row->setSpacing(12);

        auto* info = new QVBoxLayout;
        info->setSpacing(2);
        auto* name_lbl = new QLabel(addon.name, card);
        QFont nf = name_lbl->font();
        nf.setBold(true);
        name_lbl->setFont(nf);

        const QString meta_text =
            addon.version.isEmpty()
                ? addon.category
                : QStringLiteral("%1 · %2").arg(addon.category, addon.version);
        auto* meta_lbl = new QLabel(meta_text, card);
        meta_lbl->setProperty("muted", true);
        info->addWidget(name_lbl);
        info->addWidget(meta_lbl);
        row->addLayout(info, 1);

        auto* badge = new QLabel(addon.category, card);
        badge->setProperty("glassChip", true);
        row->addWidget(badge);

        if (!addon.location.isEmpty()) {
            auto* open_btn = new QPushButton(card);
            open_btn->setProperty("glassRole", QStringLiteral("icon"));
            open_btn->setIcon(TintedIcon(QStringLiteral("folder"), m_tokens.ink_muted, 15));
            open_btn->setToolTip(tr("Open Mod Folder"));
            open_btn->setFixedSize(32, 32);
            open_btn->setCursor(Qt::PointingHandCursor);
            const QString loc = addon.location;
            connect(open_btn, &QPushButton::clicked, this,
                    [this, loc] { emit OpenLocationRequested(loc); });
            row->addWidget(open_btn);

            auto* del_btn = new QPushButton(card);
            del_btn->setProperty("glassRole", QStringLiteral("icon"));
            del_btn->setIcon(TintedIcon(QStringLiteral("trash"), m_tokens.danger, 15));
            del_btn->setToolTip(tr("Delete Mod"));
            del_btn->setFixedSize(32, 32);
            del_btn->setEnabled(!m_emulation_running);
            del_btn->setCursor(Qt::PointingHandCursor);
            const u64 pid = game->program_id;
            const QString aname = addon.name;
            connect(del_btn, &QPushButton::clicked, this,
                    [this, pid, aname, loc] { emit DeleteAddonRequested(pid, aname, loc); });
            row->addWidget(del_btn);
        }

        if (addon.toggleable) {
            auto* sw = new GlassSwitch(card);
            sw->SetTokens(m_tokens);
            sw->setChecked(addon.enabled);
            sw->setEnabled(!m_emulation_running);
            const u64 pid = game->program_id;
            const QString key = addon.key;
            connect(sw, &QAbstractButton::toggled, this,
                    [this, pid, key](bool checked) { emit AddonToggled(pid, key, checked); });
            row->addWidget(sw);
        }

        m_installed_list_layout->addWidget(card);
    }

    m_installed_list_layout->addStretch();
}

void GlassModsPage::UpdateIcons() {
    m_install_zip_btn->setIcon(TintedIcon(QStringLiteral("download"), m_tokens.on_accent, 15));
    m_install_folder_btn->setIcon(TintedIcon(QStringLiteral("folder"), m_tokens.ink_full, 15));
    m_open_folder_btn->setIcon(TintedIcon(QStringLiteral("folder"), m_tokens.ink_full, 15));
    m_refresh_btn->setIcon(TintedIcon(QStringLiteral("refresh"), m_tokens.ink_full, 15));
}

} // namespace GlassUi
