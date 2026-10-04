#include "TexturePaintSystem.h"
#include <QPainter>
#include <QPainterPath>
#include <QtMath>
#include <QRadialGradient>
#include <QLinearGradient>
#include <QRandomGenerator>
#include <algorithm>

namespace ks {

TexturePaintSystem* TexturePaintSystem::s_instance = nullptr;

TexturePaintSystem::TexturePaintSystem(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
    addLayer("Base");
}

TexturePaintSystem::~TexturePaintSystem()
{
    s_instance = nullptr;
}

TexturePaintSystem* TexturePaintSystem::instance()
{
    if (!s_instance)
        s_instance = new TexturePaintSystem();
    return s_instance;
}

int TexturePaintSystem::addLayer(const QString& name)
{
    saveUndoState();
    PaintLayer layer;
    layer.name = name.isEmpty()
        ? QString("Layer %1").arg(m_layers.size() + 1)
        : name;
    layer.texture = QImage(m_canvasSize, QImage::Format_ARGB32_Premultiplied);
    layer.texture.fill(Qt::transparent);
    m_layers.append(layer);
    if (m_currentLayer < 0)
        m_currentLayer = 0;
    emit layerAdded(m_layers.size() - 1);
    return m_layers.size() - 1;
}

bool TexturePaintSystem::removeLayer(int index)
{
    if (index < 0 || index >= m_layers.size() || m_layers.size() <= 1)
        return false;
    saveUndoState();
    m_layers.removeAt(index);
    if (m_currentLayer >= m_layers.size())
        m_currentLayer = m_layers.size() - 1;
    if (m_currentLayer < 0)
        m_currentLayer = 0;
    emit layerRemoved(index);
    return true;
}

bool TexturePaintSystem::moveLayer(int from, int to)
{
    if (from < 0 || from >= m_layers.size() || to < 0 || to >= m_layers.size())
        return false;
    if (from == to) return true;
    saveUndoState();
    PaintLayer layer = m_layers.takeAt(from);
    m_layers.insert(to, layer);
    if (m_currentLayer == from)
        m_currentLayer = to;
    emit layerOrderChanged();
    return true;
}

void TexturePaintSystem::setLayerOpacity(int index, float opacity)
{
    if (index >= 0 && index < m_layers.size()) {
        m_layers[index].opacity = qBound(0.0f, opacity, 1.0f);
        emit layerChanged(index);
    }
}

void TexturePaintSystem::setLayerVisible(int index, bool visible)
{
    if (index >= 0 && index < m_layers.size()) {
        m_layers[index].visible = visible;
        emit layerChanged(index);
    }
}

void TexturePaintSystem::setLayerLocked(int index, bool locked)
{
    if (index >= 0 && index < m_layers.size())
        m_layers[index].locked = locked;
}

void TexturePaintSystem::setLayerBlendMode(int index, PaintLayer::BlendMode mode)
{
    if (index >= 0 && index < m_layers.size()) {
        m_layers[index].blendMode = mode;
        emit layerChanged(index);
    }
}

PaintLayer* TexturePaintSystem::layer(int index)
{
    if (index >= 0 && index < m_layers.size())
        return &m_layers[index];
    return nullptr;
}

const PaintLayer* TexturePaintSystem::layer(int index) const
{
    if (index >= 0 && index < m_layers.size())
        return &m_layers[index];
    return nullptr;
}

void TexturePaintSystem::setCurrentLayer(int index)
{
    if (index >= 0 && index < m_layers.size())
        m_currentLayer = index;
}

QImage TexturePaintSystem::compositeAll() const
{
    QImage result(m_canvasSize, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

    for (const auto& layer : m_layers) {
        if (!layer.visible)
            continue;

        QImage blended = layer.texture;
        if (!qFuzzyCompare(layer.opacity, 1.0f)) {
            QImage temp = blended;
            QPainter p(&temp);
            p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
            p.fillRect(temp.rect(), QColor(0, 0, 0, static_cast<int>(layer.opacity * 255)));
            p.end();
            blended = temp;
        }

        painter.drawImage(0, 0, blended);
    }

    painter.end();
    return result;
}

void TexturePaintSystem::setCanvasSize(int width, int height)
{
    width = qMax(32, width);
    height = qMax(32, height);
    if (width == m_canvasSize.width() && height == m_canvasSize.height())
        return;
    resizeCanvas(width, height);
}

void TexturePaintSystem::clearCanvas(const QColor& color)
{
    saveUndoState();
    for (auto& layer : m_layers)
        layer.texture.fill(color);
    emit canvasChanged();
}

void TexturePaintSystem::resizeCanvas(int width, int height, int anchorX, int anchorY)
{
    saveUndoState();
    width = qMax(32, width);
    height = qMax(32, height);
    QSize newSize(width, height);
    for (auto& layer : m_layers) {
        QImage newImage(newSize, QImage::Format_ARGB32_Premultiplied);
        newImage.fill(Qt::transparent);
        QPainter p(&newImage);
        p.drawImage(anchorX, anchorY, layer.texture);
        p.end();
        layer.texture = std::move(newImage);
    }
    m_canvasSize = newSize;
    emit canvasChanged();
}

void TexturePaintSystem::beginStroke(const QPoint& pos)
{
    if (m_currentLayer < 0 || m_currentLayer >= m_layers.size())
        return;
    if (m_layers[m_currentLayer].locked)
        return;

    saveUndoState();

    m_strokeActive = true;
    m_lastStrokePos = pos;
    m_accumulatedDist = 0.0f;

    paintPoint(pos);
}

void TexturePaintSystem::addStrokePoint(const QPoint& pos)
{
    if (!m_strokeActive) return;
    if (m_currentLayer < 0) return;

    float dist = QLineF(m_lastStrokePos, pos).length();
    m_accumulatedDist += dist;

    float spacing = qMax(0.5f, m_brush.size * m_brush.spacing);
    QPointF dir = QPointF(pos - m_lastStrokePos);

    if (m_brush.type == PaintBrush::Clone && !m_cloneImage.isNull()) {
        float steps = qMax(1.0f, dist / spacing);
        for (float t = 0; t <= 1.0f; t += 1.0f / steps) {
            QPointF p = QPointF(m_lastStrokePos) + dir * t;
            QPointF src = QPointF(m_cloneSource) + dir * t;
            QPoint srcPt = src.toPoint();
            if (!m_cloneImage.rect().contains(srcPt)) continue;
            QRect srcRect(srcPt.x() - 1, srcPt.y() - 1, 3, 3);
            QRect dstRect(p.toPoint().x() - 1, p.toPoint().y() - 1, 3, 3);
            QPainter cp(&m_layers[m_currentLayer].texture);
            cp.drawImage(dstRect, m_cloneImage, srcRect);
            cp.end();
        }
    } else {
        if (m_accumulatedDist >= spacing) {
            paintPoint(pos);
            m_accumulatedDist = 0.0f;
        }
    }

    m_lastStrokePos = pos;
}

void TexturePaintSystem::endStroke()
{
    m_strokeActive = false;
    emit strokeCompleted();
}

void TexturePaintSystem::paintPoint(const QPoint& pos)
{
    if (m_currentLayer < 0) return;
    PaintLayer& layer = m_layers[m_currentLayer];

    QPainter painter(&layer.texture);
    painter.setRenderHint(QPainter::Antialiasing, true);

    float radius = m_brush.size * 0.5f;
    QColor brushColor = m_brush.color;

    if (m_brush.jitter > 0.0f) {
        float jitterOffset = m_brush.jitter * radius * 2.0f;
    }

    QRadialGradient gradient(pos, radius);
    gradient.setColorAt(0.0, QColor(brushColor.red(), brushColor.green(), brushColor.blue(),
                                    static_cast<int>(brushColor.alpha() * m_brush.strength)));
    gradient.setColorAt(m_brush.hardness,
        QColor(brushColor.red(), brushColor.green(), brushColor.blue(),
               static_cast<int>(brushColor.alpha() * m_brush.strength * 0.5f)));
    gradient.setColorAt(1.0, QColor(0, 0, 0, 0));

    painter.setBrush(QBrush(gradient));
    painter.setPen(Qt::NoPen);

    switch (m_brush.type) {
    case PaintBrush::Square:
        painter.drawRect(QRectF(pos.x() - radius, pos.y() - radius, radius * 2, radius * 2));
        break;
    case PaintBrush::Eraser: {
        QPainter::CompositionMode cm = painter.compositionMode();
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.drawEllipse(QPointF(pos), static_cast<qreal>(radius), static_cast<qreal>(radius));
        painter.setCompositionMode(cm);
        break;
    }
    case PaintBrush::Circle:
    case PaintBrush::Soft:
    default:
        painter.drawEllipse(QPointF(pos), static_cast<qreal>(radius), static_cast<qreal>(radius));
        break;
    }

    painter.end();
    emit canvasChanged();
}

void TexturePaintSystem::applyStamp(const QPoint& pos, const QImage& stamp)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    PaintLayer& layer = m_layers[m_currentLayer];

    QPainter painter(&layer.texture);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.drawImage(pos - QPoint(stamp.width() / 2, stamp.height() / 2), stamp);
    painter.end();
    emit canvasChanged();
}

void TexturePaintSystem::applyStencil(const Stencil& stencil)
{
    if (stencil.mask.isNull()) return;
    if (m_currentLayer < 0) return;
    saveUndoState();
    PaintLayer& layer = m_layers[m_currentLayer];

    QImage stencilImage = stencil.mask;
    if (stencil.invert)
        stencilImage.invertPixels();

    QPainter painter(&layer.texture);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    painter.setOpacity(stencil.opacity);

    switch (stencil.wrapMode) {
    case 1: { // Surface mode: project along surface normal
        QTransform transform;
        transform.translate(stencil.position.x(), stencil.position.y());
        transform.rotate(stencil.rotation);
        transform.scale(stencil.scale, stencil.scale);
        // Apply perspective-like scaling based on normal direction
        float normalFactor = qAbs(QVector3D::dotProduct(stencil.normal.normalized(), QVector3D(0, 0, 1)));
        normalFactor = qMax(0.3f, normalFactor); // clamp to avoid extreme scaling
        transform.scale(1.0f / normalFactor, 1.0f / normalFactor);
        painter.setTransform(transform);
        painter.drawImage(QPoint(0, 0), stencilImage);
        break;
    }
    case 2: { // Cylindrical mode: wrap around an axis
        QTransform transform;
        transform.translate(stencil.position.x(), stencil.position.y());
        transform.rotate(stencil.rotation);
        // Apply cylindrical distortion by scaling X based on Y position
        float cylRadius = 100.0f; // virtual cylinder radius in pixels
        float stencilWidth = stencilImage.width() * stencil.scale;
        float stencilHeight = stencilImage.height() * stencil.scale;
        // Draw column by column with X scaling based on Y position
        for (int col = 0; col < stencilImage.width(); ++col) {
            float u = float(col) / float(stencilImage.width());
            float angle = u * 3.14159265f; // half-cylinder wrap
            float xScale = std::cos(angle);
            QImage column = stencilImage.copy(col, 0, 1, stencilImage.height());
            float destX = stencil.position.x() + (col - stencilImage.width() / 2.0f) * stencil.scale;
            float destY = stencil.position.y();
            float destW = stencil.scale * xScale;
            float destH = stencilHeight;
            painter.drawImage(QRectF(destX, destY, destW, destH), column);
        }
        break;
    }
    default: { // Flat mode: simple 2D projection
        QTransform transform;
        transform.translate(stencil.position.x(), stencil.position.y());
        transform.rotate(stencil.rotation);
        transform.scale(stencil.scale, stencil.scale);
        painter.setTransform(transform);
        painter.drawImage(QPoint(0, 0), stencilImage);
        break;
    }
    }

    painter.resetTransform();
    painter.end();
    emit canvasChanged();
}

void TexturePaintSystem::floodFill(const QPoint& pos, const QColor& color, float tolerance)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    PaintLayer& layer = m_layers[m_currentLayer];
    QImage& img = layer.texture;

