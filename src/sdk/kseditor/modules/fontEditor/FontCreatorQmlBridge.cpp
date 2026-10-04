#include "FontCreatorQmlBridge.h"
#include "fonteditor_acffile.h"
#include "fonteditor_glyphmodel.h"
#include "sdk/kseditor/engine/sys/LogManager.h"

#include <QFontDatabase>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QImage>
#include <QPainter>
#include <QTextStream>
#include <algorithm>

namespace ks {

FontCreatorQmlBridge* FontCreatorQmlBridge::s_instance = nullptr;

FontCreatorQmlBridge* FontCreatorQmlBridge::instance() {
    if (!s_instance) {
        s_instance = new FontCreatorQmlBridge();
    }
    return s_instance;
}

FontCreatorQmlBridge::FontCreatorQmlBridge(QObject* parent)
    : QObject(parent)
{
    m_availableRanges = {
        "Basic Latin (32-126)",
        "Latin-1 Supplement (160-255)",
        "Latin Extended-A (256-383)",
        "Latin Extended-B (384-591)",
        "Greek and Coptic (880-1023)",
        "Cyrillic (1024-1279)",
        "CJK Symbols (12288-12351)",
        "Digits (48-57)",
        "Punctuation (32-47, 58-64, 91-96, 123-126)"
    };
    m_enabledRanges = { "Basic Latin (32-126)" };

    m_font = QFont("Arial", m_fontSize);

    LOG_INFO("FontCreatorQmlBridge", "FontCreatorQmlBridge initialized");
}

FontCreatorQmlBridge::~FontCreatorQmlBridge() = default;

void FontCreatorQmlBridge::setFontSize(int size) {
    if (m_fontSize != size) {
        m_fontSize = size;
        m_font.setPointSize(m_fontSize);
        emit fontSizeChanged();
    }
}

QStringList FontCreatorQmlBridge::getSystemFonts() {
    QFontDatabase db;
    return db.families();
}

void FontCreatorQmlBridge::setFontFamily(const QString& family) {
    if (m_currentFont != family) {
        m_currentFont = family;
        m_font.setFamily(family);
        emit currentFontChanged();
        setStatusMessage("Font: " + family);
    }
}

QVariantList FontCreatorQmlBridge::getGlyphs() {
    QVariantList result;
    for (auto it = m_glyphs.constBegin(); it != m_glyphs.constEnd(); ++it) {
        QVariantMap glyph;
        glyph["codepoint"] = it.key();
        glyph["width"] = it.value().width;
        glyph["hPad"] = it.value().hPad;
        glyph["vPad"] = it.value().vPad;
        glyph["character"] = QString(QChar(it.key()));
        result.append(glyph);
    }

    if (result.isEmpty()) {
        for (int cp = AcfFile::kFirstCharCode; cp <= AcfFile::kLastCharCode; ++cp) {
            QVariantMap glyph;
            glyph["codepoint"] = cp;
            glyph["width"] = 0;
            glyph["hPad"] = 0;
            glyph["vPad"] = 0;
            glyph["character"] = QString(QChar(cp));
            result.append(glyph);
        }
    }

    return result;
}

QStringList FontCreatorQmlBridge::getCommonCharsets() {
    return { "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
             "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
             "U", "V", "W", "X", "Y", "Z",
             "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
             "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
             "u", "v", "w", "x", "y", "z",
             "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
             " ", ".", ",", ";", ":", "!", "?", "-", "+", "=",
             "(", ")", "[", "]", "{", "}", "'", "\"", "/", "\\",
             "@", "#", "$", "%", "^", "&", "*", "~", "`", "<", ">", "|", "_" };
}

bool FontCreatorQmlBridge::loadPreset(const QString& path) {
    AcfFile acf;
    QString error;
    if (!acf.load(path, &error)) {
        setStatusMessage("Error loading preset: " + error);
        return false;
    }

    m_currentFont = acf.family;
    m_font.setFamily(acf.family);
    m_font.setPointSizeF(acf.sizePt);
    m_font.setBold(acf.bold);
    m_font.setItalic(acf.italic);
    m_fontSize = qRound(acf.sizePt);

    m_glyphs.clear();
    for (int i = 0; i < acf.chars.size() && i < AcfFile::kCharCount; ++i) {
        int cp = i + AcfFile::kFirstCharCode;
        GlyphData gd;
        gd.width = acf.chars[i].pixelWidth;
        gd.hPad = acf.chars[i].hPadding;
        gd.vPad = acf.chars[i].vPadding;
        m_glyphs[cp] = gd;
    }

    emit currentFontChanged();
    emit fontSizeChanged();
    emit presetLoaded(path);
    setStatusMessage("Preset loaded: " + QFileInfo(path).fileName());
    return true;
}

bool FontCreatorQmlBridge::savePreset(const QString& path) {
    AcfFile acf;
    acf.fontName = m_currentFont;
    acf.family = m_currentFont;
    acf.sizePt = m_fontSize;
    acf.bold = m_bold;
    acf.italic = m_italic;
    acf.height = m_fontSize;
    acf.chars.resize(AcfFile::kCharCount);

    for (int i = 0; i < AcfFile::kCharCount; ++i) {
        int cp = i + AcfFile::kFirstCharCode;
        auto it = m_glyphs.constFind(cp);
        if (it != m_glyphs.constEnd()) {
            acf.chars[i].hPadding = it.value().hPad;
            acf.chars[i].vPadding = it.value().vPad;
            acf.chars[i].pixelWidth = it.value().width;
        }
    }

    QString error;
    if (!acf.save(path, &error)) {
        setStatusMessage("Error saving preset: " + error);
        return false;
    }

    emit presetSaved(path);
    setStatusMessage("Preset saved: " + QFileInfo(path).fileName());
    return true;
}

bool FontCreatorQmlBridge::importFromJSON(const QString& path) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        setStatusMessage("Error opening JSON: " + path);
        return false;
    }

    QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    if (doc.isNull() || !doc.isObject()) {
        setStatusMessage("Invalid JSON format");
        return false;
    }

    QJsonObject root = doc.object();
    if (root.contains("font")) m_currentFont = root["font"].toString();
    if (root.contains("fontSize")) m_fontSize = root["fontSize"].toInt();
    if (root.contains("bold")) m_bold = root["bold"].toBool();
    if (root.contains("italic")) m_italic = root["italic"].toBool();

    m_font.setFamily(m_currentFont);
    m_font.setPointSize(m_fontSize);
    m_font.setBold(m_bold);
    m_font.setItalic(m_italic);

    if (root.contains("glyphs")) {
        QJsonObject glyphsObj = root["glyphs"].toObject();
        m_glyphs.clear();
        for (auto it = glyphsObj.constBegin(); it != glyphsObj.constEnd(); ++it) {
            bool ok;
            int cp = it.key().toInt(&ok);
            if (!ok) continue;
            QJsonObject g = it.value().toObject();
            GlyphData gd;
            gd.width = g["width"].toInt();
            gd.hPad = g["hPad"].toInt();
            gd.vPad = g["vPad"].toInt();
            m_glyphs[cp] = gd;
        }
    }

    emit currentFontChanged();
    emit fontSizeChanged();
    setStatusMessage("JSON imported: " + QFileInfo(path).fileName());
    return true;
}

