// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_widgets.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include <QEasingCurve>
#include <QHBoxLayout>
#include <QLabel>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QRadialGradient>
#include <QVBoxLayout>

namespace GlassUi {
namespace {

// Fast 3-pass separable box blur on a small downscaled ARGB32 image (approximates Gaussian blur).
QImage BoxBlurImage(QImage image, int radius) {
    if (image.isNull() || radius <= 0) {
        return image;
    }
    image = image.convertToFormat(QImage::Format_ARGB32);
    const int w = image.width();
    const int h = image.height();
    if (w < 2 || h < 2) {
        return image;
    }

    QImage tmp(w, h, QImage::Format_ARGB32);
    const int window = radius * 2 + 1;

    for (int pass = 0; pass < 3; ++pass) {
        // Horizontal pass
        for (int y = 0; y < h; ++y) {
            const auto* src = reinterpret_cast<const QRgb*>(image.constScanLine(y));
            auto* dst = reinterpret_cast<QRgb*>(tmp.scanLine(y));
            int r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0;
            for (int i = -radius; i <= radius; ++i) {
                const QRgb px = src[std::clamp(i, 0, w - 1)];
                r_sum += qRed(px);
                g_sum += qGreen(px);
                b_sum += qBlue(px);
                a_sum += qAlpha(px);
            }
            for (int x = 0; x < w; ++x) {
                dst[x] = qRgba(r_sum / window, g_sum / window, b_sum / window, a_sum / window);
                const QRgb old_px = src[std::clamp(x - radius, 0, w - 1)];
                const QRgb new_px = src[std::clamp(x + radius + 1, 0, w - 1)];
                r_sum += qRed(new_px) - qRed(old_px);
                g_sum += qGreen(new_px) - qGreen(old_px);
                b_sum += qBlue(new_px) - qBlue(old_px);
                a_sum += qAlpha(new_px) - qAlpha(old_px);
            }
        }
        // Vertical pass
        for (int x = 0; x < w; ++x) {
            int r_sum = 0, g_sum = 0, b_sum = 0, a_sum = 0;
            for (int i = -radius; i <= radius; ++i) {
                const int sy = std::clamp(i, 0, h - 1);
                const QRgb px = reinterpret_cast<const QRgb*>(tmp.constScanLine(sy))[x];
                r_sum += qRed(px);
                g_sum += qGreen(px);
                b_sum += qBlue(px);
                a_sum += qAlpha(px);
            }
            for (int y = 0; y < h; ++y) {
                reinterpret_cast<QRgb*>(image.scanLine(y))[x] =
                    qRgba(r_sum / window, g_sum / window, b_sum / window, a_sum / window);
                const int old_y = std::clamp(y - radius, 0, h - 1);
                const int new_y = std::clamp(y + radius + 1, 0, h - 1);
                const QRgb old_px = reinterpret_cast<const QRgb*>(tmp.constScanLine(old_y))[x];
                const QRgb new_px = reinterpret_cast<const QRgb*>(tmp.constScanLine(new_y))[x];
                r_sum += qRed(new_px) - qRed(old_px);
                g_sum += qGreen(new_px) - qGreen(old_px);
                b_sum += qBlue(new_px) - qBlue(old_px);
                a_sum += qAlpha(new_px) - qAlpha(old_px);
            }
        }
    }

    return image;
}

} // namespace

GlassCard::GlassCard(QWidget* parent, int radius)
    : QFrame(parent), m_tokens(ActiveTokens()), m_radius(radius) {
    setAttribute(Qt::WA_StyledBackground, false);
    setFrameShape(QFrame::NoFrame);
}

void GlassCard::SetRadius(int radius) {
    m_radius = std::max(0, radius);
    update();
}

void GlassCard::SetTokens(const PaletteTokens& tokens) {
    m_tokens = tokens;
    update();
}

void GlassCard::RefreshTokens() {
    SetTokens(ActiveTokens());
}

void GlassCard::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF bounds = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath path;
    path.addRoundedRect(bounds, m_radius, m_radius);

