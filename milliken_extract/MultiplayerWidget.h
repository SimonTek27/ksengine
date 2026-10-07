#ifndef KSENGINE_QT_FREE
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QTimer>
#include <QHeaderView>
#include <memory>

namespace ks::sim {
class NetworkManager;
}

namespace ks::ui {

// ============================================================================
// ServerBrowserPanel — discover and join multiplayer servers
// ============================================================================
class ServerBrowserPanel : public QWidget {
    Q_OBJECT
public:
    explicit ServerBrowserPanel(ks::sim::NetworkManager* net, QWidget* parent = nullptr);

    void refresh();

signals:
    void joinRequested(const QString& host, int port);

private:
    void buildUI();
    void updateServerList();

    ks::sim::NetworkManager* m_net;
    QTableWidget* m_serverTable;
    QLineEdit* m_hostEdit;
    QSpinBox* m_portSpin;
    QPushButton* m_refreshBtn;
    QPushButton* m_joinBtn;
    QTimer* m_refreshTimer;

    struct ServerInfo {
        QString name;
        QString host;
        int port;
        int players;
        int maxPlayers;
        float ping;
        QString track;
        QString car;
    };
    QVector<ServerInfo> m_servers;
};

// ============================================================================
// LobbyPanel — pre-race lobby with player list and chat
// ============================================================================
class LobbyPanel : public QWidget {
    Q_OBJECT
public:
    explicit LobbyPanel(ks::sim::NetworkManager* net, QWidget* parent = nullptr);

    void setServerName(const QString& name);
    void setPlayerList(const QStringList& players);
    void addChatMessage(const QString& sender, const QString& message);
    void setStatus(const QString& status);
    void updateStats(float rtt, float packetLoss, float sendBw, float recvBw);

signals:
    void chatSent(const QString& message);
    void readyToggled(bool ready);
    void leaveRequested();

private:
    void buildUI();

    ks::sim::NetworkManager* m_net;
    QLabel* m_serverNameLabel;
    QLabel* m_statusLabel;
    QLabel* m_statsLabel;
    QListWidget* m_playerList;
    QTextEdit* m_chatLog;
    QLineEdit* m_chatInput;
    QPushButton* m_readyBtn;
    QPushButton* m_leaveBtn;
    bool m_ready = false;
};

// ============================================================================
// MultiplayerWidget — main multiplayer UI with tabs
// ============================================================================
class MultiplayerWidget : public QWidget {
    Q_OBJECT
public:
    explicit MultiplayerWidget(ks::sim::NetworkManager* net, QWidget* parent = nullptr);

    void refresh();
    void onConnectedToServer(const QString& host, int port);
    void onDisconnected();

signals:
    void closed();

private:
    void buildUI();
    void showLobby(const QString& serverName);

    ks::sim::NetworkManager* m_net;
    QTabWidget* m_tabs;
    ServerBrowserPanel* m_browserPanel;
    LobbyPanel* m_lobbyPanel;
};

} // namespace ks::ui
#endif // !KSENGINE_QT_FREE
