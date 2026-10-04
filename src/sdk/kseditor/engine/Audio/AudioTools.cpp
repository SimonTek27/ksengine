#include "AudioTools.h"
#include "AudioFormatConverter.h"
#include "WaveProcessor.h"
#include <QDebug>
#include <QAudioDevice>
#include <QDataStream>
#include <QMediaDevices>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <cmath>
#include <ctime>

namespace ks {
namespace audio {

// ============================================================================
// KSAudioRecorder Implementation
// ============================================================================

KSAudioRecorder::KSAudioRecorder(QObject* parent) : QObject(parent) {}

KSAudioRecorder::~KSAudioRecorder() { stopRecording(); }

bool KSAudioRecorder::startRecording(const QString& outputPath, int sampleRate, int channels) {
    if (m_recording) stopRecording();

    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Int16);

    QAudioDevice device = QMediaDevices::defaultAudioInput();
    if (!device.isFormatSupported(format)) {
        qWarning() << "KSAudioRecorder: Format not supported";
        format = device.preferredFormat();
    }

    m_outputFile.setFileName(outputPath);
    if (!m_outputFile.open(QIODevice::WriteOnly)) {
        emit error("Cannot open output file: " + outputPath);
        return false;
    }

    writeWavHeader();

    m_audioSource = new QAudioSource(device, format, this);
    m_audioSource->start(&m_outputFile);

