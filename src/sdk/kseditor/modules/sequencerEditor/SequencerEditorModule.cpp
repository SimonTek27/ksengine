// ============================================================================
// SequencerEditorModule.cpp
// Cinematic Sequencer implementation.
// ============================================================================

#include "SequencerEditorModule.h"
#include "sdk/kseditor/engine/Video/VideoEncoder.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSplitter>
#include <QToolBar>
#include <QToolButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QTreeWidget>
#include <QTreeWidgetItem>
#include <QPushButton>
#include <QFileDialog>
#include <QMessageBox>
#include <QJsonDocument>
#include <QFile>
#include <QDir>
#include <QDockWidget>
#include <QMainWindow>
#include <QDebug>
#include <QPainter>
#include <QScrollArea>
#include <QMouseEvent>
#include <QKeyEvent>
#include <algorithm>

namespace ks {

// ============================================================================
// SequencerTimeline serialization
// ============================================================================
QJsonObject SequencerTimeline::toJson() const
{
    QJsonObject json;
    json["name"] = name;
    json["duration"] = static_cast<double>(duration);
    json["fps"] = static_cast<double>(fps);
    json["loop"] = loop;

    QJsonArray tracksArr;
    for (const auto& track : tracks) {
        QJsonObject t;
        t["id"] = track.id.toString();
        t["name"] = track.name;
        t["type"] = static_cast<int>(track.type);
        t["muted"] = track.muted;
        t["solo"] = track.solo;
        t["locked"] = track.locked;
        t["color"] = track.color.name();
        tracksArr.append(t);
    }
    json["tracks"] = tracksArr;

    // Camera cuts
    QJsonArray cutsArr;
    for (const auto& cut : cameraCuts) {
        QJsonObject c;
        c["time"] = static_cast<double>(cut.time);
        c["duration"] = static_cast<double>(cut.duration);
        c["cameraIndex"] = cut.cameraIndex;
        c["posX"] = static_cast<double>(cut.position.x());
        c["posY"] = static_cast<double>(cut.position.y());
        c["posZ"] = static_cast<double>(cut.position.z());
        c["targetX"] = static_cast<double>(cut.target.x());
        c["targetY"] = static_cast<double>(cut.target.y());
        c["targetZ"] = static_cast<double>(cut.target.z());
        c["fov"] = static_cast<double>(cut.fov);
        c["roll"] = static_cast<double>(cut.roll);
        c["easing"] = static_cast<int>(cut.easing);
        c["transitionDuration"] = static_cast<double>(cut.transitionDuration);
        cutsArr.append(c);
    }
    json["cameraCuts"] = cutsArr;

    // Fade cues
    QJsonArray fadesArr;
    for (const auto& fade : fadeCues) {
        QJsonObject f;
        f["time"] = static_cast<double>(fade.time);
        f["duration"] = static_cast<double>(fade.duration);
        f["color"] = fade.color.name();
        f["fromOpacity"] = static_cast<double>(fade.fromOpacity);
        f["toOpacity"] = static_cast<double>(fade.toOpacity);
        f["easing"] = static_cast<int>(fade.easing);
        fadesArr.append(f);
    }
    json["fadeCues"] = fadesArr;

    // Text cues
    QJsonArray textsArr;
    for (const auto& text : textCues) {
        QJsonObject t;
        t["time"] = static_cast<double>(text.time);
        t["duration"] = static_cast<double>(text.duration);
        t["text"] = text.text;
        t["font"] = text.font;
        t["fontSize"] = text.fontSize;
        t["color"] = text.color.name();
        t["x"] = static_cast<double>(text.x);
        t["y"] = static_cast<double>(text.y);
        textsArr.append(t);
    }
    json["textCues"] = textsArr;

    // Audio cues
    QJsonArray audioArr;
    for (const auto& audio : audioCues) {
        QJsonObject a;
        a["time"] = static_cast<double>(audio.time);
        a["audioPath"] = audio.audioPath;
        a["volume"] = static_cast<double>(audio.volume);
        a["fadeIn"] = static_cast<double>(audio.fadeIn);
        a["fadeOut"] = static_cast<double>(audio.fadeOut);
        a["loop"] = audio.loop;
        audioArr.append(a);
    }
    json["audioCues"] = audioArr;

    return json;
}

void SequencerTimeline::fromJson(const QJsonObject& json)
{
    name = json["name"].toString();
    duration = static_cast<float>(json["duration"].toDouble(30.0));
    fps = static_cast<float>(json["fps"].toDouble(30.0));
    loop = json["loop"].toBool(false);

    tracks.clear();
    for (const auto& t : json["tracks"].toArray()) {
        QJsonObject to = t.toObject();
        SequencerTrack track;
        track.id = QUuid(to["id"].toString());
        track.name = to["name"].toString();
        track.type = static_cast<SequencerTrackType>(to["type"].toInt());
        track.muted = to["muted"].toBool();
        track.solo = to["solo"].toBool();
        track.locked = to["locked"].toBool();
        track.color = QColor(to["color"].toString());
        tracks.append(track);
    }

    cameraCuts.clear();
    for (const auto& c : json["cameraCuts"].toArray()) {
        QJsonObject co = c.toObject();
        SequencerCameraCut cut;
        cut.time = static_cast<float>(co["time"].toDouble());
        cut.duration = static_cast<float>(co["duration"].toDouble(2.0));
        cut.cameraIndex = co["cameraIndex"].toInt();
        cut.position = QVector3D(
            static_cast<float>(co["posX"].toDouble()),
            static_cast<float>(co["posY"].toDouble()),
            static_cast<float>(co["posZ"].toDouble()));
        cut.target = QVector3D(
            static_cast<float>(co["targetX"].toDouble()),
            static_cast<float>(co["targetY"].toDouble()),
            static_cast<float>(co["targetZ"].toDouble()));
        cut.fov = static_cast<float>(co["fov"].toDouble(60.0));
        cut.roll = static_cast<float>(co["roll"].toDouble());
        cut.easing = static_cast<SequencerEasing>(co["easing"].toInt());
        cut.transitionDuration = static_cast<float>(co["transitionDuration"].toDouble(0.5));
        cameraCuts.append(cut);
    }

    fadeCues.clear();
    for (const auto& f : json["fadeCues"].toArray()) {
        QJsonObject fo = f.toObject();
        SequencerFadeCue fade;
        fade.time = static_cast<float>(fo["time"].toDouble());
        fade.duration = static_cast<float>(fo["duration"].toDouble(1.0));
        fade.color = QColor(fo["color"].toString("#000000"));
        fade.fromOpacity = static_cast<float>(fo["fromOpacity"].toDouble());
        fade.toOpacity = static_cast<float>(fo["toOpacity"].toDouble(1.0));
        fade.easing = static_cast<SequencerEasing>(fo["easing"].toInt());
        fadeCues.append(fade);
    }

    textCues.clear();
    for (const auto& t : json["textCues"].toArray()) {
        QJsonObject to = t.toObject();
        SequencerTextCue text;
        text.time = static_cast<float>(to["time"].toDouble());
        text.duration = static_cast<float>(to["duration"].toDouble(3.0));
        text.text = to["text"].toString();
        text.font = to["font"].toString("Arial");
        text.fontSize = to["fontSize"].toInt(32);
        text.color = QColor(to["color"].toString("#ffffff"));
        text.x = static_cast<float>(to["x"].toDouble(0.5));
        text.y = static_cast<float>(to["y"].toDouble(0.9));
        textCues.append(text);
    }

    audioCues.clear();
    for (const auto& a : json["audioCues"].toArray()) {
        QJsonObject ao = a.toObject();
        SequencerAudioCue audio;
        audio.time = static_cast<float>(ao["time"].toDouble());
        audio.audioPath = ao["audioPath"].toString();
        audio.volume = static_cast<float>(ao["volume"].toDouble(1.0));
        audio.fadeIn = static_cast<float>(ao["fadeIn"].toDouble());
        audio.fadeOut = static_cast<float>(ao["fadeOut"].toDouble());
        audio.loop = ao["loop"].toBool();
        audioCues.append(audio);
    }
}

// ============================================================================
// SequencerEditorModule - Construction / Destruction
// ============================================================================
SequencerEditorModule::SequencerEditorModule(QWidget* parent)
    : EditorModule(parent)
{
}

SequencerEditorModule::~SequencerEditorModule()
{
    shutdown();
}

// ============================================================================
// SequencerEditorModule::initialize
// ============================================================================
bool SequencerEditorModule::initialize()
{
    // Set up playback timer
    connect(&m_playbackTimer, &QTimer::timeout, this, &SequencerEditorModule::onPlaybackTick);
    m_playbackTimer.setInterval(static_cast<int>(1000.0f / m_timeline.fps));

    buildUI();
    return true;
}

// ============================================================================
// SequencerEditorModule::shutdown
// ============================================================================
void SequencerEditorModule::shutdown()
{
    stop();
    m_playbackTimer.stop();
}

// ============================================================================
// SequencerEditorModule::buildUI
// ============================================================================
void SequencerEditorModule::buildUI()
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);

    // ---- Toolbar -----------------------------------------------------------
    auto* toolbar = new QToolBar("Sequencer Toolbar", this);
    toolbar->setMovable(false);

    auto* btnNew = new QToolButton();
    btnNew->setText("New");
    btnNew->setToolTip("New timeline");
    connect(btnNew, &QToolButton::clicked, [this]() {
        m_timeline = SequencerTimeline();
        m_timeline.name = "Untitled";
        emit timelineChanged();
    });
    toolbar->addWidget(btnNew);

    auto* btnAddCameraCut = new QToolButton();
    btnAddCameraCut->setText("+ Camera");
    btnAddCameraCut->setToolTip("Add camera cut at current time");
    connect(btnAddCameraCut, &QToolButton::clicked, [this]() {
        SequencerCameraCut cut;
        cut.time = m_timeline.currentTime;
        addCameraCut(cut);
    });
    toolbar->addWidget(btnAddCameraCut);

    auto* btnAddFade = new QToolButton();
    btnAddFade->setText("+ Fade");
    btnAddFade->setToolTip("Add fade cue");
    connect(btnAddFade, &QToolButton::clicked, [this]() {
        SequencerFadeCue fade;
        fade.time = m_timeline.currentTime;
        addFadeCue(fade);
    });
    toolbar->addWidget(btnAddFade);

    auto* btnAddText = new QToolButton();
    btnAddText->setText("+ Text");
    btnAddText->setToolTip("Add text overlay");
    connect(btnAddText, &QToolButton::clicked, [this]() {
        SequencerTextCue text;
        text.time = m_timeline.currentTime;
        addTextCue(text);
    });
    toolbar->addWidget(btnAddText);

    auto* btnAddAudio = new QToolButton();
    btnAddAudio->setText("+ Audio");
    btnAddAudio->setToolTip("Add audio cue");
    connect(btnAddAudio, &QToolButton::clicked, [this]() {
        SequencerAudioCue audio;
        audio.time = m_timeline.currentTime;
        addAudioCue(audio);
    });
    toolbar->addWidget(btnAddAudio);

    toolbar->addSeparator();

    // ---- Playback controls -------------------------------------------------
    auto* btnPlay = new QToolButton();
    btnPlay->setText("Play");
    btnPlay->setToolTip("Play/Pause");
    connect(btnPlay, &QToolButton::clicked, [this]() {
        if (m_playing) pause(); else play();
    });
    toolbar->addWidget(btnPlay);

    auto* btnStop = new QToolButton();
    btnStop->setText("Stop");
    btnStop->setToolTip("Stop and rewind");
    connect(btnStop, &QToolButton::clicked, this, &SequencerEditorModule::stop);
    toolbar->addWidget(btnStop);

    auto* timeLabel = new QLabel(" 00:00.000 / 00:30.000 ");
    timeLabel->setFont(QFont("Consolas", 10));
    toolbar->addWidget(timeLabel);

    auto* speedSpin = new QDoubleSpinBox();
    speedSpin->setRange(0.1, 4.0);
    speedSpin->setValue(1.0);
    speedSpin->setSingleStep(0.1);
    speedSpin->setToolTip("Playback speed");
    connect(speedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged),
            [this](double v) { m_playbackSpeed = static_cast<float>(v); });
    toolbar->addWidget(new QLabel(" Speed: "));
    toolbar->addWidget(speedSpin);

    toolbar->addSeparator();

    auto* btnRender = new QToolButton();
    btnRender->setText("Render Video");
    btnRender->setToolTip("Export timeline to video file");
    connect(btnRender, &QToolButton::clicked, [this]() {
        QString path = QFileDialog::getSaveFileName(this, "Export Video",
            QString(), "MP4 Video (*.mp4);;All Files (*)");
        if (!path.isEmpty()) {
            renderToVideo(path);
        }
    });
    toolbar->addWidget(btnRender);

    mainLayout->addWidget(toolbar);

    // ---- Splitter: Track list | Timeline view ------------------------------
    auto* splitter = new QSplitter(Qt::Horizontal, this);

    // Left panel: Track list
    auto* trackPanel = new QWidget();
    auto* trackLayout = new QVBoxLayout(trackPanel);
    trackLayout->setContentsMargins(4, 4, 4, 4);

    auto* trackLabel = new QLabel("Tracks");
    trackLabel->setStyleSheet("font-weight: bold; padding: 4px;");
    trackLayout->addWidget(trackLabel);

    auto* trackList = new QTreeWidget();
    trackList->setHeaderLabels({"Name", "Type", "Mute", "Solo"});
    trackList->setColumnCount(4);
    trackList->setRootIsDecorated(false);
    trackLayout->addWidget(trackList);

    auto* addTrackBtn = new QPushButton("Add Track");
    connect(addTrackBtn, &QPushButton::clicked, [this, trackList]() {
        addTrack("Camera", SequencerTrackType::Camera);
        // Refresh tree
        trackList->clear();
        for (const auto& track : m_timeline.tracks) {
            auto* item = new QTreeWidgetItem(trackList);
            item->setText(0, track.name);
            item->setText(1, QString::number(static_cast<int>(track.type)));
            item->setCheckState(2, track.muted ? Qt::Checked : Qt::Unchecked);
            item->setCheckState(3, track.solo ? Qt::Checked : Qt::Unchecked);
        }
    });
    trackLayout->addWidget(addTrackBtn);

    splitter->addWidget(trackPanel);

    // Right panel: Timeline canvas
    auto* timelineScroll = new QScrollArea();
    timelineScroll->setMinimumWidth(600);
    timelineScroll->setWidgetResizable(true);
    splitter->addWidget(timelineScroll);

    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);

    mainLayout->addWidget(splitter);

    // Connect timeline change signal to update time display
    connect(this, &SequencerEditorModule::timeChanged, this,
        [timeLabel](float t) {
            float dur = 30.0f; // would come from timeline
            int tMins = int(t) / 60;
            int tSecs = int(t) % 60;
            int tMs = int(t * 1000) % 1000;
            int dMins = int(dur) / 60;
            int dSecs = int(dur) % 60;
            int dMs = int(dur * 1000) % 1000;
            timeLabel->setText(QString(" %1:%2.%3 / %4:%5.%6 ")
                .arg(tMins, 2, 10, QChar('0'))
                .arg(tSecs, 2, 10, QChar('0'))
                .arg(tMs, 3, 10, QChar('0'))
                .arg(dMins, 2, 10, QChar('0'))
                .arg(dSecs, 2, 10, QChar('0'))
                .arg(dMs, 3, 10, QChar('0')));
        });
}

