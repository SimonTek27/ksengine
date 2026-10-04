#pragma once

#include <QString>
#include <QColor>
#include <QMap>
#include <QVariant>
#include <QObject>
#include <QSettings>

/**
 * @brief Post-Processing Filter Preset for Assetto Corsa
 * 
 * Implements Yebis-style post-processing effects from AC's ppfilters/*.ini files.
 * Supports: Auto Exposure, Tone Mapping, DOF, Chromatic Aberration, Glare, etc.
 */
class PPFilterPreset : public QObject
{
    Q_OBJECT

public:
    explicit PPFilterPreset(const QString& name, QObject* parent = nullptr);
    ~PPFilterPreset() = default;
    
    QString name() const { return m_name; }
    void loadFromSettings(QSettings* settings);
    void saveToSettings(QSettings* settings) const;
    
    // Section: AUTO_EXPOSURE
    struct AutoExposure {
        bool enabled = true;
        float delay = 0.0f;
        float minValue = 0.2f;
        float maxValue = 0.5f;
        float meteringWidth = 100.0f;
        float meteringHeight = 100.0f;
        float meteringOffsetX = 0.0f;
        float meteringOffsetY = 0.0f;
        float target = 0.32f;
        bool influencedByGlare = false;
    };
    AutoExposure autoExposure() const { return m_autoExposure; }
    void setAutoExposure(const AutoExposure& v) { m_autoExposure = v; emit filterChanged(); }
    
    // Section: TONEMAPPING
    struct ToneMapping {
        bool hdr = true;
        float exposure = 0.28f;
        float gamma = 1.2f;
        int function = -1; // -1 = default
        float mappingFactor = 32.0f;
        float scaleWidth = 1.0f;
        float scaleHeight = 1.0f;
        float offsetX = 0.0f;
        float offsetY = 0.0f;
    };
    ToneMapping toneMapping() const { return m_toneMapping; }
    void setToneMapping(const ToneMapping& v) { m_toneMapping = v; emit filterChanged(); }
    
    // Section: DOF
    struct DOF {
        bool enabled = true;
        int quality = 3;
        float apertureFNumber = 12.0f;
        float imageSensorHeight = 0.24f;
        float baseFOV = 50.0f;
        float adaptiveApertureFactor = 0.5f;
        float apertureParameter = 2.0f;
        int apertureFrontLevels = -1;
        int apertureBackLevels = -1;
        float backgroundMaskThreshold = 0.1f;
        int edgeQuality = 3;
        int apertureShape = 3;
    };
    DOF depthOfField() const { return m_dof; }
    void setDepthOfField(const DOF& v) { m_dof = v; emit filterChanged(); }
    
    // Section: CHROMATIC_ABERRATION
    struct ChromaticAberration {
        bool enabled = true;
        int samples = 5;
        float lateralDispersionX = 0.005f;
        float lateralDispersionY = 0.005f;
        float uniformDispersionX = 0.0005f;
        float uniformDispersionY = 0.0005f;
    };
    ChromaticAberration chromaticAberration() const { return m_chromaticAberration; }
    void setChromaticAberration(const ChromaticAberration& v) { m_chromaticAberration = v; emit filterChanged(); }
    
    // Section: VIGNETTING
    struct Vignetting {
        float strength = 0.035f;
        float fovDependence = 0.0f;
    };
    Vignetting vignetting() const { return m_vignetting; }
    void setVignetting(const Vignetting& v) { m_vignetting = v; emit filterChanged(); }
    
    // Section: GLARE
    struct Glare {
        bool enabled = true;
        int quality = 3;
        bool ghost = true;
        bool afterImage = false;
        int precision = 0;
        bool anamorphic = false;
        float luminance = 1.6f;
        int shape = 11;
        bool blur = true;
        float threshold = 5.0f;
        bool brightPass = false;
    };
    Glare glare() const { return m_glare; }
    void setGlare(const Glare& v) { m_glare = v; emit filterChanged(); }
    
    // Section: GODRAYS
    struct GodRays {
        bool enabled = true;
        bool useSunLight = true;
        float diffractionRing = 0.25f;
        float diffractionRingRadius = 5.0f;
        float diffractionRingAttenuation = 0.1f;
        QColor diffractionRingOuterColor = Qt::white;
        QColor color = Qt::white;
        float length = 11.0f;
        float glareRatio = 0.005f;
        float angleAttenuation = 5.0f;
    };
    GodRays godRays() const { return m_godRays; }
    void setGodRays(const GodRays& v) { m_godRays = v; emit filterChanged(); }
    
    // Section: COLOR
    struct ColorGrading {
        float hue = 0.0f;
        float saturation = 0.95f;
        float brightness = 1.0f;
        float contrast = 1.0f;
        float sepia = 0.0f;
        int colorTemp = 6400;
        int whiteBalance = 6400;
    };
    ColorGrading colorGrading() const { return m_colorGrading; }
    void setColorGrading(const ColorGrading& v) { m_colorGrading = v; emit filterChanged(); }
    
    // ── Built-in Preset Library ─────────────────────────────────────────
    struct PresetLibraryEntry {
        QString name;
        QString description;
        QString category; // "cinematic", "natural", "performance", "vintage"
        AutoExposure autoExposure;
        ToneMapping toneMapping;
        DOF dof;
        ChromaticAberration chromaticAberration;
        Vignetting vignetting;
        Glare glare;
        GodRays godRays;
        ColorGrading colorGrading;
    };

    static QVector<PresetLibraryEntry> builtinPresets();
    bool loadFromLibrary(const QString& presetName);
    QStringList libraryPresetNames() const;
    QStringList libraryPresetCategories() const;
    QStringList libraryPresetsByCategory(const QString& category) const;

signals:
    void filterChanged();

private:
    QString m_name;
    
    // Effect settings
    AutoExposure m_autoExposure;
    ToneMapping m_toneMapping;
    DOF m_dof;
    ChromaticAberration m_chromaticAberration;
    Vignetting m_vignetting;
    Glare m_glare;
    GodRays m_godRays;
    ColorGrading m_colorGrading;
    
    void loadAutoExposure(QSettings& s);
    void loadToneMapping(QSettings& s);
    void loadDOF(QSettings& s);
    void loadChromaticAberration(QSettings& s);
    void loadVignetting(QSettings& s);
    void loadGlare(QSettings& s);
    void loadGodRays(QSettings& s);
    void loadColorGrading(QSettings& s);
    void saveAutoExposure(QSettings& s) const;
    void saveToneMapping(QSettings& s) const;
    void saveDOF(QSettings& s) const;
    void saveChromaticAberration(QSettings& s) const;
    void saveVignetting(QSettings& s) const;
    void saveGlare(QSettings& s) const;
    void saveGodRays(QSettings& s) const;
    void saveColorGrading(QSettings& s) const;
};