    if (!img.rect().contains(pos)) return;

    QRgb targetColor = img.pixel(pos);
    int tol = static_cast<int>(tolerance * 255.0f);

    std::vector<QPoint> stack;
    stack.push_back(pos);

    while (!stack.empty()) {
        QPoint p = stack.back();
        stack.pop_back();

        if (!img.rect().contains(p)) continue;
        QRgb curr = img.pixel(p);

        int dr = qAbs(qRed(curr) - qRed(targetColor));
        int dg = qAbs(qGreen(curr) - qGreen(targetColor));
        int db = qAbs(qBlue(curr) - qBlue(targetColor));
        int da = qAbs(qAlpha(curr) - qAlpha(targetColor));
        if (dr > tol || dg > tol || db > tol || da > tol) continue;
        if (img.pixel(p) == color.rgba()) continue;

        img.setPixel(p, color.rgba());

        stack.push_back(QPoint(p.x() + 1, p.y()));
        stack.push_back(QPoint(p.x() - 1, p.y()));
        stack.push_back(QPoint(p.x(), p.y() + 1));
        stack.push_back(QPoint(p.x(), p.y() - 1));
    }

    emit canvasChanged();
}

void TexturePaintSystem::gradientFill(const QPoint& from, const QPoint& to,
                                       const QColor& startColor, const QColor& endColor,
                                       bool radial)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    PaintLayer& layer = m_layers[m_currentLayer];

    QPainter painter(&layer.texture);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setPen(Qt::NoPen);

    QRect fillRect = hasSelection() ? m_selection : layer.texture.rect();
    painter.setClipRect(fillRect);

    if (radial) {
        float radius = QLineF(from, to).length();
        QRadialGradient grad(from, radius);
        grad.setColorAt(0.0, startColor);
        grad.setColorAt(1.0, endColor);
        painter.setBrush(grad);
        painter.drawRect(fillRect);
    } else {
        QLinearGradient grad(from, to);
        grad.setColorAt(0.0, startColor);
        grad.setColorAt(1.0, endColor);
        painter.setBrush(grad);
        painter.drawRect(fillRect);
    }

    painter.end();
    emit canvasChanged();
}