// ============================================================================
// Dock Widget
// ============================================================================
QDockWidget* SequencerEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    auto* dock = new QDockWidget("Cinematic Sequencer", mainWindow);
    dock->setWidget(this);
    dock->setObjectName("SequencerEditorDock");
    return dock;
}

// ============================================================================
// Playback
// ============================================================================
void SequencerEditorModule::play()
{
    if (m_playing) return;
    m_playing = true;
    m_playbackTimer.start();
    emit playbackStarted();
}

void SequencerEditorModule::pause()
{
    m_playing = false;
    m_playbackTimer.stop();
}

void SequencerEditorModule::stop()
{
    m_playing = false;
    m_playbackTimer.stop();
    m_timeline.currentTime = 0.0f;
    emit timeChanged(0.0f);
    emit playbackStopped();
}

void SequencerEditorModule::seek(float timeSeconds)
{
    m_timeline.currentTime = qBound(0.0f, timeSeconds, m_timeline.duration);
    emit timeChanged(m_timeline.currentTime);
}

void SequencerEditorModule::onPlaybackTick()
{
    m_timeline.currentTime += (1.0f / m_timeline.fps) * m_playbackSpeed;

    if (m_timeline.currentTime >= m_timeline.duration) {
        if (m_timeline.loop) {
            m_timeline.currentTime = 0.0f;
        } else {
            stop();
            return;
        }
    }

    emit timeChanged(m_timeline.currentTime);
}

