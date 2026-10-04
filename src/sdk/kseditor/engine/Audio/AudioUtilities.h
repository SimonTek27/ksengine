#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>
#include <QJsonDocument>
#include <QDir>
#include <QAudioFormat>
#include <QUuid>
#include <QFileInfo>
#include <QFile>
#include <cmath>

namespace ks {
namespace audio {

struct AudioLabel {
    int position = 0;
    QString text;
    QString category;
};

class KSAudioTypes : public QObject
{
    Q_OBJECT
public:
    explicit KSAudioTypes(QObject* parent = nullptr) : QObject(parent) {}

    enum SampleFormat { Int8, Int16, Int24, Int32, Float32, Float64 };
    enum ChannelConfig { Mono, Stereo, Surround51, Surround71 };

    static QString formatToString(SampleFormat format);
    static int formatToBytes(SampleFormat format);

    static QString channelConfigToString(ChannelConfig config);
    static int channelCount(ChannelConfig config);
};

class KSAudioExtensions : public QObject
{
    Q_OBJECT
public:
    explicit KSAudioExtensions(QObject* parent = nullptr) : QObject(parent) {}

    static QStringList getSupportedExtensions();
    static bool isSupported(const QString& extension);
    static QString getFileFilter();

    static int getSampleSize(const QString& format);
    static int getSampleRate(const QString& path);

private:
    static QStringList s_extensions;
};

class KSAudioMetadata : public QObject
{
    Q_OBJECT
public:
    explicit KSAudioMetadata(QObject* parent = nullptr) : QObject(parent) {}
    ~KSAudioMetadata() {}

    void setTitle(const QString& title) { m_title = title; }
    QString title() const { return m_title; }
    void setArtist(const QString& artist) { m_artist = artist; }
    QString artist() const { return m_artist; }
    void setAlbum(const QString& album) { m_album = album; }
    QString album() const { return m_album; }
    void setComment(const QString& comment) { m_comment = comment; }
    QString comment() const { return m_comment; }
    void setGenre(const QString& genre) { m_genre = genre; }
    QString genre() const { return m_genre; }
    void setYear(int year) { m_year = year; }
    int year() const { return m_year; }
    void setTrack(int track) { m_track = track; }
    int track() const { return m_track; }

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    bool readFromFile(const QString& path);
    bool writeToFile(const QString& path);

signals:
    void changed();

private:
    QString m_title;
    QString m_artist;
    QString m_album;
    QString m_comment;
    QString m_genre;
    int m_year = 0;
    int m_track = 0;
};

class KSAudioPresetManager : public QObject
{
    Q_OBJECT
public:
    explicit KSAudioPresetManager(QObject* parent = nullptr) : QObject(parent) {}
    ~KSAudioPresetManager() {}

    struct Preset {
        QString id;
        QString name;
        QString category;
        QJsonObject data;
    };

    void savePreset(const QString& name, const QString& category, const QJsonObject& data);
    bool loadPreset(const QString& presetId, QJsonObject& data);
    QVector<Preset> getPresets(const QString& category = QString()) const;
    bool deletePreset(const QString& presetId);

    void loadFromFile(const QString& path);
    void saveToFile(const QString& path);

signals:
    void presetSaved(const QString& id);
    void presetLoaded(const QString& id);
    void presetDeleted(const QString& id);

private:
    QMap<QString, Preset> m_presets;
};

class KSAudioShortcutManager : public QObject
{
    Q_OBJECT
public:
    explicit KSAudioShortcutManager(QObject* parent = nullptr) : QObject(parent) {}
    ~KSAudioShortcutManager() {}

    void registerShortcut(const QString& action, const QString& keySequence);
    QString getShortcut(const QString& action) const;

    void setDefaultShortcuts();

signals:
    void shortcutTriggered(const QString& action);

private:
    QMap<QString, QString> m_shortcuts;
};

class KSAudioRecentFilesManager : public QObject
{
    Q_OBJECT
public:
    explicit KSAudioRecentFilesManager(QObject* parent = nullptr) : QObject(parent) {}
    ~KSAudioRecentFilesManager() {}

    void addFile(const QString& path);
    void removeFile(const QString& path);
    void clear();

    QStringList recentFiles(int maxCount = 10) const;
    bool hasRecentFiles() const { return !m_recentFiles.isEmpty(); }

signals:
    void fileAdded(const QString& path);
    void fileRemoved(const QString& path);
    void cleared();

private:
    QStringList m_recentFiles;
    int m_maxFiles = 10;
};

class KSSoundBankPlayer : public QObject
{
    Q_OBJECT
public:
    explicit KSSoundBankPlayer(QObject* parent = nullptr) : QObject(parent) {}
    ~KSSoundBankPlayer() {}

