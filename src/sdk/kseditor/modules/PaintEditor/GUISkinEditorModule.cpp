#include "GUISkinEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QTextStream>

namespace ks {

GUISkinEditorModule::GUISkinEditorModule(QWidget* parent) : EditorModule(parent) {}
bool GUISkinEditorModule::initialize() { LOG_INFO("GUISkinEditorModule", "Initialized"); return true; }
void GUISkinEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText(tr("Shut down")); }

QDockWidget* GUISkinEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("GUI Skin Editor"), mainWindow);
    m_dockWidget->setObjectName("GUISkinEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* colorsGroup = new QGroupBox(tr("Colors"));
    auto* colorsLayout = new QGridLayout(colorsGroup);

    auto makeColorRow = [&](int row, const QString& label, QPushButton*& btn, auto slot) {
        btn = new QPushButton(tr("Pick"));
        colorsLayout->addWidget(new QLabel(label), row, 0);
        colorsLayout->addWidget(btn, row, 1);
        connect(btn, &QPushButton::clicked, this, slot);
    };

    makeColorRow(0, tr("Window BG:"), m_windowBgBtn, &GUISkinEditorModule::onWindowBgClicked);
    makeColorRow(1, tr("Panel BG:"), m_panelBgBtn, &GUISkinEditorModule::onPanelBgClicked);
    makeColorRow(2, tr("Text:"), m_textColorBtn, &GUISkinEditorModule::onTextColorClicked);
    makeColorRow(3, tr("Accent:"), m_accentColorBtn, &GUISkinEditorModule::onAccentColorClicked);
    makeColorRow(4, tr("Highlight:"), m_highlightColorBtn, &GUISkinEditorModule::onHighlightColorClicked);
    makeColorRow(5, tr("Border:"), m_borderColorBtn, &GUISkinEditorModule::onBorderColorClicked);
    makeColorRow(6, tr("Error:"), m_errorColorBtn, &GUISkinEditorModule::onErrorColorClicked);
    makeColorRow(7, tr("Success:"), m_successColorBtn, &GUISkinEditorModule::onSuccessColorClicked);
    makeColorRow(8, tr("Warning:"), m_warningColorBtn, &GUISkinEditorModule::onWarningColorClicked);

    mainLayout->addWidget(colorsGroup);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load skin.ini")); m_saveBtn = new QPushButton(tr("Save skin.ini")); m_resetBtn = new QPushButton(tr("Reset"));
    actionLayout->addWidget(m_loadBtn); actionLayout->addWidget(m_saveBtn); actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready")); mainLayout->addWidget(m_statusLabel);

    connect(m_loadBtn, &QPushButton::clicked, this, &GUISkinEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &GUISkinEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &GUISkinEditorModule::onResetDefaults);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void GUISkinEditorModule::importFile(const QString& f) { m_filePath = f; loadFileToUI(); }
void GUISkinEditorModule::exportFile(const QString& f) { m_filePath = f; saveFileFromUI(); }
void GUISkinEditorModule::onActivation()
{
    m_statusLabel->setText(tr("Active"));
}

void GUISkinEditorModule::onDeactivation()
{
    m_statusLabel->setText(tr("Inactive"));
}

void GUISkinEditorModule::updateButtonColor(QPushButton* btn, const QColor& c)
{
    btn->setStyleSheet(QString("background-color: rgb(%1,%2,%3)").arg(c.red()).arg(c.green()).arg(c.blue()));
}

void GUISkinEditorModule::onWindowBgClicked() { QColor c = QColorDialog::getColor(m_colors.windowBg, this); if (c.isValid()) { m_colors.windowBg = c; updateButtonColor(m_windowBgBtn, c); } }
void GUISkinEditorModule::onPanelBgClicked() { QColor c = QColorDialog::getColor(m_colors.panelBg, this); if (c.isValid()) { m_colors.panelBg = c; updateButtonColor(m_panelBgBtn, c); } }
void GUISkinEditorModule::onTextColorClicked() { QColor c = QColorDialog::getColor(m_colors.textColor, this); if (c.isValid()) { m_colors.textColor = c; updateButtonColor(m_textColorBtn, c); } }
void GUISkinEditorModule::onAccentColorClicked() { QColor c = QColorDialog::getColor(m_colors.accentColor, this); if (c.isValid()) { m_colors.accentColor = c; updateButtonColor(m_accentColorBtn, c); } }
void GUISkinEditorModule::onHighlightColorClicked() { QColor c = QColorDialog::getColor(m_colors.highlightColor, this); if (c.isValid()) { m_colors.highlightColor = c; updateButtonColor(m_highlightColorBtn, c); } }
void GUISkinEditorModule::onBorderColorClicked() { QColor c = QColorDialog::getColor(m_colors.borderColor, this); if (c.isValid()) { m_colors.borderColor = c; updateButtonColor(m_borderColorBtn, c); } }
void GUISkinEditorModule::onErrorColorClicked() { QColor c = QColorDialog::getColor(m_colors.errorColor, this); if (c.isValid()) { m_colors.errorColor = c; updateButtonColor(m_errorColorBtn, c); } }
void GUISkinEditorModule::onSuccessColorClicked() { QColor c = QColorDialog::getColor(m_colors.successColor, this); if (c.isValid()) { m_colors.successColor = c; updateButtonColor(m_successColorBtn, c); } }
void GUISkinEditorModule::onWarningColorClicked() { QColor c = QColorDialog::getColor(m_colors.warningColor, this); if (c.isValid()) { m_colors.warningColor = c; updateButtonColor(m_warningColorBtn, c); } }

void GUISkinEditorModule::onLoadFile()
{
    QString p = QFileDialog::getOpenFileName(this, tr("Open skin.ini"), QString(), tr("Skin INI (*.ini)"));
    if (!p.isEmpty()) { m_filePath = p; loadFileToUI(); m_statusLabel->setText(tr("Loaded: %1").arg(p)); }
}

void GUISkinEditorModule::onSaveFile()
{
    QString p = m_filePath.isEmpty() ? QFileDialog::getSaveFileName(this, tr("Save skin.ini"), QString(), tr("Skin INI (*.ini)")) : m_filePath;
    if (!p.isEmpty()) { m_filePath = p; saveFileFromUI(); m_statusLabel->setText(tr("Saved: %1").arg(p)); }
}

void GUISkinEditorModule::onResetDefaults()
{
    m_colors = GUISkinColors();
    updateButtonColor(m_windowBgBtn, m_colors.windowBg);
    updateButtonColor(m_panelBgBtn, m_colors.panelBg);
    updateButtonColor(m_textColorBtn, m_colors.textColor);
    updateButtonColor(m_accentColorBtn, m_colors.accentColor);
    updateButtonColor(m_highlightColorBtn, m_colors.highlightColor);
    updateButtonColor(m_borderColorBtn, m_colors.borderColor);
    updateButtonColor(m_errorColorBtn, m_colors.errorColor);
    updateButtonColor(m_successColorBtn, m_colors.successColor);
    updateButtonColor(m_warningColorBtn, m_colors.warningColor);
    m_statusLabel->setText(tr("Reset to defaults"));
}

void GUISkinEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

void GUISkinEditorModule::loadFileToUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QString c = file.readAll(); file.close();
    auto parseColor = [&](const QString& key) -> QColor {
        for (const QString& line : c.split("\n")) {
            QString l = line.trimmed();
            if (l.startsWith(key + "=")) {
                QStringList v = l.mid(key.length() + 1).split(",");
                if (v.size() >= 4) return QColor(v[0].toInt(), v[1].toInt(), v[2].toInt());
                if (v.size() >= 3) return QColor(v[0].toInt(), v[1].toInt(), v[2].toInt());
            }
        }
        return Qt::white;
    };
    m_colors.windowBg = parseColor("WINDOW_BG");
    m_colors.panelBg = parseColor("PANEL_BG");
    m_colors.textColor = parseColor("TEXT_COLOR");
    m_colors.accentColor = parseColor("ACCENT_COLOR");
    m_colors.highlightColor = parseColor("HIGHLIGHT_COLOR");
    m_colors.borderColor = parseColor("BORDER_COLOR");
    m_colors.errorColor = parseColor("ERROR_COLOR");
    m_colors.successColor = parseColor("SUCCESS_COLOR");
    m_colors.warningColor = parseColor("WARNING_COLOR");
    updateButtonColor(m_windowBgBtn, m_colors.windowBg);
    updateButtonColor(m_panelBgBtn, m_colors.panelBg);
    updateButtonColor(m_textColorBtn, m_colors.textColor);
    updateButtonColor(m_accentColorBtn, m_colors.accentColor);
    updateButtonColor(m_highlightColorBtn, m_colors.highlightColor);
    updateButtonColor(m_borderColorBtn, m_colors.borderColor);
    updateButtonColor(m_errorColorBtn, m_colors.errorColor);
    updateButtonColor(m_successColorBtn, m_colors.successColor);
    updateButtonColor(m_warningColorBtn, m_colors.warningColor);
}

