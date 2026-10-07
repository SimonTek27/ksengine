#ifndef KSENGINE_QT_FREE
#include "MultiplayerWidget.h"
#include "NetworkManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QListWidget>
#include <QTableWidget>
#include <QTextEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QHeaderView>
#include <QTimer>
#include <QDebug>
#include <QDateTime>

namespace ks::ui {

// ============================================================================
// ServerBrowserPanel
// ============================================================================

ServerBrowserPanel::ServerBrowserPanel(ks::sim::NetworkManager* net, QWidget* parent)
    : QWidget(parent), m_net(net)
{
    buildUI();

    m_refreshTimer = new QTimer(this);
    connect(m_refreshTimer, &QTimer::timeout, this, &ServerBrowserPanel::refresh);
    m_refreshTimer->start(5000);
    refresh();
}

void ServerBrowserPanel::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Server list
    auto* listGroup = new QGroupBox("Servers");
    auto* listLayout = new QVBoxLayout(listGroup);

    m_serverTable = new QTableWidget(0, 6);
    m_serverTable->setHorizontalHeaderLabels({"Name", "Players", "Ping", "Track", "Car", "Port"});
    m_serverTable->horizontalHeader()->setStretchLastSection(true);
    m_serverTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_serverTable->setSelectionBehavior(QTableWidget::SelectRows);
    m_serverTable->setSelectionMode(QTableWidget::SingleSelection);
    m_serverTable->setEditTriggers(QTableWidget::NoEditTriggers);
    listLayout->addWidget(m_serverTable);

    mainLayout->addWidget(listGroup);

    // Direct connect
    auto* connectGroup = new QGroupBox("Direct Connect");
    auto* connectLayout = new QHBoxLayout(connectGroup);

    connectLayout->addWidget(new QLabel("Host:"));
    m_hostEdit = new QLineEdit("127.0.0.1");
    connectLayout->addWidget(m_hostEdit);

    connectLayout->addWidget(new QLabel("Port:"));
    m_portSpin = new QSpinBox;
    m_portSpin->setRange(1, 65535);
    m_portSpin->setValue(40000);
    connectLayout->addWidget(m_portSpin);

    mainLayout->addWidget(connectGroup);

    // Buttons
    auto* btnLayout = new QHBoxLayout;
    m_refreshBtn = new QPushButton("Refresh");
    m_joinBtn = new QPushButton("Join Server");

    btnLayout->addStretch();
    btnLayout->addWidget(m_refreshBtn);
    btnLayout->addWidget(m_joinBtn);

    mainLayout->addLayout(btnLayout);

    connect(m_refreshBtn, &QPushButton::clicked, this, &ServerBrowserPanel::refresh);
    connect(m_joinBtn, &QPushButton::clicked, this, [this]() {
        int row = m_serverTable->currentRow();
        if (row >= 0 && row < m_servers.size()) {
            emit joinRequested(m_servers[row].host, m_servers[row].port);
        } else {
            emit joinRequested(m_hostEdit->text(), m_portSpin->value());
        }
    });

    connect(m_serverTable, &QTableWidget::cellDoubleClicked, this, [this](int row) {
        if (row >= 0 && row < m_servers.size()) {
            emit joinRequested(m_servers[row].host, m_servers[row].port);
        }
    });
}

void ServerBrowserPanel::refresh()
{
    m_serverTable->setRowCount(0);
    m_servers.clear();

    ServerInfo local;
    local.name = "Local Server";
    local.host = "127.0.0.1";
    local.port = 40000;
    local.players = m_net ? m_net->clientCount() : 0;
    local.maxPlayers = 8;
    local.ping = 0;
    local.track = "N/A";
    local.car = "N/A";
    m_servers.append(local);

    m_serverTable->setRowCount(m_servers.size());
    for (int i = 0; i < m_servers.size(); ++i) {
        auto& s = m_servers[i];
        m_serverTable->setItem(i, 0, new QTableWidgetItem(s.name));
        m_serverTable->setItem(i, 1, new QTableWidgetItem(
            QString("%1/%2").arg(s.players).arg(s.maxPlayers)));
        m_serverTable->setItem(i, 2, new QTableWidgetItem(
            s.ping > 0 ? QString("%1 ms").arg(s.ping, 0, 'f', 0) : "--"));
        m_serverTable->setItem(i, 3, new QTableWidgetItem(s.track));
        m_serverTable->setItem(i, 4, new QTableWidgetItem(s.car));
        m_serverTable->setItem(i, 5, new QTableWidgetItem(QString::number(s.port)));
    }
}