// ============================================================================
// Track CRUD
// ============================================================================
void SequencerEditorModule::addTrack(const QString& name, SequencerTrackType type)
{
    SequencerTrack track;
    track.id = QUuid::createUuid();
    track.name = name;
    track.type = type;
    m_timeline.tracks.append(track);
    emit timelineChanged();
}

void SequencerEditorModule::removeTrack(const QUuid& trackId)
{
    for (int i = m_timeline.tracks.size() - 1; i >= 0; --i) {
        if (m_timeline.tracks[i].id == trackId) {
            m_timeline.tracks.removeAt(i);
            break;
        }
    }
    emit timelineChanged();
}

void SequencerEditorModule::muteTrack(const QUuid& trackId, bool muted)
{
    for (auto& track : m_timeline.tracks) {
        if (track.id == trackId) {
            track.muted = muted;
            break;
        }
    }
}

void SequencerEditorModule::soloTrack(const QUuid& trackId, bool solo)
{
    for (auto& track : m_timeline.tracks) {
        if (track.id == trackId) {
            track.solo = solo;
            break;
        }
    }
}

// ============================================================================
// Camera Cuts
// ============================================================================
void SequencerEditorModule::addCameraCut(const SequencerCameraCut& cut)
{
    m_timeline.cameraCuts.append(cut);
    std::sort(m_timeline.cameraCuts.begin(), m_timeline.cameraCuts.end(),
              [](const SequencerCameraCut& a, const SequencerCameraCut& b) {
                  return a.time < b.time;
              });
    emit cameraCutAdded(m_timeline.cameraCuts.size() - 1);
}

