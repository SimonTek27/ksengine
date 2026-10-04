#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QDir>
#include <QAudioFormat>
#include <QUuid>
#include "AudioUtilities.h"

namespace ks { namespace audio {

using ks::audio::AudioLabel;

class AudioStudio : public QObject
{
    Q_OBJECT
public:
    explicit AudioStudio(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioStudio() {}

    enum Mode { ModeWaveform, ModeEvent, ModeBank, ModeMixer, ModeDSP };

    void setMode(Mode mode) { m_mode = mode; }
    Mode mode() const { return m_mode; }

    bool loadProject(const QString& path);
    bool saveProject(const QString& path);
    void newProject();

    QString currentProjectPath() const { return m_projectPath; }
    bool isModified() const { return m_modified; }
    void setModified(bool v) { m_modified = v; }

    void setMasterVolume(float v) { m_masterVolume = v; }
    float masterVolume() const { return m_masterVolume; }

    struct Track {
        QString id;
        QString name;
        int sampleRate = 44100;
        bool muted = false;
        bool solo = false;
        float volume = 1.0f;
        QString audioFile;
        QVector<AudioLabel> labels;
    };

    struct Effect {
        QString name;
        QString type;
        bool enabled = true;
        QMap<QString, float> parameters;
    };

    struct BusConfig {
        QString name;
        float volume = 1.0f;
        bool muted = false;
        QStringList effectChain;
    };

    QVector<Track>& tracks() { return m_tracks; }
    QVector<Effect>& effects() { return m_effects; }
    QVector<BusConfig>& buses() { return m_buses; }

signals:
    void modeChanged(Mode mode);
    void projectLoaded(const QString& path);
    void projectSaved(const QString& path);
    void error(const QString& msg);

private:
    Mode m_mode = ModeWaveform;
    QString m_projectPath;
    bool m_modified = false;
    float m_masterVolume = 1.0f;
    QVector<Track> m_tracks;
    QVector<Effect> m_effects;
    QVector<BusConfig> m_buses;
};

class AudioProject : public QObject
{
    Q_OBJECT
public:
    explicit AudioProject(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioProject() {}

    void setName(const QString& name) { m_name = name; }
    QString name() const { return m_name; }

    void setAuthor(const QString& author) { m_author = author; }
    QString author() const { return m_author; }

    void setVersion(const QString& version) { m_version = version; }
    QString version() const { return m_version; }

    void addAsset(const QString& id, const QString& path);
    void removeAsset(const QString& id);
    QStringList assets() const { return m_assets.keys(); }

    void addEvent(const QString& id, const QString& name);
    void removeEvent(const QString& id);
    QStringList events() const { return m_events.keys(); }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

signals:
    void changed();

private:
    QString m_name;
    QString m_author;
    QString m_version = "1.0";
    QMap<QString, QString> m_assets;
    QMap<QString, QString> m_events;
};

class AudioEditor : public QObject
{
    Q_OBJECT
public:
    explicit AudioEditor(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioEditor() {}

    void loadFile(const QString& path);
    void saveFile(const QString& path);
    void exportSelection(const QString& path);

    void setSelection(int start, int end);
    int selectionStart() const { return m_selectionStart; }
    int selectionEnd() const { return m_selectionEnd; }

    void undo();
    void redo();
    bool canUndo() const { return m_undoStack.size() > 0; }
    bool canRedo() const { return m_redoStack.size() > 0; }

    void copy();
    void paste();
    void cut();
    void deleteSelection();

signals:
    void fileLoaded(const QString& path);
    void selectionChanged(int start, int end);
    void modificationChanged(bool modified);

public:
    QString mimeType() const { return "audio/x-ks-editor-samples"; }
    void setAudioData(const QVector<float>& samples) { m_audioData = samples; }
    const QVector<float>& audioData() const { return m_audioData; }
    void setSampleRate(int rate) { m_sampleRate = rate; }
    int sampleRate() const { return m_sampleRate; }

private:
    QString m_currentFile;
    QVector<float> m_audioData;
    QVector<float> m_clipboard;
    int m_sampleRate = 44100;
    int m_selectionStart = 0;
    int m_selectionEnd = 0;
    QStringList m_undoStack;
    QStringList m_redoStack;
};



class AudioFormatConverter : public QObject
{
    Q_OBJECT
public:
    explicit AudioFormatConverter(QObject* parent = nullptr) : QObject(parent) {}
    enum Quality { QualityLow, QualityMedium, QualityHigh };

    static bool readWav(QFile& file, QVector<float>& samples, QAudioFormat& format);
    static bool writeWav(QFile& file, const QVector<float>& samples, const QAudioFormat& format);

    bool convert(const QString& path, QVector<float>& samples, QAudioFormat& format);
    bool convertToWav(const QString& path, const QVector<float>& samples, const QAudioFormat& format);
    bool convertToMp3(const QString& path, const QVector<float>& samples, const QAudioFormat& format, Quality q);
    bool convertToOgg(const QString& path, const QVector<float>& samples, const QAudioFormat& format, Quality q);

    bool decodeOgg(const QString& path, QVector<float>& samples, QAudioFormat& format);
    bool decodeMp3(const QString& path, QVector<float>& samples, QAudioFormat& format);
    bool decodeFlac(const QString& path, QVector<float>& samples, QAudioFormat& format);
};

class AudioManager : public QObject
{
    Q_OBJECT
public:
    explicit AudioManager(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioManager() {}

    bool importAudio(const QString& path, const QString& destDir);
    bool exportAudio(const QString& sourcePath, const QString& destPath, const QString& format);

    QStringList supportedImportFormats() const { return {"wav", "ogg", "mp3", "flac", "aiff"}; }
    QStringList supportedExportFormats() const { return {"wav", "ogg", "mp3", "flac"}; }

    void setOutputDirectory(const QString& dir) { m_outputDir = dir; }
    QString outputDirectory() const { return m_outputDir; }

    struct AudioInfo {
        QString path;
        int sampleRate = 0;
        int channels = 0;
        int bitsPerSample = 16;
        double duration = 0.0;
        QString format;
    };

    AudioInfo getAudioInfo(const QString& path) const;

    signals:
    void importComplete(bool success);
    void exportComplete(bool success);
    void progress(int percent);
    void audioImported(const QString& path);

private:
    QString m_outputDir;
};

} } // namespace ks::audio