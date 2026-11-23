// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_settings_page.h"

#include <array>

#include <QAbstractButton>
#include <QColorDialog>
#include <QComboBox>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QSignalBlocker>
#include <QStackedWidget>
#include <QStyle>
#include <QTabWidget>
#include <QTimer>
#include <QVBoxLayout>

#include "lichii/glass/glass_widgets.h"
#include "lichii/glass/settings_search.h"
#include "qt_common/config/uisettings.h"

namespace GlassUi {
namespace {

bool SearchAppearanceSubtree(QWidget* root, const QString& query,
                             QList<QWidget*>& highlighted_out) {
    if (!root) {
        return false;
    }
    const QStringList tokens = query.trimmed().split(QLatin1Char(' '), Qt::SkipEmptyParts);
    if (tokens.isEmpty()) {
        return true;
    }
    bool matched = false;
    for (QWidget* w : root->findChildren<QWidget*>()) {
        if (!w || w->isHidden()) {
            continue;
        }
        QString text;
        if (const auto* lbl = qobject_cast<const QLabel*>(w)) {
            text = lbl->text();
        } else if (const auto* btn = qobject_cast<const QAbstractButton*>(w)) {
            text = btn->text();
        } else if (const auto* box = qobject_cast<const QGroupBox*>(w)) {
            text = box->title();
        } else {
            continue;
        }
        bool all = true;
        for (const QString& tok : tokens) {
            if (!text.contains(tok, Qt::CaseInsensitive)) {
                all = false;
                break;
            }
        }
        if (all) {
            matched = true;
            if (!qobject_cast<QGroupBox*>(w) && !w->property("searchMatch").toBool()) {
                w->setProperty("searchMatch", true);
                if (auto* st = w->style()) {
                    st->unpolish(w);
                    st->polish(w);
                }
                highlighted_out.push_back(w);
            }
        }
    }
    return matched;
}

void ClearHighlightsLocal(QList<QWidget*>& highlighted) {
    for (QWidget* w : highlighted) {
        if (!w) {
            continue;
        }
        w->setProperty("searchMatch", false);
        if (auto* st = w->style()) {
            st->unpolish(w);
            st->polish(w);
        }
        w->update();
    }
    highlighted.clear();
}

} // namespace

GlassSettingsPage::GlassSettingsPage(QWidget* parent)
    : QWidget(parent), m_tokens(ActiveTokens()) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Left Section Rail ────────────────────────────────────────────────
    m_rail = new QWidget(this);
    m_rail->setObjectName(QStringLiteral("glassSidebar"));
    m_rail->setFixedWidth(216);
    auto* rail_layout = new QVBoxLayout(m_rail);
    rail_layout->setContentsMargins(14, 18, 14, 18);
    rail_layout->setSpacing(4);

    auto* rail_header = new QLabel(tr("SETTINGS"), m_rail);
    rail_header->setProperty("glassSectionHeader", true);
    rail_layout->addWidget(rail_header);
    rail_layout->addSpacing(6);

    const std::array<std::tuple<const char*, QString, const char*>, 7> sections{{
        {"appearance", tr("Appearance"), "sparkles"},
        {"general", tr("General"), "sliders"},
        {"system", tr("System"), "system"},
        {"cpu", tr("CPU"), "cpu"},
        {"graphics", tr("Graphics"), "monitor"},
        {"audio", tr("Audio"), "audio"},
        {"controls", tr("Controls"), "gamepad"},
    }};

    for (const auto& [key, label, icon] : sections) {
        const QString key_str = QString::fromLatin1(key);
        auto* btn = new QPushButton(label, m_rail);
        btn->setProperty("glassRole", QStringLiteral("railItem"));
        btn->setCursor(Qt::PointingHandCursor);
        btn->setIconSize(QSize(16, 16));
        connect(btn, &QPushButton::clicked, this, [this, key_str] { SelectSection(key_str); });
        rail_layout->addWidget(btn);
        m_rail_entries.push_back({key_str, label, QString::fromLatin1(icon), btn});
    }
    rail_layout->addStretch(1);
    root->addWidget(m_rail);