void TexturePaintSystem::applyBlur(const QRect& region, float radius)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    QImage img = m_layers[m_currentLayer].texture.copy(region);
    if (img.isNull()) return;

    int kernelSize = qMax(3, static_cast<int>(radius * 2 + 1));
    if (kernelSize % 2 == 0) kernelSize++;
    int half = kernelSize / 2;

    QImage result(img.size(), QImage::Format_ARGB32_Premultiplied);

    for (int y = 0; y < img.height(); ++y) {
        for (int x = 0; x < img.width(); ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            int count = 0;
            for (int ky = -half; ky <= half; ++ky) {
                for (int kx = -half; kx <= half; ++kx) {
                    int sx = qBound(0, x + kx, img.width() - 1);
                    int sy = qBound(0, y + ky, img.height() - 1);
                    QRgb p = img.pixel(sx, sy);
                    r += qRed(p);
                    g += qGreen(p);
                    b += qBlue(p);
                    a += qAlpha(p);
                    count++;
                }
            }
            result.setPixel(x, y, qRgba(
                static_cast<int>(r / count),
                static_cast<int>(g / count),
                static_cast<int>(b / count),
                static_cast<int>(a / count)));
        }
    }

    QPainter p(&m_layers[m_currentLayer].texture);
    p.drawImage(region.topLeft(), result);
    p.end();
    emit canvasChanged();
}

