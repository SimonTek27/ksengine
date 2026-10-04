#include "RaceConfigEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QTextStream>

namespace ks {

RaceConfigEditorModule::RaceConfigEditorModule(QWidget* parent) : EditorModule(parent) {}
bool RaceConfigEditorModule::initialize() { LOG_INFO("RaceConfigEditorModule", "Initialized"); return true; }
void RaceConfigEditorModule::shutdown() { LOG_INFO("RaceConfigEditorModule", "Shutdown"); }

QDockWidget* RaceConfigEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Race Config Editor"), mainWindow);
    m_dockWidget->setObjectName("RaceConfigEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);
    m_tabWidget = new QTabWidget();

    // Track/Car
    auto* tcWidget = new QWidget(); auto* tcLayout = new QGridLayout(tcWidget);
    m_trackEdit = new QLineEdit(); tcLayout->addWidget(new QLabel(tr("Track:")), 0, 0); tcLayout->addWidget(m_trackEdit, 0, 1);
    m_trackLayoutEdit = new QLineEdit(); tcLayout->addWidget(new QLabel(tr("Layout:")), 1, 0); tcLayout->addWidget(m_trackLayoutEdit, 1, 1);
    m_carEdit = new QLineEdit(); tcLayout->addWidget(new QLabel(tr("Car:")), 2, 0); tcLayout->addWidget(m_carEdit, 2, 1);
    m_tabWidget->addTab(tcWidget, tr("Track/Car"));

    // Sessions
    auto* sessWidget = new QWidget(); auto* sessLayout = new QGridLayout(sessWidget);
    m_qualifyMinutesSpin = new QSpinBox(); m_qualifyMinutesSpin->setRange(1, 120);
    sessLayout->addWidget(new QLabel(tr("Qualify Minutes:")), 0, 0); sessLayout->addWidget(m_qualifyMinutesSpin, 0, 1);
    m_raceLapsSpin = new QSpinBox(); m_raceLapsSpin->setRange(1, 1000);
    sessLayout->addWidget(new QLabel(tr("Race Laps:")), 1, 0); sessLayout->addWidget(m_raceLapsSpin, 1, 1);
    m_raceMinutesSpin = new QSpinBox(); m_raceMinutesSpin->setRange(1, 600);
    sessLayout->addWidget(new QLabel(tr("Race Minutes:")), 2, 0); sessLayout->addWidget(m_raceMinutesSpin, 2, 1);
    m_tabWidget->addTab(sessWidget, tr("Sessions"));

    // Grid
    auto* gridWidget = new QWidget(); auto* gridLayout = new QGridLayout(gridWidget);
    m_gridSizeSpin = new QSpinBox(); m_gridSizeSpin->setRange(1, 50);
    gridLayout->addWidget(new QLabel(tr("Grid Size:")), 0, 0); gridLayout->addWidget(m_gridSizeSpin, 0, 1);
    m_gridSortCombo = new QComboBox(); m_gridSortCombo->addItems({tr("Qualifying"), tr("Random"), tr("Fixed")});
    gridLayout->addWidget(new QLabel(tr("Sort:")), 1, 0); gridLayout->addWidget(m_gridSortCombo, 1, 1);
    m_tabWidget->addTab(gridWidget, tr("Grid"));

    // Rules
    auto* rulesWidget = new QWidget(); auto* rulesLayout = new QGridLayout(rulesWidget);
    m_tcCheck = new QCheckBox(tr("Traction Control")); rulesLayout->addWidget(m_tcCheck, 0, 0);
    m_absCheck = new QCheckBox(tr("ABS")); rulesLayout->addWidget(m_absCheck, 1, 0);
    m_stabilityCheck = new QCheckBox(tr("Stability Control")); rulesLayout->addWidget(m_stabilityCheck, 2, 0);
    m_autoClutchCheck = new QCheckBox(tr("Auto Clutch")); rulesLayout->addWidget(m_autoClutchCheck, 3, 0);
    m_mgukLapsSpin = new QSpinBox(); m_mgukLapsSpin->setRange(0, 100);
    rulesLayout->addWidget(new QLabel(tr("MGU-K Laps:")), 4, 0); rulesLayout->addWidget(m_mgukLapsSpin, 4, 1);
    m_tabWidget->addTab(rulesWidget, tr("Rules"));

    // Weather
    auto* wWidget = new QWidget(); auto* wLayout = new QGridLayout(wWidget);
    m_weatherEdit = new QLineEdit(); wLayout->addWidget(new QLabel(tr("Weather:")), 0, 0); wLayout->addWidget(m_weatherEdit, 0, 1);
    m_weatherTempSpin = new QDoubleSpinBox(); m_weatherTempSpin->setRange(-20, 60);
    wLayout->addWidget(new QLabel(tr("Temp:")), 1, 0); wLayout->addWidget(m_weatherTempSpin, 1, 1);
    m_tabWidget->addTab(wWidget, tr("Weather"));

    mainLayout->addWidget(m_tabWidget);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load race.ini")); m_saveBtn = new QPushButton(tr("Save race.ini")); m_resetBtn = new QPushButton(tr("Reset"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn); actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready")); mainLayout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &RaceConfigEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &RaceConfigEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &RaceConfigEditorModule::onResetDefaults);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void RaceConfigEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void RaceConfigEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }
