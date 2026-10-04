#pragma once
#include <QString>
#include <QMap>

namespace ks { namespace fileformat {

// ============================================================================
// BankVersion — FMOD bank format versioning for different Kunos games
// ============================================================================

enum class BankVersion {
    Unknown = 0,

    // FMOD Studio 1.x (FEV2 format) — Assetto Corsa 1 (AC1)
    FMOD_1_08 = 0x00010800,  // 1.08.x — AC1 default
    FMOD_1_10 = 0x00010A00,  // 1.10.x
    FMOD_1_12 = 0x00010C00,  // 1.12.x

    // FMOD Studio 2.x (FEV2 format but different chunk layout) — ACC, ACR, ACE
    FMOD_2_00 = 0x00020000,  // 2.00.x
    FMOD_2_01 = 0x00020100,  // 2.01.x
    FMOD_2_02 = 0x00020200,  // 2.02.x — ACC/ACR typical
    FMOD_2_03 = 0x00020300,  // 2.03.x
    FMOD_2_10 = 0x00020A00,  // 2.10.x

    // FMOD Studio 3.x (future-proofing)
    FMOD_3_00 = 0x00030000,
};

// Game-specific bank profiles
enum class GameTarget {
    AutoDetect = 0,
    AssettoCorsa1,      // AC1 — FMOD 1.08.x, FEV2, XOR encryption
    AssettoCorsaCompetizione, // ACC — FMOD 2.02.x, FEV2, different chunks
    AssettoCorsaRally,  // ACR — FMOD 2.02.x, FEV2, categorized banks
    AssettoCorsaEVO     // ACE — Unknown (protobuf-based, may use FMOD)
};

struct BankProfile {
    GameTarget game = GameTarget::AutoDetect;
    BankVersion version = BankVersion::FMOD_1_08;
    QString name;
    QString description;
    bool usesXorEncryption = true;
    quint32 xorKey = 0xAE12B3F4;  // AC1 default
    bool hasStringTable = true;
    bool hasEventChunks = true;
    bool hasBusChunks = true;
    bool hasVcaChunks = true;
    bool hasSnapshotChunks = true;
    bool hasSoundChunks = true;
    bool usesFsb5 = true;
    QStringList expectedBankNames;  // e.g. {"sfx", "engine", "tyres", "dialogue"}
};

// Version detection and profile management
class BankVersionManager {
public:
    static BankVersionManager& instance();

    // Detect version from raw bank data
    static BankVersion detectVersion(const QByteArray& data);
    static GameTarget detectGameTarget(const QByteArray& data, const QString& bankName = QString());

    // Get profile for a game target
    const BankProfile& getProfile(GameTarget target) const;
    const BankProfile& getProfileForVersion(BankVersion version) const;

    // Register custom profiles (for mods/unknown games)
    void registerProfile(GameTarget target, const BankProfile& profile);

    // Get all known profiles
    QMap<GameTarget, BankProfile> getAllProfiles() const;

    // Convert version to human-readable string
    static QString versionToString(BankVersion version);
    static QString gameTargetToString(GameTarget target);

    // Check if version is FMOD 1.x or 2.x+
    static bool isFmod1x(BankVersion version);
    static bool isFmod2xPlus(BankVersion version);

private:
    BankVersionManager();
    QMap<GameTarget, BankProfile> m_profiles;
    QMap<BankVersion, BankProfile> m_versionProfiles;
};

}} // namespace ks::fileformat