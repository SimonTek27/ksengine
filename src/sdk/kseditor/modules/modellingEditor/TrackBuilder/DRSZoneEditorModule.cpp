#include "DRSZoneEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QHeaderView>
#include <QSplitter>
#include <QTextStream>
#include <QJsonObject>
#include <QJsonArray>

namespace ks {

DRSZoneEditorModule::DRSZoneEditorModule(QWidget* parent) : EditorModule(parent) {}
bool DRSZoneEditorModule::initialize() { LOG_INFO("DRSZoneEditorModule", "Initialized"); return true; }
void DRSZoneEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* DRSZoneEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("DRS Zone Editor"), mainWindow);
    m_dockWidget->setObjectName("DRSZoneEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);
    auto* splitter = new QSplitter(Qt::Vertical);

    // Table
    auto* tableWidget = new QWidget(); auto* tableLayout = new QVBoxLayout(tableWidget);
    m_zoneTable = new QTableWidget(); m_zoneTable->setColumnCount(3);
    m_zoneTable->setHorizontalHeaderLabels({tr("ID"), tr("Detection"), tr("Activation")});
    m_zoneTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_zoneTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    tableLayout->addWidget(m_zoneTable);
    auto* tableBtns = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("Add")); m_removeBtn = new QPushButton(tr("Remove"));
    tableBtns->addWidget(m_addBtn); tableBtns->addWidget(m_removeBtn); tableBtns->addStretch();
    tableLayout->addLayout(tableBtns);
    splitter->addWidget(tableWidget);

    // Props
    auto* propsWidget = new QWidget(); auto* propsLayout = new QGridLayout(propsWidget);
    m_startXSpin = new QDoubleSpinBox(); m_startXSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Start X:")), 0, 0); propsLayout->addWidget(m_startXSpin, 0, 1);
    m_startYSpin = new QDoubleSpinBox(); m_startYSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Start Y:")), 1, 0); propsLayout->addWidget(m_startYSpin, 1, 1);
    m_startZSpin = new QDoubleSpinBox(); m_startZSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("Start Z:")), 2, 0); propsLayout->addWidget(m_startZSpin, 2, 1);
    m_endXSpin = new QDoubleSpinBox(); m_endXSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("End X:")), 3, 0); propsLayout->addWidget(m_endXSpin, 3, 1);
    m_endYSpin = new QDoubleSpinBox(); m_endYSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("End Y:")), 4, 0); propsLayout->addWidget(m_endYSpin, 4, 1);
    m_endZSpin = new QDoubleSpinBox(); m_endZSpin->setRange(-100000, 100000);
    propsLayout->addWidget(new QLabel(tr("End Z:")), 5, 0); propsLayout->addWidget(m_endZSpin, 5, 1);
    m_detectionSpin = new QDoubleSpinBox(); m_detectionSpin->setRange(0, 100000);
    propsLayout->addWidget(new QLabel(tr("Detection Point:")), 6, 0); propsLayout->addWidget(m_detectionSpin, 6, 1);
    m_activationSpin = new QDoubleSpinBox(); m_activationSpin->setRange(0, 100000);
    propsLayout->addWidget(new QLabel(tr("Activation Point:")), 7, 0); propsLayout->addWidget(m_activationSpin, 7, 1);
    splitter->addWidget(propsWidget);

    mainLayout->addWidget(splitter);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load drs_zones.ini")); m_saveBtn = new QPushButton(tr("Save drs_zones.ini")); m_resetBtn = new QPushButton(tr("Reset"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn); actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready")); mainLayout->addWidget(m_statusLabel);

    connect(m_zoneTable, &QTableWidget::cellClicked, this, [this](int r, int) { onZoneSelected(r); });
    connect(m_addBtn, &QPushButton::clicked, this, &DRSZoneEditorModule::onAddZone);
    connect(m_removeBtn, &QPushButton::clicked, this, &DRSZoneEditorModule::onRemoveZone);
    connect(m_loadBtn, &QPushButton::clicked, this, &DRSZoneEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &DRSZoneEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &DRSZoneEditorModule::onResetDefaults);

    // Wire property slot connections (permanent, not in onActivation)
    connect(m_startXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onStartLineXChanged);
    connect(m_startYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onStartLineYChanged);
    connect(m_startZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onStartLineZChanged);
    connect(m_endXSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onEndLineXChanged);
    connect(m_endYSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onEndLineYChanged);
    connect(m_endZSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onEndLineZChanged);
    connect(m_detectionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onDetectionPointChanged);
    connect(m_activationSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &DRSZoneEditorModule::onActivationPointChanged);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void DRSZoneEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void DRSZoneEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }
