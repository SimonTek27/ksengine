#include "AudioEditorModule.h"
#include "sdk/kseditor/engine/Audio/WaveformEngine.h"
#include "sdk/kseditor/engine/Audio/WaveProcessor.h"
#include "sdk/kseditor/engine/Audio/AudioRecording.h"
#include "sdk/kseditor/engine/Audio/TextToSpeech.h"
#include <QTcpSocket>
#include <QAudioFormat>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMenu>
#include <QAction>
#include <QApplication>
#include <QTimer>

namespace ks {
namespace audio {

AudioEditorModule::AudioEditorModule(QWidget* parent)
    : ModuleGuiBase(parent)
    , m_tabWidget(nullptr)
    , m_engineSoundTab(nullptr)
    , m_trackTree(nullptr)
    , m_addTrackBtn(nullptr)
    , m_removeTrackBtn(nullptr)
    , m_playBtn(nullptr)
    , m_stopBtn(nullptr)
    , m_volumeSlider(nullptr)
    , m_volumeLabel(nullptr)
    , m_loadAudioBtn(nullptr)
    , m_audioInfoLabel(nullptr)
    , m_mixerList(nullptr)
    , m_masterVolumeSlider(nullptr)
    , m_masterVolumeLabel(nullptr)
    , m_waveformLabel(nullptr)
    , m_recordingTab(nullptr)
    , m_recordBtn(nullptr)
    , m_recordingStatusLabel(nullptr)
    , m_inputDeviceCombo(nullptr)
    , m_sampleRateCombo(nullptr)
    , m_recordingDurationSpin(nullptr)
    , m_inputLevelSlider(nullptr)
    , m_inputLevelLabel(nullptr)
    , m_monitorInputCheck(nullptr)
    , m_recordingInfoLabel(nullptr)
    , m_effectsTab(nullptr)
    , m_effectChainTree(nullptr)
    , m_addEffectBtn(nullptr)
    , m_removeEffectBtn(nullptr)
    , m_effectTypeCombo(nullptr)
    , m_effectParamGroup(nullptr)
    , m_effectParamLayout(nullptr)
    , m_effectInfoLabel(nullptr)
    , m_soundBanksTab(nullptr)
    , m_bankTree(nullptr)
    , m_addBankBtn(nullptr)
    , m_removeBankBtn(nullptr)
    , m_exportBankBtn(nullptr)
    , m_importBankBtn(nullptr)
    , m_sampleList(nullptr)
    , m_addSampleBtn(nullptr)
    , m_removeSampleBtn(nullptr)
    , m_bankInfoLabel(nullptr)
    , m_settingsTab(nullptr)
    , m_outputDeviceCombo(nullptr)
    , m_settingsSampleRateCombo(nullptr)
    , m_bufferSizeCombo(nullptr)
    , m_exclusiveModeCheck(nullptr)
    , m_autoConnectCheck(nullptr)
    , m_latencySpin(nullptr)
    , m_connectToSimBtn(nullptr)
    , m_disconnectFromSimBtn(nullptr)
    , m_connectionStatusLabel(nullptr)
    , m_waveProcessor(nullptr)
    , m_audioRecorder(nullptr)
    , m_tcpSocket(nullptr)
    , m_ttsTab(nullptr)
    , m_ttsInputEdit(nullptr)
    , m_ttsSpeakBtn(nullptr)
    , m_ttsStopBtn(nullptr)
    , m_ttsClearBtn(nullptr)
    , m_ttsSaveBtn(nullptr)
    , m_ttsVoiceCombo(nullptr)
    , m_ttsRateSlider(nullptr)
    , m_ttsRateLabel(nullptr)
    , m_ttsVolumeSlider(nullptr)
    , m_ttsVolumeLabel(nullptr)
    , m_tts(nullptr)
{
    setObjectName("AudioEditorModule");
    m_waveProcessor = new WaveProcessor(this);
    m_audioRecorder = new AudioRecorder(this);
    m_tcpSocket = new QTcpSocket(this);
    m_tts = new TextToSpeech(this);
}

AudioEditorModule::~AudioEditorModule() {}

bool AudioEditorModule::initialize() {
    if (m_uiBuilt) return true;
    ModuleGuiBase::initialize();
    return true;
}

void AudioEditorModule::shutdown() {
    m_uiBuilt = false;
}

void AudioEditorModule::importFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    if (suffix == "bank" || suffix == "zip") {
        log(QString("Importing sound bank: %1").arg(filePath));
        refreshBanks();
    } else if (suffix == "wav" || suffix == "mp3" || suffix == "ogg" || suffix == "flac" || suffix == "aiff" || suffix == "m4a") {
        m_audioInfoLabel->setText(QString("Loaded: %1").arg(filePath));
        log(QString("Loaded audio file: %1").arg(filePath));
    } else {
        logError(QString("Unsupported audio format: %1").arg(suffix));
    }
}

void AudioEditorModule::exportFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    if (suffix == "bank") {
        log(QString("Exporting sound bank to: %1").arg(filePath));
    } else {
        logError(QString("Unsupported export format: %1").arg(suffix));
    }
}

