// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_tokens.h"

#include <algorithm>
#include <cmath>

#include <QImage>
#include <QPainter>
#include <QPainterPath>

#include "qt_common/config/uisettings.h"

namespace GlassUi {
namespace {

double LinearizeChannel(double channel) {
    return channel <= 0.04045 ? channel / 12.92 : std::pow((channel + 0.055) / 1.055, 2.4);
}

QColor WithAlpha(QColor color, int alpha) {
    color.setAlpha(std::clamp(alpha, 0, 255));
    return color;
}

} // namespace

double RelativeLuminance(const QColor& color) {
    const double r = LinearizeChannel(color.redF());
    const double g = LinearizeChannel(color.greenF());
    const double b = LinearizeChannel(color.blueF());
    return 0.2126 * r + 0.7152 * g + 0.0722 * b;
}

double ContrastRatio(const QColor& a, const QColor& b) {
    const double l1 = RelativeLuminance(a);
    const double l2 = RelativeLuminance(b);
    const double hi = std::max(l1, l2);
    const double lo = std::min(l1, l2);
    return (hi + 0.05) / (lo + 0.05);
}

QColor MixColors(const QColor& a, const QColor& b, qreal t) {
    const qreal clamped = std::clamp<qreal>(t, 0.0, 1.0);
    const qreal inv = 1.0 - clamped;
    return QColor::fromRgbF(
        static_cast<float>(a.redF() * inv + b.redF() * clamped),
        static_cast<float>(a.greenF() * inv + b.greenF() * clamped),
        static_cast<float>(a.blueF() * inv + b.blueF() * clamped),
        static_cast<float>(a.alphaF() * inv + b.alphaF() * clamped));
}

QColor ParseAccentColor(const QString& hex) {
    QColor color(hex.trimmed());
    if (!color.isValid()) {
        color = QColor(QStringLiteral("#f0386b"));
    }
    color.setAlpha(255);
    return color;
}

bool IsEffectiveDarkMode() {
    const auto mode = QString::fromStdString(UISettings::values.ui_color_mode.GetValue())
                          .trimmed()
                          .toLower();
    if (mode == QStringLiteral("light")) {
        return false;
    }
    if (mode == QStringLiteral("system")) {
        return UISettings::IsSystemDarkMode();
    }
    return true;
}

PaletteTokens ComputeTokens(bool dark, const QColor& raw_accent, bool performance_mode) {
    PaletteTokens t{};
    t.dark = dark;
    t.performance_mode = performance_mode;
    t.accent = raw_accent.isValid() ? raw_accent : QColor(QStringLiteral("#f0386b"));
    t.accent.setAlpha(255);

    const QColor dark_ink(0x10, 0x0a, 0x1f);
    const QColor white(0xff, 0xff, 0xff);
    t.on_accent =
        ContrastRatio(t.accent, white) >= ContrastRatio(t.accent, dark_ink) ? white : dark_ink;

    t.accent_hover = dark ? MixColors(t.accent, white, 0.14) : MixColors(t.accent, dark_ink, 0.10);
    t.accent_pressed =
        dark ? MixColors(t.accent, dark_ink, 0.16) : MixColors(t.accent, dark_ink, 0.22);
    t.accent_soft = dark ? MixColors(t.accent, white, 0.48) : MixColors(t.accent, dark_ink, 0.18);
    t.accent_fill = WithAlpha(t.accent, dark ? 44 : 28);

    if (dark) {
        const QColor base_night(0x0d, 0x0c, 0x14);
        t.page_bg = MixColors(base_night, t.accent, performance_mode ? 0.04 : 0.08);
        t.sidebar_bg = performance_mode ? MixColors(QColor(0x11, 0x11, 0x18), t.accent, 0.04)
                                        : WithAlpha(MixColors(QColor(0x0c, 0x0b, 0x12), t.accent, 0.06), 228);
        t.topbar_bg = performance_mode ? MixColors(QColor(0x13, 0x13, 0x1b), t.accent, 0.05)
                                       : WithAlpha(MixColors(QColor(0x10, 0x0f, 0x18), t.accent, 0.07), 232);
        t.plate_bg = performance_mode ? QColor(0x18, 0x18, 0x20)
                                      : WithAlpha(MixColors(QColor(0x1c, 0x1b, 0x26), t.accent, 0.06), 196);
        t.plate_hover = performance_mode ? QColor(0x21, 0x21, 0x2b)
                                         : WithAlpha(MixColors(QColor(0x28, 0x27, 0x36), t.accent, 0.10), 214);
        t.plate_selected = performance_mode ? MixColors(QColor(0x25, 0x25, 0x31), t.accent, 0.22)
                                            : WithAlpha(MixColors(QColor(0x26, 0x24, 0x34), t.accent, 0.28), 228);
        t.field_bg = performance_mode ? QColor(0x13, 0x13, 0x1a)
                                      : WithAlpha(QColor(0x0d, 0x0c, 0x14), 195);
        t.rule = performance_mode ? QColor(0x2c, 0x2d, 0x38) : QColor(255, 255, 255, 28);
        t.rule_strong = performance_mode ? QColor(0x3e, 0x40, 0x4f) : QColor(255, 255, 255, 52);

        t.ink_full = QColor(0xf6, 0xf6, 0xfa);
        t.ink_muted = QColor(238, 238, 246, 178);
        t.ink_faint = QColor(225, 225, 240, 128);

        t.live = QColor(0x4a, 0xde, 0x80);
        t.warn = QColor(0xf2, 0xc4, 0x6b);
        t.danger = QColor(0xff, 0x8a, 0x8a);
    } else {
        const QColor base_day(0xf4, 0xf5, 0xfa);
        t.page_bg = MixColors(base_day, t.accent, performance_mode ? 0.02 : 0.04);
        t.sidebar_bg = performance_mode ? QColor(0xed, 0xef, 0xf6)
                                        : WithAlpha(QColor(0xff, 0xff, 0xff), 214);
        t.topbar_bg = performance_mode ? QColor(0xf9, 0xfa, 0xfd)
                                       : WithAlpha(QColor(0xff, 0xff, 0xff), 224);
        t.plate_bg = performance_mode ? QColor(0xff, 0xff, 0xff)
                                      : WithAlpha(QColor(0xff, 0xff, 0xff), 220);
        t.plate_hover = performance_mode ? QColor(0xf0, 0xf2, 0xf8)
                                         : WithAlpha(MixColors(QColor(0xff, 0xff, 0xff), t.accent, 0.08), 238);
        t.plate_selected = performance_mode ? MixColors(QColor(0xff, 0xff, 0xff), t.accent, 0.14)
                                            : WithAlpha(MixColors(QColor(0xff, 0xff, 0xff), t.accent, 0.16), 245);
        t.field_bg = performance_mode ? QColor(0xff, 0xff, 0xff)
                                      : WithAlpha(QColor(0xff, 0xff, 0xff), 232);
        t.rule = performance_mode ? QColor(0xd7, 0xd9, 0xe4) : QColor(16, 10, 31, 26);
        t.rule_strong = performance_mode ? QColor(0xbb, 0xbe, 0xce) : QColor(16, 10, 31, 52);

        t.ink_full = dark_ink;
        t.ink_muted = QColor(16, 10, 31, 178);
        t.ink_faint = QColor(16, 10, 31, 134);

        t.live = QColor(0x15, 0x80, 0x3d);
        t.warn = QColor(0xb4, 0x53, 0x09);
        t.danger = QColor(0xdc, 0x26, 0x26);
    }

    return t;
}

PaletteTokens ActiveTokens() {
    const bool dark = IsEffectiveDarkMode();
    const QColor accent =
        ParseAccentColor(QString::fromStdString(UISettings::values.ui_accent_color.GetValue()));
    const bool perf = UISettings::values.ui_performance_mode.GetValue();
    return ComputeTokens(dark, accent, perf);
}

QString RgbaCss(const QColor& color) {
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(color.red())
        .arg(color.green())
        .arg(color.blue())
        .arg(color.alpha());
}

QPalette BuildQPalette(const PaletteTokens& t) {
    QPalette pal;
    const QColor base_solid =
        t.dark ? MixColors(QColor(0x14, 0x14, 0x1c), t.accent, 0.04) : QColor(0xff, 0xff, 0xff);
    const QColor alt_solid =
        t.dark ? MixColors(QColor(0x19, 0x19, 0x22), t.accent, 0.05) : QColor(0xf6, 0xf7, 0xfb);
    const QColor button_solid =
        t.dark ? MixColors(QColor(0x1f, 0x1f, 0x29), t.accent, 0.06) : QColor(0xee, 0xf0, 0xf6);

    pal.setColor(QPalette::Window, t.page_bg);
    pal.setColor(QPalette::WindowText, t.ink_full);
    pal.setColor(QPalette::Base, base_solid);
    pal.setColor(QPalette::AlternateBase, alt_solid);
    pal.setColor(QPalette::Text, t.ink_full);
    pal.setColor(QPalette::PlaceholderText, t.ink_faint);
    pal.setColor(QPalette::Button, button_solid);
    pal.setColor(QPalette::ButtonText, t.ink_full);
    pal.setColor(QPalette::BrightText, t.on_accent);
    pal.setColor(QPalette::ToolTipBase, t.dark ? QColor(0x1a, 0x1a, 0x24) : QColor(0xff, 0xff, 0xff));
    pal.setColor(QPalette::ToolTipText, t.ink_full);
    pal.setColor(QPalette::Highlight, t.accent);
    pal.setColor(QPalette::HighlightedText, t.on_accent);
    pal.setColor(QPalette::Link, t.accent_soft);
    pal.setColor(QPalette::LinkVisited, t.accent);
    pal.setColor(QPalette::Mid, t.rule);
    pal.setColor(QPalette::Midlight, t.plate_hover);
    pal.setColor(QPalette::Dark, t.rule_strong);

    const QColor disabled_ink = WithAlpha(t.ink_full, 100);
    pal.setColor(QPalette::Disabled, QPalette::WindowText, disabled_ink);
    pal.setColor(QPalette::Disabled, QPalette::Text, disabled_ink);
    pal.setColor(QPalette::Disabled, QPalette::ButtonText, disabled_ink);
    return pal;
}

QString BuildStyleSheet(const PaletteTokens& t) {
    const QString page_bg = RgbaCss(t.page_bg);
    const QString sidebar_bg = RgbaCss(t.sidebar_bg);
    const QString topbar_bg = RgbaCss(t.topbar_bg);
    const QString plate_bg = RgbaCss(t.plate_bg);
    const QString plate_hover = RgbaCss(t.plate_hover);
    const QString plate_selected = RgbaCss(t.plate_selected);
    const QString field_bg = RgbaCss(t.field_bg);
    const QString rule = RgbaCss(t.rule);
    const QString rule_strong = RgbaCss(t.rule_strong);
    const QString ink_full = RgbaCss(t.ink_full);
    const QString ink_muted = RgbaCss(t.ink_muted);
    const QString ink_faint = RgbaCss(t.ink_faint);
    const QString accent = t.accent.name();
    const QString accent_hover = t.accent_hover.name();
    const QString accent_pressed = t.accent_pressed.name();
    const QString accent_soft = t.accent_soft.name();
    const QString accent_fill = RgbaCss(t.accent_fill);
    const QString on_accent = t.on_accent.name();
    const QString live = t.live.name();
    const QString warn = t.warn.name();
    const QString danger = t.danger.name();
    const QString menu_bg = t.dark ? QStringLiteral("#171720") : QStringLiteral("#ffffff");
    const QString scroll_thumb = t.dark ? QStringLiteral("rgba(255, 255, 255, 42)")
                                        : QStringLiteral("rgba(16, 10, 31, 44)");
    const QString scroll_thumb_hover = t.dark ? QStringLiteral("rgba(255, 255, 255, 76)")
                                              : QStringLiteral("rgba(16, 10, 31, 78)");
    const QString match_bg = RgbaCss(WithAlpha(t.accent, t.dark ? 48 : 34));

    QString qss;
    qss.reserve(8192);
    qss += QStringLiteral(
        "QMainWindow, QDialog { background: %1; color: %2; }\n"
        "QWidget { color: %2; }\n"
        "QLabel { background: transparent; }\n"
        "QLabel[muted=\"true\"] { color: %3; }\n"
        "QLabel[faint=\"true\"] { color: %4; }\n"
        "QLabel[glassSectionHeader=\"true\"] { color: %4; font-size: 11px; font-weight: 700; "
        "letter-spacing: 0.08em; }\n"
        "QLabel[glassPageTitle=\"true\"] { color: %2; font-size: 24px; font-weight: 700; }\n"
        "QLabel[glassHeroTitle=\"true\"] { color: %2; font-size: 26px; font-weight: 700; }\n"
        "QLabel[glassChip=\"true\"] { background: %5; border: 1px solid %6; border-radius: 9px; "
        "padding: 3px 9px; color: %3; font-size: 11px; font-weight: 600; }\n"
        "QLabel[glassBadge=\"accent\"] { background: %7; border: 1px solid %8; "
        "border-radius: 9px; padding: 3px 9px; color: %9; font-size: 11px; font-weight: 600; }\n"
        "QLabel[glassBadge=\"live\"] { background: rgba(74, 222, 128, 32); "
        "border: 1px solid %10; border-radius: 9px; padding: 3px 9px; color: %10; "
        "font-size: 11px; font-weight: 600; }\n")
               .arg(page_bg, ink_full, ink_muted, ink_faint, plate_bg, rule, accent_fill, accent,
                    accent_soft, live);

    qss += QStringLiteral(
        "QWidget#glassTopBar { background: %1; border-bottom: 1px solid %2; }\n"
        "QWidget#glassSidebar { background: %3; border-right: 1px solid %2; }\n"
        "QWidget#glassLibraryBar { background: %1; border-bottom: 1px solid %2; }\n"
        "QFrame#glassDivider { background: %2; border: none; max-height: 1px; min-height: 1px; }\n"
        "QFrame#glassCommandPaletteFrame { background: %4; border: 1px solid %5; "
        "border-radius: 18px; }\n")
               .arg(topbar_bg, rule, sidebar_bg, menu_bg, rule_strong);

    qss += QStringLiteral(
        "QPushButton, QToolButton { background: %1; color: %2; border: 1px solid %3; "
        "border-radius: 10px; padding: 6px 13px; font-weight: 600; min-height: 18px; }\n"
        "QPushButton:hover, QToolButton:hover { background: %4; border-color: %5; }\n"
        "QPushButton:pressed, QToolButton:pressed { background: %6; }\n"
        "QPushButton:disabled, QToolButton:disabled { opacity: 0.45; color: %7; }\n"
        "QPushButton[glassRole=\"primary\"], QToolButton[glassRole=\"primary\"] { "
        "background: %8; color: %9; border: 1px solid %8; border-radius: 11px; "
        "padding: 7px 18px; font-weight: 700; }\n"
        "QPushButton[glassRole=\"primary\"]:hover, QToolButton[glassRole=\"primary\"]:hover { "
        "background: %10; border-color: %10; }\n"
        "QPushButton[glassRole=\"primary\"]:pressed, QToolButton[glassRole=\"primary\"]:pressed { "
        "background: %11; border-color: %11; }\n"
        "QPushButton[glassRole=\"danger\"] { background: rgba(220, 38, 38, 34); color: %12; "
        "border: 1px solid %12; }\n"
        "QPushButton[glassRole=\"danger\"]:hover { background: rgba(220, 38, 38, 56); }\n"
        "QPushButton[glassRole=\"ghost\"], QToolButton[glassRole=\"ghost\"] { "
        "background: transparent; border: 1px solid transparent; padding: 6px 10px; }\n"
        "QPushButton[glassRole=\"ghost\"]:hover, QToolButton[glassRole=\"ghost\"]:hover { "
        "background: %4; border-color: %3; }\n"
        "QPushButton[glassRole=\"icon\"], QToolButton[glassRole=\"icon\"] { "
        "background: %1; border: 1px solid %3; border-radius: 11px; padding: 6px; "
        "min-width: 32px; min-height: 32px; }\n"
        "QPushButton[glassRole=\"icon\"]:hover, QToolButton[glassRole=\"icon\"]:hover { "
        "background: %4; border-color: %5; }\n"
        "QToolButton[glassRole=\"icon\"]::menu-indicator { image: none; width: 0; }\n"
        "QPushButton[glassRole=\"navTab\"] { background: transparent; color: %13; "
        "border: 1px solid transparent; border-radius: 14px; padding: 6px 14px; "
        "font-weight: 600; }\n"
        "QPushButton[glassRole=\"navTab\"]:hover { background: %4; color: %2; }\n"
        "QPushButton[glassRole=\"navTab\"][active=\"true\"] { background: %6; color: %2; "
        "border: 1px solid %8; }\n"
        "QPushButton[glassRole=\"railItem\"] { text-align: left; background: transparent; "
        "color: %13; border: 1px solid transparent; border-radius: 10px; padding: 8px 12px; "
        "font-weight: 600; }\n"
        "QPushButton[glassRole=\"railItem\"]:hover { background: %4; color: %2; }\n"
        "QPushButton[glassRole=\"railItem\"][active=\"true\"] { background: %6; color: %2; "
        "border: 1px solid %5; }\n"
        "QPushButton[glassRole=\"searchPill\"] { background: %14; color: %7; "
        "border: 1px solid %3; border-radius: 14px; padding: 5px 12px; font-weight: 500; }\n"
        "QPushButton[glassRole=\"searchPill\"]:hover { background: %4; color: %2; "
        "border-color: %5; }\n")
               .arg(plate_bg, ink_full, rule, plate_hover, rule_strong, plate_selected, ink_faint,
                    accent, on_accent, accent_hover, accent_pressed, danger, ink_muted, field_bg);

    qss += QStringLiteral(
        "QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox { "
        "background: %1; color: %2; border: 1px solid %3; border-radius: 9px; "
        "padding: 5px 10px; selection-background-color: %4; selection-color: %5; }\n"
        "QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, "
        "QDoubleSpinBox:focus, QComboBox:focus { border: 1px solid %4; }\n"
        "QComboBox::drop-down { border: none; width: 24px; }\n"
        "QComboBox QAbstractItemView { background: %6; color: %2; border: 1px solid %7; "
        "border-radius: 10px; padding: 4px; selection-background-color: %8; "
        "selection-color: %2; outline: 0; }\n"
        "QGroupBox { background: %9; border: 1px solid %3; border-radius: 14px; "
        "margin-top: 14px; padding: 12px 10px 10px 10px; font-weight: 600; }\n"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left; "
        "left: 12px; padding: 0 6px; color: %2; }\n")
               .arg(field_bg, ink_full, rule, accent, on_accent, menu_bg, rule_strong,
                    plate_selected, plate_bg);

    qss += QStringLiteral(
        "QListWidget, QTreeWidget, QTreeView, QTableView { background: %1; color: %2; "
        "border: 1px solid %3; border-radius: 12px; outline: 0; }\n"
        "QListWidget::item, QTreeWidget::item, QTreeView::item { padding: 4px 6px; "
        "border-radius: 8px; }\n"
        "QListWidget::item:hover, QTreeWidget::item:hover, QTreeView::item:hover { "
        "background: %4; }\n"
        "QListWidget::item:selected, QTreeWidget::item:selected, QTreeView::item:selected { "
        "background: %5; color: %2; }\n"
        "QHeaderView::section { background: %6; color: %7; border: none; "
        "border-bottom: 1px solid %3; padding: 6px 10px; font-weight: 600; }\n"
        "QTabWidget::pane { border: 1px solid %3; border-radius: 12px; background: %1; "
        "top: -1px; }\n"
        "QTabBar::tab { background: transparent; color: %7; border: 1px solid transparent; "
        "border-radius: 9px; padding: 6px 12px; margin: 2px 3px 6px 0; font-weight: 600; }\n"
        "QTabBar::tab:hover { background: %4; color: %2; }\n"
        "QTabBar::tab:selected { background: %5; color: %2; border: 1px solid %8; }\n")
               .arg(plate_bg, ink_full, rule, plate_hover, plate_selected, topbar_bg, ink_muted,
                    accent);

    qss += QStringLiteral(
        "QMenu { background: %1; color: %2; border: 1px solid %3; border-radius: 12px; "
        "padding: 5px; }\n"
        "QMenu::item { padding: 6px 24px 6px 12px; border-radius: 8px; }\n"
        "QMenu::item:selected { background: %4; color: %2; }\n"
        "QMenu::separator { height: 1px; background: %5; margin: 4px 8px; }\n"
        "QToolTip { background: %1; color: %2; border: 1px solid %3; border-radius: 8px; "
        "padding: 5px 8px; }\n"
        "QStatusBar { background: %6; color: %7; border-top: 1px solid %5; }\n"
        "QStatusBar::item { border: none; }\n"
        "QScrollArea { background: transparent; border: none; }\n"
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }\n"
        "QScrollBar::handle:vertical { background: %8; border-radius: 4px; min-height: 28px; }\n"
        "QScrollBar::handle:vertical:hover { background: %9; }\n"
        "QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical, "
        "QScrollBar::add-page:vertical, QScrollBar::sub-page:vertical { background: none; "
        "border: none; height: 0; }\n"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }\n"
        "QScrollBar::handle:horizontal { background: %8; border-radius: 4px; min-width: 28px; }\n"
        "QScrollBar::handle:horizontal:hover { background: %9; }\n"
        "QScrollBar::add-line:horizontal, QScrollBar::sub-line:horizontal, "
        "QScrollBar::add-page:horizontal, QScrollBar::sub-page:horizontal { background: none; "
        "border: none; width: 0; }\n"
        "QProgressBar { background: %10; border: 1px solid %5; border-radius: 7px; "
        "text-align: center; min-height: 14px; }\n"
        "QProgressBar::chunk { background: %11; border-radius: 6px; }\n"
        "QSlider::groove:horizontal { background: %5; height: 4px; border-radius: 2px; }\n"
        "QSlider::sub-page:horizontal { background: %11; height: 4px; border-radius: 2px; }\n"
        "QSlider::handle:horizontal { background: %2; border: 2px solid %11; width: 14px; "
        "height: 14px; margin: -6px 0; border-radius: 8px; }\n"
        "*[searchMatch=\"true\"] { background: %12; border: 1px solid %11; border-radius: 6px; }\n"
        "QLabel[glassWarn=\"true\"] { color: %13; }\n"
        "QLabel[glassDanger=\"true\"] { color: %14; }\n")
               .arg(menu_bg, ink_full, rule_strong, plate_selected, rule, topbar_bg, ink_muted,
                    scroll_thumb, scroll_thumb_hover, field_bg, accent, match_bg, warn, danger);

    return qss;
}

