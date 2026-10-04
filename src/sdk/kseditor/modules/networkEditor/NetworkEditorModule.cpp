#include "NetworkEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QStandardPaths>
#include <QDir>
#include <QSettings>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QScrollBar>
#include <QApplication>
#include <QThread>
#include <QDateTime>
#include <QNetworkRequest>
#include <QUrl>
#include <QHostInfo>
#include <QNetworkInterface>
#include <QCryptographicHash>
#include <QRandomGenerator>

namespace ks {
namespace network {

NetworkEditorModule::NetworkEditorModule(QWidget* parent)
    : ModuleGuiBase(parent)
    , m_tabWidget(nullptr)
    , m_cloudSyncTab(nullptr)
    , m_syncTree(nullptr)
    , m_syncBtn(nullptr)
    , m_syncAllBtn(nullptr)
    , m_syncProgress(nullptr)
    , m_syncStatus(nullptr)
    , m_autoSyncCheck(nullptr)
    , m_syncIntervalSpin(nullptr)
    , m_collabTab(nullptr)
    , m_collabNameEdit(nullptr)
    , m_collabRoomEdit(nullptr)
    , m_collabStartBtn(nullptr)
    , m_collabStopBtn(nullptr)
    , m_collabUsersTree(nullptr)
    , m_collabChat(nullptr)
    , m_collabMessageEdit(nullptr)
    , m_collabSendBtn(nullptr)
    , m_wsTab(nullptr)
    , m_wsUrlEdit(nullptr)
    , m_wsConnectBtn(nullptr)
    , m_wsDisconnectBtn(nullptr)
    , m_wsLog(nullptr)
    , m_wsSendEdit(nullptr)
    , m_wsSendBtn(nullptr)
    , m_wsProtocolCombo(nullptr)
    , m_serverTab(nullptr)
    , m_serverPortSpin(nullptr)
    , m_serverStartBtn(nullptr)
    , m_serverStopBtn(nullptr)
    , m_serverClientsTree(nullptr)
    , m_serverLog(nullptr)
    , m_serverSslCheck(nullptr)
    , m_discoveryTab(nullptr)
    , m_discoverBtn(nullptr)
    , m_discoveredDevicesTree(nullptr)
    , m_discoveryTypeCombo(nullptr)
    , m_discoveryTimeoutSpin(nullptr)
    , m_settingsTab(nullptr)
    , m_cloudProviderEdit(nullptr)
    , m_cloudApiKeyEdit(nullptr)
    , m_cloudBucketEdit(nullptr)
    , m_cloudRegionCombo(nullptr)
    , m_cloudEncryptCheck(nullptr)
    , m_settingsSaveBtn(nullptr)
    , m_settingsLoadBtn(nullptr)
    , m_webSocket(nullptr)
    , m_webSocketServer(nullptr)
    , m_networkManager(nullptr)
    , m_syncTimer(nullptr)
    , m_discoveryTimer(nullptr)
    , m_collabActive(false)
    , m_serverActive(false)
    , m_collabRoom("")
    , m_collabUserName("")
{
    setObjectName("NetworkEditorModule");
}

bool NetworkEditorModule::initialize() {
    if (m_uiBuilt) return true;
    
    m_networkManager = new QNetworkAccessManager(this);
    m_webSocket = new QWebSocket("", QWebSocketProtocol::VersionLatest, this);
    m_webSocketServer = new QWebSocketServer("ksEditor Collab Server", QWebSocketServer::NonSecureMode, this);
    m_syncTimer = new QTimer(this);
    m_discoveryTimer = new QTimer(this);
    
    connect(m_webSocket, &QWebSocket::connected, this, &NetworkEditorModule::onWebSocketConnected);
    connect(m_webSocket, &QWebSocket::disconnected, this, &NetworkEditorModule::onWebSocketDisconnected);
    connect(m_webSocket, &QWebSocket::textMessageReceived, this, &NetworkEditorModule::onWebSocketTextReceived);
    connect(m_webSocket, QOverload<QAbstractSocket::SocketError>::of(&QWebSocket::errorOccurred),
            this, &NetworkEditorModule::onWebSocketError);
    connect(m_webSocketServer, &QWebSocketServer::newConnection, this, &NetworkEditorModule::onServerNewConnection);
    connect(m_webSocketServer, &QWebSocketServer::closed, this, &NetworkEditorModule::onServerClosed);
    connect(m_syncTimer, &QTimer::timeout, this, &NetworkEditorModule::onCloudSyncClicked);
    connect(m_discoveryTimer, &QTimer::timeout, this, &NetworkEditorModule::refreshDeviceList);
    
    ModuleGuiBase::initialize();
    loadSettings();
    return true;
}

void NetworkEditorModule::shutdown() {
    if (m_collabActive) onCollabStopClicked();
    if (m_serverActive) onServerStopClicked();
    if (m_webSocket->state() == QAbstractSocket::ConnectedState) m_webSocket->close();
    saveSettings();
    ModuleGuiBase::shutdown();
}

void NetworkEditorModule::importFile(const QString& filePath) {
    QFileInfo info(filePath);
    if (info.suffix().toLower() == "sync" || info.suffix().toLower() == "json") {
        loadSettings();
        this->log(QString("Imported network config: %1").arg(filePath));
    }
}

void NetworkEditorModule::exportFile(const QString& filePath) {
    saveSettings();
    QFile file(filePath);
    if (file.open(QIODevice::WriteOnly)) {
        QJsonObject obj;
        obj["cloudProvider"] = m_cloudProviderEdit->currentText();
        obj["cloudRegion"] = m_cloudRegionCombo->currentText();
        obj["cloudBucket"] = m_cloudBucketEdit->text();
        obj["cloudEncrypt"] = m_cloudEncryptCheck->isChecked();
        obj["autoSync"] = m_autoSyncCheck->isChecked();
        obj["syncInterval"] = m_syncIntervalSpin->value();
        obj["serverPort"] = m_serverPortSpin->value();
        obj["serverSsl"] = m_serverSslCheck->isChecked();
        file.write(QJsonDocument(obj).toJson(QJsonDocument::Indented));
        this->log(QString("Exported network config: %1").arg(filePath));
    }
}

void NetworkEditorModule::buildUI() {
    m_tabWidget = new QTabWidget(this);
    m_tabWidget->setStyleSheet(
        "QTabWidget::pane { border: 1px solid #3a3a3a; background: #1e1e1e; }"
        "QTabBar::tab { background: #2d2d2d; color: #aaa; padding: 8px 16px; border: 1px solid #3a3a3a; border-bottom: none; }"
        "QTabBar::tab:selected { background: #3a5a8a; color: #fff; }"
        "QTabBar::tab:hover { background: #4a6a9a; }"
    );
    
    setupCloudSyncTab();
    setupCollaborationTab();
    setupWebSocketTab();
    setupServerTab();
    setupDiscoveryTab();
    setupSettingsTab();
    
    m_tabWidget->addTab(m_cloudSyncTab, "Cloud Sync");
    m_tabWidget->addTab(m_collabTab, "Collaboration");
    m_tabWidget->addTab(m_wsTab, "WebSocket");
    m_tabWidget->addTab(m_serverTab, "Server");
    m_tabWidget->addTab(m_discoveryTab, "Discovery");
    m_tabWidget->addTab(m_settingsTab, "Settings");
    
    m_mainLayout->insertWidget(1, m_tabWidget, 1);
}

void NetworkEditorModule::setupCloudSyncTab() {
    m_cloudSyncTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_cloudSyncTab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Status bar
    QGroupBox* statusGroup = createGroupBox("Sync Status");
    QVBoxLayout* statusLayout = new QVBoxLayout(statusGroup);
    
    QHBoxLayout* statusRow = new QHBoxLayout();
    m_syncStatus = createLabel("Status: Disconnected", "color: #ff6b6b; font-weight: bold;");
    statusRow->addWidget(m_syncStatus);
    statusRow->addStretch();
    
    QPushButton* statusRefresh = createButton("Refresh");
    connect(statusRefresh, &QPushButton::clicked, this, &NetworkEditorModule::onSyncStatusChanged);
    statusRow->addWidget(statusRefresh);
    statusLayout->addLayout(statusRow);
    
    m_syncProgress = new QProgressBar(this);
    m_syncProgress->setVisible(false);
    statusLayout->addWidget(m_syncProgress);
    
    layout->addWidget(statusGroup);
    
    // Sync items tree
    QGroupBox* syncGroup = createGroupBox("Sync Items");
    QVBoxLayout* syncLayout = new QVBoxLayout(syncGroup);
    
    m_syncTree = createTreeWidget({"Item", "Type", "Local", "Remote", "Status", "Size", "Modified"});
    m_syncTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_syncTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_syncTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_syncTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_syncTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_syncTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    m_syncTree->header()->setSectionResizeMode(6, QHeaderView::ResizeToContents);
    m_syncTree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    syncLayout->addWidget(m_syncTree);
    
    // Populate with default items
    QStringList syncItems = {
        "projects", "assets", "materials", "scripts", "configs", "shaders", "textures", "models"
    };
    for (const QString& item : syncItems) {
        QTreeWidgetItem* treeItem = new QTreeWidgetItem(m_syncTree, {item, "Folder", "✓", "✓", "Synced", "-", "Just now"});
        treeItem->setCheckState(0, Qt::Checked);
    }
    
    layout->addWidget(syncGroup, 1);
    
    // Controls
    QGroupBox* controlGroup = createGroupBox("Sync Controls");
    QHBoxLayout* controlLayout = new QHBoxLayout(controlGroup);
    
    m_autoSyncCheck = createCheckBox("Auto-sync", true);
    connect(m_autoSyncCheck, &QCheckBox::toggled, this, [this](bool checked) {
        if (checked) m_syncTimer->start(m_syncIntervalSpin->value() * 60000);
        else m_syncTimer->stop();
    });
    controlLayout->addWidget(m_autoSyncCheck);
    
    controlLayout->addWidget(createLabel("Interval (min):"));
    m_syncIntervalSpin = createSpinBox(1, 1440, 30);
    m_syncIntervalSpin->setMaximumWidth(80);
    connect(m_syncIntervalSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, [this](int val) {
        if (m_autoSyncCheck->isChecked()) m_syncTimer->start(val * 60000);
    });
    controlLayout->addWidget(m_syncIntervalSpin);
    
    controlLayout->addStretch();
    
    m_syncBtn = createButton("Sync Selected", "success");
    connect(m_syncBtn, &QPushButton::clicked, this, &NetworkEditorModule::onCloudSyncClicked);
    controlLayout->addWidget(m_syncBtn);
    
    m_syncAllBtn = createButton("Sync All", "primary");
    connect(m_syncAllBtn, &QPushButton::clicked, this, [this]() {
        for (int i = 0; i < m_syncTree->topLevelItemCount(); ++i) {
            m_syncTree->topLevelItem(i)->setCheckState(0, Qt::Checked);
        }
        onCloudSyncClicked();
    });
    controlLayout->addWidget(m_syncAllBtn);
    
    layout->addWidget(controlGroup);
}

void NetworkEditorModule::setupCollaborationTab() {
    m_collabTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_collabTab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Connection setup
    QGroupBox* connGroup = createGroupBox("Collaboration Session");
    QFormLayout* connLayout = new QFormLayout(connGroup);
    
    m_collabNameEdit = new QLineEdit();
    m_collabNameEdit->setPlaceholderText("Your name");
    m_collabNameEdit->setText(QHostInfo::localHostName());
    connLayout->addRow("Name:", m_collabNameEdit);
    
    m_collabRoomEdit = new QLineEdit();
    m_collabRoomEdit->setPlaceholderText("Room ID or leave empty for new");
    connLayout->addRow("Room:", m_collabRoomEdit);
    
    QHBoxLayout* roomBtnLayout = new QHBoxLayout();
    m_collabStartBtn = createButton("Start Session", "success");
    connect(m_collabStartBtn, &QPushButton::clicked, this, &NetworkEditorModule::onCollabStartClicked);
    roomBtnLayout->addWidget(m_collabStartBtn);
    
    m_collabStopBtn = createButton("Stop Session", "danger");
    m_collabStopBtn->setEnabled(false);
    connect(m_collabStopBtn, &QPushButton::clicked, this, &NetworkEditorModule::onCollabStopClicked);
    roomBtnLayout->addWidget(m_collabStopBtn);
    
    QPushButton* newRoomBtn = createButton("New Room");
    connect(newRoomBtn, &QPushButton::clicked, this, [this]() {
        QString roomId = QString("ksedit-%1").arg(QRandomGenerator::global()->generate() % 10000, 4, 10, QChar('0'));
        m_collabRoomEdit->setText(roomId);
    });
    roomBtnLayout->addWidget(newRoomBtn);
    
    connLayout->addRow(roomBtnLayout);
    layout->addWidget(connGroup);
    
    // Users and chat
    QSplitter* collabSplitter = createSplitter();
    
    // Users
    QGroupBox* usersGroup = createGroupBox("Connected Users");
    QVBoxLayout* usersLayout = new QVBoxLayout(usersGroup);
    
    m_collabUsersTree = createTreeWidget({"User", "Status", "Role", "Latency"});
    m_collabUsersTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_collabUsersTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_collabUsersTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_collabUsersTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    usersLayout->addWidget(m_collabUsersTree);
    
    collabSplitter->addWidget(usersGroup);
    
    // Chat
    QGroupBox* chatGroup = createGroupBox("Chat");
    QVBoxLayout* chatLayout = new QVBoxLayout(chatGroup);
    
    m_collabChat = new QTextEdit();
    m_collabChat->setReadOnly(true);
    m_collabChat->setStyleSheet("QTextEdit { background: #1a1a1a; color: #c8c8c8; font-family: Consolas; font-size: 10px; border: 1px solid #3a3a3a; }");
    chatLayout->addWidget(m_collabChat);
    
    QHBoxLayout* chatInputLayout = new QHBoxLayout();
    m_collabMessageEdit = new QLineEdit();
    m_collabMessageEdit->setPlaceholderText("Type message...");
    m_collabMessageEdit->setEnabled(false);
    connect(m_collabMessageEdit, &QLineEdit::returnPressed, this, [this]() {
        if (!m_collabMessageEdit->text().isEmpty()) {
            sendCollabMessage(m_collabMessageEdit->text());
            m_collabMessageEdit->clear();
        }
    });
    chatInputLayout->addWidget(m_collabMessageEdit);
    
    m_collabSendBtn = createButton("Send");
    m_collabSendBtn->setEnabled(false);
    connect(m_collabSendBtn, &QPushButton::clicked, this, [this]() {
        if (!m_collabMessageEdit->text().isEmpty()) {
            sendCollabMessage(m_collabMessageEdit->text());
            m_collabMessageEdit->clear();
        }
    });
    chatInputLayout->addWidget(m_collabSendBtn);
    
    chatLayout->addLayout(chatInputLayout);
    collabSplitter->addWidget(chatGroup);
    collabSplitter->setStretchFactor(0, 1);
    collabSplitter->setStretchFactor(1, 2);
    
    layout->addWidget(collabSplitter, 1);
}

void NetworkEditorModule::setupWebSocketTab() {
    m_wsTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_wsTab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Connection
    QGroupBox* connGroup = createGroupBox("WebSocket Connection");
    QHBoxLayout* connLayout = new QHBoxLayout(connGroup);
    
    connLayout->addWidget(createLabel("URL:"));
    m_wsUrlEdit = new QLineEdit();
    m_wsUrlEdit->setPlaceholderText("ws://localhost:8080/ws or wss://server.com/ws");
    m_wsUrlEdit->setText("ws://localhost:8080/ws");
    connLayout->addWidget(m_wsUrlEdit);
    
    connLayout->addWidget(createLabel("Protocol:"));
    m_wsProtocolCombo = createComboBox({"Auto", "ws", "wss"});
    m_wsProtocolCombo->setMaximumWidth(100);
    connLayout->addWidget(m_wsProtocolCombo);
    
    m_wsConnectBtn = createButton("Connect", "success");
    connect(m_wsConnectBtn, &QPushButton::clicked, this, [this]() {
        QString url = m_wsUrlEdit->text();
        if (!url.isEmpty()) {
            m_webSocket->open(QUrl(url));
            addLogEntry(QString("Connecting to %1...").arg(url), "info");
        }
    });
    connLayout->addWidget(m_wsConnectBtn);
    
    m_wsDisconnectBtn = createButton("Disconnect", "danger");
    m_wsDisconnectBtn->setEnabled(false);
    connect(m_wsDisconnectBtn, &QPushButton::clicked, this, [this]() {
        m_webSocket->close();
    });
    connLayout->addWidget(m_wsDisconnectBtn);
    
    layout->addWidget(connGroup);
    
    // Log
    QGroupBox* logGroup = createGroupBox("WebSocket Log");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    
    m_wsLog = new QTextEdit();
    m_wsLog->setReadOnly(true);
    m_wsLog->setStyleSheet("QTextEdit { background: #1a1a1a; color: #c8c8c8; font-family: Consolas; font-size: 10px; border: 1px solid #3a3a3a; }");
    logLayout->addWidget(m_wsLog);
    
    layout->addWidget(logGroup, 1);
    
    // Send
    QGroupBox* sendGroup = createGroupBox("Send Message");
    QHBoxLayout* sendLayout = new QHBoxLayout(sendGroup);
    
    m_wsSendEdit = new QLineEdit();
    m_wsSendEdit->setPlaceholderText("JSON message to send...");
    m_wsSendEdit->setEnabled(false);
    connect(m_wsSendEdit, &QLineEdit::returnPressed, this, [this]() {
        if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
            m_webSocket->sendTextMessage(m_wsSendEdit->text());
            addLogEntry(QString(">> %1").arg(m_wsSendEdit->text()), "info");
            m_wsSendEdit->clear();
        }
    });
    sendLayout->addWidget(m_wsSendEdit);
    