void AudioEditorModule::onActivation() {}
void AudioEditorModule::onDeactivation() {}

void AudioEditorModule::buildUI() {
    m_tabWidget = new QTabWidget();

    setupEngineSoundTab();
    setupRecordingTab();
    setupEffectsTab();
    setupSoundBanksTab();
    setupSettingsTab();
    setupTtsTab();

    m_mainLayout->addWidget(m_tabWidget);
    m_mainLayout->addWidget(m_logOutput);
}

void AudioEditorModule::setupEngineSoundTab() {
    m_engineSoundTab = new QWidget();
    auto* layout = new QVBoxLayout(m_engineSoundTab);

    auto* toolbar = new QHBoxLayout();
    m_loadAudioBtn = createButton("Load Audio File");
    m_playBtn = createButton("Play");
    m_stopBtn = createButton("Stop");
    m_addTrackBtn = createButton("Add Track");
    m_removeTrackBtn = createButton("Remove Track");
    toolbar->addWidget(m_loadAudioBtn);
    toolbar->addWidget(m_playBtn);
    toolbar->addWidget(m_stopBtn);
    toolbar->addSpacing(10);
    toolbar->addWidget(m_addTrackBtn);
    toolbar->addWidget(m_removeTrackBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* splitter = createSplitter(Qt::Horizontal);

    auto* trackPanel = new QWidget();
    auto* trackLayout = new QVBoxLayout(trackPanel);
    m_trackTree = createTreeWidget({"Track", "Sample", "Volume", "Pan", "Mute"});
    m_trackTree->setHeaderLabels({"Track", "Sample", "Volume", "Pan", "Mute"});
    trackLayout->addWidget(m_trackTree);
    splitter->addWidget(trackPanel);

    auto* mixerPanel = new QWidget();
    auto* mixerLayout = new QVBoxLayout(mixerPanel);
    mixerLayout->addWidget(createLabel("Mixer:"));

    auto* masterGroup = new QGroupBox("Master");
    auto* masterLayout = new QVBoxLayout(masterGroup);
    m_masterVolumeSlider = new QSlider(Qt::Horizontal);
    m_masterVolumeSlider->setRange(0, 100);
    m_masterVolumeSlider->setValue(80);
    m_masterVolumeLabel = createLabel("80%");
    masterLayout->addWidget(m_masterVolumeSlider);
    masterLayout->addWidget(m_masterVolumeLabel);
    mixerLayout->addWidget(masterGroup);

    m_mixerList = new QListWidget();
    m_mixerList->setAlternatingRowColors(true);
    mixerLayout->addWidget(createLabel("Channel Levels:"));
    mixerLayout->addWidget(m_mixerList);
    splitter->addWidget(mixerPanel);

    layout->addWidget(splitter);

    m_audioInfoLabel = createLabel("No audio loaded");
    layout->addWidget(m_audioInfoLabel);

    connect(m_loadAudioBtn, &QPushButton::clicked, this, &AudioEditorModule::onLoadAudioFile);
    connect(m_playBtn, &QPushButton::clicked, this, &AudioEditorModule::onPlayAudio);
    connect(m_stopBtn, &QPushButton::clicked, this, &AudioEditorModule::onStopAudio);
    connect(m_addTrackBtn, &QPushButton::clicked, this, &AudioEditorModule::onAddTrack);
    connect(m_removeTrackBtn, &QPushButton::clicked, this, &AudioEditorModule::onRemoveTrack);
    connect(m_masterVolumeSlider, &QSlider::valueChanged, this, &AudioEditorModule::onMasterVolumeChanged);

    m_tabWidget->addTab(m_engineSoundTab, "Engine Sound");
}

void AudioEditorModule::setupRecordingTab() {
    m_recordingTab = new QWidget();
    auto* layout = new QVBoxLayout(m_recordingTab);

    auto* deviceGroup = new QGroupBox("Recording Device");
    auto* deviceLayout = new QFormLayout(deviceGroup);

    m_inputDeviceCombo = createComboBox({"Default Input Device", "Microphone (Realtek)", "Line In (Realtek)", "Virtual Audio Cable"});
    m_sampleRateCombo = createComboBox({"44100 Hz", "48000 Hz", "96000 Hz", "192000 Hz"});
    m_recordingDurationSpin = createSpinBox(1, 3600, 30, " sec");

    deviceLayout->addRow("Input Device:", m_inputDeviceCombo);
    deviceLayout->addRow("Sample Rate:", m_sampleRateCombo);
    deviceLayout->addRow("Max Duration:", m_recordingDurationSpin);
    layout->addWidget(deviceGroup);

    auto* levelGroup = new QGroupBox("Input Level");
    auto* levelLayout = new QVBoxLayout(levelGroup);
    m_inputLevelSlider = new QSlider(Qt::Horizontal);
    m_inputLevelSlider->setRange(0, 100);
    m_inputLevelSlider->setValue(75);
    m_inputLevelLabel = createLabel("75%");
    m_monitorInputCheck = createCheckBox("Monitor Input", false);
    levelLayout->addWidget(m_inputLevelSlider);
    levelLayout->addWidget(m_inputLevelLabel);
    levelLayout->addWidget(m_monitorInputCheck);
    layout->addWidget(levelGroup);

    auto* controlLayout = new QHBoxLayout();
    m_recordBtn = createButton("Start Recording");
    m_recordingStatusLabel = createLabel("Ready");
    controlLayout->addWidget(m_recordBtn);
    controlLayout->addWidget(m_recordingStatusLabel);
    controlLayout->addStretch();
    layout->addLayout(controlLayout);

    m_recordingInfoLabel = createLabel("Configure recording settings and press Start");
    layout->addWidget(m_recordingInfoLabel);
    layout->addStretch();

    connect(m_recordBtn, &QPushButton::clicked, this, &AudioEditorModule::onRecordAudio);
    connect(m_inputLevelSlider, &QSlider::valueChanged, this, &AudioEditorModule::onVolumeChanged);

    m_tabWidget->addTab(m_recordingTab, "Recording");
}

void AudioEditorModule::setupEffectsTab() {
    m_effectsTab = new QWidget();
    auto* layout = new QVBoxLayout(m_effectsTab);

    auto* toolbar = new QHBoxLayout();
    m_effectTypeCombo = createComboBox({"Reverb", "Delay", "Chorus", "Flanger", "Phaser", "Distortion", "Compressor", "EQ (Parametric)", "EQ (Graphic)", "Limiter", "Noise Gate", "Pitch Shift", "Wah-Wah", "Vibrato", "Tremolo"});
    m_addEffectBtn = createButton("Add Effect");
    m_removeEffectBtn = createButton("Remove Effect");
    toolbar->addWidget(m_effectTypeCombo);
    toolbar->addWidget(m_addEffectBtn);
    toolbar->addWidget(m_removeEffectBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* splitter = createSplitter(Qt::Horizontal);

    m_effectChainTree = createTreeWidget({"Effect", "Type", "Bypass"});
    m_effectChainTree->setHeaderLabels({"Effect", "Type", "Bypass"});
    splitter->addWidget(m_effectChainTree);

    m_effectParamGroup = new QGroupBox("Effect Parameters");
    m_effectParamLayout = new QFormLayout(m_effectParamGroup);
    m_effectParamLayout->addRow("No effect selected", new QLabel());
    splitter->addWidget(m_effectParamGroup);

    layout->addWidget(splitter);

    m_effectInfoLabel = createLabel("Add effects to the chain to process audio");
    layout->addWidget(m_effectInfoLabel);

    connect(m_addEffectBtn, &QPushButton::clicked, this, &AudioEditorModule::onAddEffect);
    connect(m_removeEffectBtn, &QPushButton::clicked, this, &AudioEditorModule::onRemoveEffect);
    connect(m_effectChainTree, &QTreeWidget::itemClicked, this, &AudioEditorModule::onEffectSelected);

    m_tabWidget->addTab(m_effectsTab, "Effects");
}

void AudioEditorModule::setupSoundBanksTab() {
    m_soundBanksTab = new QWidget();
    auto* layout = new QVBoxLayout(m_soundBanksTab);

    auto* toolbar = new QHBoxLayout();
    m_addBankBtn = createButton("New Bank");
    m_removeBankBtn = createButton("Remove Bank");
    m_exportBankBtn = createButton("Export Bank");
    m_importBankBtn = createButton("Import Bank");
    toolbar->addWidget(m_addBankBtn);
    toolbar->addWidget(m_removeBankBtn);
    toolbar->addWidget(m_exportBankBtn);
    toolbar->addWidget(m_importBankBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* splitter = createSplitter(Qt::Horizontal);

    m_bankTree = createTreeWidget({"Bank", "Samples", "Size", "Format"});
    m_bankTree->setHeaderLabels({"Bank", "Samples", "Size", "Format"});
    splitter->addWidget(m_bankTree);

    auto* samplePanel = new QWidget();
    auto* sampleLayout = new QVBoxLayout(samplePanel);
    sampleLayout->addWidget(createLabel("Samples:"));

    m_sampleList = new QListWidget();
    m_sampleList->setAlternatingRowColors(true);
    sampleLayout->addWidget(m_sampleList);

    auto* sampleToolbar = new QHBoxLayout();
    m_addSampleBtn = createButton("Add Sample");
    m_removeSampleBtn = createButton("Remove Sample");
    sampleToolbar->addWidget(m_addSampleBtn);
    sampleToolbar->addWidget(m_removeSampleBtn);
    sampleToolbar->addStretch();
    sampleLayout->addLayout(sampleToolbar);

    splitter->addWidget(samplePanel);
    layout->addWidget(splitter);

    m_bankInfoLabel = createLabel("Create or import sound banks");
    layout->addWidget(m_bankInfoLabel);

    connect(m_addBankBtn, &QPushButton::clicked, this, &AudioEditorModule::onAddSoundBank);
    connect(m_removeBankBtn, &QPushButton::clicked, this, &AudioEditorModule::onRemoveSoundBank);
    connect(m_exportBankBtn, &QPushButton::clicked, this, &AudioEditorModule::onExportSoundBank);
    connect(m_importBankBtn, &QPushButton::clicked, this, &AudioEditorModule::onImportSoundBank);
    connect(m_bankTree, &QTreeWidget::itemClicked, this, &AudioEditorModule::onBankSelected);
    connect(m_addSampleBtn, &QPushButton::clicked, this, &AudioEditorModule::onAddSample);
    connect(m_removeSampleBtn, &QPushButton::clicked, this, &AudioEditorModule::onRemoveSample);

    m_tabWidget->addTab(m_soundBanksTab, "Sound Banks");
}

void AudioEditorModule::setupSettingsTab() {
    m_settingsTab = new QWidget();
    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    auto* container = new QWidget();
    auto* layout = new QFormLayout(container);

    m_outputDeviceCombo = createComboBox({"Default Output", "Speakers (Realtek)", "Headphones (Realtek)", "Virtual Audio Cable"});
    m_settingsSampleRateCombo = createComboBox({"44100 Hz", "48000 Hz", "96000 Hz", "192000 Hz"});
    m_bufferSizeCombo = createComboBox({"64 samples", "128 samples", "256 samples", "512 samples", "1024 samples", "2048 samples"});
    m_latencySpin = createSpinBox(1, 500, 50, " ms");
    m_exclusiveModeCheck = createCheckBox("Exclusive Mode", false);
    m_autoConnectCheck = createCheckBox("Auto-connect to simulator", true);

    layout->addRow("Output Device:", m_outputDeviceCombo);
    layout->addRow("Sample Rate:", m_settingsSampleRateCombo);
    layout->addRow("Buffer Size:", m_bufferSizeCombo);
    layout->addRow("Latency:", m_latencySpin);
    layout->addRow("", m_exclusiveModeCheck);
    layout->addRow("", m_autoConnectCheck);

    auto* connectGroup = new QGroupBox("Simulator Connection");
    auto* connectLayout = new QVBoxLayout(connectGroup);
    auto* btnLayout = new QHBoxLayout();
    m_connectToSimBtn = createButton("Connect to Sim");
    m_disconnectFromSimBtn = createButton("Disconnect");
    m_connectionStatusLabel = createLabel("Disconnected");
    btnLayout->addWidget(m_connectToSimBtn);
    btnLayout->addWidget(m_disconnectFromSimBtn);
    connectLayout->addLayout(btnLayout);
    connectLayout->addWidget(m_connectionStatusLabel);
    layout->addRow(connectGroup);

    scrollArea->setWidget(container);
    auto* mainLayout = new QVBoxLayout(m_settingsTab);
    mainLayout->addWidget(scrollArea);

    connect(m_outputDeviceCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AudioEditorModule::onOutputDeviceChanged);
    connect(m_settingsSampleRateCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AudioEditorModule::onSampleRateChanged);
    connect(m_bufferSizeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &AudioEditorModule::onBufferSizeChanged);
    connect(m_connectToSimBtn, &QPushButton::clicked, this, &AudioEditorModule::onConnectToSim);
    connect(m_disconnectFromSimBtn, &QPushButton::clicked, this, &AudioEditorModule::onDisconnectFromSim);

    m_tabWidget->addTab(m_settingsTab, "Settings");
}

void AudioEditorModule::refreshEffects() {
    m_effectChainTree->clear();
    m_effectChainTree->addTopLevelItem(new QTreeWidgetItem({"Reverb", "Reverb", "No"}));
    m_effectChainTree->addTopLevelItem(new QTreeWidgetItem({"EQ", "EQ (Parametric)", "No"}));
    m_effectChainTree->addTopLevelItem(new QTreeWidgetItem({"Compressor", "Compressor", "Yes"}));
}

void AudioEditorModule::refreshBanks() {
    m_bankTree->clear();
    auto* bank = new QTreeWidgetItem(m_bankTree, {"Engine Sounds", "4", "2.4 MB", "WAV"});
    bank->addChild(new QTreeWidgetItem({"", "engine_start.wav", "340 KB", "WAV"}));
    bank->addChild(new QTreeWidgetItem({"", "engine_idle.wav", "820 KB", "WAV"}));
    bank->addChild(new QTreeWidgetItem({"", "engine_accel.wav", "1.1 MB", "WAV"}));
    bank->addChild(new QTreeWidgetItem({"", "engine_decel.wav", "180 KB", "WAV"}));
    m_bankTree->addTopLevelItem(new QTreeWidgetItem({"Interior Sounds", "3", "1.8 MB", "WAV"}));
    m_bankTree->addTopLevelItem(new QTreeWidgetItem({"Exterior Sounds", "5", "3.2 MB", "WAV"}));
    m_bankTree->expandAll();
}

void AudioEditorModule::refreshSamples() {
    m_sampleList->clear();
    m_sampleList->addItem("engine_start.wav");
    m_sampleList->addItem("engine_idle.wav");
    m_sampleList->addItem("engine_accel.wav");
    m_sampleList->addItem("engine_decel.wav");
}

void AudioEditorModule::onLoadAudioFile() {
    QString path = selectFile("Load Audio", "Audio Files (*.wav *.mp3 *.ogg *.flac *.aiff *.m4a);;All Files (*)");
    if (path.isEmpty()) return;
    m_loadedAudioPath = path;
    if (m_waveProcessor->load(path)) {
        m_audioInfoLabel->setText(QString("Loaded: %1 (%2 ch, %3 Hz, %4 ms)")
            .arg(QFileInfo(path).fileName())
            .arg(m_waveProcessor->getChannelCount())
            .arg(m_waveProcessor->getSampleRate())
            .arg(m_waveProcessor->getDurationMs()));
        logSuccess(QString("Loaded audio: %1").arg(path));
    } else {
        logError(QString("Failed to load audio: %1").arg(path));
    }
}

void AudioEditorModule::onPlayAudio() {
    if (m_waveProcessor->getSampleCount() == 0) {
        logWarning("No audio loaded. Load a file first.");
        return;
    }
    auto* engine = WaveformEngine::instance();
    engine->setSamples(m_waveProcessor->getSamples(),
                       m_waveProcessor->getChannelCount(),
                       m_waveProcessor->getSampleRate());
    engine->play();
    log("Playback started");
}

void AudioEditorModule::onStopAudio() {
    WaveformEngine::instance()->stop();
    log("Playback stopped");
}

void AudioEditorModule::onRecordAudio() {
    if (m_audioRecorder->state() == AudioRecorder::Recording) {
        m_audioRecorder->stop();
        m_recordBtn->setText("Start Recording");
        m_recordingStatusLabel->setText("Recording saved");
        logSuccess("Recording completed");
    } else {
        QAudioFormat fmt;
        fmt.setSampleRate(44100);
        fmt.setChannelCount(2);
        fmt.setSampleFormat(QAudioFormat::Float);
        m_audioRecorder->setFormat(fmt);
        m_audioRecorder->start();
        m_recordBtn->setText("Stop Recording");
        m_recordingStatusLabel->setText("Recording...");
        log("Recording started");
    }
}

void AudioEditorModule::onVolumeChanged(int value) {
    m_inputLevelLabel->setText(QString("%1%").arg(value));
    m_waveProcessor->amplify(value / 100.0f);
}

void AudioEditorModule::onAddEffect() {
    QString name = QString("%1 %2").arg(m_effectTypeCombo->currentText()).arg(m_effectChainTree->topLevelItemCount() + 1);
    auto* item = new QTreeWidgetItem({name, m_effectTypeCombo->currentText(), "No"});
    m_effectChainTree->addTopLevelItem(item);
    log(QString("Added effect: %1").arg(name));
}

void AudioEditorModule::onRemoveEffect() {
    auto* item = m_effectChainTree->currentItem();
    if (item) {
        log(QString("Removed effect: %1").arg(item->text(0)));
        delete item;
    }
}

void AudioEditorModule::onEffectSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item) {
        m_effectInfoLabel->setText(QString("Selected: %1 (%2)").arg(item->text(0), item->text(1)));
    }
}

void AudioEditorModule::onBypassEffect(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (!item) return;
    QString current = item->text(2);
    item->setText(2, current == "Yes" ? "No" : "Yes");
    log(QString("Effect %1 bypass: %2").arg(item->text(0), item->text(2)));
}

void AudioEditorModule::onEffectParamChanged() {
    auto* item = m_effectChainTree->currentItem();
    if (item) {
        log(QString("Updated parameters for: %1").arg(item->text(0)));
    }
}

void AudioEditorModule::onConnectToSim() {
    m_tcpSocket->connectToHost(QHostAddress::LocalHost, 42420);
    if (m_tcpSocket->waitForConnected(3000)) {
        m_connectionStatusLabel->setText("Connected to simulator");
        m_connectionStatusLabel->setStyleSheet("QLabel { color: #4caf50; }");
        logSuccess("Connected to simulator on port 42420");
    } else {
        m_connectionStatusLabel->setText("Connection failed");
        m_connectionStatusLabel->setStyleSheet("QLabel { color: #f44336; }");
        logError(QString("Connection failed: %1").arg(m_tcpSocket->errorString()));
    }
}

void AudioEditorModule::onDisconnectFromSim() {
    m_tcpSocket->disconnectFromHost();
    m_connectionStatusLabel->setText("Disconnected");
    m_connectionStatusLabel->setStyleSheet("QLabel { color: #f44336; }");
    log("Disconnected from simulator");
}

void AudioEditorModule::onAddSoundBank() {
    bool ok;
    QString name = QInputDialog::getText(this, "New Sound Bank", "Bank name:", QLineEdit::Normal, "NewBank", &ok);
    if (ok && !name.isEmpty()) {
        m_bankTree->addTopLevelItem(new QTreeWidgetItem({name, "0", "0 B", "WAV"}));
        log(QString("Created sound bank: %1").arg(name));
    }
}

void AudioEditorModule::onRemoveSoundBank() {
    auto* item = m_bankTree->currentItem();
    if (item) {
        log(QString("Removed bank: %1").arg(item->text(0)));
        delete item;
    }
}

void AudioEditorModule::onExportSoundBank() {
    QString path = selectDirectory("Export Sound Bank");
    if (!path.isEmpty()) {
        log(QString("Exporting sound bank to: %1").arg(path));
    }
}

void AudioEditorModule::onImportSoundBank() {
    QString path = selectFile("Import Sound Bank", "Sound Bank Files (*.bank *.zip);;All Files (*)");
    if (!path.isEmpty()) {
        log(QString("Importing sound bank: %1").arg(path));
        refreshBanks();
    }
}

void AudioEditorModule::onBankSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item) {
        m_bankInfoLabel->setText(QString("Bank: %1").arg(item->text(0)));
        refreshSamples();
    }
}