    p.fillPath(path, m_tokens.plate_bg);

    // Subtle frosted top sheen in full Glass mode.
    if (!m_tokens.performance_mode) {
        QLinearGradient sheen(bounds.topLeft(), QPointF(bounds.left(), bounds.top() + 56.0));
        if (m_tokens.dark) {
            sheen.setColorAt(0.0, QColor(255, 255, 255, 16));
            sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        } else {
            sheen.setColorAt(0.0, QColor(255, 255, 255, 90));
            sheen.setColorAt(1.0, QColor(255, 255, 255, 0));
        }
        p.fillPath(path, sheen);
    }

    p.setPen(QPen(m_tokens.rule, 1.0));
    p.drawPath(path);
}

GlassBackdrop::GlassBackdrop(QWidget* parent) : QWidget(parent), m_tokens(ActiveTokens()) {
    setAttribute(Qt::WA_OpaquePaintEvent, true);
}

void GlassBackdrop::SetArtwork(const QPixmap& pixmap) {
    m_source = pixmap;
    RebuildBlurCache();
    update();
}

void GlassBackdrop::ClearArtwork() {
    m_source = {};
    m_blurred = {};
    update();
}

void GlassBackdrop::SetTokens(const PaletteTokens& tokens) {
    const bool perf_changed = m_tokens.performance_mode != tokens.performance_mode;
    m_tokens = tokens;
    if (perf_changed) {
        RebuildBlurCache();
    }
    update();
}

void GlassBackdrop::RefreshTokens() {
    SetTokens(ActiveTokens());
}

void GlassBackdrop::RebuildBlurCache() {
    if (m_source.isNull() || m_tokens.performance_mode) {
        m_blurred = {};
        return;
    }
    QImage small =
        m_source.toImage().scaled(112, 112, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    m_blurred = BoxBlurImage(std::move(small), 9);
}

void GlassBackdrop::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.setRenderHint(QPainter::Antialiasing);

    const QRect r = rect();
    p.fillRect(r, m_tokens.page_bg);

    if (m_tokens.performance_mode) {
        QLinearGradient grad(r.topLeft(), r.bottomRight());
        grad.setColorAt(0.0, MixColors(m_tokens.page_bg, m_tokens.accent, m_tokens.dark ? 0.10 : 0.06));
        grad.setColorAt(0.55, m_tokens.page_bg);
        grad.setColorAt(1.0, m_tokens.page_bg);
        p.fillRect(r, grad);
        return;
    }

    // Draw scaled blurred cover art filling the hero stage.
    if (!m_blurred.isNull()) {
        const QSize img_size = m_blurred.size();
        const qreal scale = std::max(static_cast<qreal>(r.width()) / img_size.width(),
                                     static_cast<qreal>(r.height()) / img_size.height());
        const QSizeF draw_size(img_size.width() * scale, img_size.height() * scale);
        const QRectF target((r.width() - draw_size.width()) / 2.0,
                            (r.height() - draw_size.height()) / 2.0, draw_size.width(),
                            draw_size.height());
        p.save();
        p.setOpacity(m_tokens.dark ? 0.42 : 0.24);
        p.drawImage(target, m_blurred);
        p.restore();
    }

    // Horizon-style atmospheric radial accent glow on upper-right.
    QRadialGradient glow(QPointF(r.width() * 0.78, r.height() * 0.14),
                         std::max(r.width(), r.height()) * 0.65);
    QColor glow_center = m_tokens.accent;
    glow_center.setAlpha(m_tokens.dark ? 56 : 36);
    QColor glow_edge = m_tokens.accent;
    glow_edge.setAlpha(0);
    glow.setColorAt(0.0, glow_center);
    glow.setColorAt(1.0, glow_edge);
    p.fillRect(r, glow);

    // Readability scrim fading cleanly into page_bg toward the bottom.
    QLinearGradient scrim(r.topLeft(), r.bottomLeft());
    if (m_tokens.dark) {
        QColor top = m_tokens.page_bg;
        top.setAlpha(118);
        QColor mid = m_tokens.page_bg;
        mid.setAlpha(206);
        QColor bot = m_tokens.page_bg;
        bot.setAlpha(246);
        scrim.setColorAt(0.0, top);
        scrim.setColorAt(0.45, mid);
        scrim.setColorAt(1.0, bot);
    } else {
        QColor top = m_tokens.page_bg;
        top.setAlpha(150);
        QColor mid = m_tokens.page_bg;
        mid.setAlpha(222);
        QColor bot = m_tokens.page_bg;
        bot.setAlpha(248);
        scrim.setColorAt(0.0, top);
        scrim.setColorAt(0.45, mid);
        scrim.setColorAt(1.0, bot);
    }
    p.fillRect(r, scrim);
}

