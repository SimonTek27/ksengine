#pragma once

#include <QObject>
#include <QImage>
#include <QColor>
#include <QVector>
#include <QPoint>
#include <QRect>
#include <QString>
#include <QVector3D>
#include <functional>

namespace ks {

struct PaintLayer {
    QString name;
    QImage texture;
    float opacity = 1.0f;
    bool visible = true;
    bool locked = false;
    enum BlendMode { Normal, Multiply, Screen, Overlay, Add, Subtract, Lighten, Darken, AlphaBlend };
    BlendMode blendMode = Normal;
    float maskStrength = 1.0f;
};

struct PaintBrush {
    enum BrushType { Circle, Square, Soft, Airbrush, Clone, Healing, Stamp, Eraser, Smudge, Blur, Sharpen };
    BrushType type = Circle;
    float size = 20.0f;
    float strength = 1.0f;
    float hardness = 0.5f;
    float spacing = 0.25f;
    float angle = 0.0f;
    QColor color = Qt::white;
    QImage stampTexture;
    bool usePressure = false;
    float jitter = 0.0f;
    float flow = 1.0f;

    bool isCloneOrHealing() const {
        return type == Clone || type == Healing;
    }
};

struct Stencil {
    QImage mask;
    QVector3D position;
    QVector3D normal;
    float scale = 1.0f;
    float rotation = 0.0f;
    bool invert = false;
    float opacity = 1.0f;
    int wrapMode = 0; // 0=Flat, 1=Surface, 2=Cylindrical
    QVector3D wrapAxis = QVector3D(0, 1, 0); // axis for cylindrical wrap
};

class TexturePaintSystem : public QObject {
    Q_OBJECT

public:
    explicit TexturePaintSystem(QObject* parent = nullptr);
    ~TexturePaintSystem();

    static TexturePaintSystem* instance();

    // Layer management
    int addLayer(const QString& name = QString());
    bool removeLayer(int index);
    bool moveLayer(int from, int to);
    void setLayerOpacity(int index, float opacity);
    void setLayerVisible(int index, bool visible);
    void setLayerLocked(int index, bool locked);
    void setLayerBlendMode(int index, PaintLayer::BlendMode mode);
    PaintLayer* layer(int index);
    const PaintLayer* layer(int index) const;
    int layerCount() const { return m_layers.size(); }
    int currentLayer() const { return m_currentLayer; }
    void setCurrentLayer(int index);
    QImage compositeAll() const;

    // Canvas
    void setCanvasSize(int width, int height);
    QSize canvasSize() const { return m_canvasSize; }
    void clearCanvas(const QColor& color = Qt::transparent);
    void resizeCanvas(int width, int height, int anchorX = 0, int anchorY = 0);

    // Painting
    void setBrush(const PaintBrush& brush) { m_brush = brush; }
    const PaintBrush& brush() const { return m_brush; }
    PaintBrush& brush() { return m_brush; }
    void beginStroke(const QPoint& pos);
    void addStrokePoint(const QPoint& pos);
    void endStroke();
    bool isStrokeActive() const { return m_strokeActive; }

    // Stamp/Stencil
    void applyStamp(const QPoint& pos, const QImage& stamp);
    void applyStencil(const Stencil& stencil);
    void floodFill(const QPoint& pos, const QColor& color, float tolerance = 0.1f);
    void gradientFill(const QPoint& from, const QPoint& to,
                      const QColor& startColor, const QColor& endColor,
                      bool radial = false);

    // Clone/Healing
    void setCloneSource(const QPoint& source) { m_cloneSource = source; }
    QPoint cloneSource() const { return m_cloneSource; }
    void setCloneImage(const QImage& image) { m_cloneImage = image; }

    // Filters
    void applyBlur(const QRect& region, float radius = 5.0f);
    void applySharpen(const QRect& region, float amount = 0.5f);
    void applyNoise(const QRect& region, float amount = 0.1f);
    void applyEmboss(const QRect& region, float strength = 1.0f);
    void applyInvert(const QRect& region);
    void applyLevels(const QRect& region, float black, float gamma, float white);
    void applyHueSaturation(const QRect& region, float hueShift, float saturation, float lightness);

    // Selection/Mask
    void setSelection(const QRect& rect) { m_selection = rect; }
    QRect selection() const { return m_selection; }
    void clearSelection() { m_selection = QRect(); }
    bool hasSelection() const { return !m_selection.isNull(); }

    // Undo/Redo
    void undo();
    void redo();
    bool canUndo() const { return m_undoIndex > 0; }
    bool canRedo() const { return m_undoIndex < m_undoStack.size() - 1; }
    void clearUndoStack();

    // Projection painting (3D)
    void projectStroke(const QVector<QVector3D>& meshVertices,
                       const QVector<QVector2D>& uvCoords,
                       const QVector<int>& faceIndices,
                       const QVector3D& cameraPos,
                       const QPoint& mousePos);

    signals:
    void layerAdded(int index);
    void layerRemoved(int index);
    void layerChanged(int index);
    void layerOrderChanged();
    void canvasChanged();
    void strokeCompleted();
    void undoStackChanged();

private:
    void paintPoint(const QPoint& pos);
    QColor blendColors(const QColor& base, const QColor& brush, float alpha,
                       PaintLayer::BlendMode mode) const;
    QColor blendNormal(const QColor& base, const QColor& brush, float alpha) const;
    QColor blendMultiply(const QColor& base, const QColor& brush, float alpha) const;
    QColor blendScreen(const QColor& base, const QColor& brush, float alpha) const;
    QColor blendOverlay(const QColor& base, const QColor& brush, float alpha) const;
    QColor blendAdd(const QColor& base, const QColor& brush, float alpha) const;
    QColor blendSubtract(const QColor& base, const QColor& brush, float alpha) const;
    QImage generateStamp() const;

    static TexturePaintSystem* s_instance;

    struct UndoEntry {
        QVector<PaintLayer> layers;
        int currentLayer;
        QSize canvasSize;
    };

    void saveUndoState();
    void restoreUndoState(const UndoEntry& entry);

    QVector<PaintLayer> m_layers;
    int m_currentLayer = -1;
    QSize m_canvasSize = {1024, 1024};
    PaintBrush m_brush;
    bool m_strokeActive = false;
    QPoint m_lastStrokePos;
    QPoint m_cloneSource;
    QImage m_cloneImage;
    QRect m_selection;
    float m_accumulatedDist = 0.0f;

    QVector<UndoEntry> m_undoStack;
    int m_undoIndex = -1;
    static constexpr int kMaxUndoSteps = 100;
};

}