void DRSZoneEditorModule::onActivation()
{
    m_statusLabel->setText(tr("Active"));
}

void DRSZoneEditorModule::onDeactivation()
{
    m_statusLabel->setText(tr("Inactive"));
}

void DRSZoneEditorModule::onZoneSelected(int r) { if (r >= 0 && r < m_zones.size()) { m_selectedIndex = r; selectZone(r); } }
void DRSZoneEditorModule::onAddZone() { DRSZone z; z.id = m_zones.size(); m_zones.append(z); updateTable(); }
void DRSZoneEditorModule::onRemoveZone() {
    if (m_selectedIndex < 0 || m_selectedIndex >= m_zones.size()) return;
    m_zones.removeAt(m_selectedIndex);
    m_selectedIndex = -1;
    updateTable();
    // Clear property fields
    m_startXSpin->setValue(0); m_startYSpin->setValue(0); m_startZSpin->setValue(0);
    m_endXSpin->setValue(0); m_endYSpin->setValue(0); m_endZSpin->setValue(0);
    m_detectionSpin->setValue(0); m_activationSpin->setValue(0);
}
void DRSZoneEditorModule::onStartLineXChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].startLine[0] = v; }
void DRSZoneEditorModule::onStartLineYChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].startLine[1] = v; }
void DRSZoneEditorModule::onStartLineZChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].startLine[2] = v; }
void DRSZoneEditorModule::onEndLineXChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].endLine[0] = v; }
void DRSZoneEditorModule::onEndLineYChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].endLine[1] = v; }
void DRSZoneEditorModule::onEndLineZChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].endLine[2] = v; }
void DRSZoneEditorModule::onDetectionPointChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].detectionPoint = v; }
void DRSZoneEditorModule::onActivationPointChanged(double v) { if (m_selectedIndex >= 0) m_zones[m_selectedIndex].activationPoint = v; }

void DRSZoneEditorModule::onLoadFile() { QString p = QFileDialog::getOpenFileName(this, tr("Open drs_zones.ini"), QString(), tr("DRS INI (*.ini)")); if (!p.isEmpty()) { m_filePath = p; loadFileToUI(); } }
void DRSZoneEditorModule::onSaveFile() { QString p = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, tr("Save drs_zones.ini"), QString(), tr("DRS INI (*.ini)")) : m_filePath; if (!p.isEmpty()) { m_filePath = p; saveFileFromUI(); } }
void DRSZoneEditorModule::onResetDefaults()
{
    m_zones.clear();
    DRSZone z1; z1.id = 0; z1.startLine[0] = 0; z1.startLine[2] = -30; z1.endLine[0] = 0; z1.endLine[2] = -20; z1.detectionPoint = 50; z1.activationPoint = 30;
    m_zones.append(z1);
    DRSZone z2; z2.id = 1; z2.startLine[0] = 100; z2.startLine[2] = -50; z2.endLine[0] = 100; z2.endLine[2] = -40; z2.detectionPoint = 200; z2.activationPoint = 120;
    m_zones.append(z2);
    m_selectedIndex = -1;
    updateTable();
    m_statusLabel->setText(tr("Reset to defaults (2 zones)"));
}
void DRSZoneEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void DRSZoneEditorModule::loadFileToUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString c = file.readAll(); file.close();
    m_zones.clear();
    QStringList sections = c.split("[", Qt::SkipEmptyParts);
    for (const QString& sec : sections) {
        if (!sec.startsWith("ZONE_")) continue;
        DRSZone z;
        for (const QString& line : sec.split("\n")) {
            QString l = line.trimmed();
            if (l.startsWith("START_LINE=")) { QStringList v = l.mid(11).split(","); if (v.size() >= 3) { z.startLine[0] = v[0].toFloat(); z.startLine[1] = v[1].toFloat(); z.startLine[2] = v[2].toFloat(); } }
            else if (l.startsWith("END_LINE=")) { QStringList v = l.mid(9).split(","); if (v.size() >= 3) { z.endLine[0] = v[0].toFloat(); z.endLine[1] = v[1].toFloat(); z.endLine[2] = v[2].toFloat(); } }
            else if (l.startsWith("DETECTION_POINT=")) z.detectionPoint = l.mid(16).toFloat();
            else if (l.startsWith("ACTIVATION_POINT=")) z.activationPoint = l.mid(17).toFloat();
        }
        m_zones.append(z);
    }
    updateTable();
}

