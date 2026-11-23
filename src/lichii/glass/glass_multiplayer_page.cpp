// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#include "lichii/glass/glass_multiplayer_page.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QStyle>
#include <QVBoxLayout>

#include "lichii/glass/glass_widgets.h"

namespace GlassUi {

GlassMultiplayerPage::GlassMultiplayerPage(QWidget* parent)
    : QWidget(parent), m_tokens(ActiveTokens()) {
    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(32, 24, 32, 24);
    root->setSpacing(20);

    auto* title = new QLabel(tr("Multiplayer"), this);
    title->setProperty("glassPageTitle", true);
    auto* subtitle = new QLabel(
        tr("Join public LDN lobbies, host a private room, or connect directly by address."), this);
    subtitle->setProperty("muted", true);
    root->addWidget(title);
    root->addWidget(subtitle);

    // ── Live Connection Status Card ──────────────────────────────────────
    m_status_card = new GlassCard(this, 16);
    auto* status_row = new QHBoxLayout(m_status_card);
    status_row->setContentsMargins(22, 18, 22, 18);
    status_row->setSpacing(14);

    auto* status_col = new QVBoxLayout;
    status_col->setSpacing(4);
    auto* title_row = new QHBoxLayout;
    title_row->setSpacing(10);
    m_status_title = new QLabel(tr("Not Connected"), m_status_card);
    QFont tf = m_status_title->font();
    tf.setBold(true);
    tf.setPointSizeF(tf.pointSizeF() + 2.0);
    m_status_title->setFont(tf);

    m_status_badge = new QLabel(tr("Offline"), m_status_card);
    m_status_badge->setProperty("glassChip", true);
    title_row->addWidget(m_status_title);
    title_row->addWidget(m_status_badge);
    title_row->addStretch();

    m_status_caption = new QLabel(
        tr("Browse public rooms or host your own room to play wireless multiplayer games."),
        m_status_card);
    m_status_caption->setProperty("muted", true);
    m_status_caption->setWordWrap(true);

    status_col->addLayout(title_row);
    status_col->addWidget(m_status_caption);
    status_row->addLayout(status_col, 1);

    m_show_room_btn = new QPushButton(tr("Show Room"), m_status_card);
    m_show_room_btn->setProperty("glassRole", QStringLiteral("primary"));
    m_show_room_btn->setCursor(Qt::PointingHandCursor);
    m_show_room_btn->setEnabled(false);

    m_leave_room_btn = new QPushButton(tr("Leave Room"), m_status_card);
    m_leave_room_btn->setProperty("glassRole", QStringLiteral("danger"));
    m_leave_room_btn->setCursor(Qt::PointingHandCursor);
    m_leave_room_btn->setEnabled(false);

    status_row->addWidget(m_show_room_btn);
    status_row->addWidget(m_leave_room_btn);
    root->addWidget(m_status_card);

    // ── Action Cards Row ─────────────────────────────────────────────────
    auto* cards_row = new QHBoxLayout;
    cards_row->setSpacing(16);

    const auto make_action_card = [&](GlassCard*& card_out, QPushButton*& btn_out,
                                      const QString& heading, const QString& desc,
                                      const QString& btn_text, bool primary) {
        card_out = new GlassCard(this, 16);
        auto* lay = new QVBoxLayout(card_out);
        lay->setContentsMargins(22, 20, 22, 20);
        lay->setSpacing(10);

        auto* h = new QLabel(heading, card_out);
        QFont hf = h->font();
        hf.setBold(true);
        hf.setPointSizeF(hf.pointSizeF() + 1.5);
        h->setFont(hf);

        auto* d = new QLabel(desc, card_out);
        d->setProperty("muted", true);
        d->setWordWrap(true);

        btn_out = new QPushButton(btn_text, card_out);
        if (primary) {
            btn_out->setProperty("glassRole", QStringLiteral("primary"));
        }
        btn_out->setMinimumHeight(36);
        btn_out->setCursor(Qt::PointingHandCursor);

        lay->addWidget(h);
        lay->addWidget(d);
        lay->addStretch();
        lay->addWidget(btn_out);
        cards_row->addWidget(card_out, 1);
    };

    make_action_card(
        m_lobby_card, m_lobby_btn, tr("Public Lobby"),
        tr("Discover active rooms hosted by other players and filter by game or ping."),
        tr("Browse Public Lobby"), true);
    make_action_card(
        m_host_card, m_host_btn, tr("Host a Room"),
        tr("Create a public or password-protected room for friends to join your session."),
        tr("Create Room"), false);
    make_action_card(
        m_direct_card, m_direct_btn, tr("Direct Connect"),
        tr("Connect straight to a known server IP address, port, and nickname."),
        tr("Direct Connect"), false);

    root->addLayout(cards_row);
    root->addStretch(1);

    connect(m_lobby_btn, &QPushButton::clicked, this, &GlassMultiplayerPage::BrowseLobbyRequested);
    connect(m_host_btn, &QPushButton::clicked, this, &GlassMultiplayerPage::CreateRoomRequested);
    connect(m_direct_btn, &QPushButton::clicked, this,
            &GlassMultiplayerPage::DirectConnectRequested);
    connect(m_show_room_btn, &QPushButton::clicked, this,
            &GlassMultiplayerPage::ShowRoomRequested);
    connect(m_leave_room_btn, &QPushButton::clicked, this,
            &GlassMultiplayerPage::LeaveRoomRequested);

    UpdateIcons();
}

void GlassMultiplayerPage::SetConnectionState(Network::RoomMember::State state,
                                              const QString& room_info) {
    const bool joined = state == Network::RoomMember::State::Joined ||
                        state == Network::RoomMember::State::Moderator;
    const bool joining = state == Network::RoomMember::State::Joining;
    m_connected = joined;

    m_show_room_btn->setEnabled(joined);
    m_leave_room_btn->setEnabled(joined || joining);

    if (joined) {
        m_status_title->setText(room_info.isEmpty() ? tr("Connected to Room") : room_info);
        m_status_badge->setText(state == Network::RoomMember::State::Moderator ? tr("● Moderator")
                                                                               : tr("● Connected"));
        m_status_badge->setProperty("glassBadge", QStringLiteral("live"));
        m_status_badge->setProperty("glassChip", false);
        m_status_caption->setText(
            tr("Your room session is active. Open the room window to chat or manage players."));
    } else if (joining) {
        m_status_title->setText(tr("Connecting…"));
        m_status_badge->setText(tr("Joining"));
        m_status_badge->setProperty("glassBadge", QStringLiteral("accent"));
        m_status_badge->setProperty("glassChip", false);
        m_status_caption->setText(tr("Establishing connection to the multiplayer room…"));
    } else {
        m_status_title->setText(tr("Not Connected"));
        m_status_badge->setText(tr("Offline"));
        m_status_badge->setProperty("glassBadge", QString{});
        m_status_badge->setProperty("glassChip", true);
        m_status_caption->setText(
            tr("Browse public rooms or host your own room to play wireless multiplayer games."));
    }
    if (auto* st = m_status_badge->style()) {
        st->unpolish(m_status_badge);
        st->polish(m_status_badge);
    }
}

void GlassMultiplayerPage::RefreshTheme() {
    m_tokens = ActiveTokens();
    m_status_card->SetTokens(m_tokens);
    m_lobby_card->SetTokens(m_tokens);
    m_host_card->SetTokens(m_tokens);
    m_direct_card->SetTokens(m_tokens);
    UpdateIcons();
}

void GlassMultiplayerPage::UpdateIcons() {
    m_lobby_btn->setIcon(TintedIcon(QStringLiteral("globe"), m_tokens.on_accent, 16));
    m_host_btn->setIcon(TintedIcon(QStringLiteral("plus"), m_tokens.ink_full, 16));
    m_direct_btn->setIcon(TintedIcon(QStringLiteral("link"), m_tokens.ink_full, 16));
    m_show_room_btn->setIcon(TintedIcon(QStringLiteral("users"), m_tokens.on_accent, 15));
    m_leave_room_btn->setIcon(TintedIcon(QStringLiteral("x"), m_tokens.danger, 15));
}

} // namespace GlassUi