    m_wsSendBtn = createButton("Send");
    m_wsSendBtn->setEnabled(false);
    connect(m_wsSendBtn, &QPushButton::clicked, this, [this]() {
        if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
            m_webSocket->sendTextMessage(m_wsSendEdit->text());
            addLogEntry(QString(">> %1").arg(m_wsSendEdit->text()), "info");
            m_wsSendEdit->clear();
        }
    });
    sendLayout->addWidget(m_wsSendBtn);
    
    layout->addWidget(sendGroup);
}

void NetworkEditorModule::setupServerTab() {
    m_serverTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_serverTab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Server controls
    QGroupBox* serverGroup = createGroupBox("Collaboration Server");
    QFormLayout* serverLayout = new QFormLayout(serverGroup);
    
    m_serverPortSpin = createSpinBox(1024, 65535, 8080);
    serverLayout->addRow("Port:", m_serverPortSpin);
    
    m_serverSslCheck = createCheckBox("SSL/TLS (wss://)", false);
    serverLayout->addRow(m_serverSslCheck);
    
    QHBoxLayout* serverBtnLayout = new QHBoxLayout();
    m_serverStartBtn = createButton("Start Server", "success");
    connect(m_serverStartBtn, &QPushButton::clicked, this, &NetworkEditorModule::onServerStartClicked);
    serverBtnLayout->addWidget(m_serverStartBtn);
    
    m_serverStopBtn = createButton("Stop Server", "danger");
    m_serverStopBtn->setEnabled(false);
    connect(m_serverStopBtn, &QPushButton::clicked, this, &NetworkEditorModule::onServerStopClicked);
    serverBtnLayout->addWidget(m_serverStopBtn);
    
    serverLayout->addRow(serverBtnLayout);
    layout->addWidget(serverGroup);
    
    // Connected clients
    QGroupBox* clientsGroup = createGroupBox("Connected Clients");
    QVBoxLayout* clientsLayout = new QVBoxLayout(clientsGroup);
    
    m_serverClientsTree = createTreeWidget({"Client", "IP", "Connected", "Messages", "Status"});
    m_serverClientsTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_serverClientsTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_serverClientsTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_serverClientsTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_serverClientsTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    clientsLayout->addWidget(m_serverClientsTree);
    
    layout->addWidget(clientsGroup, 1);
    
    // Server log
    QGroupBox* serverLogGroup = createGroupBox("Server Log");
    QVBoxLayout* serverLogLayout = new QVBoxLayout(serverLogGroup);
    
    m_serverLog = new QTextEdit();
    m_serverLog->setReadOnly(true);
    m_serverLog->setMaximumHeight(150);
    m_serverLog->setStyleSheet("QTextEdit { background: #1a1a1a; color: #c8c8c8; font-family: Consolas; font-size: 10px; border: 1px solid #3a3a3a; }");
    serverLogLayout->addWidget(m_serverLog);
    
    layout->addWidget(serverLogGroup);
}