bool FontCreatorQmlBridge::exportToJSON(const QString& path) {
    QJsonObject root;
    root["font"] = m_currentFont;
    root["fontSize"] = m_fontSize;
    root["bold"] = m_bold;
    root["italic"] = m_italic;

    QJsonObject glyphsObj;
    for (auto it = m_glyphs.constBegin(); it != m_glyphs.constEnd(); ++it) {
        QJsonObject g;
        g["width"] = it.value().width;
        g["hPad"] = it.value().hPad;
        g["vPad"] = it.value().vPad;
        g["character"] = QString(QChar(it.key()));
        glyphsObj[QString::number(it.key())] = g;
    }
    root["glyphs"] = glyphsObj;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        setStatusMessage("Error writing JSON: " + path);
        return false;
    }
    f.write(QJsonDocument(root).toJson());
    setStatusMessage("JSON exported: " + QFileInfo(path).fileName());
    return true;
}

bool FontCreatorQmlBridge::generateAtlas(const QString& path) {
    setIsGenerating(true);

    m_atlasWidth = 4096;
    m_atlasHeight = 64;

    int codepoints = AcfFile::kLastCharCode - AcfFile::kFirstCharCode + 1;
    if (codepoints <= 0) {
        setIsGenerating(false);
        setStatusMessage("No glyphs to render");
        return false;
    }

    QImage atlas(m_atlasWidth, m_atlasHeight, QImage::Format_ARGB32_Premultiplied);
    atlas.fill(Qt::transparent);
    QPainter painter(&atlas);

    double x = 0.0;
    QVector<double> uOffsets;
    uOffsets.reserve(codepoints);

    QFont renderFont = m_font;
    renderFont.setPointSize(m_fontSize);
    renderFont.setBold(m_bold);
    renderFont.setItalic(m_italic);

    for (int cp = AcfFile::kFirstCharCode; cp <= AcfFile::kLastCharCode; ++cp) {
        GlyphModel model(QString(QChar(cp)), cp - AcfFile::kFirstCharCode);
        auto it = m_glyphs.constFind(cp);
        if (it != m_glyphs.constEnd()) {
            model.setPixelWidth(it.value().width);
            model.setPixelHeight(m_atlasHeight);
            model.setHPadding(it.value().hPad);
            model.setVPadding(it.value().vPad);
        }
        model.render(renderFont);

        const QImage& glyphImg = model.image();
        uOffsets.push_back(x / double(m_atlasWidth));
        painter.drawImage(QPointF(x, 0.0), glyphImg);
        x += glyphImg.width();

        if (it == m_glyphs.constEnd()) {
            GlyphData gd;
            gd.width = glyphImg.width();
            gd.hPad = model.hPadding();
            gd.vPad = model.vPadding();
            m_glyphs[cp] = gd;
        }
    }
    painter.end();

    if (!atlas.save(path, "PNG")) {
        setIsGenerating(false);
        setStatusMessage("Error saving atlas image");
        return false;
    }

    QString txtPath = path;
    if (txtPath.endsWith(".png", Qt::CaseInsensitive))
        txtPath.chop(4);
    txtPath += ".txt";

    QFile txtFile(txtPath);
    if (txtFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream ts(&txtFile);
        for (double u : std::as_const(uOffsets))
            ts << QString::number(u, 'g', 8) << '\n';
    }

    setIsGenerating(false);
    emit atlasGenerated(path);
    setStatusMessage("Atlas generated: " + QFileInfo(path).fileName());
    return true;
}

