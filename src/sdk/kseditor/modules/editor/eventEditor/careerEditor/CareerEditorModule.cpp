#include "CareerEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QFileDialog>
#include <QDir>
#include <QTextStream>
#include <QJsonArray>

namespace ks {

CareerEditorModule::CareerEditorModule(QWidget* parent) : EditorModule(parent) {}
bool CareerEditorModule::initialize() { LOG_INFO("CareerEditorModule", "Initialized"); return true; }
void CareerEditorModule::shutdown()
{
    m_series.clear();
    m_seriesList->clear();
    m_selectedIndex = -1;
    m_dir.clear();
}

QDockWidget* CareerEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Career Editor"), mainWindow);
    m_dockWidget->setObjectName("CareerEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QHBoxLayout(centralWidget);

    auto* listWidget = new QWidget(); auto* listLayout = new QVBoxLayout(listWidget);
    m_seriesList = new QListWidget(); listLayout->addWidget(m_seriesList);
    mainLayout->addWidget(listWidget);

    auto* propsWidget = new QWidget(); auto* propsLayout = new QGridLayout(propsWidget);
    m_seriesNameEdit = new QLineEdit(); propsLayout->addWidget(new QLabel(tr("Name:")), 0, 0); propsLayout->addWidget(m_seriesNameEdit, 0, 1);
    m_seriesInfoEdit = new QTextEdit(); propsLayout->addWidget(new QLabel(tr("Info:")), 1, 0); propsLayout->addWidget(m_seriesInfoEdit, 1, 1);
    mainLayout->addWidget(propsWidget);

    auto* vMain = new QVBoxLayout(); vMain->addLayout(mainLayout);
    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load Directory"));
    m_addBtn = new QPushButton("+");
    m_removeBtn = new QPushButton("-");
    m_addBtn->setFixedSize(28, 28);
    m_removeBtn->setFixedSize(28, 28);
    actionLayout->addWidget(m_loadBtn);
    actionLayout->addStretch();
    actionLayout->addWidget(m_addBtn);
    actionLayout->addWidget(m_removeBtn);
    vMain->addLayout(actionLayout);
    m_statusLabel = new QLabel(tr("Ready")); vMain->addWidget(m_statusLabel);

    auto* wrapper = new QWidget(); wrapper->setLayout(vMain);
    m_dockWidget->setWidget(wrapper);

    connect(m_seriesList, &QListWidget::currentRowChanged, this, &CareerEditorModule::onSeriesSelected);
    connect(m_loadBtn, &QPushButton::clicked, this, &CareerEditorModule::onLoadDir);
    connect(m_seriesNameEdit, &QLineEdit::textChanged, this, &CareerEditorModule::onSeriesNameChanged);
    connect(m_seriesInfoEdit, &QTextEdit::textChanged, this, &CareerEditorModule::onSeriesInfoChanged);
    connect(m_addBtn, &QPushButton::clicked, this, &CareerEditorModule::onAddSeries);
    connect(m_removeBtn, &QPushButton::clicked, this, &CareerEditorModule::onRemoveSeries);

    return m_dockWidget;
}

void CareerEditorModule::importFile(const QString& f) { m_dir = f; loadDirToUI(); }
void CareerEditorModule::exportFile(const QString& f)
{
    if (f.isEmpty()) return;
    QDir dir(f);
    for (const auto& series : m_series) {
        QString seriesDir = dir.absoluteFilePath(series.first);
        QDir().mkpath(seriesDir);
        QString iniPath = seriesDir + "/series.ini";
        QFile file(iniPath);
        if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream out(&file);
            out << "NAME=" << series.first << "\n";
            if (!series.second.isEmpty())
                out << "INFO=" << series.second << "\n";
            file.close();
        }
    }
    m_statusLabel->setText(tr("Exported %1 series to %2").arg(m_series.size()).arg(f));
}
void CareerEditorModule::onActivation() { if (m_statusLabel) m_statusLabel->setText(tr("Active")); }
void CareerEditorModule::onDeactivation() { if (m_statusLabel) m_statusLabel->setText(tr("Inactive")); }
void CareerEditorModule::onSeriesSelected(int r) { if (r >= 0 && r < m_series.size()) { m_selectedIndex = r; m_seriesNameEdit->setText(m_series[r].first); m_seriesInfoEdit->setText(m_series[r].second); } }
void CareerEditorModule::onSeriesNameChanged(const QString& t) { if (m_selectedIndex >= 0) { m_series[m_selectedIndex].first = t; m_seriesList->item(m_selectedIndex)->setText(t); } }
void CareerEditorModule::onSeriesInfoChanged() { if (m_selectedIndex >= 0) { m_series[m_selectedIndex].second = m_seriesInfoEdit->toPlainText(); } }
void CareerEditorModule::onAddSeries() {
    QString name = tr("Series %1").arg(m_series.size() + 1);
    m_series.append(std::make_pair(name, ""));
    m_seriesList->addItem(name);
    m_statusLabel->setText(tr("Added: %1").arg(name));
}
void CareerEditorModule::onRemoveSeries() {
    int row = m_seriesList->currentRow();
    if (row < 0 || row >= m_series.size()) return;
    QString name = m_series[row].first;
    m_series.removeAt(row);
    delete m_seriesList->takeItem(row);
    if (m_selectedIndex == row) { m_selectedIndex = -1; m_seriesNameEdit->clear(); m_seriesInfoEdit->clear(); }
    m_statusLabel->setText(tr("Removed: %1").arg(name));
}

void CareerEditorModule::onLoadDir()
{
    QString d = QFileDialog::getExistingDirectory(this, tr("Open career directory"));
    if (!d.isEmpty()) { m_dir = d; loadDirToUI(); }
}

void CareerEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void CareerEditorModule::loadDirToUI()
{
    m_series.clear(); m_seriesList->clear();
    QDir dir(m_dir);
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QString iniPath = dir.absoluteFilePath(sub + "/series.ini");
        QString info;
        QFile f(iniPath);
        if (f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            info = QString::fromUtf8(f.readAll()).trimmed();
            f.close();
        }
        m_series.append({sub, info});
        m_seriesList->addItem(sub);
    }
    m_statusLabel->setText(tr("Loaded %1 series").arg(m_series.size()));
}

QJsonObject CareerEditorModule::serializeProject() const
{
    QJsonObject data;
    data["dir"] = m_dir;
    data["selectedIndex"] = m_selectedIndex;
    QJsonArray seriesArray;
    for (const auto& s : m_series) {
        QJsonObject obj;
        obj["name"] = s.first;
        obj["info"] = s.second;
        seriesArray.append(obj);
    }
    data["series"] = seriesArray;
    return data;
}

void CareerEditorModule::deserializeProject(const QJsonObject& data)
{
    m_dir = data["dir"].toString();
    m_selectedIndex = data["selectedIndex"].toInt(-1);
    m_series.clear();
    for (const auto& v : data["series"].toArray()) {
        QJsonObject obj = v.toObject();
        m_series.append(std::make_pair(obj["name"].toString(), obj["info"].toString()));
    }
}

} // namespace ks