// ============================================================================
// LobbyPanel
// ============================================================================

LobbyPanel::LobbyPanel(ks::sim::NetworkManager* net, QWidget* parent)
    : QWidget(parent), m_net(net)
{
    buildUI();
}

void LobbyPanel::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    // Server info header
    auto* headerGroup = new QGroupBox("Server Info");
    auto* headerLayout = new QGridLayout(headerGroup);

    m_serverNameLabel = new QLabel("Server: --");
    m_serverNameLabel->setStyleSheet("font-weight: bold; font-size: 14px;");
    headerLayout->addWidget(m_serverNameLabel, 0, 0, 1, 2);

    m_statusLabel = new QLabel("Status: Waiting");
    headerLayout->addWidget(m_statusLabel, 1, 0);

    m_statsLabel = new QLabel("RTT: -- ms | Loss: --%");
    headerLayout->addWidget(m_statsLabel, 1, 1);

    mainLayout->addWidget(headerGroup);

    // Player list
    auto* playerGroup = new QGroupBox("Players in Lobby");
    auto* playerLayout = new QVBoxLayout(playerGroup);

    m_playerList = new QListWidget;
    playerLayout->addWidget(m_playerList);

    mainLayout->addWidget(playerGroup);

    // Chat
    auto* chatGroup = new QGroupBox("Chat");
    auto* chatLayout = new QVBoxLayout(chatGroup);

    m_chatLog = new QTextEdit;
    m_chatLog->setReadOnly(true);
    m_chatLog->setMaximumHeight(150);
    chatLayout->addWidget(m_chatLog);

    auto* chatInputLayout = new QHBoxLayout;
    m_chatInput = new QLineEdit;
    m_chatInput->setPlaceholderText("Type a message...");
    auto* sendBtn = new QPushButton("Send");
    chatInputLayout->addWidget(m_chatInput);
    chatInputLayout->addWidget(sendBtn);
    chatLayout->addLayout(chatInputLayout);

    mainLayout->addWidget(chatGroup);

    // Bottom buttons
    auto* btnLayout = new QHBoxLayout;
    m_readyBtn = new QPushButton("Ready");
    m_readyBtn->setCheckable(true);
    m_leaveBtn = new QPushButton("Leave");

    btnLayout->addStretch();
    btnLayout->addWidget(m_readyBtn);
    btnLayout->addWidget(m_leaveBtn);

    mainLayout->addLayout(btnLayout);

    connect(sendBtn, &QPushButton::clicked, this, [this]() {
        QString msg = m_chatInput->text().trimmed();
        if (!msg.isEmpty()) {
            emit chatSent(msg);
            m_chatInput->clear();
        }
    });

    connect(m_chatInput, &QLineEdit::returnPressed, this, [this]() {
        QString msg = m_chatInput->text().trimmed();
        if (!msg.isEmpty()) {
            emit chatSent(msg);
            m_chatInput->clear();
        }
    });

    connect(m_readyBtn, &QPushButton::clicked, this, [this]() {
        m_ready = !m_ready;
        m_readyBtn->setText(m_ready ? "Not Ready" : "Ready");
        m_readyBtn->setChecked(m_ready);
        emit readyToggled(m_ready);
    });

    connect(m_leaveBtn, &QPushButton::clicked, this, &LobbyPanel::leaveRequested);
}

void LobbyPanel::setServerName(const QString& name)
{
    m_serverNameLabel->setText("Server: " + name);
}

void LobbyPanel::setPlayerList(const QStringList& players)
{
    m_playerList->clear();
    m_playerList->addItems(players);
}

void LobbyPanel::addChatMessage(const QString& sender, const QString& message)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_chatLog->append(QString("[%1] <b>%2:</b> %3").arg(timestamp, sender, message));
}

void LobbyPanel::setStatus(const QString& status)
{
    m_statusLabel->setText("Status: " + status);
}

void LobbyPanel::updateStats(float rtt, float packetLoss, float sendBw, float recvBw)
{
    m_statsLabel->setText(QString("RTT: %1 ms | Loss: %2% | Send: %3 KB/s | Recv: %4 KB/s")
        .arg(rtt, 0, 'f', 0)
        .arg(packetLoss * 100, 0, 'f', 1)
        .arg(sendBw, 0, 'f', 1)
        .arg(recvBw, 0, 'f', 1));
}