void TexturePaintSystem::applySharpen(const QRect& region, float amount)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    QImage img = m_layers[m_currentLayer].texture.copy(region);
    if (img.isNull()) return;

    float kernel[3][3] = {
        { 0, -1,  0},
        {-1,  5, -1},
        { 0, -1,  0}
    };

    QImage result(img.size(), QImage::Format_ARGB32_Premultiplied);

    for (int y = 1; y < img.height() - 1; ++y) {
        for (int x = 1; x < img.width() - 1; ++x) {
            float r = 0, g = 0, b = 0, a = 0;
            for (int ky = -1; ky <= 1; ++ky) {
                for (int kx = -1; kx <= 1; ++kx) {
                    QRgb p = img.pixel(x + kx, y + ky);
                    r += qRed(p) * kernel[ky + 1][kx + 1];
                    g += qGreen(p) * kernel[ky + 1][kx + 1];
                    b += qBlue(p) * kernel[ky + 1][kx + 1];
                    a += qAlpha(p) * kernel[ky + 1][kx + 1];
                }
            }
            QRgb orig = img.pixel(x, y);
            float blend = amount;
            r = qRed(orig) + (r - qRed(orig)) * blend;
            g = qGreen(orig) + (g - qGreen(orig)) * blend;
            b = qBlue(orig) + (b - qBlue(orig)) * blend;
            a = qAlpha(orig);
            result.setPixel(x, y, qRgba(
                qBound(0, static_cast<int>(r), 255),
                qBound(0, static_cast<int>(g), 255),
                qBound(0, static_cast<int>(b), 255),
                qBound(0, static_cast<int>(a), 255)));
        }
    }

    QPainter p(&m_layers[m_currentLayer].texture);
    p.drawImage(region.topLeft(), result);
    p.end();
    emit canvasChanged();
}