void NetworkEditorModule::setupDiscoveryTab() {
    m_discoveryTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_discoveryTab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Discovery controls
    QGroupBox* discGroup = createGroupBox("Network Discovery");
    QHBoxLayout* discLayout = new QHBoxLayout(discGroup);
    
    discLayout->addWidget(createLabel("Type:"));
    m_discoveryTypeCombo = createComboBox({"Local Network (mDNS)", "Broadcast", "Known Hosts", "All"});
    discLayout->addWidget(m_discoveryTypeCombo);
    
    discLayout->addWidget(createLabel("Timeout (s):"));
    m_discoveryTimeoutSpin = createSpinBox(1, 60, 5);
    m_discoveryTimeoutSpin->setMaximumWidth(80);
    discLayout->addWidget(m_discoveryTimeoutSpin);
    
    discLayout->addStretch();
    
    m_discoverBtn = createButton("Discover Devices", "primary");
    connect(m_discoverBtn, &QPushButton::clicked, this, &NetworkEditorModule::onDiscoverClicked);
    discLayout->addWidget(m_discoverBtn);
    
    QPushButton* autoDiscBtn = createButton("Auto-discover");
    autoDiscBtn->setCheckable(true);
    connect(autoDiscBtn, &QPushButton::toggled, this, [this](bool checked) {
        if (checked) {
            m_discoveryTimer->start(30000);
            onDiscoverClicked();
        } else {
            m_discoveryTimer->stop();
        }
    });
    discLayout->addWidget(autoDiscBtn);
    
    layout->addWidget(discGroup);
    
    // Discovered devices
    QGroupBox* devGroup = createGroupBox("Discovered Devices");
    QVBoxLayout* devLayout = new QVBoxLayout(devGroup);
    
    m_discoveredDevicesTree = createTreeWidget({"Device", "IP", "Hostname", "Services", "Status", "Last Seen"});
    m_discoveredDevicesTree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_discoveredDevicesTree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_discoveredDevicesTree->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    m_discoveredDevicesTree->header()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
    m_discoveredDevicesTree->header()->setSectionResizeMode(4, QHeaderView::ResizeToContents);
    m_discoveredDevicesTree->header()->setSectionResizeMode(5, QHeaderView::ResizeToContents);
    devLayout->addWidget(m_discoveredDevicesTree);
    
    layout->addWidget(devGroup, 1);
}

