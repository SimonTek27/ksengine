#include "KN5Decrypt.h"
#include <QDataStream>
#include <QFileInfo>
#include <QDir>
#include <QTextStream>
#include <QDebug>
#include <cstring>

// ============================================================================
// KN5Decrypt implementation
// ============================================================================

QString KN5Decrypt::m_lastError;

// CSP encryption envelope marker
static const QByteArray CSP_ENVELOPE_MARKER("__AC_SHADERS_PATCH_KN5ENC_v1__", 31);

KN5Decrypt::DecryptedKN5 KN5Decrypt::decrypt(const QString& kn5Path, QString* error) {
    DecryptedKN5 result;

    QFile file(kn5Path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = "Cannot open KN5 file: " + kn5Path;
        if (error) *error = m_lastError;
        return result;
    }

    QByteArray fileData = file.readAll();
    file.close();

    // Check for CSP encryption envelope
    int envelopeOffset = 0;
    int envelopeLength = 0;

    if (!findEncryptedEnvelope(fileData, envelopeOffset, envelopeLength)) {
        // Not CSP-protected, return as-is
        result.kn5Data = fileData;
        result.isValid = true;
        result.fullRebuild = false;
        return result;
    }

    result.isValid = true;

    // Extract the body (everything before the envelope)
    QByteArray bodyData = fileData.left(envelopeOffset);

    // Parse encrypted envelope
    QByteArray envelopeData = fileData.mid(envelopeOffset, envelopeLength);

    // Parse envelope header
    int encVersion = 0;
    int saltOffset = 0;
    if (!parseEncryptedHeader(envelopeData, encVersion, saltOffset)) {
        m_lastError = "Failed to parse CSP envelope header";
        if (error) *error = m_lastError;
        result.isValid = false;
        return result;
    }

    // Extract salt for key derivation
    QByteArray salt = envelopeData.mid(saltOffset, 32);

    // Derive decryption key from folder name and salt
    QString folderName = QFileInfo(kn5Path).dir().dirName();
    QByteArray key = deriveKey(salt, folderName);

    // For version 1, we can do a full rebuild
    if (encVersion == 1) {
        // Extract encrypted textures and vertex masks
        int dataOffset = saltOffset + 32;

        // Parse texture count
        if (dataOffset + 4 > envelopeData.size()) {
            m_lastError = "Invalid envelope data";
            if (error) *error = m_lastError;
            result.isValid = false;
            return result;
        }

        quint32 textureCount = 0;
        memcpy(&textureCount, envelopeData.constData() + dataOffset, 4);
        dataOffset += 4;

        // Extract encrypted textures
        QVector<QByteArray> textures;
        for (quint32 i = 0; i < textureCount && dataOffset < envelopeData.size(); ++i) {
            quint32 texSize = 0;
            memcpy(&texSize, envelopeData.constData() + dataOffset, 4);
            dataOffset += 4;

            if (texSize > 0 && dataOffset + texSize <= envelopeData.size()) {
                QByteArray encTex = envelopeData.mid(dataOffset, texSize);
                dataOffset += texSize;

                // Decrypt texture
                QByteArray decTex = encTex;
                for (int j = 0; j < decTex.size(); ++j) {
                    int keyByte = (int)(unsigned char)key[j % key.size()];
                    int val = (int)(unsigned char)decTex[j];
                    val = ((val - keyByte) % 256 + 256) % 256;
                    decTex[j] = (char)val;
                }
                textures.append(decTex);
            }
        }

        // Extract vertex masks
        QVector<QByteArray> vertexMasks;
        if (dataOffset + 4 <= envelopeData.size()) {
            quint32 maskCount = 0;
            memcpy(&maskCount, envelopeData.constData() + dataOffset, 4);
            dataOffset += 4;

            for (quint32 i = 0; i < maskCount && dataOffset < envelopeData.size(); ++i) {
                quint32 maskSize = 0;
                memcpy(&maskSize, envelopeData.constData() + dataOffset, 4);
                dataOffset += 4;

                if (maskSize > 0 && dataOffset + maskSize <= envelopeData.size()) {
                    QByteArray encMask = envelopeData.mid(dataOffset, maskSize);
                    dataOffset += maskSize;

                    // Decrypt mask
                    QByteArray decMask = encMask;
                    for (int j = 0; j < decMask.size(); ++j) {
                        int keyByte = (int)(unsigned char)key[j % key.size()];
                        int val = (int)(unsigned char)decMask[j];
                        val = ((val - keyByte) % 256 + 256) % 256;
                        decMask[j] = (char)val;
                    }
                    vertexMasks.append(decMask);
                }
            }
        }

        // Try to rebuild KN5 with decrypted data
        QByteArray rebuiltKN5;
        if (rebuildKN5(bodyData, textures, vertexMasks, rebuiltKN5)) {
            result.kn5Data = rebuiltKN5;
            result.textures = textures;
            result.vertexMasks = vertexMasks;
            result.fullRebuild = true;
        } else {
            // Fallback to body-only
            result.kn5Data = bodyData;
            result.textures = textures;
            result.vertexMasks = vertexMasks;
            result.fullRebuild = false;
        }
    }

    return result;
}