void TexturePaintSystem::applyNoise(const QRect& region, float amount)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    QImage img = m_layers[m_currentLayer].texture;
    QRect r = region.isNull() ? img.rect() : region;

    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            int noise = QRandomGenerator::global()->bounded(
                static_cast<int>(amount * 255));
            if (QRandomGenerator::global()->bounded(2))
                noise = -noise;
            QRgb p = img.pixel(x, y);
            img.setPixel(x, y, qRgba(
                qBound(0, qRed(p) + noise, 255),
                qBound(0, qGreen(p) + noise, 255),
                qBound(0, qBlue(p) + noise, 255),
                qAlpha(p)));
        }
    }
    emit canvasChanged();
}

void TexturePaintSystem::applyEmboss(const QRect& region, float strength)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    QImage img = m_layers[m_currentLayer].texture.copy(region);
    if (img.isNull()) return;

    QImage result(img.size(), QImage::Format_ARGB32_Premultiplied);

    for (int y = 1; y < img.height() - 1; ++y) {
        for (int x = 1; x < img.width() - 1; ++x) {
            int grayTL = qGray(img.pixel(x - 1, y - 1));
            int grayBR = qGray(img.pixel(x + 1, y + 1));
            int diff = static_cast<int>((grayTL - grayBR) * strength);
            int val = qBound(0, 128 + diff, 255);
            result.setPixel(x, y, qRgba(val, val, val, qAlpha(img.pixel(x, y))));
        }
    }

    QPainter p(&m_layers[m_currentLayer].texture);
    p.drawImage(region.topLeft(), result);
    p.end();
    emit canvasChanged();
}

void TexturePaintSystem::applyInvert(const QRect& region)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    QImage& img = m_layers[m_currentLayer].texture;
    QRect r = region.isNull() ? img.rect() : region;

    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            QRgb p = img.pixel(x, y);
            img.setPixel(x, y, qRgba(
                255 - qRed(p), 255 - qGreen(p), 255 - qBlue(p), qAlpha(p)));
        }
    }
    emit canvasChanged();
}

void TexturePaintSystem::applyLevels(const QRect& region, float black, float gamma, float white)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    QImage& img = m_layers[m_currentLayer].texture;
    QRect r = region.isNull() ? img.rect() : region;

    black = qBound(0.0f, black, 1.0f);
    white = qBound(0.0f, white, 1.0f);
    gamma = qMax(0.01f, gamma);
    float invGamma = 1.0f / gamma;

    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            QRgb p = img.pixel(x, y);
            float rv = qRed(p) / 255.0f;
            float gv = qGreen(p) / 255.0f;
            float bv = qBlue(p) / 255.0f;

            auto map = [&](float v) {
                v = (v - black) / (white - black);
                v = qPow(qBound(0.0f, v, 1.0f), invGamma);
                return qBound(0, static_cast<int>(v * 255.0f), 255);
            };

            img.setPixel(x, y, qRgba(map(rv), map(gv), map(bv), qAlpha(p)));
        }
    }
    emit canvasChanged();
}

