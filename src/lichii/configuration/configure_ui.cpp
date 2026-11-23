// SPDX-FileCopyrightText: Copyright 2026 Eden Emulator Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/configuration/configure_ui.h"

#include <array>
#include <cstdlib>
#include <set>
#include <string>
#include <utility>

#include <QCheckBox>
#include <QColorDialog>
#include <QComboBox>
#include <QCoreApplication>
#include <QDirIterator>
#include <QFileDialog>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QString>
#include <QToolButton>
#include <QTranslator>
#include <QVariant>
#include <QVBoxLayout>

#include "common/common_types.h"
#include "common/fs/path_util.h"
#include "common/settings.h"
#include "common/settings_enums.h"
#include "core/core.h"
#include "core/frontend/framebuffer_layout.h"
#include "lichii/configuration/iidb_key_dialog.h"
#include "lichii/glass/glass_tokens.h"
#include "ui_configure_ui.h"

#include "qt_common/config/uisettings.h"
#include "qt_common/qt_compat.h"

namespace {

constexpr std::array default_folder_icon_sizes{
    std::make_pair(0, QT_TRANSLATE_NOOP("ConfigureUI", "None")),
    std::make_pair(24, QT_TRANSLATE_NOOP("ConfigureUI", "Small (24x24)")),
    std::make_pair(48, QT_TRANSLATE_NOOP("ConfigureUI", "Standard (48x48)")),
    std::make_pair(72, QT_TRANSLATE_NOOP("ConfigureUI", "Large (72x72)")),
};

// clang-format off
constexpr std::array row_text_names{
    QT_TRANSLATE_NOOP("ConfigureUI", "Filename"),
    QT_TRANSLATE_NOOP("ConfigureUI", "Filetype"),
    QT_TRANSLATE_NOOP("ConfigureUI", "Title ID"),
    QT_TRANSLATE_NOOP("ConfigureUI", "Title Name"),
    QT_TRANSLATE_NOOP("ConfigureUI", "None"),
};
// clang-format on

QString GetTranslatedFolderIconSize(size_t index) {
    return QCoreApplication::translate("ConfigureUI", default_folder_icon_sizes[index].second);
}

QString GetTranslatedRowTextName(size_t index) {
    return QCoreApplication::translate("ConfigureUI", row_text_names[index]);
}

/// A translation's name in the language list, e.g. "German (Germany)" for "de".
QString LanguageName(const QString& locale) {
    const QLocale language(locale);
    return QStringLiteral("%1 (%2)").arg(QLocale::languageToString(language.language()),
                                         QLocale::territoryToString(language.territory()));
}

bool UsesLichiiGameListIcons() {
    if (UISettings::IsGlassShellEnabled()) {
        return true;
    }
    const auto theme = UISettings::EffectiveTheme();
    return theme == "lichii" || theme == "lichii_glass";
}
} // Anonymous namespace

static float GetUpFactor(Settings::ResolutionSetup res_setup) {
    Settings::ResolutionScalingInfo info{};
    Settings::TranslateResolutionInfo(res_setup, info);
    return info.up_factor;
}

static void PopulateResolutionComboBox(QComboBox* screenshot_height, QWidget* parent) {
    screenshot_height->clear();

    const auto& enumeration =
        Settings::EnumMetadata<Settings::ResolutionSetup>::Canonicalizations();
    std::set<u32> resolutions{};
    for (const auto& [name, value] : enumeration) {
        const float up_factor = GetUpFactor(value);
        u32 height_undocked = Layout::ScreenUndocked::Height * up_factor;
        u32 height_docked = Layout::ScreenDocked::Height * up_factor;
        resolutions.emplace(height_undocked);
        resolutions.emplace(height_docked);
    }

    screenshot_height->addItem(parent->tr("Auto", "Screenshot height option"));
    for (const auto res : resolutions) {
        screenshot_height->addItem(QString::fromStdString(std::to_string(res)));
    }
}

static u32 ScreenshotDimensionToInt(const QString& height) {
    return std::strtoul(height.toUtf8(), nullptr, 0);
}