    // ── Right Content Column ─────────────────────────────────────────────
    auto* content = new QWidget(this);
    auto* content_layout = new QVBoxLayout(content);
    content_layout->setContentsMargins(28, 20, 28, 20);
    content_layout->setSpacing(14);

    auto* top_row = new QHBoxLayout;
    top_row->setSpacing(12);
    auto* title = new QLabel(tr("Settings"), content);
    title->setProperty("glassPageTitle", true);
    top_row->addWidget(title);

    m_search_edit = new QLineEdit(content);
    m_search_edit->setPlaceholderText(tr("Search settings (Ctrl+F)…"));
    m_search_edit->setClearButtonEnabled(true);
    m_search_edit->setMaximumWidth(360);
    top_row->addWidget(m_search_edit, 1);

    m_search_status = new QLabel(content);
    m_search_status->setProperty("muted", true);
    top_row->addWidget(m_search_status);

    m_discard_btn = new QPushButton(tr("Discard"), content);
    m_discard_btn->setCursor(Qt::PointingHandCursor);
    m_discard_btn->hide();
    m_apply_btn = new QPushButton(tr("Apply Changes"), content);
    m_apply_btn->setProperty("glassRole", QStringLiteral("primary"));
    m_apply_btn->setCursor(Qt::PointingHandCursor);
    m_apply_btn->hide();

    top_row->addWidget(m_discard_btn);
    top_row->addWidget(m_apply_btn);
    content_layout->addLayout(top_row);

    m_stack = new QStackedWidget(content);
    BuildAppearancePage();

    // Page 1: Emulator Config Tab Container
    auto* config_wrapper = new QWidget(m_stack);
    auto* config_wrapper_layout = new QVBoxLayout(config_wrapper);
    config_wrapper_layout->setContentsMargins(0, 0, 0, 0);
    m_config_card = new GlassCard(config_wrapper, 16);
    auto* card_layout = new QVBoxLayout(m_config_card);
    card_layout->setContentsMargins(14, 14, 14, 14);
    m_tab_widget = new QTabWidget(m_config_card);
    card_layout->addWidget(m_tab_widget);
    config_wrapper_layout->addWidget(m_config_card, 1);
    m_stack->addWidget(config_wrapper);

    content_layout->addWidget(m_stack, 1);
    root->addWidget(content, 1);

    connect(m_search_edit, &QLineEdit::textChanged, this, &GlassSettingsPage::ApplySearchFilter);
    connect(m_apply_btn, &QPushButton::clicked, this, &GlassSettingsPage::ApplyPendingConfiguration);
    connect(m_discard_btn, &QPushButton::clicked, this,
            &GlassSettingsPage::DiscardPendingConfiguration);
    connect(m_tab_widget, &QTabWidget::currentChanged, this, [this](int idx) {
        if (idx >= 0 && !m_active_query.isEmpty()) {
            QTimer::singleShot(0, this, &GlassSettingsPage::ScrollToFirstMatch);
        }
    });

    auto* find_sc = new QShortcut(QKeySequence::Find, this);
    find_sc->setContext(Qt::WidgetWithChildrenShortcut);
    connect(find_sc, &QShortcut::activated, this, &GlassSettingsPage::FocusSearch);

    SyncAppearanceFromSettings();
    UpdateRailStyles();
}

GlassSettingsPage::~GlassSettingsPage() {
    DestroyProvider(false);
}

