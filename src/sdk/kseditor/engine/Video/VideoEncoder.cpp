// ============================================================================
// VideoEncoder.cpp
// FFmpeg-based video encoder implementation.
// Falls back to a stub if FFmpeg is not available at runtime.
// ============================================================================

#include "VideoEncoder.h"

#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QElapsedTimer>
#include <QDebug>
#include <QRegularExpression>

// ============================================================================
// FFmpeg headers (conditional compilation)
// ============================================================================
#if HAS_FFMPEG

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/imgutils.h>
#include <libavutil/mathematics.h>
#include <libavutil/opt.h>
#include <libswscale/swscale.h>
}

#else
// Stub types when FFmpeg is not available
struct AVCodecContext {};
struct AVFormatContext {};
struct AVFrame {};
struct AVPacket {};
struct SwsContext {};
#endif

namespace ks {

// ============================================================================
// VideoEncoderConfig::presetConfig
// ============================================================================
VideoEncoderConfig VideoEncoder::presetConfig(const QString& presetName)
{
    VideoEncoderConfig cfg;

    if (presetName == "youtube_1080p") {
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.fps = 30.0f;
        cfg.bitrate = 8000000;
        cfg.codec = VideoEncoderConfig::Codec::H264;
        cfg.quality = VideoEncoderConfig::Quality::Medium;
        cfg.preset = VideoEncoderConfig::Preset::Medium;
    } else if (presetName == "youtube_4k") {
        cfg.width = 3840;
        cfg.height = 2160;
        cfg.fps = 30.0f;
        cfg.bitrate = 35000000;
        cfg.codec = VideoEncoderConfig::Codec::H264;
        cfg.quality = VideoEncoderConfig::Quality::High;
        cfg.preset = VideoEncoderConfig::Preset::Slow;
    } else if (presetName == "twitter_720p") {
        cfg.width = 1280;
        cfg.height = 720;
        cfg.fps = 30.0f;
        cfg.bitrate = 4000000;
        cfg.codec = VideoEncoderConfig::Codec::H264;
        cfg.quality = VideoEncoderConfig::Quality::Medium;
        cfg.preset = VideoEncoderConfig::Preset::Fast;
    } else if (presetName == "preview") {
        cfg.width = 640;
        cfg.height = 360;
        cfg.fps = 15.0f;
        cfg.bitrate = 1000000;
        cfg.codec = VideoEncoderConfig::Codec::H264;
        cfg.quality = VideoEncoderConfig::Quality::Low;
        cfg.preset = VideoEncoderConfig::Preset::UltraFast;
    } else if (presetName == "lossless") {
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.fps = 30.0f;
        cfg.bitrate = 0;
        cfg.codec = VideoEncoderConfig::Codec::H264;
        cfg.quality = VideoEncoderConfig::Quality::Lossless;
        cfg.preset = VideoEncoderConfig::Preset::VerySlow;
        cfg.maxBFrames = 0;
    } else if (presetName == "h265_1080p") {
        cfg.width = 1920;
        cfg.height = 1080;
        cfg.fps = 30.0f;
        cfg.bitrate = 5000000;
        cfg.codec = VideoEncoderConfig::Codec::H265;
        cfg.quality = VideoEncoderConfig::Quality::High;
        cfg.preset = VideoEncoderConfig::Preset::Medium;
    }

    return cfg;
}

// ============================================================================
// VideoEncoder - Construction / Destruction
// ============================================================================
VideoEncoder::VideoEncoder(QObject* parent)
    : QObject(parent)
{
}

VideoEncoder::~VideoEncoder()
{
    if (m_isOpen) {
        close();
    }
}

// ============================================================================
// VideoEncoder::isAvailable
// ============================================================================
bool VideoEncoder::isAvailable()
{
#if HAS_FFMPEG
    return true;
#else
    return false;
#endif
}

// ============================================================================
// VideoEncoder::availableCodecs
// ============================================================================
QStringList VideoEncoder::availableCodecs()
{
    QStringList codecs;
#if HAS_FFMPEG
    const AVCodec* codec = nullptr;
    void* opaque = nullptr;
    while ((codec = av_codec_iterate(&opaque))) {
        if (av_codec_is_encoder(codec) && codec->type == AVMEDIA_TYPE_VIDEO) {
            codecs.append(QString::fromLatin1(codec->name));
        }
    }
#endif
    return codecs;
}

// ============================================================================
// VideoEncoder::setConfig
// ============================================================================
void VideoEncoder::setConfig(const VideoEncoderConfig& config)
{
    m_config = config;
}

// ============================================================================
// VideoEncoder::open
// ============================================================================
bool VideoEncoder::open(const QString& outputPath)
{
    if (m_isOpen) {
        m_lastError = "Encoder already open";
        return false;
    }

#if HAS_FFMPEG
    if (!initEncoder()) {
        return false;
    }

    // Open output file
    int ret = avformat_alloc_output_context2(&m_formatContext, nullptr, nullptr,
                                             outputPath.toUtf8().constData());
    if (ret < 0 || !m_formatContext) {
        m_lastError = QString("Failed to allocate output context: %1").arg(ret);
        cleanup();
        return false;
    }

    // Add video stream
    const AVCodec* codec = nullptr;
    switch (m_config.codec) {
        case VideoEncoderConfig::Codec::H264:
            codec = avcodec_find_encoder_by_name("libx264");
            if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_H264);
            break;
        case VideoEncoderConfig::Codec::H265:
            codec = avcodec_find_encoder_by_name("libx265");
            if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_HEVC);
            break;
        case VideoEncoderConfig::Codec::VP9:
            codec = avcodec_find_encoder_by_name("libvpx-vp9");
            if (!codec) codec = avcodec_find_encoder(AV_CODEC_ID_VP9);
            break;
    }

    if (!codec) {
        m_lastError = "Encoder not found for selected codec";
        cleanup();
        return false;
    }

    AVStream* stream = avformat_new_stream(m_formatContext, nullptr);
    if (!stream) {
        m_lastError = "Failed to create stream";
        cleanup();
        return false;
    }

    // Create codec context
    m_codecContext = avcodec_alloc_context3(codec);
    if (!m_codecContext) {
        m_lastError = "Failed to allocate codec context";
        cleanup();
        return false;
    }

    // Configure codec
    m_codecContext->width = m_config.width;
    m_codecContext->height = m_config.height;
    m_codecContext->time_base = {1, static_cast<int>(m_config.fps)};
    m_codecContext->framerate = {static_cast<int>(m_config.fps), 1};
    m_codecContext->gop_size = m_config.gopSize;
    m_codecContext->max_b_frames = m_config.maxBFrames;

    // Pixel format
    if (m_config.pixelFormat == VideoEncoderConfig::PixelFormat::YUV444P) {
        m_codecContext->pix_fmt = AV_PIX_FMT_YUV444P;
    } else {
        m_codecContext->pix_fmt = AV_PIX_FMT_YUV420P;
    }

    // Quality / bitrate
    if (m_config.bitrate > 0) {
        m_codecContext->bit_rate = m_config.bitrate;
    }

    // H.264/H.265 specific settings
    if (codec->id == AV_CODEC_ID_H264 || codec->id == AV_CODEC_ID_HEVC) {
        // Set preset
        const char* presetName = "medium";
        switch (m_config.preset) {
            case VideoEncoderConfig::Preset::UltraFast:  presetName = "ultrafast"; break;
            case VideoEncoderConfig::Preset::SuperFast:  presetName = "superfast"; break;
            case VideoEncoderConfig::Preset::VeryFast:   presetName = "veryfast";  break;
            case VideoEncoderConfig::Preset::Faster:     presetName = "faster";    break;
            case VideoEncoderConfig::Preset::Fast:       presetName = "fast";      break;
            case VideoEncoderConfig::Preset::Medium:     presetName = "medium";    break;
            case VideoEncoderConfig::Preset::Slow:       presetName = "slow";      break;
            case VideoEncoderConfig::Preset::Slower:     presetName = "slower";    break;
            case VideoEncoderConfig::Preset::VerySlow:   presetName = "veryslow";  break;
        }
        av_opt_set(m_codecContext->priv_data, "preset", presetName, 0);

        // Set CRF (quality)
        int crf = 23; // default medium
        switch (m_config.quality) {
            case VideoEncoderConfig::Quality::Lossless:        crf = 0;  break;
            case VideoEncoderConfig::Quality::VisuallyLossless: crf = 15; break;
            case VideoEncoderConfig::Quality::High:             crf = 18; break;
            case VideoEncoderConfig::Quality::Medium:           crf = 23; break;
            case VideoEncoderConfig::Quality::Low:              crf = 28; break;
            case VideoEncoderConfig::Quality::VeryLow:          crf = 35; break;
        }
        av_opt_set_int(m_codecContext->priv_data, "crf", crf, 0);
    }

    // Global header if container requires it
    if (m_formatContext->oformat->flags & AVFMT_GLOBALHEADER) {
        m_codecContext->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    }

    // Open codec
    ret = avcodec_open2(m_codecContext, codec, nullptr);
    if (ret < 0) {
        m_lastError = QString("Failed to open codec: %1").arg(ret);
        cleanup();
        return false;
    }

    // Copy codec parameters to stream
    ret = avcodec_parameters_from_context(stream->codecpar, m_codecContext);
    if (ret < 0) {
        m_lastError = QString("Failed to copy codec params: %1").arg(ret);
        cleanup();
        return false;
    }

    stream->time_base = m_codecContext->time_base;

    // Open output file
    if (!(m_formatContext->oformat->flags & AVFMT_NOFILE)) {
        ret = avio_open(&m_formatContext->pb, outputPath.toUtf8().constData(),
                        AVIO_FLAG_WRITE);
        if (ret < 0) {
            m_lastError = QString("Failed to open output file: %1").arg(ret);
            cleanup();
            return false;
        }
    }

    // Write header
    ret = avformat_write_header(m_formatContext, nullptr);
    if (ret < 0) {
        m_lastError = QString("Failed to write header: %1").arg(ret);
        cleanup();
        return false;
    }

    // Allocate packet
    m_packet = av_packet_alloc();
    if (!m_packet) {
        m_lastError = "Failed to allocate packet";
        cleanup();
        return false;
    }

    m_isOpen = true;
    m_framesEncoded = 0;
    m_cancelled = false;

    return true;