void RaceConfigEditorModule::onActivation() { if (m_statusLabel) m_statusLabel->setText(tr("Active")); }
void RaceConfigEditorModule::onDeactivation() { if (m_statusLabel) m_statusLabel->setText(tr("Inactive")); }

void RaceConfigEditorModule::onLoadFile()
{
    QString p = QFileDialog::getOpenFileName(this, tr("Open race.ini"), QString(), tr("Race INI (*.ini)"));
    if (!p.isEmpty()) { m_filePath = p; loadFileToUI(); m_statusLabel->setText(tr("Loaded: %1").arg(p)); }
}

void RaceConfigEditorModule::onSaveFile()
{
    QString p = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, tr("Save race.ini"), QString(), tr("Race INI (*.ini)")) : m_filePath;
    if (!p.isEmpty()) { m_filePath = p; saveFileFromUI(); m_statusLabel->setText(tr("Saved: %1").arg(p)); }
}

void RaceConfigEditorModule::onResetDefaults()
{
    m_trackEdit->clear(); m_trackLayoutEdit->clear(); m_carEdit->clear();
    m_qualifyMinutesSpin->setValue(15); m_raceLapsSpin->setValue(10); m_raceMinutesSpin->setValue(0);
    m_gridSizeSpin->setValue(20); m_gridSortCombo->setCurrentIndex(0);
    m_tcCheck->setChecked(true); m_absCheck->setChecked(true); m_stabilityCheck->setChecked(false); m_autoClutchCheck->setChecked(false);
    m_weatherEdit->setText("clear_2"); m_weatherTempSpin->setValue(22);
    m_statusLabel->setText(tr("Reset to defaults"));
}

void RaceConfigEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void RaceConfigEditorModule::loadFileToUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString c = file.readAll(); file.close();
    auto val = [&](const QString& key, const QString& def) -> QString {
        for (const QString& line : c.split("\n")) { QString l = line.trimmed(); if (l.startsWith(key + "=")) return l.mid(key.length() + 1); }
        return def;
    };
    m_trackEdit->setText(val("TRACK", ""));
    m_trackLayoutEdit->setText(val("CONFIG_TRACK", ""));
    m_carEdit->setText(val("MODEL", ""));
    m_qualifyMinutesSpin->setValue(val("QUALIFY", "15").toInt());
    m_raceLapsSpin->setValue(val("LAPS", "10").toInt());
    m_raceMinutesSpin->setValue(val("RACE_MINUTES", "0").toInt());
    m_gridSizeSpin->setValue(val("GRID_SIZE", "20").toInt());
    m_gridSortCombo->setCurrentIndex(val("GRID_SORT", "0").toInt());
    m_tcCheck->setChecked(val("TC", "1").toInt() != 0);
    m_absCheck->setChecked(val("ABS", "1").toInt() != 0);
    m_stabilityCheck->setChecked(val("STABILITY", "0").toInt() != 0);
    m_autoClutchCheck->setChecked(val("AUTO_CLUTCH", "0").toInt() != 0);
    m_mgukLapsSpin->setValue(val("MGUK_LAPS", "0").toInt());
    m_weatherEdit->setText(val("WEATHER", "clear_2"));
    m_weatherTempSpin->setValue(val("WEATHER_TEMP", "22").toDouble());
}