QIcon TintedIcon(const QString& name, const QColor& color, int size_px) {
    QStringList candidates{
        QStringLiteral(":/lichii/icons/menu_%1@2x.png").arg(name),
        QStringLiteral(":/lichii/icons/%1@2x.png").arg(name),
        QStringLiteral(":/lichii/icons/menu_%1.png").arg(name),
        QStringLiteral(":/lichii/icons/%1.png").arg(name),
    };
    QImage src;
    for (const QString& path : candidates) {
        if (src.load(path)) {
            break;
        }
    }
    if (src.isNull()) {
        return {};
    }

    QImage argb = src.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    argb.setDevicePixelRatio(1.0);
    int min_x = argb.width();
    int min_y = argb.height();
    int max_x = -1;
    int max_y = -1;
    for (int y = 0; y < argb.height(); ++y) {
        const auto* row = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            if (qAlpha(row[x]) > 8) {
                min_x = std::min(min_x, x);
                min_y = std::min(min_y, y);
                max_x = std::max(max_x, x);
                max_y = std::max(max_y, y);
            }
        }
    }
    if (max_x >= min_x && max_y >= min_y) {
        argb = argb.copy(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
        argb.setDevicePixelRatio(1.0);
    }

    const auto render_tinted = [&](int px, qreal dpr) -> QPixmap {
        const int pad = std::max(1, px / 12);
        const int inner = std::max(4, px - pad * 2);
        QImage scaled = argb.scaled(inner, inner, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        scaled.setDevicePixelRatio(1.0);
        QImage out(px, px, QImage::Format_ARGB32_Premultiplied);
        out.fill(Qt::transparent);

        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        p.setRenderHint(QPainter::SmoothPixmapTransform);
        const int dx = (px - scaled.width()) / 2;
        const int dy = (px - scaled.height()) / 2;
        p.drawImage(dx, dy, scaled);
        p.setCompositionMode(QPainter::CompositionMode_SourceIn);
        p.fillRect(out.rect(), color);
        p.end();

        QPixmap pm = QPixmap::fromImage(out);
        pm.setDevicePixelRatio(dpr);
        return pm;
    };

    const int logical = std::max(8, size_px);
    QIcon icon;
    icon.addPixmap(render_tinted(logical, 1.0));
    icon.addPixmap(render_tinted(logical * 2, 2.0));
    return icon;
}

QPixmap RoundedPixmap(const QPixmap& source, int size, qreal radius) {
    if (source.isNull() || size <= 0) {
        return {};
    }
    const int px = size * 2;
    QPixmap scaled =
        source.scaled(px, px, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int sx = std::max(0, (scaled.width() - px) / 2);
    const int sy = std::max(0, (scaled.height() - px) / 2);
    QPixmap cropped = scaled.copy(sx, sy, px, px);

    QImage out(px, px, QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    QPainterPath clip;
    clip.addRoundedRect(QRectF(0, 0, px, px), radius * 2.0, radius * 2.0);
    p.setClipPath(clip);
    p.drawPixmap(0, 0, cropped);
    p.end();

    QPixmap pm = QPixmap::fromImage(out);
    pm.setDevicePixelRatio(2.0);
    return pm;
}

} // namespace GlassUi