#else
    m_lastError = "FFmpeg not available - video encoding disabled";
    Q_UNUSED(outputPath);
    return false;
#endif
}

// ============================================================================
// VideoEncoder::encodeFrame (QImage)
// ============================================================================
bool VideoEncoder::encodeFrame(const QImage& frame)
{
    if (!m_isOpen) {
        m_lastError = "Encoder not open";
        return false;
    }

    if (m_cancelled) {
        return false;
    }

#if HAS_FFMPEG
    // Create frame
    AVFrame* avFrame = createFrame(m_config.width, m_config.height);
    if (!avFrame) {
        m_lastError = "Failed to create frame";
        return false;
    }

    // Convert QImage to YUV frame
    QImage scaled = frame.scaled(m_config.width, m_config.height,
                                 Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    convertImageToFrame(scaled, avFrame);

    // Encode
    bool result = writeFrame(avFrame);

    av_frame_free(&avFrame);

    if (result) {
        m_framesEncoded++;

        // Report progress
        if (m_progressCallback) {
            VideoEncodeProgress progress;
            progress.encodedFrames = m_framesEncoded;
            progress.percentComplete = 0.0f; // caller should set total
            m_progressCallback(progress);
        }

        emit frameEncoded(m_framesEncoded);
    }

    return result;
#else
    Q_UNUSED(frame);
    m_lastError = "FFmpeg not available";
    return false;
#endif
}

// ============================================================================
// VideoEncoder::encodeFrameRGBA
// ============================================================================
bool VideoEncoder::encodeFrameRGBA(const uint8_t* rgbaData, int width, int height, int stride)
{
    QImage image(rgbaData, width, height, stride, QImage::Format_RGBA8888);
    return encodeFrame(image);
}

// ============================================================================
// VideoEncoder::close
// ============================================================================
bool VideoEncoder::close()
{
    if (!m_isOpen) {
        return true;
    }

    bool success = true;

#if HAS_FFMPEG
    if (m_codecContext && m_formatContext) {
        // Flush encoder
        AVFrame* flushFrame = nullptr;
        writeFrame(flushFrame);

        // Write trailer
        if (m_formatContext->pb) {
            av_write_trailer(m_formatContext);
        }
    }
#endif

    cleanup();
    m_isOpen = false;

    emit encodingFinished(success, QString("Encoded %1 frames").arg(m_framesEncoded));
    return success;
}

// ============================================================================
// VideoEncoder::cancel
// ============================================================================
void VideoEncoder::cancel()
{
    m_cancelled = true;
}

// ============================================================================
// Private: initEncoder
// ============================================================================
bool VideoEncoder::initEncoder()
{
#if HAS_FFMPEG
    // avcodec / avformat are automatically initialized in modern FFmpeg
    // but we call it explicitly for safety
    // av_register_all() is deprecated and no-op in FFmpeg 4.0+
    return true;
#else
    return false;
#endif
}

// ============================================================================
// Private: createFrame
// ============================================================================
AVFrame* VideoEncoder::createFrame(int width, int height)
{
#if HAS_FFMPEG
    AVFrame* frame = av_frame_alloc();
    if (!frame) return nullptr;

    frame->format = m_codecContext->pix_fmt;
    frame->width = width;
    frame->height = height;

    int ret = av_frame_get_buffer(frame, 32);
    if (ret < 0) {
        av_frame_free(&frame);
        return nullptr;
    }

    return frame;
#else
    Q_UNUSED(width);
    Q_UNUSED(height);
    return nullptr;
#endif
}

// ============================================================================
// Private: convertImageToFrame
// ============================================================================
void VideoEncoder::convertImageToFrame(const QImage& image, AVFrame* frame)
{
#if HAS_FFMPEG
    if (!frame || image.isNull()) return;

    // Ensure image is in RGB888 format
    QImage rgb = image.convertToFormat(QImage::Format_RGB888);

    // Create sws context for conversion
    SwsContext* sws = sws_getContext(
        rgb.width(), rgb.height(), AV_PIX_FMT_RGB24,
        frame->width, frame->height,
        static_cast<AVPixelFormat>(frame->format),
        SWS_BILINEAR, nullptr, nullptr, nullptr);

    if (!sws) return;

    // Convert
    uint8_t* srcData[4] = { rgb.bits(), nullptr, nullptr, nullptr };
    int srcLinesize[4] = { rgb.bytesPerLine(), 0, 0, 0 };

    sws_scale(sws, srcData, srcLinesize, 0, rgb.height(),
              frame->data, frame->linesize);

    sws_freeContext(sws);
#else
    Q_UNUSED(image);
    Q_UNUSED(frame);
#endif
}

// ============================================================================
// Private: writeFrame
// ============================================================================
bool VideoEncoder::writeFrame(AVFrame* frame)
{
#if HAS_FFMPEG
    if (!m_codecContext || !m_formatContext) return false;

    if (frame) {
        frame->pts = m_framesEncoded;
    }

    int ret = avcodec_send_frame(m_codecContext, frame);
    if (ret < 0) {
        m_lastError = QString("Error sending frame to encoder: %1").arg(ret);
        return false;
    }

    while (ret >= 0) {
        ret = avcodec_receive_packet(m_codecContext, m_packet);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
            break;
        }
        if (ret < 0) {
            m_lastError = QString("Error receiving packet: %1").arg(ret);
            return false;
        }

        av_packet_rescale_ts(m_packet, m_codecContext->time_base,
                             m_formatContext->streams[m_packet->stream_index]->time_base);

        ret = av_interleaved_write_frame(m_formatContext, m_packet);
        if (ret < 0) {
            m_lastError = QString("Error writing packet: %1").arg(ret);
            return false;
        }
    }

    return true;
#else
    Q_UNUSED(frame);
    return false;
#endif
}

