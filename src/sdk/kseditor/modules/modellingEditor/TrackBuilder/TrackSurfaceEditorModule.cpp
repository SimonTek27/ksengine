#include "TrackSurfaceEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QSplitter>
#include <QJsonArray>

namespace ks {

TrackSurfaceEditorModule::TrackSurfaceEditorModule(QWidget* parent)
    : EditorModule(parent)
{
}

bool TrackSurfaceEditorModule::initialize() { LOG_INFO("TrackSurfaceEditorModule", "Initialized"); return true; }
void TrackSurfaceEditorModule::shutdown() { if (m_statusLabel) m_statusLabel->setText("Shut down"); }

QDockWidget* TrackSurfaceEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (m_dockWidget) return m_dockWidget;

    m_dockWidget = new QDockWidget("Track Surface Editor", mainWindow);
    m_dockWidget->setObjectName("TrackSurfaceEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QVBoxLayout(centralWidget);

    auto* splitter = new QSplitter(Qt::Vertical);

    // Surface table
    auto* tableWidget = new QWidget();
    auto* tableLayout = new QVBoxLayout(tableWidget);
    m_surfaceTable = new QTableWidget();
    m_surfaceTable->setColumnCount(4);
    m_surfaceTable->setHorizontalHeaderLabels({"Key", "Friction", "Valid Track", "Pitlane"});
    m_surfaceTable->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    m_surfaceTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_surfaceTable->setSelectionMode(QAbstractItemView::SingleSelection);
    tableLayout->addWidget(m_surfaceTable);

    auto* tableBtnLayout = new QHBoxLayout();
    m_addSurfaceBtn = new QPushButton("Add");
    m_removeSurfaceBtn = new QPushButton("Remove");
    m_duplicateSurfaceBtn = new QPushButton("Duplicate");
    tableBtnLayout->addWidget(m_addSurfaceBtn);
    tableBtnLayout->addWidget(m_removeSurfaceBtn);
    tableBtnLayout->addWidget(m_duplicateSurfaceBtn);
    tableBtnLayout->addStretch();
    tableLayout->addLayout(tableBtnLayout);

    splitter->addWidget(tableWidget);

    // Properties panel
    auto* propsWidget = new QWidget();
    auto* propsLayout = new QGridLayout(propsWidget);

    m_surfaceKeyEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel("Key:"), 0, 0);
    propsLayout->addWidget(m_surfaceKeyEdit, 0, 1);

    m_frictionSpin = new QDoubleSpinBox();
    m_frictionSpin->setRange(0.0, 2.0);
    m_frictionSpin->setSingleStep(0.05);
    propsLayout->addWidget(new QLabel("Friction:"), 1, 0);
    propsLayout->addWidget(m_frictionSpin, 1, 1);

    m_dampingSpin = new QDoubleSpinBox();
    m_dampingSpin->setRange(0.0, 10.0);
    m_dampingSpin->setSingleStep(0.1);
    propsLayout->addWidget(new QLabel("Damping:"), 2, 0);
    propsLayout->addWidget(m_dampingSpin, 2, 1);

    m_soundFileEdit = new QLineEdit();
    propsLayout->addWidget(new QLabel("Sound WAV:"), 3, 0);
    propsLayout->addWidget(m_soundFileEdit, 3, 1);

    m_soundPitchSpin = new QDoubleSpinBox();
    m_soundPitchSpin->setRange(-12.0, 12.0);
    propsLayout->addWidget(new QLabel("Pitch:"), 4, 0);
    propsLayout->addWidget(m_soundPitchSpin, 4, 1);

    m_ffEffectCombo = new QComboBox();
    m_ffEffectCombo->addItems({"NULL", "Rumble", "Vibrate", "Smooth"});
    propsLayout->addWidget(new QLabel("FF Effect:"), 5, 0);
    propsLayout->addWidget(m_ffEffectCombo, 5, 1);

    m_dirtAdditiveSpin = new QDoubleSpinBox();
    m_dirtAdditiveSpin->setRange(0.0, 1.0);
    m_dirtAdditiveSpin->setSingleStep(0.05);
    propsLayout->addWidget(new QLabel("Dirt Additive:"), 6, 0);
    propsLayout->addWidget(m_dirtAdditiveSpin, 6, 1);

    m_isValidTrackCheck = new QCheckBox();
    propsLayout->addWidget(new QLabel("Valid Track:"), 7, 0);
    propsLayout->addWidget(m_isValidTrackCheck, 7, 1);