void DRSZoneEditorModule::saveFileFromUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&file);
    for (int i = 0; i < m_zones.size(); ++i) {
        const auto& z = m_zones[i];
        o << "[ZONE_" << i << "]\n";
        o << "START_LINE=" << z.startLine[0] << "," << z.startLine[1] << "," << z.startLine[2] << "\n";
        o << "END_LINE=" << z.endLine[0] << "," << z.endLine[1] << "," << z.endLine[2] << "\n";
        o << "DETECTION_POINT=" << z.detectionPoint << "\n";
        o << "ACTIVATION_POINT=" << z.activationPoint << "\n\n";
    }
    file.close();
}

void DRSZoneEditorModule::updateTable()
{
    m_zoneTable->setRowCount(m_zones.size());
    for (int i = 0; i < m_zones.size(); ++i) {
        m_zoneTable->setItem(i, 0, new QTableWidgetItem(QString::number(i)));
        m_zoneTable->setItem(i, 1, new QTableWidgetItem(QString::number(m_zones[i].detectionPoint, 'f', 1)));
        m_zoneTable->setItem(i, 2, new QTableWidgetItem(QString::number(m_zones[i].activationPoint, 'f', 1)));
    }
}

void DRSZoneEditorModule::selectZone(int idx)
{
    if (idx < 0 || idx >= m_zones.size()) return;
    const auto& z = m_zones[idx];
    m_startXSpin->setValue(z.startLine[0]); m_startYSpin->setValue(z.startLine[1]); m_startZSpin->setValue(z.startLine[2]);
    m_endXSpin->setValue(z.endLine[0]); m_endYSpin->setValue(z.endLine[1]); m_endZSpin->setValue(z.endLine[2]);
    m_detectionSpin->setValue(z.detectionPoint); m_activationSpin->setValue(z.activationPoint);
}

QJsonObject DRSZoneEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    QJsonArray zonesArray;
    for (const auto& z : m_zones) {
        QJsonObject obj;
        obj["id"] = z.id;
        obj["detectionPoint"] = static_cast<double>(z.detectionPoint);
        obj["activationPoint"] = static_cast<double>(z.activationPoint);
        QJsonArray startLine;
        for (int i = 0; i < 3; ++i) startLine.append(static_cast<double>(z.startLine[i]));
        obj["startLine"] = startLine;
        QJsonArray endLine;
        for (int i = 0; i < 3; ++i) endLine.append(static_cast<double>(z.endLine[i]));
        obj["endLine"] = endLine;
        zonesArray.append(obj);
    }
    data["zones"] = zonesArray;
    return data;
}

void DRSZoneEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    m_zones.clear();
    for (const auto& v : data["zones"].toArray()) {
        QJsonObject obj = v.toObject();
        DRSZone z;
        z.id = obj["id"].toInt();
        z.detectionPoint = static_cast<float>(obj["detectionPoint"].toDouble());
        z.activationPoint = static_cast<float>(obj["activationPoint"].toDouble());
        QJsonArray startLine = obj["startLine"].toArray();
        for (int i = 0; i < qMin(3, startLine.size()); ++i)
            z.startLine[i] = static_cast<float>(startLine[i].toDouble());
        QJsonArray endLine = obj["endLine"].toArray();
        for (int i = 0; i < qMin(3, endLine.size()); ++i)
            z.endLine[i] = static_cast<float>(endLine[i].toDouble());
        m_zones.append(z);
    }
}

} // namespace ks