void SequencerEditorModule::removeCameraCut(int index)
{
    if (index >= 0 && index < m_timeline.cameraCuts.size()) {
        m_timeline.cameraCuts.removeAt(index);
        emit timelineChanged();
    }
}

void SequencerEditorModule::updateCameraCut(int index, const SequencerCameraCut& cut)
{
    if (index >= 0 && index < m_timeline.cameraCuts.size()) {
        m_timeline.cameraCuts[index] = cut;
        emit timelineChanged();
    }
}

// ============================================================================
// Fade Cues
// ============================================================================
void SequencerEditorModule::addFadeCue(const SequencerFadeCue& cue)
{
    m_timeline.fadeCues.append(cue);
    std::sort(m_timeline.fadeCues.begin(), m_timeline.fadeCues.end(),
              [](const SequencerFadeCue& a, const SequencerFadeCue& b) {
                  return a.time < b.time;
              });
}

void SequencerEditorModule::removeFadeCue(int index)
{
    if (index >= 0 && index < m_timeline.fadeCues.size()) {
        m_timeline.fadeCues.removeAt(index);
        emit timelineChanged();
    }
}

// ============================================================================
// Text Cues
// ============================================================================
void SequencerEditorModule::addTextCue(const SequencerTextCue& cue)
{
    m_timeline.textCues.append(cue);
    std::sort(m_timeline.textCues.begin(), m_timeline.textCues.end(),
              [](const SequencerTextCue& a, const SequencerTextCue& b) {
                  return a.time < b.time;
              });
}