GlassSwitch::GlassSwitch(QWidget* parent)
    : QAbstractButton(parent), m_tokens(ActiveTokens()),
      m_anim(new QPropertyAnimation(this, "offset", this)) {
    setCheckable(true);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::StrongFocus);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    m_anim->setEasingCurve(QEasingCurve::OutCubic);
}

QSize GlassSwitch::sizeHint() const {
    return QSize(38, 22);
}

void GlassSwitch::SetOffset(qreal value) {
    m_offset = std::clamp<qreal>(value, 0.0, 1.0);
    update();
}

void GlassSwitch::SetTokens(const PaletteTokens& tokens) {
    m_tokens = tokens;
    update();
}

void GlassSwitch::RefreshTokens() {
    SetTokens(ActiveTokens());
}

void GlassSwitch::checkStateSet() {
    QAbstractButton::checkStateSet();
    m_anim->stop();
    m_offset = isChecked() ? 1.0 : 0.0;
    update();
}

void GlassSwitch::nextCheckState() {
    QAbstractButton::nextCheckState();
    AnimateTo(isChecked() ? 1.0 : 0.0);
}

void GlassSwitch::AnimateTo(qreal target) {
    m_anim->stop();
    if (m_tokens.performance_mode) {
        SetOffset(target);
        return;
    }
    m_anim->setDuration(180);
    m_anim->setStartValue(m_offset);
    m_anim->setEndValue(target);
    m_anim->start();
}

void GlassSwitch::paintEvent(QPaintEvent* /*event*/) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF track = QRectF(1.0, 1.0, width() - 2.0, height() - 2.0);
    const qreal radius = track.height() / 2.0;

    const QColor off_track =
        m_tokens.dark ? QColor(255, 255, 255, 36) : QColor(16, 10, 31, 40);
    const QColor on_track = m_tokens.accent;
    QColor track_color = MixColors(off_track, on_track, m_offset);
    if (!isEnabled()) {
        track_color.setAlpha(track_color.alpha() / 2);
    }

    p.setPen(hasFocus() ? QPen(m_tokens.accent_soft, 1.5) : QPen(m_tokens.rule, 1.0));
    p.setBrush(track_color);
    p.drawRoundedRect(track, radius, radius);

    const qreal knob_d = track.height() - 6.0;
    const qreal left_x = track.left() + 3.0;
    const qreal right_x = track.right() - 3.0 - knob_d;
    const qreal knob_x = left_x + (right_x - left_x) * m_offset;
    const QRectF knob(knob_x, track.top() + 3.0, knob_d, knob_d);

    QColor knob_color = isChecked() ? m_tokens.on_accent : QColor(0xff, 0xff, 0xff);
    if (!isEnabled()) {
        knob_color.setAlpha(140);
    }
    p.setPen(Qt::NoPen);
    p.setBrush(knob_color);
    p.drawEllipse(knob);
}

