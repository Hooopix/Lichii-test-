// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QAbstractButton>
#include <QFrame>
#include <QPixmap>
#include <QStringList>
#include <QVector>
#include <QWidget>

#include "lichii/glass/glass_tokens.h"

class QHBoxLayout;
class QLabel;
class QPropertyAnimation;
class QPushButton;

namespace GlassUi {

class GlassCard : public QFrame {
    Q_OBJECT

public:
    explicit GlassCard(QWidget* parent = nullptr, int radius = 16);

    void SetRadius(int radius);
    void SetTokens(const PaletteTokens& tokens);
    void RefreshTokens();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    PaletteTokens m_tokens;
    int m_radius{16};
};

class GlassBackdrop : public QWidget {
    Q_OBJECT

public:
    explicit GlassBackdrop(QWidget* parent = nullptr);

    void SetArtwork(const QPixmap& pixmap);
    void ClearArtwork();
    void SetTokens(const PaletteTokens& tokens);
    void RefreshTokens();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void RebuildBlurCache();

    PaletteTokens m_tokens;
    QPixmap m_source;
    QImage m_blurred;
};

class GlassSwitch : public QAbstractButton {
    Q_OBJECT
    Q_PROPERTY(qreal offset READ Offset WRITE SetOffset)

public:
    explicit GlassSwitch(QWidget* parent = nullptr);

    [[nodiscard]] QSize sizeHint() const override;
    [[nodiscard]] qreal Offset() const {
        return m_offset;
    }
    void SetOffset(qreal value);
    void SetTokens(const PaletteTokens& tokens);
    void RefreshTokens();

protected:
    void paintEvent(QPaintEvent* event) override;
    void checkStateSet() override;
    void nextCheckState() override;

private:
    void AnimateTo(qreal target);

    PaletteTokens m_tokens;
    QPropertyAnimation* m_anim{nullptr};
    qreal m_offset{0.0};
};

class GlassSegmentedControl : public QFrame {
    Q_OBJECT

public:
    explicit GlassSegmentedControl(QWidget* parent = nullptr);

    void SetSegments(const QStringList& labels);
    void SetCurrentIndex(int index);
    [[nodiscard]] int CurrentIndex() const {
        return m_current;
    }
    void SetTokens(const PaletteTokens& tokens);
    void RefreshTokens();

signals:
    void CurrentIndexChanged(int index);

private:
    void UpdateStyles();

    PaletteTokens m_tokens;
    QHBoxLayout* m_layout{nullptr};
    QVector<QPushButton*> m_buttons;
    int m_current{0};
};

class GlassSettingRow : public QWidget {
    Q_OBJECT

public:
    explicit GlassSettingRow(const QString& title, const QString& caption, QWidget* control,
                             QWidget* parent = nullptr);

    [[nodiscard]] QLabel* TitleLabel() const {
        return m_title;
    }
    [[nodiscard]] QLabel* CaptionLabel() const {
        return m_caption;
    }

private:
    QLabel* m_title{nullptr};
    QLabel* m_caption{nullptr};
};

} // namespace GlassUi