void SequencerEditorModule::removeTextCue(int index)
{
    if (index >= 0 && index < m_timeline.textCues.size()) {
        m_timeline.textCues.removeAt(index);
        emit timelineChanged();
    }
}

// ============================================================================
// Audio Cues
// ============================================================================
void SequencerEditorModule::addAudioCue(const SequencerAudioCue& cue)
{
    m_timeline.audioCues.append(cue);
    std::sort(m_timeline.audioCues.begin(), m_timeline.audioCues.end(),
              [](const SequencerAudioCue& a, const SequencerAudioCue& b) {
                  return a.time < b.time;
              });
}

void SequencerEditorModule::removeAudioCue(int index)
{
    if (index >= 0 && index < m_timeline.audioCues.size()) {
        m_timeline.audioCues.removeAt(index);
        emit timelineChanged();
    }
}

// ============================================================================
// Event Cues
// ============================================================================
void SequencerEditorModule::addEventCue(const SequencerEventCue& cue)
{
    m_timeline.eventCues.append(cue);
    std::sort(m_timeline.eventCues.begin(), m_timeline.eventCues.end(),
              [](const SequencerEventCue& a, const SequencerEventCue& b) {
                  return a.time < b.time;
              });
}

void SequencerEditorModule::removeEventCue(int index)
{
    if (index >= 0 && index < m_timeline.eventCues.size()) {
        m_timeline.eventCues.removeAt(index);
        emit timelineChanged();
    }
}