void GlassSettingsPage::BuildAppearancePage() {
    auto* scroll = new QScrollArea(m_stack);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setWidgetResizable(true);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->viewport()->setObjectName(QStringLiteral("glassAppearanceViewport"));
    scroll->viewport()->setStyleSheet(
        QStringLiteral("QWidget#glassAppearanceViewport { background: transparent; }"));

    auto* host = new QWidget(scroll);
    host->setObjectName(QStringLiteral("glassAppearanceHost"));
    host->setStyleSheet(
        QStringLiteral("QWidget#glassAppearanceHost { background: transparent; }"));
    auto* layout = new QVBoxLayout(host);
    layout->setContentsMargins(0, 0, 6, 0);
    layout->setSpacing(16);

    auto* subtitle = new QLabel(
        tr("Customize Lichii's Horizon Glass shell, dark/light polarity, accent color, and "
           "performance profile. Appearance changes apply live."),
        host);
    subtitle->setProperty("muted", true);
    subtitle->setWordWrap(true);
    layout->addWidget(subtitle);

    m_appearance_card = new GlassCard(host, 16);
    auto* card_lay = new QVBoxLayout(m_appearance_card);
    card_lay->setContentsMargins(22, 12, 22, 12);
    card_lay->setSpacing(0);

    const auto add_divider = [&] {
        auto* div = new QFrame(m_appearance_card);
        div->setObjectName(QStringLiteral("glassDivider"));
        card_lay->addWidget(div);
    };

    m_shell_control = new GlassSegmentedControl(m_appearance_card);
    m_shell_control->SetSegments({tr("Glass"), tr("Classic")});
    card_lay->addWidget(new GlassSettingRow(
        tr("Interface Shell"),
        tr("Switch between the Horizon Glass shell and Lichii's classic menu bar & game list."),
        m_shell_control, m_appearance_card));
    add_divider();

    m_color_mode_control = new GlassSegmentedControl(m_appearance_card);
    m_color_mode_control->SetSegments({tr("System"), tr("Dark"), tr("Light")});
    card_lay->addWidget(new GlassSettingRow(
        tr("Color Mode"),
        tr("Follow your operating system theme automatically or lock Dark or Light mode."),
        m_color_mode_control, m_appearance_card));
    add_divider();

    auto* swatch_host = new QWidget(m_appearance_card);
    auto* swatch_row = new QHBoxLayout(swatch_host);
    swatch_row->setContentsMargins(0, 0, 0, 0);
    swatch_row->setSpacing(8);
    for (const auto& preset : AccentPresets) {
        const QString hex = QString::fromLatin1(preset.hex);
        auto* btn = new QPushButton(swatch_host);
        btn->setFixedSize(28, 28);
        btn->setToolTip(QString::fromUtf8(preset.name));
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, hex] {
            UISettings::values.ui_accent_color = hex.toStdString();
            UpdateAccentSwatches();
            emit AppearanceChanged();
        });
        m_accent_buttons.push_back({btn, hex});
        swatch_row->addWidget(btn);
    }
    m_custom_accent_btn = new QPushButton(tr("Custom…"), swatch_host);
    m_custom_accent_btn->setCursor(Qt::PointingHandCursor);
    connect(m_custom_accent_btn, &QPushButton::clicked, this, [this] {
        const QColor current =
            ParseAccentColor(QString::fromStdString(UISettings::values.ui_accent_color.GetValue()));
        const QColor chosen = QColorDialog::getColor(current, this, tr("Choose Accent Color"));
        if (chosen.isValid()) {
            UISettings::values.ui_accent_color = chosen.name().toStdString();
            UpdateAccentSwatches();
            emit AppearanceChanged();
        }
    });
    m_accent_hex_label = new QLabel(swatch_host);
    m_accent_hex_label->setProperty("faint", true);
    swatch_row->addWidget(m_custom_accent_btn);
    swatch_row->addWidget(m_accent_hex_label);

    card_lay->addWidget(new GlassSettingRow(
        tr("Accent Color"),
        tr("Tints active tabs, primary action pills, switches, and ambient backdrop glow."),
        swatch_host, m_appearance_card));
    add_divider();

    m_perf_switch = new GlassSwitch(m_appearance_card);
    card_lay->addWidget(new GlassSettingRow(
        tr("Performance Mode"),
        tr("Replace blurred cover-art backdrops and switch animations with crisp solid plates."),
        m_perf_switch, m_appearance_card));

    layout->addWidget(m_appearance_card);

    m_classic_theme_card = new GlassCard(host, 16);
    auto* classic_lay = new QVBoxLayout(m_classic_theme_card);
    classic_lay->setContentsMargins(22, 12, 22, 12);
    m_classic_theme_combo = new QComboBox(m_classic_theme_card);
    m_classic_theme_combo->setMinimumWidth(220);
    for (const auto& theme : UISettings::themes) {
        m_classic_theme_combo->addItem(QString::fromUtf8(theme.first),
                                       QString::fromUtf8(theme.second));
    }
    classic_lay->addWidget(new GlassSettingRow(
        tr("Classic Shell Theme"),
        tr("Stylesheet theme applied when running in Classic interface mode."),
        m_classic_theme_combo, m_classic_theme_card));
    layout->addWidget(m_classic_theme_card);
    layout->addStretch(1);

    scroll->setWidget(host);
    m_stack->addWidget(scroll);

    connect(m_shell_control, &GlassSegmentedControl::CurrentIndexChanged, this, [this](int index) {
        if (m_syncing_appearance) {
            return;
        }
        const bool use_glass = (index == 0);
        UISettings::values.use_glass_shell = use_glass;
        emit ShellModeChangeRequested(use_glass);
    });
    connect(m_color_mode_control, &GlassSegmentedControl::CurrentIndexChanged, this,
            [this](int index) {
                if (m_syncing_appearance) {
                    return;
                }
                const char* mode = index == 0 ? "system" : index == 2 ? "light" : "dark";
                UISettings::values.ui_color_mode = mode;
                emit AppearanceChanged();
            });
    connect(m_perf_switch, &QAbstractButton::toggled, this, [this](bool checked) {
        if (m_syncing_appearance) {
            return;
        }
        UISettings::values.ui_performance_mode = checked;
        emit AppearanceChanged();
    });
    connect(m_classic_theme_combo, &QComboBox::currentIndexChanged, this, [this](int index) {
        if (m_syncing_appearance || index < 0) {
            return;
        }
        UISettings::values.theme = m_classic_theme_combo->itemData(index).toString().toStdString();
        emit AppearanceChanged();
    });
}