void AudioEditorModule::onAddSample() {
    QString path = selectFile("Add Sample", "Audio Files (*.wav *.mp3 *.ogg *.flac);;All Files (*)");
    if (!path.isEmpty()) {
        QFileInfo fi(path);
        m_sampleList->addItem(fi.fileName());
        m_waveProcessor->load(path);
        log(QString("Added sample: %1 (%2 samples)")
            .arg(fi.fileName())
            .arg(m_waveProcessor->getSampleCount()));
    }
}

void AudioEditorModule::onRemoveSample() {
    auto* item = m_sampleList->currentItem();
    if (item) {
        log(QString("Removed sample: %1").arg(item->text()));
        delete item;
    }
}

void AudioEditorModule::onAddTrack() {
    int count = m_trackTree->topLevelItemCount() + 1;
    auto* item = new QTreeWidgetItem({QString("Track %1").arg(count), "None", "100%", "Center", "No"});
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_trackTree->addTopLevelItem(item);
    m_mixerList->addItem(QString("Track %1").arg(count));
    log(QString("Added audio track %1").arg(count));
}

void AudioEditorModule::onRemoveTrack() {
    auto* item = m_trackTree->currentItem();
    if (item) {
        log(QString("Removed track: %1").arg(item->text(0)));
        delete item;
    }
}