    struct SoundBank {
        QString name;
        QString path;
        int eventCount;
    };

    bool loadBank(const QString& path);
    void unloadBank(const QString& name);
    QVector<SoundBank> loadedBanks() const { return m_banks; }

    QString playEvent(const QString& eventName);
    void stopEvent(const QString& playbackId);
    void stopAll();

    void setVolume(float volume) { m_volume = volume; }
    float volume() const { return m_volume; }

signals:
    void bankLoaded(const QString& name);
    void eventStarted(const QString& eventId);
    void eventStopped(const QString& eventId);

private:
    QVector<SoundBank> m_banks;
    float m_volume = 1.0f;
};

class KSMultiAudioTrackEditor : public QObject
{
    Q_OBJECT
public:
    explicit KSMultiAudioTrackEditor(QObject* parent = nullptr) : QObject(parent) {}
    ~KSMultiAudioTrackEditor() {}

    struct Track {
        QString id;
        QString name;
        QVector<float> audioData;
        int sampleRate;
        bool muted;
        bool solo;
        float volume;
    };

    void addTrack(const QString& name);
    void removeTrack(const QString& trackId);
    Track getTrack(const QString& trackId) const;
    QVector<Track> allTracks() const { return m_tracks.values(); }

    void setTrackAudio(const QString& trackId, const QVector<float>& audio);
    void setTrackVolume(const QString& trackId, float volume);
    void setTrackMute(const QString& trackId, bool muted);
    void setTrackSolo(const QString& trackId, bool solo);

    QVector<float> mix(int sampleRate);

signals:
    void trackAdded(const QString& trackId);
    void trackRemoved(const QString& trackId);
    void mixComplete(const QVector<float>& result);

private:
    QMap<QString, Track> m_tracks;
};

class KSSilenceFinder : public QObject
{
    Q_OBJECT
public:
    explicit KSSilenceFinder(QObject* parent = nullptr) : QObject(parent) {}
    ~KSSilenceFinder() {}

    struct SilenceRegion {
        int startSample;
        int endSample;
        float durationMs;
    };

    void setThreshold(float db) { m_threshold = db; }
    float threshold() const { return m_threshold; }

    void setMinDuration(float ms) { m_minDuration = ms; }
    float minDuration() const { return m_minDuration; }

    QVector<SilenceRegion> find(const QVector<float>& audio, int sampleRate);

signals:
    void found(const QVector<SilenceRegion>& regions);

private:
    float m_threshold = -60.0f;
    float m_minDuration = 500.0f;
};

class KSLabelAudioTrack : public QObject
{
    Q_OBJECT
public:
    explicit KSLabelAudioTrack(QObject* parent = nullptr) : QObject(parent) {}
    ~KSLabelAudioTrack() {}

    using Label = AudioLabel;

    void addLabel(int position, const QString& text, const QString& category = QString());
    void removeLabel(int position);
    void updateLabel(int position, const QString& text);

    QVector<Label> getLabels() const { return m_labels; }
    QVector<Label> getLabelsInRange(int start, int end) const;

signals:
    void labelAdded(const Label& label);
    void labelRemoved(int position);

private:
    QVector<Label> m_labels;
};

class KSVSTEffectsChain : public QObject
{
    Q_OBJECT
public:
    explicit KSVSTEffectsChain(QObject* parent = nullptr) : QObject(parent) {}
    ~KSVSTEffectsChain() {}

    struct VSTPlugin {
        QString id;
        QString name;
        QString path;
        bool enabled;
        QMap<QString, float> parameters;
    };

    void loadPlugin(const QString& path);
    void unloadPlugin(const QString& pluginId);
    VSTPlugin getPlugin(const QString& pluginId) const;

    void addEffect(const QString& pluginId);
    void removeEffect(int index);
    void moveEffect(int from, int to);

    QVector<float> process(const QVector<float>& input, int sampleRate);

    void setBypass(bool bypass) { m_bypass = bypass; }
    bool bypass() const { return m_bypass; }

signals:
    void pluginLoaded(const QString& pluginId);
    void pluginUnloaded(const QString& pluginId);
    void chainChanged();

private:
    QMap<QString, VSTPlugin> m_plugins;
    QStringList m_effectOrder;
    bool m_bypass = false;
};

} // namespace audio
} // namespace ks