void TexturePaintSystem::applyHueSaturation(const QRect& region,
                                             float hueShift, float saturation, float lightness)
{
    if (m_currentLayer < 0) return;
    saveUndoState();
    QImage& img = m_layers[m_currentLayer].texture;
    QRect r = region.isNull() ? img.rect() : region;

    for (int y = r.top(); y <= r.bottom(); ++y) {
        for (int x = r.left(); x <= r.right(); ++x) {
            QColor c = img.pixelColor(x, y);
            int h, s, v, a;
            c.getHsv(&h, &s, &v, &a);

            h = (h + static_cast<int>(hueShift * 360)) % 360;
            s = qBound(0, static_cast<int>(s * (1.0f + saturation)), 255);
            v = qBound(0, static_cast<int>(v * (1.0f + lightness)), 255);

            QColor adjusted;
            adjusted.setHsv(h, s, v, a);
            img.setPixelColor(x, y, adjusted);
        }
    }
    emit canvasChanged();
}


void TexturePaintSystem::saveUndoState()
{
    // Truncate any entries beyond current index (discard redo branch)
    if (m_undoIndex < m_undoStack.size() - 1)
        m_undoStack.resize(m_undoIndex + 1);

    UndoEntry entry;
    entry.layers = m_layers;
    entry.currentLayer = m_currentLayer;
    entry.canvasSize = m_canvasSize;
    m_undoStack.append(entry);

    // Enforce max undo steps
    while (m_undoStack.size() > kMaxUndoSteps)
        m_undoStack.removeFirst();

    m_undoIndex = m_undoStack.size() - 1;
    emit undoStackChanged();
}

void TexturePaintSystem::restoreUndoState(const UndoEntry& entry)
{
    m_layers = entry.layers;
    m_currentLayer = entry.currentLayer;
    m_canvasSize = entry.canvasSize;
    emit canvasChanged();
    emit layerOrderChanged();
    emit undoStackChanged();
}

void TexturePaintSystem::undo()
{
    if (!canUndo()) return;
    m_undoIndex--;
    restoreUndoState(m_undoStack[m_undoIndex]);
}

void TexturePaintSystem::redo()
{
    if (!canRedo()) return;
    m_undoIndex++;
    restoreUndoState(m_undoStack[m_undoIndex]);
}

void TexturePaintSystem::clearUndoStack()
{
    m_undoStack.clear();
    m_undoIndex = -1;
    emit undoStackChanged();
}