    m_blackFlagTimeSpin = new QDoubleSpinBox();
    m_blackFlagTimeSpin->setRange(0.0, 60.0);
    propsLayout->addWidget(new QLabel("Black Flag Time:"), 8, 0);
    propsLayout->addWidget(m_blackFlagTimeSpin, 8, 1);

    m_sinHeightSpin = new QDoubleSpinBox();
    m_sinHeightSpin->setRange(0.0, 1.0);
    m_sinHeightSpin->setSingleStep(0.01);
    propsLayout->addWidget(new QLabel("Sine Height:"), 9, 0);
    propsLayout->addWidget(m_sinHeightSpin, 9, 1);

    m_sinLengthSpin = new QDoubleSpinBox();
    m_sinLengthSpin->setRange(0.0, 100.0);
    propsLayout->addWidget(new QLabel("Sine Length:"), 10, 0);
    propsLayout->addWidget(m_sinLengthSpin, 10, 1);

    m_isPitlaneCheck = new QCheckBox();
    propsLayout->addWidget(new QLabel("Pitlane:"), 11, 0);
    propsLayout->addWidget(m_isPitlaneCheck, 11, 1);

    m_vibrationGainSpin = new QDoubleSpinBox();
    m_vibrationGainSpin->setRange(0.0, 1.0);
    m_vibrationGainSpin->setSingleStep(0.05);
    propsLayout->addWidget(new QLabel("Vibration Gain:"), 12, 0);
    propsLayout->addWidget(m_vibrationGainSpin, 12, 1);

    m_vibrationLengthSpin = new QDoubleSpinBox();
    m_vibrationLengthSpin->setRange(0.0, 100.0);
    propsLayout->addWidget(new QLabel("Vibration Length:"), 13, 0);
    propsLayout->addWidget(m_vibrationLengthSpin, 13, 1);

    splitter->addWidget(propsWidget);
    mainLayout->addWidget(splitter);

    // Action buttons
    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton("Load surfaces.ini");
    m_saveBtn = new QPushButton("Save surfaces.ini");
    m_resetBtn = new QPushButton("Reset Defaults");
    actionLayout->addWidget(m_loadBtn);
    actionLayout->addWidget(m_saveBtn);
    actionLayout->addWidget(m_resetBtn);
    mainLayout->addLayout(actionLayout);

    m_statusLabel = new QLabel("Ready");
    mainLayout->addWidget(m_statusLabel);

    // Connections
    connect(m_surfaceTable, &QTableWidget::cellClicked, this, [this](int row, int) { onSurfaceSelected(row); });
    connect(m_addSurfaceBtn, &QPushButton::clicked, this, &TrackSurfaceEditorModule::onAddSurface);
    connect(m_removeSurfaceBtn, &QPushButton::clicked, this, &TrackSurfaceEditorModule::onRemoveSurface);
    connect(m_duplicateSurfaceBtn, &QPushButton::clicked, this, &TrackSurfaceEditorModule::onDuplicateSurface);
    connect(m_surfaceKeyEdit, &QLineEdit::textChanged, this, &TrackSurfaceEditorModule::onSurfaceKeyChanged);
    connect(m_frictionSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onFrictionChanged);
    connect(m_dampingSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onDampingChanged);
    connect(m_soundFileEdit, &QLineEdit::textChanged, this, &TrackSurfaceEditorModule::onSoundFileChanged);
    connect(m_soundPitchSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onSoundPitchChanged);
    connect(m_ffEffectCombo, QOverload<const QString&>::of(&QComboBox::currentTextChanged), this, &TrackSurfaceEditorModule::onFFEffectChanged);
    connect(m_dirtAdditiveSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onDirtAdditiveChanged);
    connect(m_isValidTrackCheck, &QCheckBox::toggled, this, &TrackSurfaceEditorModule::onIsValidTrackToggled);
    connect(m_blackFlagTimeSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onBlackFlagTimeChanged);
    connect(m_sinHeightSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onSinHeightChanged);
    connect(m_sinLengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onSinLengthChanged);
    connect(m_isPitlaneCheck, &QCheckBox::toggled, this, &TrackSurfaceEditorModule::onIsPitlaneToggled);
    connect(m_vibrationGainSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onVibrationGainChanged);
    connect(m_vibrationLengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &TrackSurfaceEditorModule::onVibrationLengthChanged);
    connect(m_loadBtn, &QPushButton::clicked, this, &TrackSurfaceEditorModule::onLoadFile);
    connect(m_saveBtn, &QPushButton::clicked, this, &TrackSurfaceEditorModule::onSaveFile);
    connect(m_resetBtn, &QPushButton::clicked, this, &TrackSurfaceEditorModule::onResetDefaults);