void AudioEditorModule::onMixerLevelChanged(int channel, int value) {
    Q_UNUSED(channel);
    Q_UNUSED(value);
}

void AudioEditorModule::onMasterVolumeChanged(int value) {
    m_masterVolumeLabel->setText(QString("%1%").arg(value));
    WaveformEngine* engine = WaveformEngine::instance();
    if (engine->isPlaying()) {
        QAudioSink* sink = engine->findChild<QAudioSink*>();
        if (sink) sink->setVolume(value / 100.0f);
    }
}

void AudioEditorModule::onOutputDeviceChanged(int index) {
    log(QString("Output device: %1").arg(m_outputDeviceCombo->currentText()));
}

void AudioEditorModule::onSampleRateChanged(int index) {
    log(QString("Sample rate: %1").arg(m_settingsSampleRateCombo->currentText()));
}

void AudioEditorModule::onBufferSizeChanged(int index) {
    log(QString("Buffer size: %1").arg(m_bufferSizeCombo->currentText()));
}

// â”€â”€ Text-to-Speech Tab â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void AudioEditorModule::setupTtsTab() {
    m_ttsTab = new QWidget();
    auto* layout = new QVBoxLayout(m_ttsTab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(8);

    // Voice selection
    auto* voiceGroup = new QGroupBox("Voice");
    auto* voiceLayout = new QHBoxLayout(voiceGroup);
    voiceLayout->addWidget(createLabel("Voice:"));
    m_ttsVoiceCombo = createComboBox(m_tts->availableVoices());
    voiceLayout->addWidget(m_ttsVoiceCombo);
    voiceLayout->addStretch();
    layout->addWidget(voiceGroup);

    // Text input
    auto* inputGroup = new QGroupBox("Text");
    auto* inputLayout = new QVBoxLayout(inputGroup);
    m_ttsInputEdit = new QTextEdit();
    m_ttsInputEdit->setPlaceholderText("Enter text to speak...");
    m_ttsInputEdit->setMaximumHeight(120);
    inputLayout->addWidget(m_ttsInputEdit);
    layout->addWidget(inputGroup);

    // Controls
    auto* controlLayout = new QHBoxLayout();
    m_ttsSpeakBtn = createButton("Speak", "success");
    m_ttsStopBtn = createButton("Stop", "danger");
    m_ttsClearBtn = createButton("Clear");
    m_ttsSaveBtn = createButton("Save to WAV");
    controlLayout->addWidget(m_ttsSpeakBtn);
    controlLayout->addWidget(m_ttsStopBtn);
    controlLayout->addWidget(m_ttsClearBtn);
    controlLayout->addWidget(m_ttsSaveBtn);
    controlLayout->addStretch();
    layout->addLayout(controlLayout);

    // Rate slider
    auto* rateGroup = new QGroupBox("Rate");
    auto* rateLayout = new QHBoxLayout(rateGroup);
    m_ttsRateSlider = new QSlider(Qt::Horizontal);
    m_ttsRateSlider->setRange(-10, 10);
    m_ttsRateSlider->setValue(0);
    m_ttsRateLabel = createLabel("0");
    m_ttsRateLabel->setMinimumWidth(30);
    rateLayout->addWidget(createLabel("Slow"));
    rateLayout->addWidget(m_ttsRateSlider);
    rateLayout->addWidget(createLabel("Fast"));
    rateLayout->addWidget(m_ttsRateLabel);
    layout->addWidget(rateGroup);

    // Volume slider
    auto* volumeGroup = new QGroupBox("Volume");
    auto* volumeLayout = new QHBoxLayout(volumeGroup);
    m_ttsVolumeSlider = new QSlider(Qt::Horizontal);
    m_ttsVolumeSlider->setRange(0, 100);
    m_ttsVolumeSlider->setValue(100);
    m_ttsVolumeLabel = createLabel("100%");
    m_ttsVolumeLabel->setMinimumWidth(40);
    volumeLayout->addWidget(createLabel("Min"));
    volumeLayout->addWidget(m_ttsVolumeSlider);
    volumeLayout->addWidget(createLabel("Max"));
    volumeLayout->addWidget(m_ttsVolumeLabel);
    layout->addWidget(volumeGroup);

    layout->addStretch();

    // Connections
    connect(m_ttsSpeakBtn, &QPushButton::clicked, this, &AudioEditorModule::onTtsSpeak);
    connect(m_ttsStopBtn, &QPushButton::clicked, this, &AudioEditorModule::onTtsStop);
    connect(m_ttsClearBtn, &QPushButton::clicked, this, &AudioEditorModule::onTtsClear);
    connect(m_ttsSaveBtn, &QPushButton::clicked, this, &AudioEditorModule::onTtsSaveToWav);
    connect(m_ttsVoiceCombo, &QComboBox::currentTextChanged, this, [this](const QString& name) {
        m_tts->setVoice(name);
    });
    connect(m_ttsRateSlider, &QSlider::valueChanged, this, [this](int value) {
        m_tts->setRate(value);
        m_ttsRateLabel->setText(QString::number(value));
    });
    connect(m_ttsVolumeSlider, &QSlider::valueChanged, this, [this](int value) {
        m_tts->setVolume(value);
        m_ttsVolumeLabel->setText(QString("%1%").arg(value));
    });
    connect(m_tts, &TextToSpeech::started, this, [this]() {
        log("Speech started");
    });
    connect(m_tts, &TextToSpeech::finished, this, [this]() {
        log("Speech finished");
    });

    m_tabWidget->addTab(m_ttsTab, "Text to Speech");
}

void AudioEditorModule::onTtsSpeak() {
    QString text = m_ttsInputEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        logWarning("No text to speak");
        return;
    }
    m_tts->speak(text);
    log(QString("Speaking: %1").arg(text.left(50)));
}

void AudioEditorModule::onTtsStop() {
    m_tts->stop();
    log("Speech stopped");
}

void AudioEditorModule::onTtsClear() {
    m_ttsInputEdit->clear();
}

void AudioEditorModule::onTtsSaveToWav() {
    QString text = m_ttsInputEdit->toPlainText().trimmed();
    if (text.isEmpty()) {
        logWarning("No text to save");
        return;
    }
    QString filePath = selectFile("Save TTS as WAV", "WAV Audio (*.wav)", QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
    if (filePath.isEmpty()) return;
    if (m_tts->saveToWav(text, filePath)) {
        logSuccess(QString("Saved TTS audio to: %1").arg(filePath));
    } else {
        logError("Failed to save TTS audio");
    }
}

} // namespace audio
} // namespace ks