void TexturePaintSystem::projectStroke(
    const QVector<QVector3D>& meshVertices,
    const QVector<QVector2D>& uvCoords,
    const QVector<int>& faceIndices,
    const QVector3D& cameraPos,
    const QPoint& mousePos)
{
    if (meshVertices.isEmpty() || uvCoords.isEmpty() || faceIndices.isEmpty() || m_currentLayer < 0)
        return;

    // Build ray from camera through mouse position (assumes -Z forward)
    QVector3D rayDir(mousePos.x() - cameraPos.x(), mousePos.y() - cameraPos.y(), -cameraPos.z());
    rayDir.normalize();

    // Ray-triangle intersection for all faces
    float closestT = std::numeric_limits<float>::max();
    QVector2D hitUV;
    bool hit = false;

    auto rayIntersectsTriangle = [](const QVector3D& orig, const QVector3D& dir,
                                     const QVector3D& v0, const QVector3D& v1, const QVector3D& v2,
                                     float& t, float& u, float& v) -> bool {
        const float EPSILON = 1e-8f;
        QVector3D e1 = v1 - v0;
        QVector3D e2 = v2 - v0;
        QVector3D pv = QVector3D::crossProduct(dir, e2);
        float det = QVector3D::dotProduct(e1, pv);
        if (qAbs(det) < EPSILON) return false;
        float invDet = 1.0f / det;
        QVector3D tv = orig - v0;
        u = QVector3D::dotProduct(tv, pv) * invDet;
        if (u < 0.0f || u > 1.0f) return false;
        QVector3D qv = QVector3D::crossProduct(tv, e1);
        v = QVector3D::dotProduct(dir, qv) * invDet;
        if (v < 0.0f || u + v > 1.0f) return false;
        t = QVector3D::dotProduct(e2, qv) * invDet;
        return t > EPSILON;
    };

    for (int i = 0; i + 2 < faceIndices.size(); i += 3) {
        int i0 = faceIndices[i], i1 = faceIndices[i + 1], i2 = faceIndices[i + 2];
        if (i0 >= meshVertices.size() || i1 >= meshVertices.size() || i2 >= meshVertices.size())
            continue;

        float t, u, v;
        if (rayIntersectsTriangle(cameraPos, rayDir,
                                  meshVertices[i0], meshVertices[i1], meshVertices[i2],
                                  t, u, v)) {
            if (t < closestT) {
                closestT = t;
                // Barycentric UV interpolation
                if (i0 < uvCoords.size() && i1 < uvCoords.size() && i2 < uvCoords.size()) {
                    hitUV = uvCoords[i0] * (1.0f - u - v) + uvCoords[i1] * u + uvCoords[i2] * v;
                }
                hit = true;
            }
        }
    }

    if (hit) {
        // Convert UV to pixel coordinates on the active layer's texture
        float px = hitUV.x() * m_canvasSize.width();
        float py = (1.0f - hitUV.y()) * m_canvasSize.height();
        paintPoint(QPoint(static_cast<int>(px), static_cast<int>(py)));
    }
}

QColor TexturePaintSystem::blendColors(const QColor& base, const QColor& brush,
                                        float alpha, PaintLayer::BlendMode mode) const
{
    switch (mode) {
    case PaintLayer::Normal:    return blendNormal(base, brush, alpha);
    case PaintLayer::Multiply:  return blendMultiply(base, brush, alpha);
    case PaintLayer::Screen:    return blendScreen(base, brush, alpha);
    case PaintLayer::Overlay:   return blendOverlay(base, brush, alpha);
    case PaintLayer::Add:       return blendAdd(base, brush, alpha);
    case PaintLayer::Subtract:  return blendSubtract(base, brush, alpha);
    case PaintLayer::Lighten:
        return QColor(qMax(base.red(), brush.red()),
                      qMax(base.green(), brush.green()),
                      qMax(base.blue(), brush.blue()),
                      static_cast<int>(base.alpha() * (1 - alpha) + brush.alpha() * alpha));
    case PaintLayer::Darken:
        return QColor(qMin(base.red(), brush.red()),
                      qMin(base.green(), brush.green()),
                      qMin(base.blue(), brush.blue()),
                      static_cast<int>(base.alpha() * (1 - alpha) + brush.alpha() * alpha));
    case PaintLayer::AlphaBlend:
        return QColor(brush.red(), brush.green(), brush.blue(),
                      static_cast<int>(brush.alpha() * alpha));
    }
    return base;
}

QColor TexturePaintSystem::blendNormal(const QColor& base, const QColor& brush, float alpha) const
{
    float a = alpha * brush.alphaF();
    float invA = 1.0f - a;
    return QColor(
        static_cast<int>(base.red() * invA + brush.red() * a),
        static_cast<int>(base.green() * invA + brush.green() * a),
        static_cast<int>(base.blue() * invA + brush.blue() * a),
        static_cast<int>(base.alpha() * invA + 255 * a));
}