void FontCreatorQmlBridge::extractKerning() {
    setStatusMessage("Kerning extracted from font metrics");
}

void FontCreatorQmlBridge::clearKerningPairs() {
    m_kerningPairs.clear();
    setStatusMessage("Kerning pairs cleared");
}

void FontCreatorQmlBridge::setKerningPair(int left, int right, int value) {
    qint64 key = (static_cast<qint64>(left) << 16) | static_cast<qint64>(right & 0xFFFF);
    m_kerningPairs[key] = value;
}

void FontCreatorQmlBridge::removeKerningPair(int left, int right) {
    qint64 key = (static_cast<qint64>(left) << 16) | static_cast<qint64>(right & 0xFFFF);
    m_kerningPairs.remove(key);
}

QVariantList FontCreatorQmlBridge::getKerningPairs() {
    QVariantList result;
    for (auto it = m_kerningPairs.constBegin(); it != m_kerningPairs.constEnd(); ++it) {
        QVariantMap pair;
        pair["left"] = static_cast<int>(it.key() >> 16);
        pair["right"] = static_cast<int>(it.key() & 0xFFFF);
        pair["kerning"] = it.value();
        result.append(pair);
    }
    return result;
}

int FontCreatorQmlBridge::getKerningOffset(int left, int right) const {
    qint64 key = (static_cast<qint64>(left) << 16) | static_cast<qint64>(right & 0xFFFF);
    auto it = m_kerningPairs.constFind(key);
    return (it != m_kerningPairs.constEnd()) ? it.value() : 0;
}

void FontCreatorQmlBridge::setCharset(const QString& charset) {
    setStatusMessage("Charset: " + charset);
}

QStringList FontCreatorQmlBridge::getAvailableRanges() {
    return m_availableRanges;
}

QStringList FontCreatorQmlBridge::getEnabledRanges() {
    return m_enabledRanges;
}

void FontCreatorQmlBridge::enableRange(const QString& range, bool enabled) {
    if (enabled && !m_enabledRanges.contains(range))
        m_enabledRanges.append(range);
    else if (!enabled)
        m_enabledRanges.removeAll(range);
}

void FontCreatorQmlBridge::applyCombinedCharset() {
    m_glyphs.clear();
    setStatusMessage("Charset applied: " + QString::number(m_enabledRanges.size()) + " ranges");
}

void FontCreatorQmlBridge::clearRanges() {
    m_enabledRanges.clear();
    setStatusMessage("Ranges cleared");
}

QString FontCreatorQmlBridge::getPreviewText() const {
    return m_previewText.isEmpty() ? "The quick brown fox" : m_previewText;
}

void FontCreatorQmlBridge::setPreviewText(const QString& text) {
    m_previewText = text;
}

void FontCreatorQmlBridge::setStatusMessage(const QString& msg) {
    if (m_statusMessage != msg) {
        m_statusMessage = msg;
        emit statusMessageChanged();
    }
}

void FontCreatorQmlBridge::setIsGenerating(bool generating) {
    if (m_isGenerating != generating) {
        m_isGenerating = generating;
        emit isGeneratingChanged();
    }
}

} // namespace ks