void NetworkEditorModule::setupSettingsTab() {
    m_settingsTab = new QWidget();
    QVBoxLayout* layout = new QVBoxLayout(m_settingsTab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);
    
    // Cloud provider settings
    QGroupBox* cloudGroup = createGroupBox("Cloud Storage Provider");
    QFormLayout* cloudLayout = new QFormLayout(cloudGroup);
    
    m_cloudProviderEdit = createComboBox({"AWS S3", "Google Cloud Storage", "Azure Blob", "MinIO", "Custom S3-Compatible", "WebDAV", "Nextcloud", "TrueNAS", "Google Drive", "Dropbox", "OneDrive", "Local"});
    cloudLayout->addRow("Provider:", m_cloudProviderEdit);
    
    m_cloudRegionCombo = createComboBox({"us-east-1", "us-west-1", "us-west-2", "eu-west-1", "eu-central-1", "ap-southeast-1", "ap-northeast-1", "Custom"});
    cloudLayout->addRow("Region:", m_cloudRegionCombo);
    
    m_cloudBucketEdit = new QLineEdit();
    m_cloudBucketEdit->setPlaceholderText("Bucket/container name");
    cloudLayout->addRow("Bucket:", m_cloudBucketEdit);
    
    m_cloudApiKeyEdit = new QLineEdit();
    m_cloudApiKeyEdit->setPlaceholderText("API Key / Access Key");
    m_cloudApiKeyEdit->setEchoMode(QLineEdit::Password);
    cloudLayout->addRow("API Key:", m_cloudApiKeyEdit);
    
    QLineEdit* secretKeyEdit = new QLineEdit();
    secretKeyEdit->setPlaceholderText("Secret Key");
    secretKeyEdit->setEchoMode(QLineEdit::Password);
    cloudLayout->addRow("Secret:", secretKeyEdit);
    
    m_cloudEncryptCheck = createCheckBox("Encrypt files before upload", true);
    cloudLayout->addRow(m_cloudEncryptCheck);
    
    QCheckBox* compressCheck = createCheckBox("Compress before upload", true);
    cloudLayout->addRow(compressCheck);
    
    QSpinBox* chunkSizeSpin = createSpinBox(1, 100, 50, " MB");
    cloudLayout->addRow("Chunk Size:", chunkSizeSpin);
    
    layout->addWidget(cloudGroup);
    
    // Sync settings
    QGroupBox* syncGroup = createGroupBox("Sync Settings");
    QFormLayout* syncLayout = new QFormLayout(syncGroup);
    
    QCheckBox* conflictCheck = createCheckBox("Auto-resolve conflicts (newer wins)", true);
    syncLayout->addRow(conflictCheck);
    
    QCheckBox* deleteCheck = createCheckBox("Sync deletions", false);
    syncLayout->addRow(deleteCheck);
    
    QComboBox* conflictCombo = createComboBox({"Newer wins", "Larger wins", "Keep both", "Ask me"});
    syncLayout->addRow("Conflict Resolution:", conflictCombo);
    
    QSpinBox* bandwidthSpin = createSpinBox(0, 1000, 0, " Mbps (0 = unlimited)");
    syncLayout->addRow("Bandwidth Limit:", bandwidthSpin);
    
    layout->addWidget(syncGroup);
    
    // Buttons
    QHBoxLayout* btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    
    m_settingsSaveBtn = createButton("Save Settings", "success");
    connect(m_settingsSaveBtn, &QPushButton::clicked, this, &NetworkEditorModule::onSettingsSave);
    btnLayout->addWidget(m_settingsSaveBtn);
    
    m_settingsLoadBtn = createButton("Load Settings");
    connect(m_settingsLoadBtn, &QPushButton::clicked, this, &NetworkEditorModule::onSettingsLoad);
    btnLayout->addWidget(m_settingsLoadBtn);
    
    QPushButton* testBtn = createButton("Test Connection", "primary");
    connect(testBtn, &QPushButton::clicked, this, [this]() {
        testCloudConnection();
    });
    btnLayout->addWidget(testBtn);
    
    layout->addLayout(btnLayout);
    layout->addStretch();
}