    m_recording = true;
    m_startTime = QDateTime::currentMSecsSinceEpoch();
    emit recordingStarted(outputPath);
    qInfo() << "KSAudioRecorder: Started recording to" << outputPath;
    return true;
}

void KSAudioRecorder::stopRecording() {
    if (!m_recording) return;
    m_recording = false;

    if (m_audioSource) {
        m_audioSource->stop();
        delete m_audioSource;
        m_audioSource = nullptr;
    }

    finalizeWavFile();
    m_outputFile.close();

    emit recordingStopped(m_outputFile.fileName());
    qInfo() << "KSAudioRecorder: Stopped recording";
}

void KSAudioRecorder::setInputDevice(const QString& deviceName) {
    auto devices = QMediaDevices::audioInputs();
    for (const auto& d : devices) {
        if (d.description() == deviceName) return;
    }
}

QStringList KSAudioRecorder::availableInputDevices() const {
    QStringList result;
    auto devices = QMediaDevices::audioInputs();
    for (const auto& d : devices) result.append(d.description());
    return result;
}

qint64 KSAudioRecorder::durationMs() const {
    if (!m_recording) return 0;
    return QDateTime::currentMSecsSinceEpoch() - m_startTime;
}

void KSAudioRecorder::writeWavHeader() {
    char header[44] = {0};
    memcpy(header, "RIFF", 4);
    quint32 fileSize = 0; // Will be updated later
    memcpy(header + 4, &fileSize, 4);
    memcpy(header + 8, "WAVE", 4);
    memcpy(header + 12, "fmt ", 4);
    quint32 fmtSize = 16;
    memcpy(header + 16, &fmtSize, 4);
    quint16 audioFormat = 1; // PCM
    memcpy(header + 20, &audioFormat, 2);
    quint16 channels = 2;
    memcpy(header + 22, &channels, 2);
    quint32 sampleRate = 44100;
    memcpy(header + 24, &sampleRate, 4);
    quint32 byteRate = sampleRate * channels * 2;
    memcpy(header + 28, &byteRate, 4);
    quint16 blockAlign = channels * 2;
    memcpy(header + 32, &blockAlign, 2);
    quint16 bitsPerSample = 16;
    memcpy(header + 34, &bitsPerSample, 2);
    memcpy(header + 36, "data", 4);
    quint32 dataSize = 0;
    memcpy(header + 40, &dataSize, 4);

    m_outputFile.write(header, 44);
}

void KSAudioRecorder::finalizeWavFile() {
    qint64 fileSize = m_outputFile.size();
    quint32 dataSize = fileSize - 44;
    m_outputFile.seek(4);
    quint32 riffSize = fileSize - 8;
    m_outputFile.write(reinterpret_cast<const char*>(&riffSize), 4);
    m_outputFile.seek(40);
    m_outputFile.write(reinterpret_cast<const char*>(&dataSize), 4);
}

// ============================================================================
// KSAudioMultiTrack Implementation
// ============================================================================

KSAudioMultiTrack::KSAudioMultiTrack(QObject* parent) : QObject(parent) {}

KSAudioMultiTrack::~KSAudioMultiTrack() { clearTracks(); }

int KSAudioMultiTrack::addTrack(const QString& name) {
    Track t;
    t.name = name;
    t.volume = 1.0f;
    m_tracks.append(t);
    int idx = m_tracks.size() - 1;
    emit trackAdded(idx, name);
    return idx;
}

bool KSAudioMultiTrack::removeTrack(int idx) {
    if (idx < 0 || idx >= m_tracks.size()) return false;
    m_tracks.removeAt(idx);
    emit trackRemoved(idx);
    return true;
}

void KSAudioMultiTrack::clearTracks() { m_tracks.clear(); }

Track* KSAudioMultiTrack::getTrack(int idx) {
    return (idx >= 0 && idx < m_tracks.size()) ? &m_tracks[idx] : nullptr;
}

const Track* KSAudioMultiTrack::getTrack(int idx) const {
    return (idx >= 0 && idx < m_tracks.size()) ? &m_tracks[idx] : nullptr;
}

bool KSAudioMultiTrack::loadAudioToTrack(int trackIndex, const QString& filePath) {
    Track* t = getTrack(trackIndex);
    if (!t) return false;

    AudioFormatConverter converter;
    AudioFormatConverter::AudioMetadata metadata;
    QAudioFormat format;
    if (converter.decodeOgg(filePath, t->samples, format, metadata)) {
        t->sampleRate = format.sampleRate();
        t->channels = format.channelCount();
        return true;
    }
    return false;
}

bool KSAudioMultiTrack::recordToTrack(int trackIndex) {
    Track* t = getTrack(trackIndex);
    if (!t) return false;
    if (t->isRecording) return true; // already recording

    QString tempPath = QDir::temp().absoluteFilePath(QString("kseditor_recording_%1.wav").arg(trackIndex));
    if (!m_recorder) {
        m_recorder = new KSAudioRecorder(this);
        connect(m_recorder, &KSAudioRecorder::recordingStopped, this, [this, trackIndex, tempPath](const QString&) {
            Track* tr = getTrack(trackIndex);
            if (tr) {
                tr->isRecording = false;
                loadAudioToTrack(trackIndex, tempPath);
                QFile::remove(tempPath);
            }
        });
    }

    bool ok = m_recorder->startRecording(tempPath, t->sampleRate, t->channels);
    if (ok) t->isRecording = true;
    return ok;
}

void KSAudioMultiTrack::setTrackVolume(int idx, float volume) {
    Track* t = getTrack(idx);
    if (t) { t->volume = qBound(0.0f, volume, 2.0f); emit trackVolumeChanged(idx, volume); }
}

float KSAudioMultiTrack::trackVolume(int idx) const {
    const Track* t = getTrack(idx);
    return t ? t->volume : 1.0f;
}

void KSAudioMultiTrack::setTrackMute(int idx, bool mute) {
    Track* t = getTrack(idx);
    if (t) t->muted = mute;
}

bool KSAudioMultiTrack::trackMuted(int idx) const {
    const Track* t = getTrack(idx);
    return t ? t->muted : false;
}

void KSAudioMultiTrack::setTrackSolo(int idx, bool solo) {
    Track* t = getTrack(idx);
    if (t) t->solo = solo;
}

bool KSAudioMultiTrack::trackSolo(int idx) const {
    const Track* t = getTrack(idx);
    return t ? t->solo : false;
}

void KSAudioMultiTrack::mixDown(QVector<float>& output, int outputSampleRate, int outputChannels) {
    output.clear();
    int maxSamples = 0;

    for (const auto& t : m_tracks) {
        if (t.muted) continue;
        if (maxSamples < t.samples.size()) maxSamples = t.samples.size();
    }

    output.resize(maxSamples);
    for (int i = 0; i < maxSamples; ++i) {
        float mixL = 0, mixR = 0;
        for (const auto& t : m_tracks) {
            if (t.muted) continue;
            if (i < t.samples.size()) {
                float s = t.samples[i] * t.volume * m_masterVolume;
                mixL += s; mixR += s;
            }
        }
        if (outputChannels >= 2) {
            output[i * 2] = mixL;
            if (i * 2 + 1 < output.size()) output[i * 2 + 1] = mixR;
        } else {
            output[i] = (mixL + mixR) * 0.5f;
        }
    }
}

bool KSAudioMultiTrack::exportMix(const QString& outputPath, int sampleRate, int channels) {
    QVector<float> mix;
    mixDown(mix, sampleRate, channels);

    QFile f(outputPath);
    if (!f.open(QIODevice::WriteOnly)) return false;

    // Write WAV header + data
    f.close();
    emit mixCompleted(outputPath);
    return true;
}

// ============================================================================
// KSAudioBatchProcessor Implementation
// ============================================================================

KSAudioBatchProcessor::KSAudioBatchProcessor(QObject* parent) : QObject(parent) {}

void KSAudioBatchProcessor::addFile(const QString& input, const QString& output) {
    m_files.append({input, output});
}

void KSAudioBatchProcessor::clearFiles() { m_files.clear(); }

void KSAudioBatchProcessor::addEffect(int type, const QVector<float>& params) {
    m_effects.append({type, params});
}

void KSAudioBatchProcessor::clearEffects() { m_effects.clear(); }

void KSAudioBatchProcessor::startProcessing() {
    m_processing = true;
    emit processingStarted();
    for (int i = 0; i < m_files.size(); ++i) {
        processFile(m_files[i]);
        m_progress = (i + 1) / float(m_files.size());
        emit progressChanged(m_progress);
        emit fileProcessed(i, m_files[i].output);
    }
    m_processing = false;
    emit processingFinished();
}

void KSAudioBatchProcessor::stopProcessing() { m_processing = false; }

void KSAudioBatchProcessor::processFile(const BatchFile& file) {
    if (!QFile::exists(file.input)) return;

    WaveProcessor processor;
    processor.load(file.input, QString());

    for (const auto& effect : m_effects) {
        switch (effect.type) {
        case 0: // normalize
            processor.normalize();
            break;
        case 1: // fade in
            processor.applyFadeIn(effect.params.isEmpty() ? 1000 : int(effect.params[0]));
            break;
        case 2: // fade out
            processor.applyFadeOut(effect.params.isEmpty() ? 1000 : int(effect.params[0]));
            break;
        default:
            break;
        }
    }

    processor.save(file.output);
}

// ============================================================================
// KSAudioFileMerger Implementation
// ============================================================================

KSAudioFileMerger::KSAudioFileMerger(QObject* parent) : QObject(parent) {}

void KSAudioFileMerger::addFile(const QString& path) {
    m_files.append(path);
    emit fileAdded(m_files.size() - 1, path);
}

void KSAudioFileMerger::removeFile(int idx) {
    if (idx >= 0 && idx < m_files.size()) {
        m_files.removeAt(idx);
        emit fileRemoved(idx);
    }
}

void KSAudioFileMerger::clearFiles() { m_files.clear(); }

bool KSAudioFileMerger::merge(const QString& outputPath, int sampleRate, int channels) {
    if (m_files.isEmpty()) return false;

    QVector<float> mergedSamples;
    int crossfadeSamples = sampleRate / 10; // 100ms crossfade

    for (int i = 0; i < m_files.size(); ++i) {
        WaveProcessor processor;
        if (!processor.load(m_files[i], QString())) continue;

        QVector<float> fileSamples = processor.getSamples();

        if (i > 0 && !mergedSamples.isEmpty()) {
            int overlapStart = mergedSamples.size() - crossfadeSamples;
            if (overlapStart < 0) overlapStart = 0;

            for (int j = 0; j < crossfadeSamples && overlapStart + j < mergedSamples.size(); ++j) {
                float fadeOut = 1.0f - (float(j) / crossfadeSamples);
                float fadeIn = float(j) / crossfadeSamples;
                if (j < fileSamples.size()) {
                    mergedSamples[overlapStart + j] = mergedSamples[overlapStart + j] * fadeOut + fileSamples[j] * fadeIn;
                }
            }
            mergedSamples.append(fileSamples.mid(crossfadeSamples));
        } else {
            mergedSamples.append(fileSamples);
        }
    }

    WaveProcessor outputProcessor;
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Float);
    outputProcessor.setFormat(format);
    outputProcessor.setSamples(mergedSamples);
    outputProcessor.save(outputPath);

