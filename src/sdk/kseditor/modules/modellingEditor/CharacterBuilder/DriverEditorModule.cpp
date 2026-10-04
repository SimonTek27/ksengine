#include "DriverEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QTextStream>
#include <QJsonArray>
#include <QJsonObject>
#include <QJsonDocument>

namespace ks {

DriverEditorModule::DriverEditorModule(QWidget* parent) : EditorModule(parent) {}
bool DriverEditorModule::initialize() { LOG_INFO("DriverEditorModule", "Initialized"); return true; }
void DriverEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* DriverEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Driver Editor"), mainWindow);
    m_dockWidget->setObjectName("DriverEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QHBoxLayout(centralWidget);

    // Driver list
    auto* listWidget = new QWidget();
    auto* listLayout = new QVBoxLayout(listWidget);
    m_driverList = new QListWidget();
    listLayout->addWidget(m_driverList);
    m_refreshBtn = new QPushButton(tr("Refresh"));
    listLayout->addWidget(m_refreshBtn);
    mainLayout->addWidget(listWidget);

    // Properties
    auto* propsWidget = new QWidget();
    auto* propsLayout = new QGridLayout(propsWidget);

    m_suitPathEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel(tr("Suit:")), 0, 0); propsLayout->addWidget(m_suitPathEdit, 0, 1);

    m_glovesPathEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel(tr("Gloves:")), 1, 0); propsLayout->addWidget(m_glovesPathEdit, 1, 1);

    m_helmetPathEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel(tr("Helmet:")), 2, 0); propsLayout->addWidget(m_helmetPathEdit, 2, 1);

    m_helmetBaseEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel(tr("Helmet Base:")), 3, 0); propsLayout->addWidget(m_helmetBaseEdit, 3, 1);

    m_helmetVariantSpin = new QSpinBox();
    m_helmetVariantSpin->setRange(0, 50);
    propsLayout->addWidget(new QLabel(tr("Variant:")), 4, 0); propsLayout->addWidget(m_helmetVariantSpin, 4, 1);

    m_brandEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel(tr("Brand:")), 5, 0); propsLayout->addWidget(m_brandEdit, 5, 1);

    mainLayout->addWidget(propsWidget);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load skins.ini"));
    m_saveBtn = new QPushButton(tr("Save skins.ini"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn);
    mainLayout->addLayout(actionLayout);

    // ... vertical main
    auto* vMain = new QVBoxLayout();
    vMain->addLayout(mainLayout);
    m_statusLabel = new QLabel(tr("Ready"));
    vMain->addWidget(m_statusLabel);

    auto* wrapper = new QWidget();
    wrapper->setLayout(vMain);
    m_dockWidget->setWidget(wrapper);

    // Wire slot connections
    connect(m_driverList, &QListWidget::currentRowChanged, this, &DriverEditorModule::onDriverSelected);
    connect(m_suitPathEdit, &QLineEdit::textChanged, this, &DriverEditorModule::onSuitPathChanged);
    connect(m_glovesPathEdit, &QLineEdit::textChanged, this, &DriverEditorModule::onGlovesPathChanged);
    connect(m_helmetPathEdit, &QLineEdit::textChanged, this, &DriverEditorModule::onHelmetPathChanged);
    connect(m_helmetBaseEdit, &QLineEdit::textChanged, this, &DriverEditorModule::onHelmetBaseChanged);
    connect(m_helmetVariantSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &DriverEditorModule::onHelmetVariantChanged);
    connect(m_brandEdit, &QLineEdit::textChanged, this, &DriverEditorModule::onBrandChanged);
    connect(m_loadBtn, &QPushButton::clicked, this, &DriverEditorModule::onLoadSkinsDir);
    connect(m_saveBtn, &QPushButton::clicked, this, &DriverEditorModule::onSaveSkinsDir);
    connect(m_refreshBtn, &QPushButton::clicked, this, &DriverEditorModule::onRefreshDrivers);

    return m_dockWidget;
}

void DriverEditorModule::importFile(const QString& f) { m_skinsDir = f; loadSkinsIniToUI(); }
void DriverEditorModule::exportFile(const QString& f) { m_skinsDir = f; saveSkinsIniFromUI(); }
void DriverEditorModule::onActivation()
{
    m_statusLabel->setText(tr("Active"));
}

void DriverEditorModule::onDeactivation()
{
    m_statusLabel->setText(tr("Inactive"));
}