void NetworkEditorModule::loadSettings() {
    QSettings settings;
    settings.beginGroup("NetworkEditor");
    
    m_cloudProviderEdit->setCurrentText(settings.value("cloudProvider", "AWS S3").toString());
    m_cloudRegionCombo->setCurrentText(settings.value("cloudRegion", "us-east-1").toString());
    m_cloudBucketEdit->setText(settings.value("cloudBucket", "").toString());
    m_cloudEncryptCheck->setChecked(settings.value("cloudEncrypt", true).toBool());
    m_autoSyncCheck->setChecked(settings.value("autoSync", true).toBool());
    m_syncIntervalSpin->setValue(settings.value("syncInterval", 30).toInt());
    m_serverPortSpin->setValue(settings.value("serverPort", 8080).toInt());
    m_serverSslCheck->setChecked(settings.value("serverSsl", false).toBool());
    
    settings.endGroup();
}

void NetworkEditorModule::saveSettings() {
    QSettings settings;
    settings.beginGroup("NetworkEditor");
    
    settings.setValue("cloudProvider", m_cloudProviderEdit->currentText());
    settings.setValue("cloudRegion", m_cloudRegionCombo->currentText());
    settings.setValue("cloudBucket", m_cloudBucketEdit->text());
    settings.setValue("cloudEncrypt", m_cloudEncryptCheck->isChecked());
    settings.setValue("autoSync", m_autoSyncCheck->isChecked());
    settings.setValue("syncInterval", m_syncIntervalSpin->value());
    settings.setValue("serverPort", m_serverPortSpin->value());
    settings.setValue("serverSsl", m_serverSslCheck->isChecked());
    
    settings.endGroup();
}

void NetworkEditorModule::updateConnectionStatus(const QString& status, bool connected) {
    m_syncStatus->setText(QString("Status: %1").arg(status));
    m_syncStatus->setStyleSheet(QString("color: %1; font-weight: bold;").arg(connected ? "#6bff6b" : "#ff6b6b"));
}

void NetworkEditorModule::addLogEntry(const QString& msg, const QString& level) {
    QTextEdit* log = m_wsLog;
    if (!log) return;
    
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss.zzz");
    QString color = "#c8c8c8";
    if (level == "error") color = "#ff6b6b";
    else if (level == "warning") color = "#ffcc66";
    else if (level == "success") color = "#6bff6b";
    
    log->append(QString("<span style='color:#888'>[%1]</span> <span style='color:%2'>%3</span>")
        .arg(timestamp, color, msg.toHtmlEscaped()));
    
    QScrollBar* bar = log->verticalScrollBar();
    bar->setValue(bar->maximum());
}