// ============================================================================
// MultiplayerWidget
// ============================================================================

MultiplayerWidget::MultiplayerWidget(ks::sim::NetworkManager* net, QWidget* parent)
    : QWidget(parent), m_net(net)
{
    setWindowTitle("Multiplayer");
    setMinimumSize(600, 500);
    buildUI();
}

void MultiplayerWidget::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);

    m_tabs = new QTabWidget;

    m_browserPanel = new ServerBrowserPanel(m_net);
    m_lobbyPanel = new LobbyPanel(m_net);

    m_tabs->addTab(m_browserPanel, "Server Browser");
    m_tabs->addTab(m_lobbyPanel, "Lobby");
    m_tabs->setTabEnabled(1, false);

    mainLayout->addWidget(m_tabs);

    auto* bottomLayout = new QHBoxLayout;
    auto* closeBtn = new QPushButton("Close");
    bottomLayout->addStretch();
    bottomLayout->addWidget(closeBtn);

    mainLayout->addLayout(bottomLayout);

    connect(closeBtn, &QPushButton::clicked, this, &MultiplayerWidget::closed);

    // Handle join requests from browser
    connect(m_browserPanel, &ServerBrowserPanel::joinRequested,
        this, [this](const QString& host, int port) {
            if (!m_net) return;
            qInfo() << "Multiplayer: Joining" << host << ":" << port;
            m_net->joinServer(host, static_cast<uint16_t>(port), "Player", "gte3");
        });

    // Handle lobby signals
    connect(m_lobbyPanel, &LobbyPanel::chatSent, this, [this](const QString& msg) {
        if (m_net) m_net->sendChatMessage(msg);
    });

    connect(m_lobbyPanel, &LobbyPanel::leaveRequested, this, [this]() {
        if (m_net) m_net->disconnectFromServer();
        m_tabs->setTabEnabled(1, false);
        m_tabs->setCurrentIndex(0);
    });

    // Wire up NetworkManager signals
    if (m_net) {
        connect(m_net, &ks::sim::NetworkManager::clientConnectedToServer,
            this, &MultiplayerWidget::onConnectedToServer);

        connect(m_net, &ks::sim::NetworkManager::disconnectedFromServer,
            this, &MultiplayerWidget::onDisconnected);

        connect(m_net, &ks::sim::NetworkManager::chatMessageReceived,
            this, [this](uint32_t, const QString& sender, const QString& msg) {
                m_lobbyPanel->addChatMessage(sender, msg);
            });

        connect(m_net, &ks::sim::NetworkManager::playerListUpdated,
            this, [this](const QStringList& players) {
                m_lobbyPanel->setPlayerList(players);
            });

        connect(m_net, &ks::sim::NetworkManager::remoteClientJoined,
            this, [this](int, uint32_t, const QString& name) {
                m_lobbyPanel->addChatMessage("System", name + " joined the server");
            });

        connect(m_net, &ks::sim::NetworkManager::remoteClientLeft,
            this, [this](int, const QString& name) {
                m_lobbyPanel->addChatMessage("System", name + " left the server");
            });

        connect(m_net, &ks::sim::NetworkManager::statsUpdated,
            this, [this](const ks::sim::net::NetworkStats& stats) {
                m_lobbyPanel->updateStats(stats.rtt, stats.packetLoss,
                                          stats.sendBandwidth, stats.recvBandwidth);
            });

        connect(m_net, &ks::sim::NetworkManager::serverStarted,
            this, [this](uint16_t port) {
                showLobby("Local Server :" + QString::number(port));
            });
    }
}

void MultiplayerWidget::refresh()
{
    m_browserPanel->refresh();
}

void MultiplayerWidget::onConnectedToServer(const QString& host, int port)
{
    showLobby(host + ":" + QString::number(port));
}

void MultiplayerWidget::onDisconnected()
{
    m_tabs->setTabEnabled(1, false);
    m_tabs->setCurrentIndex(0);
    m_lobbyPanel->setStatus("Disconnected");
}

void MultiplayerWidget::showLobby(const QString& serverName)
{
    m_lobbyPanel->setServerName(serverName);
    m_lobbyPanel->setStatus("Connected");
    m_tabs->setTabEnabled(1, true);
    m_tabs->setCurrentIndex(1);
    m_lobbyPanel->addChatMessage("System", "Connected to server");
}

} // namespace ks::ui
#endif // !KSENGINE_QT_FREE