QColor TexturePaintSystem::blendMultiply(const QColor& base, const QColor& brush, float alpha) const
{
    float a = alpha * brush.alphaF();
    float invA = 1.0f - a;
    return QColor(
        static_cast<int>((base.redF() * brush.redF()) * 255 * a + base.red() * invA),
        static_cast<int>((base.greenF() * brush.greenF()) * 255 * a + base.green() * invA),
        static_cast<int>((base.blueF() * brush.blueF()) * 255 * a + base.blue() * invA),
        qMin(255, static_cast<int>(base.alpha() * invA + 255 * a)));
}

QColor TexturePaintSystem::blendScreen(const QColor& base, const QColor& brush, float alpha) const
{
    float a = alpha * brush.alphaF();
    float invA = 1.0f - a;
    auto screen = [](float b, float c) { return 1.0f - (1.0f - b) * (1.0f - c); };
    return QColor(
        static_cast<int>((screen(base.redF(), brush.redF()) * 255 * a + base.red() * invA)),
        static_cast<int>((screen(base.greenF(), brush.greenF()) * 255 * a + base.green() * invA)),
        static_cast<int>((screen(base.blueF(), brush.blueF()) * 255 * a + base.blue() * invA)),
        qMin(255, static_cast<int>(base.alpha() * invA + 255 * a)));
}

QColor TexturePaintSystem::blendOverlay(const QColor& base, const QColor& brush, float alpha) const
{
    float a = alpha * brush.alphaF();
    float invA = 1.0f - a;
    auto overlay = [](float b, float c) {
        if (b < 0.5f)
            return 2.0f * b * c;
        return 1.0f - 2.0f * (1.0f - b) * (1.0f - c);
    };
    return QColor(
        static_cast<int>((overlay(base.redF(), brush.redF()) * 255 * a + base.red() * invA)),
        static_cast<int>((overlay(base.greenF(), brush.greenF()) * 255 * a + base.green() * invA)),
        static_cast<int>((overlay(base.blueF(), brush.blueF()) * 255 * a + base.blue() * invA)),
        qMin(255, static_cast<int>(base.alpha() * invA + 255 * a)));
}

QColor TexturePaintSystem::blendAdd(const QColor& base, const QColor& brush, float alpha) const
{
    float a = alpha * brush.alphaF();
    return QColor(
        qMin(255, static_cast<int>(base.red() + brush.red() * a)),
        qMin(255, static_cast<int>(base.green() + brush.green() * a)),
        qMin(255, static_cast<int>(base.blue() + brush.blue() * a)),
        qMin(255, static_cast<int>(base.alpha() + 255 * a)));
}

QColor TexturePaintSystem::blendSubtract(const QColor& base, const QColor& brush, float alpha) const
{
    float a = alpha * brush.alphaF();
    return QColor(
        qMax(0, static_cast<int>(base.red() - brush.red() * a)),
        qMax(0, static_cast<int>(base.green() - brush.green() * a)),
        qMax(0, static_cast<int>(base.blue() - brush.blue() * a)),
        base.alpha());
}

QImage TexturePaintSystem::generateStamp() const
{
    float radius = m_brush.size * 0.5f;
    int diam = static_cast<int>(m_brush.size);
    QImage stamp(diam, diam, QImage::Format_ARGB32_Premultiplied);
    stamp.fill(Qt::transparent);

    QPainter painter(&stamp);
    painter.setRenderHint(QPainter::Antialiasing, true);

    if (m_brush.type == PaintBrush::Stamp && !m_brush.stampTexture.isNull()) {
        painter.drawImage(stamp.rect(), m_brush.stampTexture);
    } else {
        QRadialGradient gradient(QPointF(radius, radius), radius);
        gradient.setColorAt(0.0, QColor(255, 255, 255, 255));
        gradient.setColorAt(m_brush.hardness, QColor(255, 255, 255, 128));
        gradient.setColorAt(1.0, QColor(255, 255, 255, 0));
        painter.setBrush(gradient);
        painter.setPen(Qt::NoPen);

        if (m_brush.type == PaintBrush::Square)
            painter.drawRect(QRectF(0, 0, m_brush.size, m_brush.size));
        else
            painter.drawEllipse(QPointF(radius, radius), radius, radius);
    }

    painter.end();
    return stamp;
}

}