    m_dockWidget->setWidget(centralWidget);
    return m_dockWidget;
}

void TrackSurfaceEditorModule::importFile(const QString& filePath)
{
    m_filePath = filePath;
    loadFileToUI();
}

void TrackSurfaceEditorModule::exportFile(const QString& filePath)
{
    m_filePath = filePath;
    saveFileFromUI();
}

void TrackSurfaceEditorModule::onActivation()
{
    // Signals are already connected in getOrCreateDockWidget(); no need to reconnect
    m_statusLabel->setText("Active");
}

void TrackSurfaceEditorModule::onDeactivation()
{
    // Connections are permanent (set up in getOrCreateDockWidget); no need to disconnect.
    // Slots already guard against invalid state via m_selectedSurfaceIndex checks.
    m_statusLabel->setText("Inactive");
}

void TrackSurfaceEditorModule::onSurfaceSelected(int row)
{
    if (row < 0 || row >= m_surfaces.size()) return;
    m_selectedSurfaceIndex = row;
    selectSurface(row);
}

void TrackSurfaceEditorModule::onAddSurface()
{
    SurfaceData s;
    s.key = "NEW_SURFACE";
    s.friction = 0.8f;
    m_surfaces.append(s);
    updateSurfaceTable();
    m_surfaceTable->selectRow(m_surfaces.size() - 1);
}

void TrackSurfaceEditorModule::onRemoveSurface()
{
    if (m_selectedSurfaceIndex < 0 || m_selectedSurfaceIndex >= m_surfaces.size()) return;
    m_surfaces.removeAt(m_selectedSurfaceIndex);
    updateSurfaceTable();
    m_selectedSurfaceIndex = -1;
}

void TrackSurfaceEditorModule::onDuplicateSurface()
{
    if (m_selectedSurfaceIndex < 0 || m_selectedSurfaceIndex >= m_surfaces.size()) return;
    SurfaceData s = m_surfaces[m_selectedSurfaceIndex];
    s.key += "_COPY";
    m_surfaces.append(s);
    updateSurfaceTable();
}

void TrackSurfaceEditorModule::onFrictionChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].friction = v; }
void TrackSurfaceEditorModule::onDampingChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].damping = v; }
void TrackSurfaceEditorModule::onSoundFileChanged(const QString& t) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].wav = t; }
void TrackSurfaceEditorModule::onSoundPitchChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].wavPitch = v; }
void TrackSurfaceEditorModule::onFFEffectChanged(const QString& t) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].ffEffect = t; }
void TrackSurfaceEditorModule::onDirtAdditiveChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].dirtAdditive = v; }
void TrackSurfaceEditorModule::onIsValidTrackToggled(bool c) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].isValidTrack = c; }
void TrackSurfaceEditorModule::onBlackFlagTimeChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].blackFlagTime = v; }
void TrackSurfaceEditorModule::onSinHeightChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].sinHeight = v; }
void TrackSurfaceEditorModule::onSinLengthChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].sinLength = v; }
void TrackSurfaceEditorModule::onIsPitlaneToggled(bool c) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].isPitlane = c; }
void TrackSurfaceEditorModule::onVibrationGainChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].vibrationGain = v; }
void TrackSurfaceEditorModule::onVibrationLengthChanged(double v) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].vibrationLength = v; }
void TrackSurfaceEditorModule::onSurfaceKeyChanged(const QString& t) { if (m_selectedSurfaceIndex >= 0) m_surfaces[m_selectedSurfaceIndex].key = t; }

void TrackSurfaceEditorModule::onLoadFile()
{
    QString path = QFileDialog::getOpenFileName(this, "Open surfaces.ini", QString(), "Surfaces INI (*.ini);;All Files (*)");
    if (!path.isEmpty()) {
        m_filePath = path;
        loadFileToUI();
        m_statusLabel->setText("Loaded: " + path);
    }
}

void TrackSurfaceEditorModule::onSaveFile()
{
    QString path = m_filePath.isEmpty() ?
        QFileDialog::getSaveFileName(this, "Save surfaces.ini", QString(), "Surfaces INI (*.ini)") : m_filePath;
    if (!path.isEmpty()) {
        m_filePath = path;
        saveFileFromUI();
        m_statusLabel->setText("Saved: " + path);
    }
}