GlassSegmentedControl::GlassSegmentedControl(QWidget* parent)
    : QFrame(parent), m_tokens(ActiveTokens()), m_layout(new QHBoxLayout(this)) {
    setObjectName(QStringLiteral("glassSegmented"));
    m_layout->setContentsMargins(3, 3, 3, 3);
    m_layout->setSpacing(3);
    setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    UpdateStyles();
}

void GlassSegmentedControl::SetSegments(const QStringList& labels) {
    for (auto* button : m_buttons) {
        m_layout->removeWidget(button);
        button->deleteLater();
    }
    m_buttons.clear();
    m_current = 0;

    for (int i = 0; i < labels.size(); ++i) {
        auto* btn = new QPushButton(labels[i], this);
        btn->setCheckable(true);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setFocusPolicy(Qt::StrongFocus);
        connect(btn, &QPushButton::clicked, this, [this, i] { SetCurrentIndex(i); });
        m_layout->addWidget(btn);
        m_buttons.push_back(btn);
    }
    if (!m_buttons.isEmpty()) {
        m_buttons[0]->setChecked(true);
    }
    UpdateStyles();
}

void GlassSegmentedControl::SetCurrentIndex(int index) {
    if (index < 0 || index >= m_buttons.size()) {
        return;
    }
    const bool changed = (m_current != index);
    m_current = index;
    for (int i = 0; i < m_buttons.size(); ++i) {
        m_buttons[i]->setChecked(i == m_current);
    }
    UpdateStyles();
    if (changed) {
        emit CurrentIndexChanged(m_current);
    }
}

void GlassSegmentedControl::SetTokens(const PaletteTokens& tokens) {
    m_tokens = tokens;
    UpdateStyles();
}

void GlassSegmentedControl::RefreshTokens() {
    SetTokens(ActiveTokens());
}

void GlassSegmentedControl::UpdateStyles() {
    setStyleSheet(QStringLiteral("QFrame#glassSegmented { background: %1; border: 1px solid %2; "
                                 "border-radius: 13px; }")
                      .arg(RgbaCss(m_tokens.field_bg), RgbaCss(m_tokens.rule)));

    const QString inactive =
        QStringLiteral("QPushButton { background: transparent; color: %1; border: none; "
                       "border-radius: 10px; padding: 5px 13px; font-weight: 600; min-height: 16px; }"
                       "QPushButton:hover { color: %2; background: %3; }")
            .arg(RgbaCss(m_tokens.ink_muted), RgbaCss(m_tokens.ink_full),
                 RgbaCss(m_tokens.plate_hover));

    const QString active =
        QStringLiteral("QPushButton { background: %1; color: %2; border: none; "
                       "border-radius: 10px; padding: 5px 13px; font-weight: 700; min-height: 16px; }")
            .arg(m_tokens.accent.name(), m_tokens.on_accent.name());

    for (int i = 0; i < m_buttons.size(); ++i) {
        m_buttons[i]->setStyleSheet(i == m_current ? active : inactive);
    }
}

GlassSettingRow::GlassSettingRow(const QString& title, const QString& caption, QWidget* control,
                                 QWidget* parent)
    : QWidget(parent) {
    auto* root = new QHBoxLayout(this);
    root->setContentsMargins(0, 10, 0, 10);
    root->setSpacing(16);

    auto* text_col = new QVBoxLayout;
    text_col->setContentsMargins(0, 0, 0, 0);
    text_col->setSpacing(2);

    m_title = new QLabel(title, this);
    QFont title_font = m_title->font();
    title_font.setBold(true);
    m_title->setFont(title_font);

    m_caption = new QLabel(caption, this);
    m_caption->setProperty("muted", true);
    m_caption->setWordWrap(true);

    text_col->addWidget(m_title);
    if (!caption.isEmpty()) {
        text_col->addWidget(m_caption);
    } else {
        m_caption->hide();
    }

    root->addLayout(text_col, 1);
    if (control) {
        root->addWidget(control, 0, Qt::AlignRight | Qt::AlignVCenter);
    }
}

} // namespace GlassUi
