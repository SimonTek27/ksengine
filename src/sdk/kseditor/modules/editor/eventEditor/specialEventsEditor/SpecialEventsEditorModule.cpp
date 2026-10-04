#include "SpecialEventsEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QDir>
#include <QTextStream>
#include <QJsonArray>
#include <QTableWidget>
#include <QHeaderView>

namespace ks {

SpecialEventsEditorModule::SpecialEventsEditorModule(QWidget* parent) : EditorModule(parent) {}
bool SpecialEventsEditorModule::initialize() { LOG_INFO("SpecialEventsEditorModule", "Initialized"); return true; }
void SpecialEventsEditorModule::shutdown()
{
    m_events.clear();
    m_eventList->clear();
    m_selectedIndex = -1;
    m_dir.clear();
}

QDockWidget* SpecialEventsEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Special Events Editor"), mainWindow);
    m_dockWidget->setObjectName("SpecialEventsEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QHBoxLayout(centralWidget);

    auto* listWidget = new QWidget(); auto* listLayout = new QVBoxLayout(listWidget);
    m_eventList = new QListWidget(); listLayout->addWidget(m_eventList);
    auto* listBtns = new QHBoxLayout();
    m_addBtn = new QPushButton(tr("Add")); m_removeBtn = new QPushButton(tr("Remove"));
    listBtns->addWidget(m_addBtn); listBtns->addWidget(m_removeBtn); listBtns->addStretch();
    listLayout->addLayout(listBtns);
    mainLayout->addWidget(listWidget);

    auto* propsWidget = new QWidget(); auto* propsLayout = new QGridLayout(propsWidget);
    m_eventNameEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel(tr("Name:")), 0, 0); propsLayout->addWidget(m_eventNameEdit, 0, 1);

    // Settings table
    auto* settingsLabel = new QLabel(tr("Settings:"));
    settingsLabel->setStyleSheet("font-weight: bold; margin-top: 8px;");
    propsLayout->addWidget(settingsLabel, 1, 0, 1, 2);
    m_settingsTable = new QTableWidget(0, 2);
    m_settingsTable->setHorizontalHeaderLabels({tr("Key"), tr("Value")});
    m_settingsTable->horizontalHeader()->setStretchLastSection(true);
    m_settingsTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_settingsTable->setMaximumHeight(200);
    propsLayout->addWidget(m_settingsTable, 2, 0, 1, 2);
    auto* settingBtns = new QHBoxLayout();
    m_addSettingBtn = new QPushButton("+"); m_addSettingBtn->setFixedSize(28, 28);
    m_removeSettingBtn = new QPushButton("-"); m_removeSettingBtn->setFixedSize(28, 28);
    settingBtns->addWidget(m_addSettingBtn); settingBtns->addWidget(m_removeSettingBtn); settingBtns->addStretch();
    propsLayout->addLayout(settingBtns, 3, 0, 1, 2);

    mainLayout->addWidget(propsWidget);

    auto* vMain = new QVBoxLayout(); vMain->addLayout(mainLayout);
    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load Directory")); m_saveBtn = new QPushButton(tr("Save"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn);
    vMain->addLayout(actionLayout);
    m_statusLabel = new QLabel(tr("Ready")); vMain->addWidget(m_statusLabel);

    auto* wrapper = new QWidget(); wrapper->setLayout(vMain);
    m_dockWidget->setWidget(wrapper);

    connect(m_eventList, &QListWidget::currentRowChanged, this, &SpecialEventsEditorModule::onEventSelected);
    connect(m_addBtn, &QPushButton::clicked, this, &SpecialEventsEditorModule::onAddEvent);
    connect(m_removeBtn, &QPushButton::clicked, this, &SpecialEventsEditorModule::onRemoveEvent);
    connect(m_loadBtn, &QPushButton::clicked, this, &SpecialEventsEditorModule::onLoadDir);
    connect(m_saveBtn, &QPushButton::clicked, this, &SpecialEventsEditorModule::onSaveDir);
    connect(m_eventNameEdit, &QLineEdit::textChanged, this, &SpecialEventsEditorModule::onEventNameChanged);
    connect(m_settingsTable, &QTableWidget::cellChanged, this, &SpecialEventsEditorModule::onSettingChanged);
    connect(m_addSettingBtn, &QPushButton::clicked, this, &SpecialEventsEditorModule::onAddSetting);
    connect(m_removeSettingBtn, &QPushButton::clicked, this, &SpecialEventsEditorModule::onRemoveSetting);