void RaceConfigEditorModule::saveFileFromUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&file);
    o << "TRACK=" << m_trackEdit->text() << "\n";
    o << "CONFIG_TRACK=" << m_trackLayoutEdit->text() << "\n";
    o << "MODEL=" << m_carEdit->text() << "\n";
    o << "QUALIFY=" << m_qualifyMinutesSpin->value() << "\n";
    o << "LAPS=" << m_raceLapsSpin->value() << "\n";
    o << "RACE_MINUTES=" << m_raceMinutesSpin->value() << "\n";
    o << "GRID_SIZE=" << m_gridSizeSpin->value() << "\n";
    o << "GRID_SORT=" << m_gridSortCombo->currentIndex() << "\n";
    o << "TC=" << (m_tcCheck->isChecked() ? 1 : 0) << "\n";
    o << "ABS=" << (m_absCheck->isChecked() ? 1 : 0) << "\n";
    o << "STABILITY=" << (m_stabilityCheck->isChecked() ? 1 : 0) << "\n";
    o << "AUTO_CLUTCH=" << (m_autoClutchCheck->isChecked() ? 1 : 0) << "\n";
    o << "MGUK_LAPS=" << m_mgukLapsSpin->value() << "\n";
    o << "WEATHER=" << m_weatherEdit->text() << "\n";
    o << "WEATHER_TEMP=" << m_weatherTempSpin->value() << "\n";
    file.close();
}

QJsonObject RaceConfigEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    data["track"] = m_trackEdit ? m_trackEdit->text() : QString();
    data["trackLayout"] = m_trackLayoutEdit ? m_trackLayoutEdit->text() : QString();
    data["car"] = m_carEdit ? m_carEdit->text() : QString();
    data["qualifyMinutes"] = m_qualifyMinutesSpin ? m_qualifyMinutesSpin->value() : 0;
    data["raceLaps"] = m_raceLapsSpin ? m_raceLapsSpin->value() : 0;
    data["raceMinutes"] = m_raceMinutesSpin ? m_raceMinutesSpin->value() : 0;
    data["gridSize"] = m_gridSizeSpin ? m_gridSizeSpin->value() : 0;
    data["gridSort"] = m_gridSortCombo ? m_gridSortCombo->currentIndex() : 0;
    data["tc"] = m_tcCheck ? m_tcCheck->isChecked() : false;
    data["abs"] = m_absCheck ? m_absCheck->isChecked() : false;
    data["stability"] = m_stabilityCheck ? m_stabilityCheck->isChecked() : false;
    data["autoClutch"] = m_autoClutchCheck ? m_autoClutchCheck->isChecked() : false;
    data["mgukLaps"] = m_mgukLapsSpin ? m_mgukLapsSpin->value() : 0;
    data["weather"] = m_weatherEdit ? m_weatherEdit->text() : QString();
    data["weatherTemp"] = m_weatherTempSpin ? m_weatherTempSpin->value() : 0.0;
    return data;
}

void RaceConfigEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    if (m_trackEdit) m_trackEdit->setText(data["track"].toString());
    if (m_trackLayoutEdit) m_trackLayoutEdit->setText(data["trackLayout"].toString());
    if (m_carEdit) m_carEdit->setText(data["car"].toString());
    if (m_qualifyMinutesSpin) m_qualifyMinutesSpin->setValue(data["qualifyMinutes"].toInt());
    if (m_raceLapsSpin) m_raceLapsSpin->setValue(data["raceLaps"].toInt());
    if (m_raceMinutesSpin) m_raceMinutesSpin->setValue(data["raceMinutes"].toInt());
    if (m_gridSizeSpin) m_gridSizeSpin->setValue(data["gridSize"].toInt());
    if (m_gridSortCombo) m_gridSortCombo->setCurrentIndex(data["gridSort"].toInt());
    if (m_tcCheck) m_tcCheck->setChecked(data["tc"].toBool());
    if (m_absCheck) m_absCheck->setChecked(data["abs"].toBool());
    if (m_stabilityCheck) m_stabilityCheck->setChecked(data["stability"].toBool());
    if (m_autoClutchCheck) m_autoClutchCheck->setChecked(data["autoClutch"].toBool());
    if (m_mgukLapsSpin) m_mgukLapsSpin->setValue(data["mgukLaps"].toInt());
    if (m_weatherEdit) m_weatherEdit->setText(data["weather"].toString());
    if (m_weatherTempSpin) m_weatherTempSpin->setValue(data["weatherTemp"].toDouble());
}

} // namespace ks