void GlassSettingsPage::SetTabsProviderFactory(TabsProviderFactory factory) {
    m_provider_factory = std::move(factory);
}

void GlassSettingsPage::Activate() {
    m_active = true;
    SyncAppearanceFromSettings();
    if (m_current_section != QStringLiteral("appearance")) {
        EnsureProvider();
        PopulateCategoryTabs(m_current_section);
    }
}

void GlassSettingsPage::Deactivate(bool apply_pending) {
    if (!m_active && !m_provider) {
        return;
    }
    m_active = false;
    if (apply_pending && m_provider) {
        ApplyPendingConfiguration();
    }
    DestroyProvider(!apply_pending);
    m_apply_btn->hide();
    m_discard_btn->hide();
}

void GlassSettingsPage::SelectSection(const QString& key) {
    QString target_section = key;
    QString target_subtab;
    if (key == QStringLiteral("hotkeys")) {
        target_section = QStringLiteral("general");
        target_subtab = QStringLiteral("Hotkeys");
    } else if (key == QStringLiteral("debug")) {
        target_section = QStringLiteral("general");
        target_subtab = QStringLiteral("Debug");
    }

    m_current_section = target_section;
    UpdateRailStyles();

    if (target_section == QStringLiteral("appearance")) {
        m_stack->setCurrentIndex(0);
        m_apply_btn->setVisible(m_provider != nullptr);
        m_discard_btn->setVisible(m_provider != nullptr);
        return;
    }

    EnsureProvider();
    m_stack->setCurrentIndex(1);
    PopulateCategoryTabs(target_section);
    if (!target_subtab.isEmpty()) {
        for (int i = 0; i < m_tab_widget->count(); ++i) {
            if (m_tab_widget->tabText(i).compare(target_subtab, Qt::CaseInsensitive) == 0) {
                m_tab_widget->setCurrentIndex(i);
                break;
            }
        }
    }
    m_apply_btn->setVisible(m_provider != nullptr);
    m_discard_btn->setVisible(m_provider != nullptr);
}