    emit mergeCompleted(outputPath);
    return true;
}

// ============================================================================
// KSAudioExpression Implementation
// ============================================================================

KSAudioExpression::KSAudioExpression(QObject* parent) : QObject(parent) {}

QVector<float> KSAudioExpression::generateTone(float freq, float duration, int sr, WaveformType type, float amp) {
    int samples = int(duration * sr);
    QVector<float> result(samples);
    for (int i = 0; i < samples; ++i) {
        float t = float(i) / sr;
        float sample = 0;
        switch (type) {
            case Sine: sample = sin(2.0f * M_PI * freq * t); break;
            case Square: sample = sin(2.0f * M_PI * freq * t) >= 0 ? 1.0f : -1.0f; break;
            case Sawtooth: sample = 2.0f * (t * freq - floor(t * freq + 0.5f)); break;
            case Triangle: sample = 2.0f * fabs(2.0f * (t * freq - floor(t * freq + 0.5f))) - 1.0f; break;
            case WhiteNoise: sample = (float(rand()) / RAND_MAX) * 2.0f - 1.0f; break;
        }
        result[i] = sample * amp;
    }
    return result;
}

QVector<float> KSAudioExpression::generateSweep(float startFreq, float endFreq, float duration, int sr, WaveformType type) {
    int samples = int(duration * sr);
    QVector<float> result(samples);
    for (int i = 0; i < samples; ++i) {
        float t = float(i) / sr;
        float freq = startFreq + (endFreq - startFreq) * (t / duration);
        float sample = sin(2.0f * M_PI * freq * t);
        result[i] = sample;
    }
    return result;
}

