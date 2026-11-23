// SPDX-FileCopyrightText: Copyright 2026 Lichii Project
// SPDX-License-Identifier: GPL-3.0-or-later

#pragma once

#include <QWidget>

#include "lichii/glass/glass_tokens.h"
#include "network/network.h"

class QLabel;
class QPushButton;

namespace GlassUi {

class GlassCard;

class GlassMultiplayerPage : public QWidget {
    Q_OBJECT

public:
    explicit GlassMultiplayerPage(QWidget* parent = nullptr);

    void SetConnectionState(Network::RoomMember::State state, const QString& room_info = {});
    void RefreshTheme();

signals:
    void BrowseLobbyRequested();
    void CreateRoomRequested();
    void DirectConnectRequested();
    void ShowRoomRequested();
    void LeaveRoomRequested();

private:
    void UpdateIcons();

    PaletteTokens m_tokens;
    bool m_connected{false};

    GlassCard* m_status_card{nullptr};
    QLabel* m_status_badge{nullptr};
    QLabel* m_status_title{nullptr};
    QLabel* m_status_caption{nullptr};
    QPushButton* m_show_room_btn{nullptr};
    QPushButton* m_leave_room_btn{nullptr};

    GlassCard* m_lobby_card{nullptr};
    GlassCard* m_host_card{nullptr};
    GlassCard* m_direct_card{nullptr};
    QPushButton* m_lobby_btn{nullptr};
    QPushButton* m_host_btn{nullptr};
    QPushButton* m_direct_btn{nullptr};
};

} // namespace GlassUi