ConfigureUi::ConfigureUi(Core::System& system_, QWidget* parent)
    : QWidget(parent), ui{std::make_unique<Ui::ConfigureUi>()},
      ratio{Settings::values.aspect_ratio.GetValue()},
      resolution_setting{Settings::values.resolution_setup.GetValue()}, system{system_} {
    ui->setupUi(this);
    InitializeAppearanceSection();
    connect(ui->iidb_api_key, &QPushButton::clicked, this, [this] { IiDB::ConfigureKey(this); });

    // Optional SteamGridDB artwork lookup for games without artwork; off while the key is empty.
    {
        auto* const key_layout = new QHBoxLayout;
        auto* const key_label = new QLabel(tr("SteamGridDB API key:"), this);
        steamgriddb_key_edit = new QLineEdit(this);
        steamgriddb_key_edit->setEchoMode(QLineEdit::Password);
        steamgriddb_key_edit->setPlaceholderText(tr("Empty: no online lookup"));
        steamgriddb_key_edit->setClearButtonEnabled(true);
        const QString tooltip =
            tr("Artwork is looked up on steamgriddb.com only when a key is set, for games that "
               "have no icon and no custom artwork. The picture is saved as the game's custom "
               "artwork, so you can still replace it in Configure Game.");
        key_label->setToolTip(tooltip);
        steamgriddb_key_edit->setToolTip(tooltip);
        auto* const key_link = new QLabel(
            tr("<a href=\"%1\">Get a key</a>")
                .arg(QStringLiteral("https://www.steamgriddb.com/profile/preferences/api")),
            this);
        key_link->setOpenExternalLinks(true);
        key_link->setTextInteractionFlags(Qt::TextBrowserInteraction);
        key_layout->addWidget(key_label);
        key_layout->addWidget(steamgriddb_key_edit, 1);
        key_layout->addWidget(key_link);
        ui->GeneralVerticalLayout->addLayout(key_layout);
    }

    InitializeLanguageComboBox();

    for (const auto& theme : UISettings::themes) {
        ui->theme_combobox->addItem(QString::fromUtf8(theme.first),
                                    QString::fromUtf8(theme.second));
    }

    InitializeIconSizeComboBox();
    InitializeRowComboBoxes();

    PopulateResolutionComboBox(ui->screenshot_height, this);

    SetConfiguration();

    // Update text ComboBoxes after user interaction.
    connect(ui->row_1_text_combobox, QOverload<int>::of(&QComboBox::activated),
            [this] { ConfigureUi::UpdateSecondRowComboBox(); });
    connect(ui->row_2_text_combobox, QOverload<int>::of(&QComboBox::activated),
            [this] { ConfigureUi::UpdateFirstRowComboBox(); });

    // Set screenshot path to user specification.
    connect(ui->screenshot_path_button, &QToolButton::clicked, this, [this] {
        auto dir =
            QFileDialog::getExistingDirectory(this, tr("Select Screenshots Path..."),
                                              QString::fromStdString(Common::FS::GetEdenPathString(
                                                  Common::FS::EdenPath::ScreenshotsDir)));
        if (!dir.isEmpty()) {
            if (dir.back() != QChar::fromLatin1('/')) {
                dir.append(QChar::fromLatin1('/'));
            }

            ui->screenshot_path_edit->setText(dir);
        }
    });

    connect(ui->screenshot_height, &QComboBox::currentTextChanged, [this]() { UpdateWidthText(); });

    UpdateWidthText();
}

ConfigureUi::~ConfigureUi() = default;

void ConfigureUi::InitializeAppearanceSection() {
    auto* box = new QGroupBox(tr("Glass Interface & Appearance"), this);
    auto* vbox = new QVBoxLayout(box);
    vbox->setSpacing(8);

    auto* shell_row = new QHBoxLayout;
    shell_row->addWidget(new QLabel(tr("Interface Shell:"), box));
    shell_mode_combobox = new QComboBox(box);
    shell_mode_combobox->addItem(tr("Glass (Horizon Shell)"), true);
    shell_mode_combobox->addItem(tr("Classic (Traditional Menu & Game List)"), false);
    shell_row->addWidget(shell_mode_combobox, 1);
    vbox->addLayout(shell_row);

    auto* mode_row = new QHBoxLayout;
    mode_row->addWidget(new QLabel(tr("Color Mode:"), box));
    color_mode_combobox = new QComboBox(box);
    color_mode_combobox->addItem(tr("System (Auto)"), QStringLiteral("system"));
    color_mode_combobox->addItem(tr("Dark"), QStringLiteral("dark"));
    color_mode_combobox->addItem(tr("Light"), QStringLiteral("light"));
    mode_row->addWidget(color_mode_combobox, 1);
    vbox->addLayout(mode_row);

    auto* accent_row = new QHBoxLayout;
    accent_row->addWidget(new QLabel(tr("Accent Color:"), box));
    for (const auto& preset : GlassUi::AccentPresets) {
        const QString hex = QString::fromLatin1(preset.hex);
        auto* swatch = new QPushButton(box);
        swatch->setFixedSize(26, 26);
        swatch->setToolTip(QString::fromUtf8(preset.name));
        swatch->setCursor(Qt::PointingHandCursor);
        connect(swatch, &QPushButton::clicked, this, [this, hex] {
            selected_accent_hex = hex;
            UpdateAccentButtons();
        });
        accent_swatch_buttons.push_back({swatch, hex});
        accent_row->addWidget(swatch);
    }
    custom_accent_button = new QPushButton(tr("Custom…"), box);
    connect(custom_accent_button, &QPushButton::clicked, this, [this] {
        const QColor chosen =
            QColorDialog::getColor(GlassUi::ParseAccentColor(selected_accent_hex), this,
                                   tr("Choose Accent Color"));
        if (chosen.isValid()) {
            selected_accent_hex = chosen.name();
            UpdateAccentButtons();
        }
    });
    accent_row->addWidget(custom_accent_button);
    accent_row->addStretch();
    vbox->addLayout(accent_row);

    performance_mode_checkbox =
        new QCheckBox(tr("Performance Mode (disable backdrop blur and switch animations)"), box);
    vbox->addWidget(performance_mode_checkbox);

    ui->verticalLayout->insertWidget(0, box);
}