    return m_dockWidget;
}

void SpecialEventsEditorModule::importFile(const QString& f) { m_dir = f; loadDirToUI(); }
void SpecialEventsEditorModule::exportFile(const QString& f)
{
    if (f.isEmpty()) return;
    QDir dir(f);
    for (const auto& event : m_events) {
        QString eventDir = dir.absoluteFilePath(event.name);
        QDir().mkpath(eventDir);
        QString iniPath = eventDir + "/event.ini";
        QFile file(iniPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "NAME=" << event.name << "\n";
            for (auto it = event.settings.constBegin(); it != event.settings.constEnd(); ++it)
                out << it.key() << "=" << it.value() << "\n";
            file.close();
        }
    }
    m_statusLabel->setText(tr("Exported %1 events to %2").arg(m_events.size()).arg(f));
}
void SpecialEventsEditorModule::onActivation() { if (m_statusLabel) m_statusLabel->setText(tr("Active")); }
void SpecialEventsEditorModule::onDeactivation() { if (m_statusLabel) m_statusLabel->setText(tr("Inactive")); }
void SpecialEventsEditorModule::onEventSelected(int r) {
    if (r >= 0 && r < m_events.size()) {
        m_selectedIndex = r;
        m_eventNameEdit->setText(m_events[r].name);
        m_updatingSettings = true;
        m_settingsTable->setRowCount(0);
        const auto& settings = m_events[r].settings;
        for (auto it = settings.constBegin(); it != settings.constEnd(); ++it) {
            int row = m_settingsTable->rowCount();
            m_settingsTable->insertRow(row);
            m_settingsTable->setItem(row, 0, new QTableWidgetItem(it.key()));
            m_settingsTable->setItem(row, 1, new QTableWidgetItem(it.value()));
        }
        m_updatingSettings = false;
    }
}
void SpecialEventsEditorModule::onAddEvent() { SpecialEvent e; e.name = tr("New Event"); m_events.append(e); m_eventList->addItem(e.name); m_statusLabel->setText(tr("Added: %1").arg(e.name)); }
void SpecialEventsEditorModule::onRemoveEvent() {
    int row = m_eventList->currentRow();
    if (row >= 0) { m_events.removeAt(row); delete m_eventList->takeItem(row); m_statusLabel->setText(tr("Removed event")); }
}
void SpecialEventsEditorModule::onEventNameChanged(const QString& t) { if (m_selectedIndex >= 0) { m_events[m_selectedIndex].name = t; m_eventList->item(m_selectedIndex)->setText(t); } }
void SpecialEventsEditorModule::onAddSetting() {
    if (m_selectedIndex < 0) return;
    int row = m_settingsTable->rowCount();
    m_settingsTable->insertRow(row);
    m_settingsTable->setItem(row, 0, new QTableWidgetItem(tr("key")));
    m_settingsTable->setItem(row, 1, new QTableWidgetItem(tr("value")));
    m_events[m_selectedIndex].settings[tr("key")] = tr("value");
    m_statusLabel->setText(tr("Added setting"));
}
void SpecialEventsEditorModule::onRemoveSetting() {
    if (m_selectedIndex < 0) return;
    int row = m_settingsTable->currentRow();
    if (row < 0) return;
    QTableWidgetItem* keyItem = m_settingsTable->item(row, 0);
    if (keyItem) m_events[m_selectedIndex].settings.remove(keyItem->text());
    m_settingsTable->removeRow(row);
    m_statusLabel->setText(tr("Removed setting"));
}
void SpecialEventsEditorModule::onSettingChanged(int row, int col) {
    if (m_updatingSettings || m_selectedIndex < 0) return;
    QTableWidgetItem* keyItem = m_settingsTable->item(row, 0);
    QTableWidgetItem* valItem = m_settingsTable->item(row, 1);
    if (keyItem && valItem) {
        m_events[m_selectedIndex].settings[keyItem->text()] = valItem->text();
    }
}

