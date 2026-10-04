#include "AudioCore.h"
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#ifdef Q_OS_WIN
#include <windows.h>
#endif

// Studio lives in Studio.cpp

namespace ks { namespace audio {

// ============================================================================
// KSAudioCore - singleton audio engine manager
// ============================================================================

KSAudioCore* KSAudioCore::s_instance = nullptr;
QMutex KSAudioCore::s_mutex;

KSAudioCore::KSAudioCore(QObject* parent) : QObject(parent) {}

KSAudioCore::~KSAudioCore() { shutdown(); }

KSAudioCore* KSAudioCore::instance() {
    QMutexLocker locker(&s_mutex);
    if (!s_instance) {
        s_instance = new KSAudioCore();
    }
    return s_instance;
}

bool KSAudioCore::initialize(const QString& simContentPath) {
    if (m_initialized) {
        qWarning() << "KSAudioCore: Already initialized";
        return false;
    }

    m_simContentPath = simContentPath;
    qInfo() << "KSAudioCore: Initializing ksAudioStudio engine for:" << simContentPath;

    if (!initSystem()) {
        emit errorOccurred("Failed to initialize ksAudioStudio engine");
        emit initialized(false);
        return false;
    }

    m_initialized = true;
    m_version = "2.0.0";
    emit initialized(true);
    qInfo() << "KSAudioCore: Initialized successfully, version:" << m_version;
    return true;
}

void KSAudioCore::shutdown() {
    if (!m_initialized) return;
    qInfo() << "KSAudioCore: Shutting down ksAudioStudio engine...";
    cleanupSystem();
    m_initialized = false;
    emit initialized(false);
}

bool KSAudioCore::initSystem() {
    QAudioFormat fmt;
    fmt.setSampleRate(48000);
    fmt.setChannelCount(2);
    fmt.setSampleFormat(QAudioFormat::Float);

    m_studio = new Studio(this);
    if (!m_studio->openOutput(fmt)) {
        qCritical() << "KSAudioCore: Failed to open audio output";
        return false;
    }

    connect(m_studio, &Studio::error, this, [this](const QString& msg) {
        emit errorOccurred(msg);
    });

    qInfo() << "KSAudioCore: ksAudioStudio system ready (Qt Multimedia, 48kHz stereo float)";
    return true;
}

void KSAudioCore::cleanupSystem() {
    if (m_studio) {
        m_studio->closeOutput();
        m_studio->closeInput();
        m_studio->deleteLater();
        m_studio = nullptr;
    }
}

float KSAudioCore::getCPUUsage() const {
    static ULARGE_INTEGER s_lastIdleTime = {};
    static ULARGE_INTEGER s_lastKernelTime = {};
    static ULARGE_INTEGER s_lastUserTime = {};

    FILETIME idleTime, kernelTime, userTime;
    if (!GetSystemTimes(&idleTime, &kernelTime, &userTime)) {
        return 0.0f;
    }

    auto toULL = [](const FILETIME& ft) -> ULARGE_INTEGER {
        ULARGE_INTEGER u;
        u.LowPart = ft.dwLowDateTime;
        u.HighPart = ft.dwHighDateTime;
        return u;
    };

    ULARGE_INTEGER idle = toULL(idleTime);
    ULARGE_INTEGER kernel = toULL(kernelTime);
    ULARGE_INTEGER user = toULL(userTime);

    ULARGE_INTEGER sysIdleDiff;
    sysIdleDiff.QuadPart = idle.QuadPart - s_lastIdleTime.QuadPart;
    ULARGE_INTEGER sysKernelDiff;
    sysKernelDiff.QuadPart = kernel.QuadPart - s_lastKernelTime.QuadPart;
    ULARGE_INTEGER sysUserDiff;
    sysUserDiff.QuadPart = user.QuadPart - s_lastUserTime.QuadPart;

    s_lastIdleTime = idle;
    s_lastKernelTime = kernel;
    s_lastUserTime = user;

    ULARGE_INTEGER sysTotal;
    sysTotal.QuadPart = sysKernelDiff.QuadPart + sysUserDiff.QuadPart;
    if (sysTotal.QuadPart == 0) return 0.0f;

    return static_cast<float>(sysTotal.QuadPart - sysIdleDiff.QuadPart) / static_cast<float>(sysTotal.QuadPart);
}

}}