void ConfigureUi::UpdateAccentButtons() {
    for (const auto& [btn, hex] : accent_swatch_buttons) {
        const bool selected = selected_accent_hex.compare(hex, Qt::CaseInsensitive) == 0;
        btn->setStyleSheet(
            QStringLiteral("QPushButton { background: %1; border: %2px solid %3; "
                           "border-radius: 13px; padding: 0; min-height: 0; }")
                .arg(hex)
                .arg(selected ? 3 : 1)
                .arg(selected ? QStringLiteral("#ffffff") : QStringLiteral("rgba(255,255,255,90)")));
    }
}

void ConfigureUi::ApplyConfiguration() {
    const bool old_show_add_ons = UISettings::values.show_add_ons.GetValue();
    const bool old_show_size = UISettings::values.show_size.GetValue();
    const bool old_show_types = UISettings::values.show_types.GetValue();
    const bool old_show_play_time = UISettings::values.show_play_time.GetValue();
    const u32 old_folder_icon_size = UISettings::values.folder_icon_size.GetValue();
    const u32 old_row_1 = UISettings::values.row_1_text_id.GetValue();
    const u32 old_row_2 = UISettings::values.row_2_text_id.GetValue();
    const std::string old_sgdb = UISettings::values.steamgriddb_api_key.GetValue();
    const bool old_lichii_icons = UsesLichiiGameListIcons();

    UISettings::values.use_glass_shell = shell_mode_combobox->currentData().toBool();
    UISettings::values.ui_color_mode =
        color_mode_combobox->currentData().toString().toStdString();
    UISettings::values.ui_performance_mode = performance_mode_checkbox->isChecked();
    if (!selected_accent_hex.isEmpty()) {
        UISettings::values.ui_accent_color = selected_accent_hex.toStdString();
    }

    UISettings::values.theme =
        ui->theme_combobox->itemData(ui->theme_combobox->currentIndex()).toString().toStdString();
    UISettings::values.show_add_ons = ui->show_add_ons->isChecked();
    UISettings::values.show_size = ui->show_size->isChecked();
    UISettings::values.show_types = ui->show_types->isChecked();
    UISettings::values.show_play_time = ui->show_play_time->isChecked();
    UISettings::values.folder_icon_size = ui->folder_icon_size_combobox->currentData().toUInt();
    UISettings::values.row_1_text_id = ui->row_1_text_combobox->currentData().toUInt();
    UISettings::values.row_2_text_id = ui->row_2_text_combobox->currentData().toUInt();
    UISettings::values.steamgriddb_api_key =
        steamgriddb_key_edit->text().trimmed().toStdString();

    UISettings::values.enable_screenshot_save_as = ui->enable_screenshot_save_as->isChecked();
    Common::FS::SetEdenPath(Common::FS::EdenPath::ScreenshotsDir,
                            ui->screenshot_path_edit->text().toStdString());

    const u32 height = ScreenshotDimensionToInt(ui->screenshot_height->currentText());
    UISettings::values.screenshot_height.SetValue(height);

    const bool game_list_changed =
        old_show_add_ons != UISettings::values.show_add_ons.GetValue() ||
        old_show_size != UISettings::values.show_size.GetValue() ||
        old_show_types != UISettings::values.show_types.GetValue() ||
        old_show_play_time != UISettings::values.show_play_time.GetValue() ||
        old_folder_icon_size != UISettings::values.folder_icon_size.GetValue() ||
        old_row_1 != UISettings::values.row_1_text_id.GetValue() ||
        old_row_2 != UISettings::values.row_2_text_id.GetValue() ||
        old_sgdb != UISettings::values.steamgriddb_api_key.GetValue() ||
        old_lichii_icons != UsesLichiiGameListIcons();

    if (game_list_changed) {
        RequestGameListUpdate();
    }
    system.ApplySettings();
}

