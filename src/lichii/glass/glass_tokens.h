// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <array>

#include <QColor>
#include <QIcon>
#include <QPalette>
#include <QPixmap>
#include <QString>

namespace GlassUi {

struct AccentPreset {
    const char* name;
    const char* hex;
};

inline constexpr std::array<AccentPreset, 5> AccentPresets{{
    {"Lichii Rose", "#f0386b"},
    {"Horizon Violet", "#6b4bff"},
    {"Sky", "#38bdf8"},
    {"Emerald", "#34d399"},
    {"Amber", "#f59e0b"},
}};

struct PaletteTokens {
    bool dark{true};
    bool performance_mode{false};

    QColor accent;
    QColor accent_hover;
    QColor accent_pressed;
    QColor accent_soft;
    QColor accent_fill;
    QColor on_accent;

    QColor page_bg;
    QColor sidebar_bg;
    QColor topbar_bg;
    QColor plate_bg;
    QColor plate_hover;
    QColor plate_selected;
    QColor field_bg;
    QColor rule;
    QColor rule_strong;

    QColor ink_full;
    QColor ink_muted;
    QColor ink_faint;

    QColor live;
    QColor warn;
    QColor danger;
};

[[nodiscard]] double RelativeLuminance(const QColor& color);
[[nodiscard]] double ContrastRatio(const QColor& a, const QColor& b);
[[nodiscard]] QColor MixColors(const QColor& a, const QColor& b, qreal t);
[[nodiscard]] QColor ParseAccentColor(const QString& hex);
[[nodiscard]] bool IsEffectiveDarkMode();

[[nodiscard]] PaletteTokens ComputeTokens(bool dark, const QColor& accent, bool performance_mode);
[[nodiscard]] PaletteTokens ActiveTokens();

[[nodiscard]] QString RgbaCss(const QColor& color);
[[nodiscard]] QPalette BuildQPalette(const PaletteTokens& tokens);
[[nodiscard]] QString BuildStyleSheet(const PaletteTokens& tokens);

[[nodiscard]] QIcon TintedIcon(const QString& name, const QColor& color, int size_px = 16);
[[nodiscard]] QPixmap RoundedPixmap(const QPixmap& source, int size, qreal radius);

} // namespace GlassUi