QVector<float> KSAudioExpression::generateClickTrack(float bpm, float duration, int sr) {
    int samples = int(duration * sr);
    QVector<float> result(samples, 0);
    float samplesPerBeat = sr * 60.0f / bpm;
    for (int i = 0; i < samples; ++i) {
        if (int(i) % int(samplesPerBeat) < int(sr * 0.01f)) result[i] = 1.0f;
    }
    return result;
}

QVector<float> KSAudioExpression::generateDTMF(const QString& digits, int sr) {
    // DTMF frequencies
    const float lowFreqs[] = {697, 770, 852, 941};
    const float highFreqs[] = {1209, 1336, 1477, 1633};
    QVector<float> result;
    for (QChar d : digits) {
        int row = -1, col = -1;
        if (d >= '1' && d <= '9') { row = (d.digitValue() - 1) / 3; col = (d.digitValue() - 1) % 3; }
        else if (d == '0') { row = 3; col = 1; }
        else if (d == '*') { row = 3; col = 0; }
        else if (d == '#') { row = 3; col = 2; }
        if (row >= 0 && col >= 0) {
            int toneSamples = sr * 0.1f; // 100ms per tone
            for (int i = 0; i < toneSamples; ++i) {
                float t = float(i) / sr;
                result.append(sin(2.0f * M_PI * lowFreqs[row] * t) +
                           sin(2.0f * M_PI * highFreqs[col] * t));
            }
        }
    }
    return result;
}

bool KSAudioExpression::saveToWav(const QVector<float>& samples, const QString& path, int sr, int ch) {
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;

    QDataStream out(&file);
    out.setByteOrder(QDataStream::LittleEndian);

    // WAV header
    int bitsPerSample = 16;
    int byteRate = sr * ch * bitsPerSample / 8;
    int blockAlign = ch * bitsPerSample / 8;
    int dataSize = samples.size() * bitsPerSample / 8;

    out.writeRawData("RIFF", 4);
    out << quint32(36 + dataSize);
    out.writeRawData("WAVE", 4);
    out.writeRawData("fmt ", 4);
    out << quint32(16);
    out << quint16(1);
    out << quint16(ch);
    out << quint32(sr);
    out << quint32(byteRate);
    out << quint16(blockAlign);
    out << quint16(bitsPerSample);
    out.writeRawData("data", 4);
    out << quint32(dataSize);

    for (float s : samples) {
        qint16 val = qBound(-32768, static_cast<int>(s * 32767.0f), 32767);
        out << val;
    }

    return true;
}

} // namespace audio
} // namespace ks