void ConfigureUi::RequestGameListUpdate() {
    UISettings::values.is_game_list_reload_pending.exchange(true);
}

void ConfigureUi::SetConfiguration() {
    shell_mode_combobox->setCurrentIndex(
        shell_mode_combobox->findData(UISettings::values.use_glass_shell.GetValue()));
    const int mode_idx = color_mode_combobox->findData(
        QString::fromStdString(UISettings::values.ui_color_mode.GetValue()));
    color_mode_combobox->setCurrentIndex(mode_idx >= 0 ? mode_idx : 1);
    performance_mode_checkbox->setChecked(UISettings::values.ui_performance_mode.GetValue());
    selected_accent_hex =
        GlassUi::ParseAccentColor(
            QString::fromStdString(UISettings::values.ui_accent_color.GetValue()))
            .name();
    UpdateAccentButtons();

    steamgriddb_key_edit->setText(
        QString::fromStdString(UISettings::values.steamgriddb_api_key.GetValue()));
    ui->theme_combobox->setCurrentIndex(
        ui->theme_combobox->findData(QString::fromStdString(UISettings::values.theme)));
    const QString language = QString::fromStdString(UISettings::values.language.GetValue());
    if (ui->language_combobox->findData(language) == -1) {
        // A language the list doesn't have, listed so that the setting stays until another one is
        // chosen. It is marked missing only if the main window's load fails too: that load falls
        // back to the base language (de.qm for de_AT), and shows English without one.
        QTranslator probe;
        QString name = LanguageName(language);
        if (!probe.load(language, QStringLiteral(":/languages/"))) {
            name = tr("%1 - not in this build").arg(name);
        }
        ui->language_combobox->addItem(name, language);
    }
    ui->language_combobox->setCurrentIndex(ui->language_combobox->findData(language));
    ui->show_add_ons->setChecked(UISettings::values.show_add_ons.GetValue());
    ui->show_size->setChecked(UISettings::values.show_size.GetValue());
    ui->show_types->setChecked(UISettings::values.show_types.GetValue());
    ui->show_play_time->setChecked(UISettings::values.show_play_time.GetValue());
    ui->folder_icon_size_combobox->setCurrentIndex(
        ui->folder_icon_size_combobox->findData(UISettings::values.folder_icon_size.GetValue()));

    ui->enable_screenshot_save_as->setChecked(
        UISettings::values.enable_screenshot_save_as.GetValue());
    ui->screenshot_path_edit->setText(QString::fromStdString(
        Common::FS::GetEdenPathString(Common::FS::EdenPath::ScreenshotsDir)));

    const auto height = UISettings::values.screenshot_height.GetValue();
    if (height == 0) {
        ui->screenshot_height->setCurrentIndex(0);
    } else {
        ui->screenshot_height->setCurrentText(QStringLiteral("%1").arg(height));
    }
}

void ConfigureUi::changeEvent(QEvent* event) {
    if (event->type() == QEvent::LanguageChange) {
        RetranslateUI();
    }

    QWidget::changeEvent(event);
}

void ConfigureUi::RetranslateUI() {
    ui->retranslateUi(this);

    for (int i = 0; i < ui->folder_icon_size_combobox->count(); i++) {
        ui->folder_icon_size_combobox->setItemText(
            i, GetTranslatedFolderIconSize(static_cast<size_t>(i)));
    }

    // Each row list leaves names out (the first has no None, and neither lists the other row's
    // choice), so an item's name comes from its data, not its position.
    for (QComboBox* const combobox : {ui->row_1_text_combobox, ui->row_2_text_combobox}) {
        for (int i = 0; i < combobox->count(); i++) {
            combobox->setItemText(
                i, GetTranslatedRowTextName(static_cast<size_t>(combobox->itemData(i).toUInt())));
        }
    }
}

