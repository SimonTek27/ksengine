#include "BankVersion.h"
#include <QDebug>

namespace ks { namespace fileformat {

// ============================================================================
// BankVersionManager implementation
// ============================================================================

BankVersionManager& BankVersionManager::instance() {
    static BankVersionManager inst;
    return inst;
}

BankVersionManager::BankVersionManager() {
    // ---------- Assetto Corsa 1 (AC1) ----------
    BankProfile ac1;
    ac1.game = GameTarget::AssettoCorsa1;
    ac1.version = BankVersion::FMOD_1_08;
    ac1.name = "Assetto Corsa 1";
    ac1.description = "Original AC (2014) — FMOD Studio 1.08.x, FEV2 format, XOR encryption";
    ac1.usesXorEncryption = true;
    ac1.xorKey = 0xAE12B3F4;
    ac1.hasStringTable = true;
    ac1.hasEventChunks = true;
    ac1.hasBusChunks = true;
    ac1.hasVcaChunks = true;
    ac1.hasSnapshotChunks = true;
    ac1.hasSoundChunks = true;
    ac1.usesFsb5 = true;
    ac1.expectedBankNames = {"sfx", "engine", "exhaust", "tyres", "transmission", "brakes", "body", "ambience", "ui"};
    m_profiles[GameTarget::AssettoCorsa1] = ac1;
    m_versionProfiles[BankVersion::FMOD_1_08] = ac1;

    // ---------- Assetto Corsa Competizione (ACC) ----------
    BankProfile acc;
    acc.game = GameTarget::AssettoCorsaCompetizione;
    acc.version = BankVersion::FMOD_2_02;
    acc.name = "Assetto Corsa Competizione";
    acc.description = "ACC (2019) — FMOD Studio 2.02.x, FEV2 format, categorized banks";
    acc.usesXorEncryption = false;
    acc.xorKey = 0;
    acc.hasStringTable = true;
    acc.hasEventChunks = true;
    acc.hasBusChunks = true;
    acc.hasVcaChunks = true;
    acc.hasSnapshotChunks = true;
    acc.hasSoundChunks = true;
    acc.usesFsb5 = true;
    acc.expectedBankNames = {
        "Master", "Engine", "Exhaust", "Transmission", "Tyres", "Brakes", "Suspension",
        "Body", "Aero", "Wind", "Ambience", "UI", "Dialogue", "Crowd", "Replay"
    };
    m_profiles[GameTarget::AssettoCorsaCompetizione] = acc;
    m_versionProfiles[BankVersion::FMOD_2_02] = acc;

    // ---------- Assetto Corsa Rally (ACR) ----------
    BankProfile acr;
    acr.game = GameTarget::AssettoCorsaRally;
    acr.version = BankVersion::FMOD_2_02;
    acr.name = "Assetto Corsa Rally";
    acr.description = "ACR (2025) — FMOD Studio 2.02.x, FEV2 format, rally-specific categories";
    acr.usesXorEncryption = false;
    acr.xorKey = 0;
    acr.hasStringTable = true;
    acr.hasEventChunks = true;
    acr.hasBusChunks = true;
    acr.hasVcaChunks = true;
    acr.hasSnapshotChunks = true;
    acr.hasSoundChunks = true;
    acr.usesFsb5 = true;
    acr.expectedBankNames = {
        "Master", "EngExh", "Transmission", "Tyres", "Brakes", "Suspension",
        "Body", "Environment", "Dialogue", "Crowd", "UI"
    };
    m_profiles[GameTarget::AssettoCorsaRally] = acr;
    m_versionProfiles[BankVersion::FMOD_2_02] = acr;

    // ---------- Assetto Corsa EVO (ACE) ----------
    BankProfile ace;
    ace.game = GameTarget::AssettoCorsaEVO;
    ace.version = BankVersion::FMOD_2_02;
    ace.name = "Assetto Corsa EVO";
    ace.description = "ACE (2025) — Protobuf packages, FMOD version TBD";
    ace.usesXorEncryption = false;
    ace.xorKey = 0;
    ace.hasStringTable = true;
    ace.hasEventChunks = true;
    ace.hasBusChunks = true;
    ace.hasVcaChunks = true;
    ace.hasSnapshotChunks = true;
    ace.hasSoundChunks = true;
    ace.usesFsb5 = true;
    ace.expectedBankNames = {};
    m_profiles[GameTarget::AssettoCorsaEVO] = ace;
    m_versionProfiles[BankVersion::FMOD_2_02] = ace;
}

BankVersion BankVersionManager::detectVersion(const QByteArray& data) {
    if (data.size() < 8) return BankVersion::Unknown;

    quint32 magic = 0;
    memcpy(&magic, data.constData(), 4);

    if (magic != 0x46455632) return BankVersion::Unknown;

    if (data.size() < 12) return BankVersion::FMOD_1_08;

    quint32 version = 0;
    memcpy(&version, data.constData() + 4, 4);

    switch (version) {
        case 0x00010800: return BankVersion::FMOD_1_08;
        case 0x00010A00: return BankVersion::FMOD_1_10;
        case 0x00010C00: return BankVersion::FMOD_1_12;
        case 0x00020000: return BankVersion::FMOD_2_00;
        case 0x00020100: return BankVersion::FMOD_2_01;
        case 0x00020200: return BankVersion::FMOD_2_02;
        case 0x00020300: return BankVersion::FMOD_2_03;
        case 0x00020A00: return BankVersion::FMOD_2_10;
        case 0x00030000: return BankVersion::FMOD_3_00;
        default:
            quint16 major = (version >> 16) & 0xFFFF;
            if (major == 1) return BankVersion::FMOD_1_08;
            if (major == 2) return BankVersion::FMOD_2_02;
            if (major >= 3) return BankVersion::FMOD_3_00;
            return BankVersion::Unknown;
    }
}

GameTarget BankVersionManager::detectGameTarget(const QByteArray& data, const QString& bankName) {
    BankVersion version = detectVersion(data);

    if (!bankName.isEmpty()) {
        QString lower = bankName.toLower();

        if (lower.contains("sfx") || lower.contains("engine") || lower.contains("exhaust") ||
            lower.contains("tyre") || lower.contains("transmission") || lower.contains("brake") ||
            lower.contains("body") || lower.contains("ambience")) {
            if (version == BankVersion::FMOD_1_08 || version == BankVersion::FMOD_1_10 || version == BankVersion::FMOD_1_12) {
                return GameTarget::AssettoCorsa1;
            }
        }

        if (lower.contains("eng exh") || lower.contains("engexh") ||
            lower.contains("master") || lower.contains("dialogue") ||
            lower.contains("crowd") || lower.contains("replay")) {
            if (isFmod2xPlus(version)) {
                if (lower.contains("crowd") || lower.contains("dialogue") || lower.contains("replay")) {
                    return GameTarget::AssettoCorsaCompetizione;
                }
                if (lower.contains("engexh") || lower.contains("environment")) {
                    return GameTarget::AssettoCorsaRally;
                }
                return GameTarget::AssettoCorsaCompetizione;
            }
        }
    }

    if (isFmod1x(version)) return GameTarget::AssettoCorsa1;
    if (isFmod2xPlus(version)) return GameTarget::AssettoCorsaCompetizione;

    return GameTarget::AutoDetect;
}

const BankProfile& BankVersionManager::getProfile(GameTarget target) const {
    static BankProfile defaultProfile;
    auto it = m_profiles.find(target);
    if (it != m_profiles.end()) return it.value();
    return defaultProfile;
}

const BankProfile& BankVersionManager::getProfileForVersion(BankVersion version) const {
    static BankProfile defaultProfile;
    auto it = m_versionProfiles.find(version);
    if (it != m_versionProfiles.end()) return it.value();
    return defaultProfile;
}

void BankVersionManager::registerProfile(GameTarget target, const BankProfile& profile) {
    m_profiles[target] = profile;
    m_versionProfiles[profile.version] = profile;
}

QMap<GameTarget, BankProfile> BankVersionManager::getAllProfiles() const {
    return m_profiles;
}

QString BankVersionManager::versionToString(BankVersion version) {
    switch (version) {
        case BankVersion::FMOD_1_08: return "FMOD 1.08.x";
        case BankVersion::FMOD_1_10: return "FMOD 1.10.x";
        case BankVersion::FMOD_1_12: return "FMOD 1.12.x";
        case BankVersion::FMOD_2_00: return "FMOD 2.00.x";
        case BankVersion::FMOD_2_01: return "FMOD 2.01.x";
        case BankVersion::FMOD_2_02: return "FMOD 2.02.x";
        case BankVersion::FMOD_2_03: return "FMOD 2.03.x";
        case BankVersion::FMOD_2_10: return "FMOD 2.10.x";
        case BankVersion::FMOD_3_00: return "FMOD 3.00.x";
        default: return "Unknown";
    }
}

QString BankVersionManager::gameTargetToString(GameTarget target) {
    switch (target) {
        case GameTarget::AssettoCorsa1: return "Assetto Corsa 1";
        case GameTarget::AssettoCorsaCompetizione: return "Assetto Corsa Competizione";
        case GameTarget::AssettoCorsaRally: return "Assetto Corsa Rally";
        case GameTarget::AssettoCorsaEVO: return "Assetto Corsa EVO";
        default: return "Auto-detect";
    }
}

bool BankVersionManager::isFmod1x(BankVersion version) {
    quint32 v = static_cast<quint32>(version);
    return (v & 0xFFFF0000) == 0x00010000;
}

bool BankVersionManager::isFmod2xPlus(BankVersion version) {
    quint32 v = static_cast<quint32>(version);
    return (v & 0xFFFF0000) >= 0x00020000;
}

}} // namespace ks::fileformat