void TrackSurfaceEditorModule::onResetDefaults()
{
    m_surfaces.clear();
    SurfaceData road; road.key = "ROAD"; road.friction = 1.0; m_surfaces.append(road);
    SurfaceData grass; grass.key = "GRASS"; grass.friction = 0.7; m_surfaces.append(grass);
    SurfaceData kerb; kerb.key = "KERB"; kerb.friction = 0.9; kerb.sinHeight = 0.02; m_surfaces.append(kerb);
    SurfaceData sand; sand.key = "SAND"; sand.friction = 0.5; m_surfaces.append(sand);
    updateSurfaceTable();
    m_statusLabel->setText("Reset to defaults");
}

void TrackSurfaceEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText("UI Ready"); }

void TrackSurfaceEditorModule::loadFileToUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) return;
    QTextStream in(&file);
    QString content = file.readAll();
    file.close();

    m_surfaces.clear();
    QStringList sections = content.split("[", Qt::SkipEmptyParts);
    for (const QString& sec : sections) {
        if (!sec.startsWith("SURFACE_")) continue;
        SurfaceData s;
        QStringList lines = sec.split("\n", Qt::SkipEmptyParts);
        for (const QString& line : lines) {
            QString l = line.trimmed();
            if (l.startsWith("KEY=")) s.key = l.mid(4);
            else if (l.startsWith("FRICTION=")) s.friction = l.mid(9).toFloat();
            else if (l.startsWith("DAMPING=")) s.damping = l.mid(8).toFloat();
            else if (l.startsWith("WAV=")) s.wav = l.mid(4);
            else if (l.startsWith("WAV_PITCH=")) s.wavPitch = l.mid(10).toFloat();
            else if (l.startsWith("FF_EFFECT=")) s.ffEffect = l.mid(10);
            else if (l.startsWith("DIRT_ADDITIVE=")) s.dirtAdditive = l.mid(14).toFloat();
            else if (l.startsWith("IS_VALID_TRACK=")) s.isValidTrack = (l.mid(15) == "1");
            else if (l.startsWith("BLACK_FLAG_TIME=")) s.blackFlagTime = l.mid(16).toFloat();
            else if (l.startsWith("SIN_HEIGHT=")) s.sinHeight = l.mid(11).toFloat();
            else if (l.startsWith("SIN_LENGTH=")) s.sinLength = l.mid(11).toFloat();
            else if (l.startsWith("IS_PITLANE=")) s.isPitlane = (l.mid(11) == "1");
            else if (l.startsWith("VIBRATION_GAIN=")) s.vibrationGain = l.mid(16).toFloat();
            else if (l.startsWith("VIBRATION_LENGTH=")) s.vibrationLength = l.mid(17).toFloat();
        }
        m_surfaces.append(s);
    }
    updateSurfaceTable();
}

