#pragma once

#include <QObject>
#include <QColor>
#include <QImage>
#include <QPoint>
#include <QRect>
#include <QStringList>
#include "TexturePaintSystem.h"

namespace ks {

class TexturePaintQmlBridge : public QObject {
    Q_OBJECT
    Q_PROPERTY(int brushSize READ brushSize WRITE setBrushSize NOTIFY brushChanged)
    Q_PROPERTY(float brushStrength READ brushStrength WRITE setBrushStrength NOTIFY brushChanged)
    Q_PROPERTY(float brushHardness READ brushHardness WRITE setBrushHardness NOTIFY brushChanged)
    Q_PROPERTY(int brushType READ brushType WRITE setBrushType NOTIFY brushChanged)
    Q_PROPERTY(QColor brushColor READ brushColor WRITE setBrushColor NOTIFY brushChanged)
    Q_PROPERTY(int layerCount READ layerCount NOTIFY layersChanged)
    Q_PROPERTY(int currentLayer READ currentLayer WRITE setCurrentLayer NOTIFY layersChanged)
    Q_PROPERTY(int canvasWidth READ canvasWidth NOTIFY canvasChanged)
    Q_PROPERTY(int canvasHeight READ canvasHeight NOTIFY canvasChanged)
    Q_PROPERTY(bool hasSelection READ hasSelection NOTIFY selectionChanged)

public:
    explicit TexturePaintQmlBridge(QObject* parent = nullptr);

    static TexturePaintQmlBridge* instance();

    // Brush
    Q_INVOKABLE void setBrushSize(int size);
    int brushSize() const;
    Q_INVOKABLE void setBrushStrength(float strength);
    float brushStrength() const;
    Q_INVOKABLE void setBrushHardness(float hardness);
    float brushHardness() const;
    Q_INVOKABLE void setBrushType(int type);
    int brushType() const;
    Q_INVOKABLE void setBrushColor(const QColor& color);
    QColor brushColor() const;
    Q_INVOKABLE QStringList brushTypeNames() const;

    // Canvas
    int canvasWidth() const;
    int canvasHeight() const;
    Q_INVOKABLE void setCanvasSize(int width, int height);
    Q_INVOKABLE void clearCanvas();
    Q_INVOKABLE void resizeCanvas(int width, int height, int anchorX = 0, int anchorY = 0);
    Q_INVOKABLE QImage compositeAll() const;

    // Layers
    int layerCount() const;
    int currentLayer() const;
    Q_INVOKABLE void setCurrentLayer(int index);
    Q_INVOKABLE int addLayer(const QString& name = QString());
    Q_INVOKABLE bool removeLayer(int index);
    Q_INVOKABLE bool moveLayer(int from, int to);
    Q_INVOKABLE void setLayerOpacity(int index, float opacity);
    Q_INVOKABLE void setLayerVisible(int index, bool visible);
    Q_INVOKABLE void setLayerLocked(int index, bool locked);
    Q_INVOKABLE void setLayerBlendMode(int index, int mode);
    Q_INVOKABLE QStringList layerNames() const;
    Q_INVOKABLE QStringList blendModeNames() const;
    Q_INVOKABLE QImage layerImage(int index) const;

    // Painting
    Q_INVOKABLE void beginStroke(int x, int y);
    Q_INVOKABLE void addStrokePoint(int x, int y);
    Q_INVOKABLE void endStroke();

    // Fill
    Q_INVOKABLE void floodFill(int x, int y, float tolerance = 0.1f);
    Q_INVOKABLE void gradientFill(int x1, int y1, int x2, int y2,
                                  const QColor& startColor, const QColor& endColor,
                                  bool radial = false);

    // Filters
    Q_INVOKABLE void applyBlur(float radius = 5.0f);
    Q_INVOKABLE void applySharpen(float amount = 0.5f);
    Q_INVOKABLE void applyNoise(float amount = 0.1f);
    Q_INVOKABLE void applyEmboss(float strength = 1.0f);
    Q_INVOKABLE void applyInvert();
    Q_INVOKABLE void applyLevels(float black, float gamma, float white);
    Q_INVOKABLE void applyHueSaturation(float hueShift, float saturation, float lightness);

    // Selection
    bool hasSelection() const;
    Q_INVOKABLE void setSelection(int x, int y, int w, int h);
    Q_INVOKABLE void clearSelection();

    // Undo/Redo
    Q_INVOKABLE void undo();
    Q_INVOKABLE void redo();
    Q_INVOKABLE bool canUndo() const;
    Q_INVOKABLE bool canRedo() const;
    Q_INVOKABLE void clearUndoStack();

    // Load/Save
    Q_INVOKABLE bool loadTexture(const QString& path);
    Q_INVOKABLE bool saveTexture(const QString& path) const;

    signals:
    void brushChanged();
    void layersChanged();
    void canvasChanged();
    void selectionChanged();
    void strokeCompleted();
    void undoStackChanged();

private:
    TexturePaintSystem* m_system;
    static TexturePaintQmlBridge* s_instance;
};

} // namespace ks