// ============================================================================
// Private: cleanup
// ============================================================================
void VideoEncoder::cleanup()
{
#if HAS_FFMPEG
    if (m_packet) {
        av_packet_free(&m_packet);
    }
    if (m_codecContext) {
        avcodec_free_context(&m_codecContext);
    }
    if (m_formatContext) {
        if (!(m_formatContext->oformat->flags & AVFMT_NOFILE) && m_formatContext->pb) {
            avio_closep(&m_formatContext->pb);
        }
        avformat_free_context(m_formatContext);
        m_formatContext = nullptr;
    }
    if (m_swsContext) {
        sws_freeContext(m_swsContext);
        m_swsContext = nullptr;
    }
#endif
}

// ============================================================================
// VideoEncoderBatch
// ============================================================================
VideoEncoderBatch::VideoEncoderBatch(QObject* parent)
    : QObject(parent)
{
}

VideoEncoderBatch::~VideoEncoderBatch()
{
}

// ============================================================================
// VideoEncoderBatch::encodeImageSequence
// ============================================================================
bool VideoEncoderBatch::encodeImageSequence(const QString& inputDir,
                                            const QString& filePattern,
                                            const QString& outputPath,
                                            const VideoEncoderConfig& config,
                                            ProgressCallback progress)
{
    QDir dir(inputDir);
    if (!dir.exists()) {
        return false;
    }

    // Get all matching files
    QStringList filters;
    // Convert printf-style pattern to glob pattern
    QString globPattern = filePattern;
    globPattern.replace(QRegularExpression("%\\d*d"), "*");
    filters << globPattern;

    QStringList files = dir.entryList(filters, QDir::Files, QDir::Name);
    if (files.isEmpty()) {
        return false;
    }

    // Load all images
    QVector<QImage> frames;
    frames.reserve(files.size());
    for (const QString& file : files) {
        QImage img(dir.absoluteFilePath(file));
        if (!img.isNull()) {
            frames.append(img);
        }
    }

    if (frames.isEmpty()) {
        return false;
    }

    return encodeFrameList(frames, outputPath, config, progress);
}

