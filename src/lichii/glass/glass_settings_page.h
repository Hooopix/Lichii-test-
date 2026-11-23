// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <functional>
#include <memory>

#include <QHash>
#include <QIcon>
#include <QList>
#include <QSet>
#include <QString>
#include <QWidget>

#include "lichii/glass/glass_tokens.h"

class QComboBox;
class QLabel;
class QLineEdit;
class QPushButton;
class QScrollArea;
class QStackedWidget;
class QTabWidget;

namespace GlassUi {

class GlassCard;
class GlassSegmentedControl;
class GlassSwitch;

struct TabSearchResult;

class SettingsTabsProvider {
public:
    virtual ~SettingsTabsProvider() = default;
    [[nodiscard]] virtual QList<QWidget*> TabsForSection(const QString& section_key) const = 0;
    [[nodiscard]] virtual QIcon TabIcon(QWidget* tab) const = 0;
    [[nodiscard]] virtual QScrollArea* WrapTab(QWidget* tab) = 0;
    virtual TabSearchResult SearchTab(QWidget* tab, const QString& query,
                                      QList<QWidget*>& highlighted_out) = 0;
    virtual void ClearHotkeysFilter() = 0;
    virtual void ApplyAll() = 0;
    virtual void RestoreInput() = 0;
};

class GlassSettingsPage : public QWidget {
    Q_OBJECT

public:
    using TabsProviderFactory = std::function<std::unique_ptr<SettingsTabsProvider>(QWidget* parent)>;

    explicit GlassSettingsPage(QWidget* parent = nullptr);
    ~GlassSettingsPage() override;

    void SetTabsProviderFactory(TabsProviderFactory factory);
    void Activate();
    void Deactivate(bool apply_pending = false);
    void SelectSection(const QString& key);
    void FocusSearch();
    void SyncAppearanceFromSettings();
    void RefreshTheme();

    [[nodiscard]] QString CurrentSection() const {
        return m_current_section;
    }

signals:
    void AppearanceChanged();
    void ShellModeChangeRequested(bool use_glass);
    void ConfigurationApplied();

private:
    struct RailEntry {
        QString key;
        QString label;
        QString icon_name;
        QPushButton* button{nullptr};
    };

    void BuildAppearancePage();
    void EnsureProvider();
    void DestroyProvider(bool restore_input);
    void PopulateCategoryTabs(const QString& section_key);
    void ApplySearchFilter(const QString& query);
    void ScrollToFirstMatch();
    void UpdateRailStyles();
    void UpdateAccentSwatches();
    void ApplyPendingConfiguration();
    void DiscardPendingConfiguration();

    PaletteTokens m_tokens;
    TabsProviderFactory m_provider_factory;
    std::unique_ptr<SettingsTabsProvider> m_provider;
    bool m_active{false};
    bool m_syncing_appearance{false};
    QString m_current_section{QStringLiteral("appearance")};
    QString m_active_query;
    QSet<QWidget*> m_matched_tabs;
    QHash<QWidget*, QWidget*> m_first_match_by_tab;
    QList<QWidget*> m_highlighted_widgets;
    QHash<QWidget*, QScrollArea*> m_scroll_areas;

    // Rail
    QWidget* m_rail{nullptr};
    QList<RailEntry> m_rail_entries;

    // Header + content
    QLineEdit* m_search_edit{nullptr};
    QLabel* m_search_status{nullptr};
    QPushButton* m_discard_btn{nullptr};
    QPushButton* m_apply_btn{nullptr};
    QStackedWidget* m_stack{nullptr};

    // Page 0: Appearance
    GlassCard* m_appearance_card{nullptr};
    GlassCard* m_classic_theme_card{nullptr};
    GlassSegmentedControl* m_shell_control{nullptr};
    GlassSegmentedControl* m_color_mode_control{nullptr};
    GlassSwitch* m_perf_switch{nullptr};
    QComboBox* m_classic_theme_combo{nullptr};
    QList<std::pair<QPushButton*, QString>> m_accent_buttons;
    QPushButton* m_custom_accent_btn{nullptr};
    QLabel* m_accent_hex_label{nullptr};

    // Page 1: Emulator Config Tabs
    GlassCard* m_config_card{nullptr};
    QTabWidget* m_tab_widget{nullptr};
};

} // namespace GlassUi