void NetworkEditorModule::onCloudSyncClicked() {
    QList<QTreeWidgetItem*> selected = m_syncTree->selectedItems();
    if (selected.isEmpty()) {
        for (int i = 0; i < m_syncTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_syncTree->topLevelItem(i);
            if (item->checkState(0) == Qt::Checked) selected.append(item);
        }
    }
    
    if (selected.isEmpty()) {
        this->log("No items selected for sync", "warning");
        return;
    }
    
    QString bucket = m_cloudBucketEdit->text();
    QString apiKey = m_cloudApiKeyEdit->text();
    if (bucket.isEmpty()) {
        logError("Configure a bucket in Settings first.");
        return;
    }
    
    m_syncProgress->setVisible(true);
    m_syncProgress->setMaximum(selected.size());
    m_syncProgress->setValue(0);
    updateConnectionStatus("Syncing...", false);
    
    auto state = std::make_shared<int>(0);
    auto onFileSynced = [this, state, selected](const QString& name) {
        (*state)++;
        m_syncProgress->setValue(*state);
        for (int i = 0; i < selected.size(); ++i) {
            if (selected[i]->text(0) == name) {
                selected[i]->setText(4, "Synced");
                selected[i]->setText(6, QDateTime::currentDateTime().toString("HH:mm:ss"));
                break;
            }
        }
        if (*state >= selected.size()) {
            updateConnectionStatus("Synced", true);
            m_syncProgress->setVisible(false);
            logSuccess(QString("Synced %1 items").arg(selected.size()));
        }
    };
    
    for (int i = 0; i < selected.size(); ++i) {
        QTreeWidgetItem* item = selected[i];
        QString name = item->text(0);
        item->setText(4, "Syncing...");
        
        QByteArray payload = QJsonDocument({{"name", name}, {"timestamp", QDateTime::currentDateTime().toString(Qt::ISODate)}}).toJson();
        QUrl reqUrl(QString("%1/%2").arg(bucket, name));
        QNetworkRequest req(reqUrl);
        req.setRawHeader("Content-Type", "application/json");
        if (!apiKey.isEmpty())
            req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
        
        QNetworkReply* reply = m_networkManager->put(req, payload);
        connect(reply, &QNetworkReply::finished, this, [reply, onFileSynced, name]() {
            reply->deleteLater();
            onFileSynced(name);
        });
    }
}

void NetworkEditorModule::onCollabStartClicked() {
    QString name = m_collabNameEdit->text().trimmed();
    QString room = m_collabRoomEdit->text().trimmed();
    
    if (name.isEmpty()) {
        logError("Please enter your name");
        return;
    }
    
    m_collabUserName = name;
    m_collabRoom = room.isEmpty() ? QString("ksedit-%1").arg(QRandomGenerator::global()->generate() % 10000, 4, 10, QChar('0')) : room;
    m_collabRoomEdit->setText(m_collabRoom);
    
    // Connect to WebSocket server
    QString wsUrl = QString("ws://localhost:%1/ws?room=%2&name=%3").arg(m_serverPortSpin->value()).arg(m_collabRoom).arg(name);
    m_webSocket->open(QUrl(wsUrl));
    
    m_collabActive = true;
    m_collabStartBtn->setEnabled(false);
    m_collabStopBtn->setEnabled(true);
    m_collabNameEdit->setEnabled(false);
    m_collabRoomEdit->setEnabled(false);
    m_collabMessageEdit->setEnabled(true);
    m_collabSendBtn->setEnabled(true);
    
    // Add self to users list
    QTreeWidgetItem* selfItem = new QTreeWidgetItem(m_collabUsersTree, {name, "Connected", "Host", "0 ms"});
    selfItem->setData(0, Qt::UserRole, true); // mark as self
    
    m_collabChat->append(QString("<span style='color:#6bff6b'>[%1] You joined room: %2</span>")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), m_collabRoom));
    
    logSuccess(QString("Collaboration session started: %1").arg(m_collabRoom));
}

void NetworkEditorModule::onCollabStopClicked() {
    if (m_webSocket->state() == QAbstractSocket::ConnectedState) {
        m_webSocket->close();
    }
    
    m_collabActive = false;
    m_collabStartBtn->setEnabled(true);
    m_collabStopBtn->setEnabled(false);
    m_collabNameEdit->setEnabled(true);
    m_collabRoomEdit->setEnabled(true);
    m_collabMessageEdit->setEnabled(false);
    m_collabSendBtn->setEnabled(false);
    m_collabUsersTree->clear();
    m_collabChat->clear();
    
    this->log("Collaboration session stopped");
}

