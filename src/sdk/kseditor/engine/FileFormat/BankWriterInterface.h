#pragma once
#include <QObject>
#include <QString>
#include "../Audio/AudioTypes.h"
#include "BankVersion.h"

namespace ks { namespace fileformat {

using ks::audio::KSAudioProject;
using ks::audio::SoundBank;

// ============================================================================
// IBankWriter — Abstract interface for version-specific bank writers
// ============================================================================

class IBankWriter : public QObject {
    Q_OBJECT
public:
    explicit IBankWriter(QObject* parent = nullptr) : QObject(parent) {}
    virtual ~IBankWriter() = default;

    // Write banks from project
    virtual bool writeProjectBanks(const KSAudioProject& project, const QString& outputDir,
                                   const QString& assetsDir = QString()) = 0;

    // Write single bank
    virtual bool writeBank(const SoundBank& bank, const QString& assetsDir,
                           const QString& outputPath) = 0;

    // Write GUIDs file
    virtual bool writeGUIDsFile(const KSAudioProject& project, const QString& outputPath) = 0;

    // Convert ksaudio to bank
    virtual bool convertKSAudioToBank(const QString& ksaudioPath, const QString& assetsDir,
                                      const QString& bankOutputPath) = 0;

    // Get last error
    virtual QString lastError() const = 0;

    // Get version this writer produces
    virtual BankVersion version() const = 0;

    // Get game target this writer is optimized for
    virtual GameTarget gameTarget() const = 0;

signals:
    void writeStarted(const QString& bankName);
    void writeProgress(int percent);
    void writeCompleted(const QString& bankPath);
    void writeFailed(const QString& error);
};

// ============================================================================
// BankWriterFactory — Creates appropriate writer for bank version/game
// ============================================================================

class BankWriterFactory : public QObject {
    Q_OBJECT
public:
    static BankWriterFactory& instance();

    void registerWriter(BankVersion version, IBankWriter* writer);
    void registerWriter(GameTarget target, IBankWriter* writer);

    IBankWriter* getWriter(BankVersion version);
    IBankWriter* getWriter(GameTarget target);

    void unregisterWriter(BankVersion version);
    void unregisterWriter(GameTarget target);

private:
    BankWriterFactory(QObject* parent = nullptr) : QObject(parent) {}
    QMap<BankVersion, IBankWriter*> m_versionWriters;
    QMap<GameTarget, IBankWriter*> m_gameWriters;
};

}} // namespace ks::fileformat