void DriverEditorModule::onDriverSelected(int row) {
    if (row >= 0 && row < m_skins.size()) {
        m_selectedIndex = row;
        const auto& skin = m_skins[row];
        m_suitPathEdit->setText(skin.suitPath);
        m_glovesPathEdit->setText(skin.glovesPath);
        m_helmetPathEdit->setText(skin.helmetPath);
        m_helmetBaseEdit->setText(skin.helmetBase);
        m_helmetVariantSpin->setValue(skin.helmetVariant);
        m_brandEdit->setText(skin.brand);
        m_statusLabel->setText(tr("Selected: %1").arg(skin.name));
    }
}
void DriverEditorModule::onLoadDriver() { loadSkinsIniToUI(); }
void DriverEditorModule::onSuitPathChanged(const QString& t) { if (m_selectedIndex >= 0) m_skins[m_selectedIndex].suitPath = t; }
void DriverEditorModule::onGlovesPathChanged(const QString& t) { if (m_selectedIndex >= 0) m_skins[m_selectedIndex].glovesPath = t; }
void DriverEditorModule::onHelmetPathChanged(const QString& t) { if (m_selectedIndex >= 0) m_skins[m_selectedIndex].helmetPath = t; }
void DriverEditorModule::onHelmetBaseChanged(const QString& t) { if (m_selectedIndex >= 0) m_skins[m_selectedIndex].helmetBase = t; }
void DriverEditorModule::onHelmetVariantChanged(int v) { if (m_selectedIndex >= 0) m_skins[m_selectedIndex].helmetVariant = v; }
void DriverEditorModule::onBrandChanged(const QString& t) { if (m_selectedIndex >= 0) m_skins[m_selectedIndex].brand = t; }
void DriverEditorModule::onLoadSkinsDir() { QString d = QFileDialog::getExistingDirectory(this, tr("Open skins directory")); if (!d.isEmpty()) { m_skinsDir = d; loadSkinsIniToUI(); } }
void DriverEditorModule::onSaveSkinsDir() { saveSkinsIniFromUI(); }
void DriverEditorModule::onRefreshDrivers() { loadSkinsIniToUI(); }
void DriverEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void DriverEditorModule::loadSkinsIniToUI()
{
    if (m_skinsDir.isEmpty()) return;

    QDir dir(m_skinsDir);
    m_skins.clear();

    // Scan for skin directories
    QStringList dirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString& skinDir : dirs) {
        QString skinIniPath = m_skinsDir + "/" + skinDir + "/skin.ini";
        if (QFileInfo::exists(skinIniPath)) {
            DriverSkin skin;
            skin.name = skinDir;
            QFile file(skinIniPath);
            if (file.open(QIODevice::ReadOnly | QIODevice::Text)) {
                QString content = file.readAll();
                for (const QString& line : content.split("\n")) {
                    QString l = line.trimmed();
                    if (l.startsWith("SUIT=")) skin.suitPath = l.mid(5);
                    else if (l.startsWith("GLOVES=")) skin.glovesPath = l.mid(7);
                    else if (l.startsWith("HELMET=")) skin.helmetPath = l.mid(7);
                    else if (l.startsWith("HELMET_BASE=")) skin.helmetBase = l.mid(12);
                    else if (l.startsWith("BRAND=")) skin.brand = l.mid(6);
                }
                file.close();
            }
            m_skins.append(skin);
        }
    }

    m_statusLabel->setText(tr("Loaded %1 driver skins from %2").arg(m_skins.size()).arg(m_skinsDir));

    m_driverList->clear();
    for (const auto& skin : m_skins)
        m_driverList->addItem(skin.name);
    m_selectedIndex = -1;
}

void DriverEditorModule::saveSkinsIniFromUI()
{
    if (m_skinsDir.isEmpty() || m_selectedIndex < 0 || m_selectedIndex >= m_skins.size()) return;

    const auto& skin = m_skins[m_selectedIndex];
    QString skinIniPath = m_skinsDir + "/" + skin.name + "/skin.ini";

    QFile file(skinIniPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;

    QTextStream o(&file);
    o << "[driver]\n";
    o << "SUIT=" << skin.suitPath << "\n";
    o << "GLOVES=" << skin.glovesPath << "\n";
    o << "HELMET=" << skin.helmetPath << "\n";
    o << "HELMET_BASE=" << skin.helmetBase << "\n";
    o << "BRAND=" << skin.brand << "\n";
    file.close();

    m_statusLabel->setText(tr("Saved: %1").arg(skinIniPath));
}

QJsonObject DriverEditorModule::serializeProject() const
{
    QJsonObject data;
    data["skinsDir"] = m_skinsDir;
    QJsonArray skinsArray;
    for (const auto& s : m_skins) {
        QJsonObject obj;
        obj["name"] = s.name;
        obj["suitPath"] = s.suitPath;
        obj["glovesPath"] = s.glovesPath;
        obj["helmetPath"] = s.helmetPath;
        obj["helmetBase"] = s.helmetBase;
        obj["helmetVariant"] = s.helmetVariant;
        obj["brand"] = s.brand;
        skinsArray.append(obj);
    }
    data["skins"] = skinsArray;
    return data;
}

void DriverEditorModule::deserializeProject(const QJsonObject& data)
{
    m_skinsDir = data["skinsDir"].toString();
    m_skins.clear();
    for (const auto& v : data["skins"].toArray()) {
        QJsonObject obj = v.toObject();
        DriverSkin s;
        s.name = obj["name"].toString();
        s.suitPath = obj["suitPath"].toString();
        s.glovesPath = obj["glovesPath"].toString();
        s.helmetPath = obj["helmetPath"].toString();
        s.helmetBase = obj["helmetBase"].toString();
        s.helmetVariant = obj["helmetVariant"].toInt();
        s.brand = obj["brand"].toString();
        m_skins.append(s);
    }
}

} // namespace ks
