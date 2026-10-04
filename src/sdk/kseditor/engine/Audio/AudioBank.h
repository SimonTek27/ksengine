#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QMap>
#include <QJsonObject>

namespace ks { namespace audio {

class AudioBank : public QObject
{
    Q_OBJECT
public:
    explicit AudioBank(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioBank() {}

    enum Format { FormatWAV, FormatOGG, FormatKsAudio };

    struct Config {
        QString outputDir;
        Format format = FormatWAV;
        int sampleRate = 44100;
        int channels = 2;
        bool encrypt = false;
        QString encryptionKey;
    };

    void setConfig(const Config& config) { m_config = config; }
    Config config() const { return m_config; }

    bool generate(const QStringList& inputFiles);
    bool buildBanks();

signals:
    void progress(int percent);
    void status(const QString& msg);
    void finished(bool success);
    void error(const QString& msg);

private:
    Config m_config;
};

class AudioBankEx : public QObject
{
    Q_OBJECT
public:
    explicit AudioBankEx(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioBankEx() {}

    struct BankEvent {
        QString id;
        QString name;
        QString path;
        QMap<QString, float> parameters;
    };

    struct BankInfo {
        QString name;
        QString path;
        int eventCount;
        bool encrypted;
    };

    bool loadBank(const QString& path);
    bool saveBank(const QString& path);
    QVector<BankEvent> getEvents() const { return m_events; }
    QVector<BankInfo> getLoadedBanks() const { return m_banks; }

    void addEvent(const BankEvent& event);
    void removeEvent(const QString& eventId);

    bool isEncrypted() const { return m_encrypted; }
    void setEncryptionKey(const QString& key) { m_encryptionKey = key; }

signals:
    void bankLoaded(const QString& name);
    void bankSaved(const QString& name);

private:
    QVector<BankEvent> m_events;
    QVector<BankInfo> m_banks;
    bool m_encrypted = false;
    QString m_encryptionKey;
};

class FSBExtractor : public QObject
{
    Q_OBJECT
public:
    explicit FSBExtractor(QObject* parent = nullptr) : QObject(parent) {}
    ~FSBExtractor() {}

    struct FSBSample {
        QString name;
        quint32 offset;
        quint32 size;
        quint32 sampleRate;
        quint16 channels;
        QString format;
    };

    bool extractFile(const QString& fsbPath, const QString& outputDir);
    QVector<FSBSample> getSamples() const { return m_samples; }
    static bool isValidFSB(const QString& fsbPath);

signals:
    void progress(int percent);
    void status(const QString& msg);
    void extractionComplete(bool success);
    void error(const QString& msg);

private:
    bool parseHeader(const QByteArray& data);
    bool extractSamples(const QString& outputDir);

    QVector<FSBSample> m_samples;
    QByteArray m_fsbData;
};

class AudioExporter : public QObject
{
    Q_OBJECT
public:
    explicit AudioExporter(QObject* parent = nullptr) : QObject(parent) {}
    ~AudioExporter() {}

    enum ExportFormat { WAV, OGG, MP3, FLAC, AC3 };

    void setFormat(ExportFormat format) { m_format = format; }
    ExportFormat format() const { return m_format; }

    void setQuality(int quality) { m_quality = qBound(0, quality, 100); }
    int quality() const { return m_quality; }

    bool exportFile(const QString& inputPath, const QString& outputPath);
    bool exportBuffer(const QVector<float>& audio, const QString& outputPath, int sampleRate);

    void setMetadata(const QMap<QString, QString>& metadata) { m_metadata = metadata; }
    QMap<QString, QString> metadata() const { return m_metadata; }

signals:
    void exportProgress(int percent);
    void exportComplete(bool success);

private:
    ExportFormat m_format = WAV;
    int m_quality = 100;
    QMap<QString, QString> m_metadata;
};

} } // namespace ks::audio