void GlassSettingsPage::FocusSearch() {
    m_search_edit->setFocus();
    m_search_edit->selectAll();
}

void GlassSettingsPage::SyncAppearanceFromSettings() {
    m_syncing_appearance = true;
    m_shell_control->SetCurrentIndex(UISettings::values.use_glass_shell.GetValue() ? 0 : 1);

    const auto mode = QString::fromStdString(UISettings::values.ui_color_mode.GetValue())
                          .trimmed()
                          .toLower();
    m_color_mode_control->SetCurrentIndex(mode == QStringLiteral("system") ? 0
                                          : mode == QStringLiteral("light") ? 2
                                                                            : 1);
    m_perf_switch->setChecked(UISettings::values.ui_performance_mode.GetValue());
    const int theme_idx =
        m_classic_theme_combo->findData(QString::fromStdString(UISettings::values.theme));
    if (theme_idx >= 0) {
        m_classic_theme_combo->setCurrentIndex(theme_idx);
    }
    UpdateAccentSwatches();
    m_syncing_appearance = false;
}

void GlassSettingsPage::RefreshTheme() {
    m_tokens = ActiveTokens();
    m_appearance_card->SetTokens(m_tokens);
    m_classic_theme_card->SetTokens(m_tokens);
    m_config_card->SetTokens(m_tokens);
    m_shell_control->SetTokens(m_tokens);
    m_color_mode_control->SetTokens(m_tokens);
    m_perf_switch->SetTokens(m_tokens);
    UpdateRailStyles();
    UpdateAccentSwatches();
}

void GlassSettingsPage::EnsureProvider() {
    if (m_provider || !m_provider_factory) {
        return;
    }
    m_provider = m_provider_factory(this);
    if (m_provider) {
        m_apply_btn->show();
        m_discard_btn->show();
    }
}

void GlassSettingsPage::DestroyProvider(bool restore_input) {
    ClearHighlightsLocal(m_highlighted_widgets);
    m_matched_tabs.clear();
    m_first_match_by_tab.clear();
    m_tab_widget->clear();

    for (auto it = m_scroll_areas.begin(); it != m_scroll_areas.end(); ++it) {
        if (QScrollArea* area = it.value()) {
            area->takeWidget();
            area->deleteLater();
        }
    }
    m_scroll_areas.clear();

    if (m_provider) {
        if (restore_input) {
            m_provider->RestoreInput();
        }
        m_provider.reset();
    }
}

void GlassSettingsPage::PopulateCategoryTabs(const QString& section_key) {
    const QSignalBlocker block(m_tab_widget);
    m_tab_widget->clear();
    if (!m_provider) {
        return;
    }

    for (QWidget* tab : m_provider->TabsForSection(section_key)) {
        if (!tab) {
            continue;
        }
        if (!m_active_query.isEmpty() && !m_matched_tabs.contains(tab)) {
            continue;
        }
        auto& area = m_scroll_areas[tab];
        if (!area) {
            area = m_provider->WrapTab(tab);
        }
        m_tab_widget->addTab(area, m_provider->TabIcon(tab), tab->accessibleName());
    }
}