// ============================================================================
// VideoEncoderBatch::encodeFrameList
// ============================================================================
bool VideoEncoderBatch::encodeFrameList(const QVector<QImage>& frames,
                                        const QString& outputPath,
                                        const VideoEncoderConfig& config,
                                        ProgressCallback progress)
{
    if (frames.isEmpty()) return false;

    VideoEncoder encoder;
    encoder.setConfig(config);

    if (!encoder.open(outputPath)) {
        return false;
    }

    int total = frames.size();
    if (progress) {
        VideoEncodeProgress p;
        p.totalFrames = total;
        progress(p);
    }

    for (int i = 0; i < total; ++i) {
        if (m_cancelled) {
            encoder.cancel();
            break;
        }

        if (!encoder.encodeFrame(frames[i])) {
            encoder.close();
            return false;
        }

        if (progress) {
            VideoEncodeProgress p;
            p.totalFrames = total;
            p.encodedFrames = i + 1;
            p.percentComplete = static_cast<float>(i + 1) / total * 100.0f;
            progress(p);
        }
    }

    bool ok = encoder.close();
    return ok;
}

// ============================================================================
// VideoEncoderBatch::encodeWithRenderCallback
// ============================================================================
bool VideoEncoderBatch::encodeWithRenderCallback(
    int totalFrames,
    std::function<QImage(int)> renderCallback,
    const QString& outputPath,
    const VideoEncoderConfig& config,
    ProgressCallback progress)
{
    if (!renderCallback || totalFrames <= 0) return false;

    VideoEncoder encoder;
    encoder.setConfig(config);

    if (!encoder.open(outputPath)) {
        return false;
    }

    QElapsedTimer timer;
    timer.start();

    if (progress) {
        VideoEncodeProgress p;
        p.totalFrames = totalFrames;
        progress(p);
    }

    for (int i = 0; i < totalFrames; ++i) {
        if (m_cancelled) {
            encoder.cancel();
            break;
        }

        QImage frame = renderCallback(i);
        if (frame.isNull()) {
            continue;
        }

        if (!encoder.encodeFrame(frame)) {
            encoder.close();
            return false;
        }

        if (progress) {
            double elapsed = timer.elapsed() / 1000.0;
            double speed = (i + 1) / qMax(elapsed, 0.001);

            VideoEncodeProgress p;
            p.totalFrames = totalFrames;
            p.encodedFrames = i + 1;
            p.percentComplete = static_cast<float>(i + 1) / totalFrames * 100.0f;
            p.encodingSpeed = speed;
            p.elapsedTime = elapsed;
            progress(p);
        }
    }

    bool ok = encoder.close();
    return ok;
}

} // namespace ks