void ConfigureUi::InitializeLanguageComboBox() {
    ui->language_combobox->addItem(tr("<System>"), QString{});
    ui->language_combobox->addItem(tr("English"), QStringLiteral("en"));
    QDirIterator it(QStringLiteral(":/languages"), QDirIterator::NoIteratorFlags);
    while (it.hasNext()) {
        QString locale = it.next();
        locale.truncate(locale.lastIndexOf(QLatin1Char{'.'}));
        locale.remove(0, locale.lastIndexOf(QLatin1Char{'/'}) + 1);
        ui->language_combobox->addItem(LanguageName(locale), locale);
    }

    // Unlike other configuration changes, interface language changes need to be reflected on the
    // interface immediately. This is done by passing a signal to the main window, and then
    // retranslating when passing back.
    connect(ui->language_combobox, qOverload<int>(&QComboBox::currentIndexChanged), this,
            &ConfigureUi::OnLanguageChanged);
}

void ConfigureUi::InitializeIconSizeComboBox() {
    for (size_t i = 0; i < default_folder_icon_sizes.size(); i++) {
        const auto size = default_folder_icon_sizes[i].first;
        ui->folder_icon_size_combobox->addItem(GetTranslatedFolderIconSize(i), size);
    }
}

void ConfigureUi::InitializeRowComboBoxes() {
    UpdateFirstRowComboBox(true);
    UpdateSecondRowComboBox(true);
}

void ConfigureUi::UpdateFirstRowComboBox(bool init) {
    const int currentIndex =
        init ? UISettings::values.row_1_text_id.GetValue()
             : ui->row_1_text_combobox->findData(ui->row_1_text_combobox->currentData());

    ui->row_1_text_combobox->clear();

    for (std::size_t i = 0; i < row_text_names.size(); i++) {
        const QString row_text_name = GetTranslatedRowTextName(i);
        ui->row_1_text_combobox->addItem(row_text_name, QVariant::fromValue(i));
    }

    ui->row_1_text_combobox->setCurrentIndex(ui->row_1_text_combobox->findData(currentIndex));

    ui->row_1_text_combobox->removeItem(4); // None
    ui->row_1_text_combobox->removeItem(
        ui->row_1_text_combobox->findData(ui->row_2_text_combobox->currentData()));
}

void ConfigureUi::UpdateSecondRowComboBox(bool init) {
    const int currentIndex =
        init ? UISettings::values.row_2_text_id.GetValue()
             : ui->row_2_text_combobox->findData(ui->row_2_text_combobox->currentData());

    ui->row_2_text_combobox->clear();

    for (std::size_t i = 0; i < row_text_names.size(); ++i) {
        const QString row_text_name = GetTranslatedRowTextName(i);
        ui->row_2_text_combobox->addItem(row_text_name, QVariant::fromValue(i));
    }

    ui->row_2_text_combobox->setCurrentIndex(ui->row_2_text_combobox->findData(currentIndex));

    ui->row_2_text_combobox->removeItem(
        ui->row_2_text_combobox->findData(ui->row_1_text_combobox->currentData()));
}

void ConfigureUi::OnLanguageChanged(int index) {
    if (index == -1)
        return;

    emit LanguageChanged(ui->language_combobox->itemData(index).toString());
}

void ConfigureUi::UpdateWidthText() {
    const u32 height = ScreenshotDimensionToInt(ui->screenshot_height->currentText());
    const u32 width = UISettings::CalculateWidth(height, ratio);
    if (height == 0) {
        const auto up_factor = GetUpFactor(resolution_setting);
        const u32 height_docked = Layout::ScreenDocked::Height * up_factor;
        const u32 width_docked = UISettings::CalculateWidth(height_docked, ratio);
        const u32 height_undocked = Layout::ScreenUndocked::Height * up_factor;
        const u32 width_undocked = UISettings::CalculateWidth(height_undocked, ratio);
        ui->screenshot_width->setText(tr("Auto (%1 x %2, %3 x %4)", "Screenshot width value")
                                          .arg(width_undocked)
                                          .arg(height_undocked)
                                          .arg(width_docked)
                                          .arg(height_docked));
    } else {
        ui->screenshot_width->setText(QStringLiteral("%1 x").arg(width));
    }
}

void ConfigureUi::UpdateScreenshotInfo(Settings::AspectRatio ratio_,
                                       Settings::ResolutionSetup resolution_setting_) {
    ratio = ratio_;
    resolution_setting = resolution_setting_;
    UpdateWidthText();
}