void NetworkEditorModule::onServerStartClicked() {
    quint16 port = m_serverPortSpin->value();
    
    if (m_serverSslCheck->isChecked()) {
        // SSL server would need certificate setup
        logWarning("SSL server requires certificate configuration");
        return;
    }
    
    if (m_webSocketServer->listen(QHostAddress::Any, port)) {
        m_serverActive = true;
        m_serverStartBtn->setEnabled(false);
        m_serverStopBtn->setEnabled(true);
        m_serverPortSpin->setEnabled(false);
        m_serverSslCheck->setEnabled(false);
        
        m_serverLog->append(QString("<span style='color:#6bff6b'>[%1] Server started on port %2</span>")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss")).arg(port));
        
        logSuccess(QString("Server started on port %1").arg(port));
    } else {
        logError(QString("Failed to start server: %1").arg(m_webSocketServer->errorString()));
    }
}

void NetworkEditorModule::onServerStopClicked() {
    m_webSocketServer->close();
    m_serverActive = false;
    m_serverStartBtn->setEnabled(true);
    m_serverStopBtn->setEnabled(false);
    m_serverPortSpin->setEnabled(true);
    m_serverSslCheck->setEnabled(true);
    m_serverClientsTree->clear();
    
    m_serverLog->append(QString("<span style='color:#ff6b6b'>[%1] Server stopped</span>")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
    
    this->log("Server stopped");
}

void NetworkEditorModule::onDiscoverClicked() {
    m_discoveredDevicesTree->clear();
    addLogEntry("Discovering devices...", "info");
    
    QList<QNetworkInterface> interfaces = QNetworkInterface::allInterfaces();
    QString localIP;
    QMap<QString, QString> interfaceMap;
    for (const QNetworkInterface& iface : interfaces) {
        if (!(iface.flags() & QNetworkInterface::IsUp) || !(iface.flags() & QNetworkInterface::IsRunning))
            continue;
        if (iface.flags() & QNetworkInterface::IsLoopBack) continue;
        for (const QNetworkAddressEntry& entry : iface.addressEntries()) {
            if (entry.ip().protocol() == QAbstractSocket::IPv4Protocol) {
                interfaceMap[entry.ip().toString()] = iface.humanReadableName();
                if (localIP.isEmpty()) localIP = entry.ip().toString();
                QTreeWidgetItem* item = new QTreeWidgetItem(m_discoveredDevicesTree,
                    {QHostInfo::localHostName(), entry.ip().toString(), iface.humanReadableName(),
                     "ksEditor", "Local", "Now"});
                item->setData(0, Qt::UserRole, true);
            }
        }
    }
    
    QUdpSocket* discoverySocket = new QUdpSocket(this);
    QByteArray probe = QJsonDocument({
        {"type", "kseditor_discovery"},
        {"host", QHostInfo::localHostName()},
        {"port", 42042}
    }).toJson();
    
    for (auto it = interfaceMap.begin(); it != interfaceMap.end(); ++it) {
        QString ip = it.key();
        QStringList parts = ip.split('.');
        if (parts.size() == 4) {
            QString broadcast = parts[0] + "." + parts[1] + "." + parts[2] + ".255";
            discoverySocket->writeDatagram(probe, QHostAddress(broadcast), 42042);
        }
    }
    
    int timeoutMs = m_discoveryTimeoutSpin->value();
    discoverySocket->waitForReadyRead(timeoutMs);
    while (discoverySocket->hasPendingDatagrams()) {
        QByteArray datagram;
        datagram.resize(discoverySocket->pendingDatagramSize());
        QHostAddress sender;
        quint16 port;
        discoverySocket->readDatagram(datagram.data(), datagram.size(), &sender, &port);
        QJsonDocument doc = QJsonDocument::fromJson(datagram);
        if (doc.isObject()) {
            QJsonObject obj = doc.object();
            QString devName = obj["host"].toString();
            QString devIP = sender.toString();
            if (devIP != localIP && devName != QHostInfo::localHostName()) {
                bool exists = false;
                for (int i = 0; i < m_discoveredDevicesTree->topLevelItemCount(); ++i) {
                    if (m_discoveredDevicesTree->topLevelItem(i)->text(1) == devIP) {
                        exists = true; break;
                    }
                }
                if (!exists) {
                    new QTreeWidgetItem(m_discoveredDevicesTree,
                        {devName, devIP, interfaceMap.value(devIP, "Unknown"),
                         "ksEditor", "Online", "Just now"});
                }
            }
        }
    }
    discoverySocket->deleteLater();
    
    logSuccess(QString("Discovered %1 devices").arg(m_discoveredDevicesTree->topLevelItemCount()));
}

void NetworkEditorModule::onSettingsSave() {
    saveSettings();
    logSuccess("Settings saved");
}

void NetworkEditorModule::onSettingsLoad() {
    loadSettings();
    this->log("Settings loaded");
}

void NetworkEditorModule::onSyncStatusChanged() {
    updateConnectionStatus("Checking...", false);
    QString bucket = m_cloudBucketEdit->text();
    if (bucket.isEmpty()) {
        updateConnectionStatus("Offline", false);
        return;
    }
    QUrl url(bucket);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "ksEditor/1.0");
    QNetworkReply* reply = m_networkManager->head(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError) {
            updateConnectionStatus("Online", true);
        } else {
            updateConnectionStatus("Offline", false);
        }
    });
}

void NetworkEditorModule::refreshDeviceList() {
    if (m_discoveryTimer->isActive()) {
        onDiscoverClicked();
    }
}

void NetworkEditorModule::onWebSocketConnected() {
    m_wsConnectBtn->setEnabled(false);
    m_wsDisconnectBtn->setEnabled(true);
    m_wsSendEdit->setEnabled(true);
    m_wsSendBtn->setEnabled(true);
    
    addLogEntry("Connected", "success");
    
    // Send join message for collaboration
    if (m_collabActive) {
        QJsonObject msg;
        msg["type"] = "join";
        msg["room"] = m_collabRoom;
        msg["name"] = m_collabUserName;
        m_webSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    }
}

void NetworkEditorModule::onWebSocketDisconnected() {
    m_wsConnectBtn->setEnabled(true);
    m_wsDisconnectBtn->setEnabled(false);
    m_wsSendEdit->setEnabled(false);
    m_wsSendBtn->setEnabled(false);
    
    addLogEntry("Disconnected", "warning");
    
    if (m_collabActive) {
        onCollabStopClicked();
    }
}

void NetworkEditorModule::onWebSocketTextReceived(const QString& message) {
    addLogEntry(QString("<< %1").arg(message), "info");
    
    QJsonDocument doc = QJsonDocument::fromJson(message.toUtf8());
    if (!doc.isObject()) return;
    
    QJsonObject obj = doc.object();
    QString type = obj["type"].toString();
    
    if (type == "chat") {
        QString user = obj["user"].toString();
        QString text = obj["text"].toString();
        QString time = obj["time"].toString();
        m_collabChat->append(QString("<span style='color:#888'>[%1]</span> <b>%2:</b> %3")
            .arg(time, user.toHtmlEscaped(), text.toHtmlEscaped()));
    } else if (type == "user_join") {
        QString user = obj["user"].toString();
        QTreeWidgetItem* item = new QTreeWidgetItem(m_collabUsersTree, {user, "Connected", "Guest", "0 ms"});
        m_collabChat->append(QString("<span style='color:#6bff6b'>[%1] %2 joined</span>")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), user));
    } else if (type == "user_leave") {
        QString user = obj["user"].toString();
        for (int i = 0; i < m_collabUsersTree->topLevelItemCount(); ++i) {
            if (m_collabUsersTree->topLevelItem(i)->text(0) == user) {
                delete m_collabUsersTree->takeTopLevelItem(i);
                break;
            }
        }
        m_collabChat->append(QString("<span style='color:#ff6b6b'>[%1] %2 left</span>")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), user));
    } else if (type == "sync") {
        // Handle sync messages
    }
}

