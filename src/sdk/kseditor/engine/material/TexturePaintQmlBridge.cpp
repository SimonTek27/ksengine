#include "TexturePaintQmlBridge.h"
#include <QFile>

namespace ks {

TexturePaintQmlBridge* TexturePaintQmlBridge::s_instance = nullptr;

TexturePaintQmlBridge::TexturePaintQmlBridge(QObject* parent)
    : QObject(parent)
    , m_system(TexturePaintSystem::instance())
{
    s_instance = this;
}

TexturePaintQmlBridge* TexturePaintQmlBridge::instance()
{
    return s_instance;
}

// Brush
void TexturePaintQmlBridge::setBrushSize(int size)
{
    PaintBrush b = m_system->brush();
    b.size = qMax(1.0f, static_cast<float>(size));
    m_system->setBrush(b);
    emit brushChanged();
}

int TexturePaintQmlBridge::brushSize() const
{
    return static_cast<int>(m_system->brush().size);
}

void TexturePaintQmlBridge::setBrushStrength(float strength)
{
    PaintBrush b = m_system->brush();
    b.strength = qBound(0.0f, strength, 1.0f);
    m_system->setBrush(b);
    emit brushChanged();
}

float TexturePaintQmlBridge::brushStrength() const
{
    return m_system->brush().strength;
}

void TexturePaintQmlBridge::setBrushHardness(float hardness)
{
    PaintBrush b = m_system->brush();
    b.hardness = qBound(0.0f, hardness, 1.0f);
    m_system->setBrush(b);
    emit brushChanged();
}

float TexturePaintQmlBridge::brushHardness() const
{
    return m_system->brush().hardness;
}

void TexturePaintQmlBridge::setBrushType(int type)
{
    PaintBrush b = m_system->brush();
    b.type = static_cast<PaintBrush::BrushType>(qBound(0, type, 10));
    m_system->setBrush(b);
    emit brushChanged();
}

int TexturePaintQmlBridge::brushType() const
{
    return static_cast<int>(m_system->brush().type);
}

void TexturePaintQmlBridge::setBrushColor(const QColor& color)
{
    PaintBrush b = m_system->brush();
    b.color = color;
    m_system->setBrush(b);
    emit brushChanged();
}

QColor TexturePaintQmlBridge::brushColor() const
{
    return m_system->brush().color;
}

QStringList TexturePaintQmlBridge::brushTypeNames() const
{
    return {"Circle", "Square", "Soft", "Clone", "Stamp", "Eraser"};
}

// Canvas
int TexturePaintQmlBridge::canvasWidth() const { return m_system->canvasSize().width(); }
int TexturePaintQmlBridge::canvasHeight() const { return m_system->canvasSize().height(); }

void TexturePaintQmlBridge::setCanvasSize(int width, int height)
{
    m_system->setCanvasSize(width, height);
    emit canvasChanged();
}

void TexturePaintQmlBridge::clearCanvas()
{
    m_system->clearCanvas(Qt::transparent);
    emit canvasChanged();
}

void TexturePaintQmlBridge::resizeCanvas(int width, int height, int anchorX, int anchorY)
{
    m_system->resizeCanvas(width, height, anchorX, anchorY);
    emit canvasChanged();
}

QImage TexturePaintQmlBridge::compositeAll() const
{
    return m_system->compositeAll();
}

// Layers
int TexturePaintQmlBridge::layerCount() const { return m_system->layerCount(); }
int TexturePaintQmlBridge::currentLayer() const { return m_system->currentLayer(); }

void TexturePaintQmlBridge::setCurrentLayer(int index)
{
    m_system->setCurrentLayer(index);
    emit layersChanged();
}

int TexturePaintQmlBridge::addLayer(const QString& name)
{
    int idx = m_system->addLayer(name);
    emit layersChanged();
    return idx;
}

bool TexturePaintQmlBridge::removeLayer(int index)
{
    bool ok = m_system->removeLayer(index);
    if (ok) emit layersChanged();
    return ok;
}

bool TexturePaintQmlBridge::moveLayer(int from, int to)
{
    bool ok = m_system->moveLayer(from, to);
    if (ok) emit layersChanged();
    return ok;
}

void TexturePaintQmlBridge::setLayerOpacity(int index, float opacity)
{
    m_system->setLayerOpacity(index, opacity);
    emit layersChanged();
}

void TexturePaintQmlBridge::setLayerVisible(int index, bool visible)
{
    m_system->setLayerVisible(index, visible);
    emit layersChanged();
}

void TexturePaintQmlBridge::setLayerLocked(int index, bool locked)
{
    m_system->setLayerLocked(index, locked);
}

void TexturePaintQmlBridge::setLayerBlendMode(int index, int mode)
{
    m_system->setLayerBlendMode(index, static_cast<PaintLayer::BlendMode>(mode));
    emit layersChanged();
}

QStringList TexturePaintQmlBridge::layerNames() const
{
    QStringList names;
    for (int i = 0; i < m_system->layerCount(); i++) {
        const PaintLayer* l = m_system->layer(i);
        names.append(l ? l->name : QString("Layer %1").arg(i + 1));
    }
    return names;
}

QStringList TexturePaintQmlBridge::blendModeNames() const
{
    return {"Normal", "Multiply", "Screen", "Overlay", "Add", "Subtract", "Lighten", "Darken", "AlphaBlend"};
}

QImage TexturePaintQmlBridge::layerImage(int index) const
{
    const PaintLayer* l = m_system->layer(index);
    return l ? l->texture : QImage();
}

// Painting
void TexturePaintQmlBridge::beginStroke(int x, int y)
{
    m_system->beginStroke(QPoint(x, y));
}

void TexturePaintQmlBridge::addStrokePoint(int x, int y)
{
    m_system->addStrokePoint(QPoint(x, y));
}

void TexturePaintQmlBridge::endStroke()
{
    m_system->endStroke();
    emit strokeCompleted();
}

// Fill
void TexturePaintQmlBridge::floodFill(int x, int y, float tolerance)
{
    m_system->floodFill(QPoint(x, y), m_system->brush().color, tolerance);
}

void TexturePaintQmlBridge::gradientFill(int x1, int y1, int x2, int y2,
                                          const QColor& startColor, const QColor& endColor,
                                          bool radial)
{
    m_system->gradientFill(QPoint(x1, y1), QPoint(x2, y2), startColor, endColor, radial);
}

// Filters
void TexturePaintQmlBridge::applyBlur(float radius)
{
    m_system->applyBlur(QRect(), radius);
}

void TexturePaintQmlBridge::applySharpen(float amount)
{
    m_system->applySharpen(QRect(), amount);
}

void TexturePaintQmlBridge::applyNoise(float amount)
{
    m_system->applyNoise(QRect(), amount);
}

void TexturePaintQmlBridge::applyEmboss(float strength)
{
    m_system->applyEmboss(QRect(), strength);
}

void TexturePaintQmlBridge::applyInvert()
{
    m_system->applyInvert(QRect());
}

void TexturePaintQmlBridge::applyLevels(float black, float gamma, float white)
{
    m_system->applyLevels(QRect(), black, gamma, white);
}

void TexturePaintQmlBridge::applyHueSaturation(float hueShift, float saturation, float lightness)
{
    m_system->applyHueSaturation(QRect(), hueShift, saturation, lightness);
}

// Selection
bool TexturePaintQmlBridge::hasSelection() const { return m_system->hasSelection(); }

void TexturePaintQmlBridge::setSelection(int x, int y, int w, int h)
{
    m_system->setSelection(QRect(x, y, w, h));
    emit selectionChanged();
}

void TexturePaintQmlBridge::clearSelection()
{
    m_system->clearSelection();
    emit selectionChanged();
}

// Undo/Redo
void TexturePaintQmlBridge::undo()
{
    m_system->undo();
    emit undoStackChanged();
    emit layersChanged();
    emit canvasChanged();
}

void TexturePaintQmlBridge::redo()
{
    m_system->redo();
    emit undoStackChanged();
    emit layersChanged();
    emit canvasChanged();
}

bool TexturePaintQmlBridge::canUndo() const { return m_system->canUndo(); }
bool TexturePaintQmlBridge::canRedo() const { return m_system->canRedo(); }

void TexturePaintQmlBridge::clearUndoStack()
{
    m_system->clearUndoStack();
    emit undoStackChanged();
}

// Load/Save
bool TexturePaintQmlBridge::loadTexture(const QString& path)
{
    QImage img(path);
    if (img.isNull()) return false;
    m_system->setCanvasSize(img.width(), img.height());
    m_system->layer(0)->texture = img.copy();
    m_system->clearUndoStack();
    emit canvasChanged();
    return true;
}

bool TexturePaintQmlBridge::saveTexture(const QString& path) const
{
    return m_system->compositeAll().save(path);
}

} // namespace ks
