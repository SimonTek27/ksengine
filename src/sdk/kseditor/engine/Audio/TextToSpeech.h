#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

struct ISpVoice;

namespace ks {
namespace audio {

class TextToSpeech : public QObject {
    Q_OBJECT
public:
    explicit TextToSpeech(QObject* parent = nullptr);
    ~TextToSpeech() override;

    bool isSpeaking() const;
    QStringList availableVoices() const;
    QString currentVoice() const;
    int volume() const;
    int rate() const;

public slots:
    void speak(const QString& text);
    void speakAsync(const QString& text);
    void stop();
    void pause();
    void resume();
    void setVoice(const QString& name);
    void setVolume(int percent);
    void setRate(int rate);
    bool saveToWav(const QString& text, const QString& filePath);

signals:
    void started();
    void finished();
    void paused();
    void resumed();

private:
    bool initSapi();
    void shutdownSapi();

    ISpVoice* m_voice;
    bool m_isSpeaking;
    int m_volume;
    int m_rate;
    QString m_currentVoiceName;
    QStringList m_voiceNames;
};

} // namespace audio
} // namespace ks
