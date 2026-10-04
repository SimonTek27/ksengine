#pragma once
// ============================================================================
// VideoEncoder.h
// FFmpeg-based video encoder for rendering viewport frames, replays,
// and animations to MP4/H.264/H.265 video files.
// ============================================================================

#include <QObject>
#include <QString>
#include <QImage>
#include <QVector>
#include <QSize>
#include <functional>
#include <memory>

// Forward declarations to avoid requiring FFmpeg headers in the public API
struct AVCodecContext;
struct AVFormatContext;
struct AVFrame;
struct AVPacket;
struct SwsContext;

namespace ks {

// ============================================================================
// Video encoding configuration
// ============================================================================
struct VideoEncoderConfig {
    int width = 1920;
    int height = 1080;
    float fps = 30.0f;
    int bitrate = 8000000;       // 8 Mbps default
    int gopSize = 12;            // keyframe every 12 frames
    int maxBFrames = 2;          // B-frames for compression

    enum class Codec {
        H264,                    // H.264/AVC (most compatible)
        H265,                    // H.265/HEVC (better compression)
        VP9                      // VP9 (open, good for web)
    };
    Codec codec = Codec::H264;

    enum class PixelFormat {
        YUV420P,                 // Standard 4:2:0
        YUV444P                  // High quality 4:4:4
    };
    PixelFormat pixelFormat = PixelFormat::YUV420P;

    enum class Preset {
        UltraFast,
        SuperFast,
        VeryFast,
        Faster,
        Fast,
        Medium,
        Slow,
        Slower,
        VerySlow
    };
    Preset preset = Preset::Medium;

    enum class Quality {
        Lossless,                // CRF 0
        VisuallyLossless,        // CRF 15
        High,                    // CRF 18
        Medium,                  // CRF 23 (default x264)
        Low,                     // CRF 28
        VeryLow                  // CRF 35
    };
    Quality quality = Quality::Medium;

    // Audio settings
    bool enableAudio = false;
    int audioBitrate = 192000;   // 192 kbps
    int audioSampleRate = 44100;
    int audioChannels = 2;
};

// ============================================================================
// Encoding progress callback
// ============================================================================
struct VideoEncodeProgress {
    int totalFrames = 0;
    int encodedFrames = 0;
    float percentComplete = 0.0f;
    double encodingSpeed = 0.0f;  // frames per second
    double elapsedTime = 0.0;     // seconds
    bool cancelled = false;
};

using ProgressCallback = std::function<void(const VideoEncodeProgress&)>;

// ============================================================================
// VideoEncoder - FFmpeg-based video encoder
// ============================================================================
class VideoEncoder : public QObject
{
    Q_OBJECT
public:
    explicit VideoEncoder(QObject* parent = nullptr);
    ~VideoEncoder();

    // ---- Configuration -----------------------------------------------------
    void setConfig(const VideoEncoderConfig& config);
    VideoEncoderConfig config() const { return m_config; }

    // ---- Encoding lifecycle ------------------------------------------------

    // Open output file and initialize encoder.
    // Returns true on success.
    bool open(const QString& outputPath);

    // Encode a single frame from a QImage (RGB888/ARGB32).
    // The image is automatically converted to YUV.
    // Returns true on success.
    bool encodeFrame(const QImage& frame);

    // Encode a single frame from raw RGBA pixel data.
    bool encodeFrameRGBA(const uint8_t* rgbaData, int width, int height, int stride);

    // Flush remaining frames and close the file.
    // Must be called after all frames are encoded.
    bool close();

    // Cancel encoding (can be called from progress callback).
    void cancel();

    // ---- Status ------------------------------------------------------------
    bool isOpen() const { return m_isOpen; }
    int framesEncoded() const { return m_framesEncoded; }
    QString lastError() const { return m_lastError; }

    // ---- Progress callback -------------------------------------------------
    void setProgressCallback(ProgressCallback callback) { m_progressCallback = callback; }

    // ---- Static helpers ----------------------------------------------------

    // Get list of available codecs on this system.
    static QStringList availableCodecs();

    // Get recommended config for a given use case.
    static VideoEncoderConfig presetConfig(const QString& presetName);

    // Check if FFmpeg is available (linked or loadable).
    static bool isAvailable();

signals:
    void encodingStarted(int totalFrames);
    void frameEncoded(int frameNumber);
    void encodingFinished(bool success, const QString& message);
    void encodingProgress(float percent);

private:
    bool initEncoder();
    bool writeFrame(AVFrame* frame);
    AVFrame* createFrame(int width, int height);
    void convertImageToFrame(const QImage& image, AVFrame* frame);
    void cleanup();

    VideoEncoderConfig m_config;

    // FFmpeg contexts (opaque pointers to avoid header dependency)
    AVFormatContext* m_formatContext = nullptr;
    AVCodecContext* m_codecContext = nullptr;
    AVPacket* m_packet = nullptr;
    SwsContext* m_swsContext = nullptr;

    bool m_isOpen = false;
    int m_framesEncoded = 0;
    bool m_cancelled = false;
    QString m_lastError;
    ProgressCallback m_progressCallback;
};

// ============================================================================
// VideoEncoderBatch - Batch encode a sequence of images or frames
// ============================================================================
class VideoEncoderBatch : public QObject
{
    Q_OBJECT
public:
    explicit VideoEncoderBatch(QObject* parent = nullptr);
    ~VideoEncoderBatch();

    // Encode a directory of images (sorted by name) to video.
    bool encodeImageSequence(const QString& inputDir,
                             const QString& filePattern,  // e.g., "frame_%04d.png"
                             const QString& outputPath,
                             const VideoEncoderConfig& config,
                             ProgressCallback progress = nullptr);

    // Encode QImage list to video.
    bool encodeFrameList(const QVector<QImage>& frames,
                         const QString& outputPath,
                         const VideoEncoderConfig& config,
                         ProgressCallback progress = nullptr);

    // Encode with a render callback (for real-time viewport capture).
    // renderCallback(frameNumber) should return the QImage for that frame.
    bool encodeWithRenderCallback(int totalFrames,
                                  std::function<QImage(int)> renderCallback,
                                  const QString& outputPath,
                                  const VideoEncoderConfig& config,
                                  ProgressCallback progress = nullptr);

    void cancel() { m_cancelled = true; }
    bool isCancelled() const { return m_cancelled; }

private:
    bool m_cancelled = false;
};

} // namespace ks