void NetworkEditorModule::onWebSocketError(QAbstractSocket::SocketError error) {
    QString errMsg = m_webSocket->errorString();
    addLogEntry(QString("Error: %1").arg(errMsg), "error");
    logError(QString("WebSocket error: %1").arg(errMsg));
}

void NetworkEditorModule::onServerNewConnection() {
    QWebSocket* client = m_webSocketServer->nextPendingConnection();
    if (!client) return;
    
    QString clientId = QString("Client-%1").arg(m_serverClientsTree->topLevelItemCount() + 1);
    QString peerAddr = client->peerAddress().toString();
    
    QTreeWidgetItem* item = new QTreeWidgetItem(m_serverClientsTree, 
        {clientId, peerAddr, QDateTime::currentDateTime().toString("HH:mm:ss"), "0", "Connected"});
    item->setData(0, Qt::UserRole, QVariant::fromValue(client));
    
    connect(client, &QWebSocket::textMessageReceived, this, [this, client, clientId](const QString& msg) {
        // Update message count
        for (int i = 0; i < m_serverClientsTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_serverClientsTree->topLevelItem(i);
            if (item->data(0, Qt::UserRole).value<QWebSocket*>() == client) {
                int count = item->text(3).toInt() + 1;
                item->setText(3, QString::number(count));
                break;
            }
        }
        
        // Broadcast to all other clients
        QJsonObject obj;
        obj["type"] = "broadcast";
        obj["from"] = clientId;
        obj["data"] = msg;
        QJsonDocument doc(obj);
        
        for (int i = 0; i < m_serverClientsTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_serverClientsTree->topLevelItem(i);
            QWebSocket* otherClient = item->data(0, Qt::UserRole).value<QWebSocket*>();
            if (otherClient && otherClient != client && otherClient->state() == QAbstractSocket::ConnectedState) {
                otherClient->sendTextMessage(doc.toJson(QJsonDocument::Compact));
            }
        }
        
        m_serverLog->append(QString("<span style='color:#ccc'>[%1] %2: %3</span>")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), clientId, msg.left(100)));
    });
    
    connect(client, &QWebSocket::disconnected, this, [this, client, clientId]() {
        for (int i = 0; i < m_serverClientsTree->topLevelItemCount(); ++i) {
            QTreeWidgetItem* item = m_serverClientsTree->topLevelItem(i);
            if (item->data(0, Qt::UserRole).value<QWebSocket*>() == client) {
                delete m_serverClientsTree->takeTopLevelItem(i);
                break;
            }
        }
        m_serverLog->append(QString("<span style='color:#ff6b6b'>[%1] %2 disconnected</span>")
            .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), clientId));
    });
    
    m_serverLog->append(QString("<span style='color:#6bff6b'>[%1] %2 connected from %3</span>")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss"), clientId, peerAddr));
}

void NetworkEditorModule::onServerClosed() {
    m_serverLog->append(QString("<span style='color:#ff6b6b'>[%1] Server closed</span>")
        .arg(QDateTime::currentDateTime().toString("HH:mm:ss")));
}

void NetworkEditorModule::onDownloadProgress(qint64 bytesReceived, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        m_syncProgress->setMaximum(bytesTotal);
        m_syncProgress->setValue(bytesReceived);
    }
}

void NetworkEditorModule::onUploadProgress(qint64 bytesSent, qint64 bytesTotal) {
    if (bytesTotal > 0) {
        m_syncProgress->setMaximum(bytesTotal);
        m_syncProgress->setValue(bytesSent);
    }
}

void NetworkEditorModule::sendCollabMessage(const QString& text) {
    if (m_webSocket->state() != QAbstractSocket::ConnectedState) return;
    
    QJsonObject msg;
    msg["type"] = "chat";
    msg["user"] = m_collabUserName;
    msg["text"] = text;
    msg["time"] = QDateTime::currentDateTime().toString("HH:mm:ss");
    
    m_webSocket->sendTextMessage(QJsonDocument(msg).toJson(QJsonDocument::Compact));
    m_collabChat->append(QString("<span style='color:#888'>[%1]</span> <b>You:</b> %2")
        .arg(msg["time"].toString(), text.toHtmlEscaped()));
}

void NetworkEditorModule::testCloudConnection() {
    QString provider = m_cloudProviderEdit->currentText();
    QString bucket = m_cloudBucketEdit->text();
    QString apiKey = m_cloudApiKeyEdit->text();
    
    if (bucket.isEmpty()) {
        logError("Please enter a bucket name");
        return;
    }
    
    this->log(QString("Testing connection to %1...").arg(provider));
    
    QUrl url(bucket);
    QNetworkRequest req(url);
    req.setRawHeader("User-Agent", "ksEditor/1.0");
    if (!apiKey.isEmpty())
        req.setRawHeader("Authorization", ("Bearer " + apiKey).toUtf8());
    
    QNetworkReply* reply = m_networkManager->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, provider]() {
        reply->deleteLater();
        if (reply->error() == QNetworkReply::NoError || reply->error() == QNetworkReply::AuthenticationRequiredError) {
            logSuccess(QString("Connection to %1 successful!").arg(provider));
        } else {
            logError(QString("Connection failed: %1").arg(reply->errorString()));
        }
    });
}

void NetworkEditorModule::onActivation() {
    ModuleGuiBase::onActivation();
}

void NetworkEditorModule::onDeactivation() {
    ModuleGuiBase::onDeactivation();
}

void NetworkEditorModule::onSettingsChanged() {
}

} // namespace network
} // namespace ks