void TrackSurfaceEditorModule::saveFileFromUI()
{
    QFile file(m_filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream out(&file);
    for (int i = 0; i < m_surfaces.size(); ++i) {
        const auto& s = m_surfaces[i];
        out << "[SURFACE_" << i << "]\n";
        out << "KEY=" << s.key << "\n";
        out << "FRICTION=" << s.friction << "\n";
        out << "DAMPING=" << s.damping << "\n";
        out << "WAV=" << s.wav << "\n";
        out << "WAV_PITCH=" << s.wavPitch << "\n";
        out << "FF_EFFECT=" << s.ffEffect << "\n";
        out << "DIRT_ADDITIVE=" << s.dirtAdditive << "\n";
        out << "IS_VALID_TRACK=" << (s.isValidTrack ? "1" : "0") << "\n";
        out << "BLACK_FLAG_TIME=" << s.blackFlagTime << "\n";
        out << "SIN_HEIGHT=" << s.sinHeight << "\n";
        out << "SIN_LENGTH=" << s.sinLength << "\n";
        out << "IS_PITLANE=" << (s.isPitlane ? "1" : "0") << "\n";
        out << "VIBRATION_GAIN=" << s.vibrationGain << "\n";
        out << "VIBRATION_LENGTH=" << s.vibrationLength << "\n\n";
    }
    file.close();
}

void TrackSurfaceEditorModule::updateSurfaceTable()
{
    m_surfaceTable->setRowCount(m_surfaces.size());
    for (int i = 0; i < m_surfaces.size(); ++i) {
        const auto& s = m_surfaces[i];
        m_surfaceTable->setItem(i, 0, new QTableWidgetItem(s.key));
        m_surfaceTable->setItem(i, 1, new QTableWidgetItem(QString::number(s.friction, 'f', 2)));
        auto* validItem = new QTableWidgetItem(s.isValidTrack ? "Yes" : "No");
        m_surfaceTable->setItem(i, 2, validItem);
        auto* pitItem = new QTableWidgetItem(s.isPitlane ? "Yes" : "No");
        m_surfaceTable->setItem(i, 3, pitItem);
    }
}

void TrackSurfaceEditorModule::selectSurface(int index)
{
    if (index < 0 || index >= m_surfaces.size()) return;
    const auto& s = m_surfaces[index];
    m_surfaceKeyEdit->setText(s.key);
    m_frictionSpin->setValue(s.friction);
    m_dampingSpin->setValue(s.damping);
    m_soundFileEdit->setText(s.wav);
    m_soundPitchSpin->setValue(s.wavPitch);
    m_dirtAdditiveSpin->setValue(s.dirtAdditive);
    m_isValidTrackCheck->setChecked(s.isValidTrack);
    m_blackFlagTimeSpin->setValue(s.blackFlagTime);
    m_sinHeightSpin->setValue(s.sinHeight);
    m_sinLengthSpin->setValue(s.sinLength);
    m_isPitlaneCheck->setChecked(s.isPitlane);
    m_vibrationGainSpin->setValue(s.vibrationGain);
    m_vibrationLengthSpin->setValue(s.vibrationLength);
}

QJsonObject TrackSurfaceEditorModule::serializeProject() const
{
    QJsonObject data;
    data["filePath"] = m_filePath;

    QJsonArray surfacesArray;
    for (const auto& s : m_surfaces) {
        QJsonObject obj;
        obj["key"] = s.key;
        obj["friction"] = static_cast<double>(s.friction);
        obj["damping"] = static_cast<double>(s.damping);
        obj["wav"] = s.wav;
        obj["wavPitch"] = static_cast<double>(s.wavPitch);
        obj["ffEffect"] = s.ffEffect;
        obj["dirtAdditive"] = static_cast<double>(s.dirtAdditive);
        obj["isValidTrack"] = s.isValidTrack;
        obj["blackFlagTime"] = static_cast<double>(s.blackFlagTime);
        obj["sinHeight"] = static_cast<double>(s.sinHeight);
        obj["sinLength"] = static_cast<double>(s.sinLength);
        obj["isPitlane"] = s.isPitlane;
        obj["vibrationGain"] = static_cast<double>(s.vibrationGain);
        obj["vibrationLength"] = static_cast<double>(s.vibrationLength);
        surfacesArray.append(obj);
    }
    data["surfaces"] = surfacesArray;
    return data;
}

void TrackSurfaceEditorModule::deserializeProject(const QJsonObject& data)
{
    m_filePath = data["filePath"].toString();
    m_surfaces.clear();

    for (const auto& v : data["surfaces"].toArray()) {
        QJsonObject obj = v.toObject();
        SurfaceData s;
        s.key = obj["key"].toString();
        s.friction = static_cast<float>(obj["friction"].toDouble(0.8));
        s.damping = static_cast<float>(obj["damping"].toDouble(0.0));
        s.wav = obj["wav"].toString();
        s.wavPitch = static_cast<float>(obj["wavPitch"].toDouble(0.0));
        s.ffEffect = obj["ffEffect"].toString();
        s.dirtAdditive = static_cast<float>(obj["dirtAdditive"].toDouble(0.0));
        s.isValidTrack = obj["isValidTrack"].toBool(true);
        s.blackFlagTime = static_cast<float>(obj["blackFlagTime"].toDouble(0.0));
        s.sinHeight = static_cast<float>(obj["sinHeight"].toDouble(0.0));
        s.sinLength = static_cast<float>(obj["sinLength"].toDouble(0.0));
        s.isPitlane = obj["isPitlane"].toBool(false);
        s.vibrationGain = static_cast<float>(obj["vibrationGain"].toDouble(0.0));
        s.vibrationLength = static_cast<float>(obj["vibrationLength"].toDouble(0.0));
        m_surfaces.append(s);
    }

    updateSurfaceTable();
    if (!m_surfaces.isEmpty()) {
        selectSurface(0);
    }
}

} // namespace ks