// ============================================================================
// File I/O
// ============================================================================
void SequencerEditorModule::exportFile(const QString& path)
{
    saveProject(path);
}

void SequencerEditorModule::importFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return;

    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    if (doc.isObject()) {
        deserializeProject(doc.object());
    }
}

void SequencerEditorModule::newProject(const QString& name, const QString& /*path*/)
{
    m_timeline = SequencerTimeline();
    m_timeline.name = name;
    emit timelineChanged();
}

void SequencerEditorModule::saveProject(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return;

    QJsonDocument doc(serializeProject());
    file.write(doc.toJson());
}

// ============================================================================
// Serialization
// ============================================================================
QJsonObject SequencerEditorModule::serializeProject() const
{
    return m_timeline.toJson();
}

void SequencerEditorModule::deserializeProject(const QJsonObject& json)
{
    m_timeline.fromJson(json);
    emit timelineChanged();
}

// ============================================================================
// Render to video
// ============================================================================
bool SequencerEditorModule::renderToVideo(const QString& outputPath, int width, int height, float fps)
{
    // Use the VideoEncoder batch system
    int totalFrames = static_cast<int>(m_timeline.duration * fps);

    VideoEncoderConfig cfg;
    cfg.width = width;
    cfg.height = height;
    cfg.fps = fps;
    cfg.codec = VideoEncoderConfig::Codec::H264;
    cfg.quality = VideoEncoderConfig::Quality::High;

    VideoEncoderBatch batch;
    bool ok = batch.encodeWithRenderCallback(
        totalFrames,
        [this, width, height, fps, totalFrames](int frameNum) -> QImage {
            float t = static_cast<float>(frameNum) / fps;

            QImage frame(width, height, QImage::Format_RGB888);
            frame.fill(Qt::black);

            QPainter painter(&frame);
            painter.setRenderHint(QPainter::Antialiasing);

            // Evaluate camera cuts at this time
            for (const auto& cut : m_timeline.cameraCuts) {
                if (t >= cut.time && t < cut.time + cut.duration) {
                    // Draw camera info overlay
                    painter.fillRect(0, 0, width, height, QColor(15, 20, 30));
                    painter.setPen(QColor(100, 200, 255));
                    painter.setFont(QFont("Consolas", 18, QFont::Bold));
                    painter.drawText(20, 40,
                        QString("CAMERA %1 @ %2s").arg(cut.cameraIndex).arg(t, 0, 'f', 2));
                    painter.setPen(QColor(80, 160, 220));
                    painter.setFont(QFont("Consolas", 12));
                    painter.drawText(20, 70,
                        QString("POS: %1, %2, %3")
                        .arg(cut.position.x(), 0, 'f', 1)
                        .arg(cut.position.y(), 0, 'f', 1)
                        .arg(cut.position.z(), 0, 'f', 1));
                    painter.drawText(20, 90,
                        QString("FOV: %1°").arg(cut.fov, 0, 'f', 1));
                }
            }

            // Evaluate fade cues
            for (const auto& fade : m_timeline.fadeCues) {
                if (t >= fade.time && t < fade.time + fade.duration) {
                    float progress = (t - fade.time) / fade.duration;
                    float opacity = fade.fromOpacity + (fade.toOpacity - fade.fromOpacity) * progress;
                    QColor fadeColor = fade.color;
                    fadeColor.setAlphaF(opacity);
                    painter.fillRect(0, 0, width, height, fadeColor);
                }
            }

            // Evaluate text cues
            for (const auto& text : m_timeline.textCues) {
                if (t >= text.time && t < text.time + text.duration) {
                    painter.setPen(text.shadowColor);
                    painter.setFont(QFont(text.font.toUtf8(), text.fontSize));
                    int tx = static_cast<int>(text.x * width);
                    int ty = static_cast<int>(text.y * height);
                    painter.drawText(tx + text.shadowOffset, ty + text.shadowOffset, text.text);
                    painter.setPen(text.color);
                    painter.drawText(tx, ty, text.text);
                }
            }

            // Timeline progress bar
            painter.fillRect(0, height - 4, width, 4, QColor(40, 40, 60));
            float progressPct = static_cast<float>(frameNum) / static_cast<float>(totalFrames);
            painter.fillRect(0, height - 4, static_cast<int>(width * progressPct), 4,
                           QColor(100, 200, 255));

            painter.end();
            return frame;
        },
        outputPath,
        cfg);

    return ok;
}

} // namespace ks