KN5Decrypt::DecryptedKN5 KN5Decrypt::decryptToFolder(const QString& kn5Path, const QString& outputDir, QString* error) {
    DecryptedKN5 result = decrypt(kn5Path, error);
    if (!result.isValid) return result;

    QDir dir(outputDir);
    if (!dir.exists()) dir.mkpath(".");

    // Save decrypted KN5
    QString outPath = outputDir + "/" + QFileInfo(kn5Path).fileName() + ".decrypted.kn5";
    QFile outFile(outPath);
    if (outFile.open(QIODevice::WriteOnly)) {
        outFile.write(result.kn5Data);
        outFile.close();
    }

    // Save textures
    for (int i = 0; i < result.textures.size(); ++i) {
        QString texPath = outputDir + "/texture_" + QString::number(i) + ".dds";
        QFile texFile(texPath);
        if (texFile.open(QIODevice::WriteOnly)) {
            texFile.write(result.textures[i]);
            texFile.close();
        }
    }

    // Save vertex masks
    for (int i = 0; i < result.vertexMasks.size(); ++i) {
        QString maskPath = outputDir + "/vertexmask_" + QString::number(i) + ".bin";
        QFile maskFile(maskPath);
        if (maskFile.open(QIODevice::WriteOnly)) {
            maskFile.write(result.vertexMasks[i]);
            maskFile.close();
        }
    }

    return result;
}

bool KN5Decrypt::unprotect(const QString& kn5Path, QString* error) {
    QFile file(kn5Path);
    if (!file.open(QIODevice::ReadOnly)) {
        m_lastError = "Cannot open KN5 file: " + kn5Path;
        if (error) *error = m_lastError;
        return false;
    }

    QByteArray fileData = file.readAll();
    file.close();

    // Real KN5 magic: 6 ASCII bytes "sc6969"
    if (fileData.size() < 10 || memcmp(fileData.constData(), "sc6969", 6) != 0) {
        m_lastError = "Not a valid KN5 file";
        if (error) *error = m_lastError;
        return false;
    }

    // The only protection this class handles is the CSP kn5enc envelope;
    // decrypt() strips it. There is no in-header "unpack flag" in the real
    // format — an earlier implementation that cleared bit 0x02 of the u32
    // at offset 8 actually corrupted textureCount/materialCount.
    if (fileData.contains(CSP_ENVELOPE_MARKER)) {
        m_lastError = "KN5 is CSP-protected (__AC_SHADERS_PATCH_KN5ENC_v1__); "
                      "use decrypt() instead";
        if (error) *error = m_lastError;
        return false;
    }

    // Plain KN5: nothing to remove.
    return true;
}