void SpecialEventsEditorModule::onLoadDir()
{
    QString d = QFileDialog::getExistingDirectory(this, tr("Open specialevents directory"));
    if (!d.isEmpty()) { m_dir = d; loadDirToUI(); }
}

void SpecialEventsEditorModule::onSaveDir()
{
    if (m_dir.isEmpty()) return;
    QDir dir(m_dir);
    for (const auto& event : m_events) {
        QString eventDir = dir.absoluteFilePath(event.name);
        QDir().mkpath(eventDir);
        if (!event.iniPath.isEmpty()) {
            QFile file(event.iniPath);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                QTextStream out(&file);
                out << "NAME=" << event.name << "\n";
                for (auto it = event.settings.constBegin(); it != event.settings.constEnd(); ++it)
                    out << it.key() << "=" << it.value() << "\n";
                file.close();
            }
        }
    }
    m_statusLabel->setText(tr("Saved %1 events to %2").arg(m_events.size()).arg(m_dir));
}

void SpecialEventsEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void SpecialEventsEditorModule::loadDirToUI()
{
    m_events.clear(); m_eventList->clear();
    QDir dir(m_dir);
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        SpecialEvent e;
        e.name = sub;
        e.iniPath = dir.absoluteFilePath(sub + "/event.ini");
        e.previewPath = dir.absoluteFilePath(sub + "/preview.png");
        QFile f(e.iniPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream in(&f);
            while (!in.atEnd()) {
                QString line = in.readLine().trimmed();
                if (line.contains('=') && !line.startsWith('[')) {
                    int eq = line.indexOf('=');
                    e.settings[line.left(eq).trimmed()] = line.mid(eq + 1).trimmed();
                }
            }
            f.close();
        }
        m_events.append(e);
        m_eventList->addItem(sub);
    }
    m_statusLabel->setText(tr("Loaded %1 events").arg(m_events.size()));
}

QJsonObject SpecialEventsEditorModule::serializeProject() const
{
    QJsonObject data;
    data["dir"] = m_dir;
    data["selectedIndex"] = m_selectedIndex;
    QJsonArray eventsArray;
    for (const auto& ev : m_events) {
        QJsonObject obj;
        obj["name"] = ev.name;
        obj["iniPath"] = ev.iniPath;
        obj["previewPath"] = ev.previewPath;
        QJsonObject settingsObj;
        for (auto it = ev.settings.constBegin(); it != ev.settings.constEnd(); ++it)
            settingsObj[it.key()] = it.value();
        obj["settings"] = settingsObj;
        eventsArray.append(obj);
    }
    data["events"] = eventsArray;
    return data;
}

void SpecialEventsEditorModule::deserializeProject(const QJsonObject& data)
{
    m_dir = data["dir"].toString();
    m_selectedIndex = data["selectedIndex"].toInt(-1);
    m_events.clear();
    for (const auto& v : data["events"].toArray()) {
        QJsonObject obj = v.toObject();
        SpecialEvent ev;
        ev.name = obj["name"].toString();
        ev.iniPath = obj["iniPath"].toString();
        ev.previewPath = obj["previewPath"].toString();
        QJsonObject settingsObj = obj["settings"].toObject();
        for (auto it = settingsObj.constBegin(); it != settingsObj.constEnd(); ++it)
            ev.settings[it.key()] = it.value().toString();
        m_events.append(ev);
    }
}

} // namespace ks