void GUISkinEditorModule::saveFileFromUI()
{
    QFile file(m_filePath); if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&file);
    auto writeColor = [&](const QString& key, const QColor& c) { o << key << "=" << c.red() << "," << c.green() << "," << c.blue() << "\n"; };
    writeColor("WINDOW_BG", m_colors.windowBg); writeColor("PANEL_BG", m_colors.panelBg);
    writeColor("TEXT_COLOR", m_colors.textColor); writeColor("ACCENT_COLOR", m_colors.accentColor);
    writeColor("HIGHLIGHT_COLOR", m_colors.highlightColor); writeColor("BORDER_COLOR", m_colors.borderColor);
    writeColor("ERROR_COLOR", m_colors.errorColor); writeColor("SUCCESS_COLOR", m_colors.successColor);
    writeColor("WARNING_COLOR", m_colors.warningColor);
    file.close();
}

QJsonObject GUISkinEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;
    QJsonObject colors;
    colors["windowBg"] = m_colors.windowBg.name();
    colors["panelBg"] = m_colors.panelBg.name();
    colors["textColor"] = m_colors.textColor.name();
    colors["accentColor"] = m_colors.accentColor.name();
    colors["highlightColor"] = m_colors.highlightColor.name();
    colors["borderColor"] = m_colors.borderColor.name();
    colors["errorColor"] = m_colors.errorColor.name();
    colors["successColor"] = m_colors.successColor.name();
    colors["warningColor"] = m_colors.warningColor.name();
    data["colors"] = colors;
    return data;
}

void GUISkinEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    QJsonObject colors = data["colors"].toObject();
    m_colors.windowBg = QColor(colors["windowBg"].toString("#2B2B2B"));
    m_colors.panelBg = QColor(colors["panelBg"].toString("#3C3C3C"));
    m_colors.textColor = QColor(colors["textColor"].toString("#FFFFFF"));
    m_colors.accentColor = QColor(colors["accentColor"].toString("#4A90D9"));
    m_colors.highlightColor = QColor(colors["highlightColor"].toString("#2196F3"));
    m_colors.borderColor = QColor(colors["borderColor"].toString("#555555"));
    m_colors.errorColor = QColor(colors["errorColor"].toString("#FF4444"));
    m_colors.successColor = QColor(colors["successColor"].toString("#44FF44"));
    m_colors.warningColor = QColor(colors["warningColor"].toString("#FFAA00"));
}

} // namespace ks