void GlassSettingsPage::ApplySearchFilter(const QString& query) {
    ClearHighlightsLocal(m_highlighted_widgets);
    m_matched_tabs.clear();
    m_first_match_by_tab.clear();
    m_active_query = query.trimmed();

    if (m_active_query.isEmpty()) {
        if (m_provider) {
            m_provider->ClearHotkeysFilter();
        }
        for (const auto& entry : m_rail_entries) {
            entry.button->setVisible(true);
        }
        m_search_status->clear();
        if (m_current_section != QStringLiteral("appearance")) {
            PopulateCategoryTabs(m_current_section);
        }
        return;
    }

    EnsureProvider();

    int matched_sections = 0;
    QString first_matching_section;

    for (const auto& entry : m_rail_entries) {
        bool section_matched = false;
        if (entry.key == QStringLiteral("appearance")) {
            section_matched =
                SearchAppearanceSubtree(m_stack->widget(0), m_active_query, m_highlighted_widgets) ||
                entry.label.contains(m_active_query, Qt::CaseInsensitive);
        } else if (m_provider) {
            for (QWidget* tab : m_provider->TabsForSection(entry.key)) {
                const auto res = m_provider->SearchTab(tab, m_active_query, m_highlighted_widgets);
                if (res.matched) {
                    section_matched = true;
                    m_matched_tabs.insert(tab);
                    if (res.first_match) {
                        m_first_match_by_tab.insert(tab, res.first_match);
                    }
                }
            }
        }
        entry.button->setVisible(section_matched);
        if (section_matched) {
            ++matched_sections;
            if (first_matching_section.isEmpty()) {
                first_matching_section = entry.key;
            }
        }
    }

    m_search_status->setText(matched_sections == 0
                                 ? tr("No matching settings")
                                 : tr("%n section(s) matched", "", matched_sections));

    bool current_visible = false;
    for (const auto& entry : m_rail_entries) {
        if (entry.key == m_current_section) {
            current_visible = entry.button->isVisible();
            break;
        }
    }
    if (!current_visible && !first_matching_section.isEmpty()) {
        SelectSection(first_matching_section);
    } else if (m_current_section != QStringLiteral("appearance")) {
        PopulateCategoryTabs(m_current_section);
    }

    QTimer::singleShot(0, this, &GlassSettingsPage::ScrollToFirstMatch);
}

void GlassSettingsPage::ScrollToFirstMatch() {
    if (m_stack->currentIndex() != 1) {
        return;
    }
    auto* const area = qobject_cast<QScrollArea*>(m_tab_widget->currentWidget());
    if (!area || !area->widget()) {
        return;
    }
    if (auto* const target = m_first_match_by_tab.value(area->widget(), nullptr)) {
        area->ensureWidgetVisible(target, 24, 48);
    }
}

void GlassSettingsPage::UpdateRailStyles() {
    for (const auto& entry : m_rail_entries) {
        const bool active = entry.key == m_current_section;
        entry.button->setProperty("active", active);
        entry.button->setIcon(TintedIcon(entry.icon_name,
                                         active ? m_tokens.accent_soft : m_tokens.ink_muted, 16));
        if (auto* st = entry.button->style()) {
            st->unpolish(entry.button);
            st->polish(entry.button);
        }
    }
}

void GlassSettingsPage::UpdateAccentSwatches() {
    const QString current_hex =
        ParseAccentColor(QString::fromStdString(UISettings::values.ui_accent_color.GetValue()))
            .name();
    m_accent_hex_label->setText(current_hex.toUpper());
    for (const auto& [btn, hex] : m_accent_buttons) {
        const bool selected = current_hex.compare(hex, Qt::CaseInsensitive) == 0;
        btn->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; border: %2px solid %3; "
                           "border-radius: 14px; padding: 0; min-height: 0; }")
                .arg(hex)
                .arg(selected ? 3 : 1)
                .arg(selected ? m_tokens.ink_full.name() : RgbaCss(m_tokens.rule_strong)));
    }
}

void GlassSettingsPage::ApplyPendingConfiguration() {
    if (!m_provider) {
        return;
    }
    m_provider->ApplyAll();
    SyncAppearanceFromSettings();
    emit ConfigurationApplied();
}

void GlassSettingsPage::DiscardPendingConfiguration() {
    if (!m_provider) {
        return;
    }
    DestroyProvider(true);
    if (m_current_section != QStringLiteral("appearance")) {
        EnsureProvider();
        PopulateCategoryTabs(m_current_section);
    }
}

} // namespace GlassUi