bool KN5Decrypt::isCSPProtected(const QString& kn5Path) {
    QFile file(kn5Path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray header = file.peek(1024);
    file.close();

    return header.contains(CSP_ENVELOPE_MARKER);
}

bool KN5Decrypt::isKN5Unprotectable(const QString& kn5Path) {
    QFile file(kn5Path);
    if (!file.open(QIODevice::ReadOnly)) return false;

    QByteArray header = file.peek(1024);
    file.close();

    // Only the CSP kn5enc envelope is removable (via decrypt()). The earlier
    // implementation inspected a nonexistent "protection flag" at offset 8.
    return header.contains(CSP_ENVELOPE_MARKER);
}

bool KN5Decrypt::findEncryptedEnvelope(const QByteArray& data, int& offset, int& length) {
    int pos = data.indexOf(CSP_ENVELOPE_MARKER);
    if (pos == -1) return false;

    offset = pos;
    length = data.size() - pos;
    return true;
}

bool KN5Decrypt::parseEncryptedHeader(const QByteArray& headerData, int& encVersion, int& saltOffset) {
    if (headerData.size() < 64) return false;

    // Find version string after marker
    int markerEnd = headerData.indexOf(CSP_ENVELOPE_MARKER) + CSP_ENVELOPE_MARKER.size();
    if (markerEnd >= headerData.size()) return false;

    // Parse version number
    QByteArray versionStr;
    for (int i = markerEnd; i < headerData.size() && i < markerEnd + 16; ++i) {
        char c = headerData[i];
        if (c >= '0' && c <= '9') {
            versionStr.append(c);
        } else if (!versionStr.isEmpty()) {
            break;
        }
    }

    encVersion = versionStr.toInt();
    saltOffset = markerEnd + versionStr.size() + 1; // +1 for delimiter

    return encVersion > 0;
}

QByteArray KN5Decrypt::deriveKey(const QByteArray& salt, const QString& folderName) {
    // Simple key derivation: XOR salt with folder name hash
    QByteArray key(32, 0);

    QByteArray folderBytes = folderName.toLower().toUtf8();
    int folderLen = folderBytes.size();

    for (int i = 0; i < 32; ++i) {
        char saltByte = (i < salt.size()) ? salt[i] : 0;
        char folderByte = (i < folderLen) ? folderBytes[i] : 0;
        key[i] = saltByte ^ folderByte;
    }

    return key;
}

bool KN5Decrypt::decryptTextures(const QByteArray& encryptedTextures, QVector<QByteArray>& textures, const QByteArray& key) {
    // Simple XOR decryption for textures
    QByteArray decrypted = encryptedTextures;
    for (int i = 0; i < decrypted.size(); ++i) {
        int keyByte = (int)(unsigned char)key[i % key.size()];
        int val = (int)(unsigned char)decrypted[i];
        val = ((val - keyByte) % 256 + 256) % 256;
        decrypted[i] = (char)val;
    }

    // Split into individual textures by DDS magic
    int pos = 0;
    const QByteArray ddsMagic("DDS ", 4);
    while (pos < decrypted.size() - 4) {
        int found = decrypted.indexOf(ddsMagic, pos);
        if (found < 0) break;

        int nextFound = decrypted.indexOf(ddsMagic, found + 4);
        int endPos = (nextFound > 0) ? nextFound : decrypted.size();

        QByteArray texture = decrypted.mid(found, endPos - found);
        if (texture.size() > 128) {
            textures.append(texture);
        }
        pos = endPos;
    }

    if (textures.isEmpty() && !decrypted.isEmpty()) {
        textures.append(decrypted);
    }

    return true;
}

bool KN5Decrypt::decryptVertexMasks(const QByteArray& encryptedMasks, QVector<QByteArray>& masks, const QByteArray& key) {
    // Simple XOR decryption for vertex masks
    QByteArray decrypted = encryptedMasks;
    for (int i = 0; i < decrypted.size(); ++i) {
        int keyByte = (int)(unsigned char)key[i % key.size()];
        int val = (int)(unsigned char)decrypted[i];
        val = ((val - keyByte) % 256 + 256) % 256;
        decrypted[i] = (char)val;
    }

    masks.append(decrypted);
    return true;
}

bool KN5Decrypt::rebuildKN5(const QByteArray& originalBody, const QVector<QByteArray>& textures,
                            const QVector<QByteArray>& vertexMasks, QByteArray& result) {
    // For version 1, try to reconstruct the full KN5
    // This is a simplified version - full implementation would need to
    // properly integrate textures and vertex masks into the KN5 structure

    result = originalBody;

    // Append decrypted textures as a separate section
    if (!textures.isEmpty()) {
        QByteArray texSection;
        texSection.append("DECRYPTED_TEXTURES:", 19);
        texSection.append(QByteArray::number(textures.size()));
        texSection.append(":", 1);

        for (const QByteArray& tex : textures) {
            texSection.append(QByteArray::number(tex.size()));
            texSection.append(":", 1);
            texSection.append(tex);
        }

        result.append(texSection);
    }

    return true;
}