#include "SkinIniEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QTextStream>

namespace ks {

SkinIniEditorModule::SkinIniEditorModule(QWidget* parent) : EditorModule(parent) {}
bool SkinIniEditorModule::initialize() { LOG_INFO("SkinIniEditorModule", "Initialized"); return true; }
void SkinIniEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* SkinIniEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Skin INI Editor"), mainWindow);
    m_dockWidget->setObjectName("SkinIniEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);

    m_tabWidget = new QTabWidget();

    // Driver tab
    auto* driverWidget = new QWidget();
    auto* driverLayout = new QGridLayout(driverWidget);
    m_suitEdit = new QLineEdit(); driverLayout->addWidget(new QLabel(tr("Suit:")), 0, 0); driverLayout->addWidget(m_suitEdit, 0, 1);
    m_glovesEdit = new QLineEdit(); driverLayout->addWidget(new QLabel(tr("Gloves:")), 1, 0); driverLayout->addWidget(m_glovesEdit, 1, 1);
    m_helmetEdit = new QLineEdit(); driverLayout->addWidget(new QLabel(tr("Helmet:")), 2, 0); driverLayout->addWidget(m_helmetEdit, 2, 1);
    m_brandEdit = new QLineEdit(); driverLayout->addWidget(new QLabel(tr("Brand:")), 3, 0); driverLayout->addWidget(m_brandEdit, 3, 1);
    m_tabWidget->addTab(driverWidget, tr("Driver"));

    // Crew tab
    auto* crewWidget = new QWidget();
    auto* crewLayout = new QGridLayout(crewWidget);
    m_crewSuitEdit = new QLineEdit(); crewLayout->addWidget(new QLabel(tr("Suit:")), 0, 0); crewLayout->addWidget(m_crewSuitEdit, 0, 1);
    m_crewHelmetEdit = new QLineEdit(); crewLayout->addWidget(new QLabel(tr("Helmet:")), 1, 0); crewLayout->addWidget(m_crewHelmetEdit, 1, 1);
    m_tabWidget->addTab(crewWidget, tr("Crew"));

    mainLayout->addWidget(m_tabWidget);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load skin.ini"));
    m_saveBtn = new QPushButton(tr("Save skin.ini"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready"));
    mainLayout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &SkinIniEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &SkinIniEditorModule::onSaveFile);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void SkinIniEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void SkinIniEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }
void SkinIniEditorModule::onActivation()
{
    m_statusLabel->setText(tr("Active"));
}

void SkinIniEditorModule::onDeactivation()
{
    m_statusLabel->setText(tr("Inactive"));
}

void SkinIniEditorModule::onLoadFile()
{
    QString path = QFileDialog::getOpenFileName(this, tr("Open skin.ini"), QString(), tr("Skin INI (*.ini)"));
    if (!path.isEmpty()) { m_filePath = path; loadFileToUI(); m_statusLabel->setText(tr("Loaded: %1").arg(path)); }
}

void SkinIniEditorModule::onSaveFile()
{
    QString path = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, tr("Save skin.ini"), QString(), tr("Skin INI (*.ini)")) : m_filePath;
    if (!path.isEmpty()) { m_filePath = path; saveFileFromUI(); m_statusLabel->setText(tr("Saved: %1").arg(path)); }
}

void SkinIniEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void SkinIniEditorModule::loadFileToUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString content = file.readAll(); file.close();

    auto parseVal = [&](const QString& section, const QString& key) -> QString {
        int si = content.indexOf("[" + section); if (si < 0) return "";
        int ei = content.indexOf("[", si + 1); if (ei < 0) ei = content.length();
        QString sec = content.mid(si, ei - si);
        for (const QString& line : sec.split("\n")) {
            QString l = line.trimmed();
            if (l.startsWith(key + "=")) return l.mid(key.length() + 1);
        }
        return "";
    };

    m_suitEdit->setText(parseVal("driver", "SUIT"));
    m_glovesEdit->setText(parseVal("driver", "GLOVES"));
    m_helmetEdit->setText(parseVal("driver", "HELMET"));
    m_brandEdit->setText(parseVal("driver", "BRAND"));
    m_crewSuitEdit->setText(parseVal("CREW", "SUIT"));
    m_crewHelmetEdit->setText(parseVal("CREW", "HELMET"));
}

void SkinIniEditorModule::saveFileFromUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    out << "[driver]\n";
    out << "SUIT=" << m_suitEdit->text() << "\n";
    out << "GLOVES=" << m_glovesEdit->text() << "\n";
    out << "HELMET=" << m_helmetEdit->text() << "\n";
    out << "BRAND=" << m_brandEdit->text() << "\n\n";
    out << "[CREW]\n";
    out << "SUIT=" << m_crewSuitEdit->text() << "\n";
    out << "HELMET=" << m_crewHelmetEdit->text() << "\n";
    file.close();
}

QJsonObject SkinIniEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    data["suit"] = m_suitEdit ? m_suitEdit->text() : QString();
    data["gloves"] = m_glovesEdit ? m_glovesEdit->text() : QString();
    data["helmet"] = m_helmetEdit ? m_helmetEdit->text() : QString();
    data["brand"] = m_brandEdit ? m_brandEdit->text() : QString();
    data["crewSuit"] = m_crewSuitEdit ? m_crewSuitEdit->text() : QString();
    data["crewHelmet"] = m_crewHelmetEdit ? m_crewHelmetEdit->text() : QString();
    return data;
}

void SkinIniEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    if (m_suitEdit) m_suitEdit->setText(data["suit"].toString());
    if (m_glovesEdit) m_glovesEdit->setText(data["gloves"].toString());
    if (m_helmetEdit) m_helmetEdit->setText(data["helmet"].toString());
    if (m_brandEdit) m_brandEdit->setText(data["brand"].toString());
    if (m_crewSuitEdit) m_crewSuitEdit->setText(data["crewSuit"].toString());
    if (m_crewHelmetEdit) m_crewHelmetEdit->setText(data["crewHelmet"].toString());
}

} // namespace ks
