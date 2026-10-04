#include "PPFilterPreset.h"

PPFilterPreset::PPFilterPreset(const QString& name, QObject* parent)
    : QObject(parent)
    , m_name(name)
{
}

void PPFilterPreset::loadFromSettings(QSettings* settings)
{
    if (!settings) return;
    
    loadAutoExposure(*settings);
    loadToneMapping(*settings);
    loadDOF(*settings);
    loadChromaticAberration(*settings);
    loadVignetting(*settings);
    loadGlare(*settings);
    loadGodRays(*settings);
    loadColorGrading(*settings);
    
    emit filterChanged();
}

void PPFilterPreset::saveToSettings(QSettings* settings) const
{
    if (!settings) return;
    saveAutoExposure(*settings);
    saveToneMapping(*settings);
    saveDOF(*settings);
    saveChromaticAberration(*settings);
    saveVignetting(*settings);
    saveGlare(*settings);
    saveGodRays(*settings);
    saveColorGrading(*settings);
}

void PPFilterPreset::loadAutoExposure(QSettings& s)
{
    s.beginGroup("AUTO_EXPOSURE");
    m_autoExposure.enabled = s.value("ENABLED", 1).toBool();
    m_autoExposure.delay = s.value("DELAY", 0.0f).toFloat();
    m_autoExposure.minValue = s.value("MIN", 0.2f).toFloat();
    m_autoExposure.maxValue = s.value("MAX", 0.5f).toFloat();
    m_autoExposure.target = s.value("TARGET", 0.32f).toFloat();
    m_autoExposure.meteringWidth = s.value("METERING_WIDTH", 100.0f).toFloat();
    m_autoExposure.meteringHeight = s.value("METERING_HEIGHT", 100.0f).toFloat();
    m_autoExposure.meteringOffsetX = s.value("METERING_OFFSET_X", 0.0f).toFloat();
    m_autoExposure.meteringOffsetY = s.value("METERING_OFFSET_Y", 0.0f).toFloat();
    m_autoExposure.influencedByGlare = s.value("INFLUENCED_BY_GLARE", 0).toBool();
    s.endGroup();
}

void PPFilterPreset::loadToneMapping(QSettings& s)
{
    s.beginGroup("TONEMAPPING");
    m_toneMapping.hdr = s.value("HDR", 1).toBool();
    m_toneMapping.exposure = s.value("EXPOSURE", 0.28f).toFloat();
    m_toneMapping.gamma = s.value("GAMMA", 1.2f).toFloat();
    m_toneMapping.function = s.value("FUNCTION", -1).toInt();
    m_toneMapping.mappingFactor = s.value("MAPPING_FACTOR", 32.0f).toFloat();
    m_toneMapping.scaleWidth = s.value("SCALE_WIDTH", 1.0f).toFloat();
    m_toneMapping.scaleHeight = s.value("SCALE_HEIGHT", 1.0f).toFloat();
    m_toneMapping.offsetX = s.value("OFFSET_X", 0.0f).toFloat();
    m_toneMapping.offsetY = s.value("OFFSET_Y", 0.0f).toFloat();
    s.endGroup();
}

void PPFilterPreset::loadDOF(QSettings& s)
{
    s.beginGroup("DOF");
    m_dof.enabled = s.value("ENABLED", 1).toBool();
    m_dof.quality = s.value("QUALITY", 3).toInt();
    m_dof.apertureFNumber = s.value("APERTURE_F_NUMBER", 12.0f).toFloat();
    m_dof.imageSensorHeight = s.value("IMAGE_SENSOR_HEIGHT", 0.24f).toFloat();
    m_dof.baseFOV = s.value("BASE_FOV", 50.0f).toFloat();
    m_dof.adaptiveApertureFactor = s.value("ADAPTIVE_APERTURE_FACTOR", 0.5f).toFloat();
    m_dof.apertureParameter = s.value("APERTURE_PARAMETER", 2.0f).toFloat();
    m_dof.apertureFrontLevels = s.value("APERTURE_FRONT_LEVELS_NUMBER", -1).toInt();
    m_dof.apertureBackLevels = s.value("APERTURE_BACK_LEVELS_NUMBER", -1).toInt();
    m_dof.backgroundMaskThreshold = s.value("BACKGROUND_MASK_THRESHOLD", 0.1f).toFloat();
    m_dof.edgeQuality = s.value("EDGE_QUALITY", 3).toInt();
    m_dof.apertureShape = s.value("APERTURE_SHAPE", 3).toInt();
    s.endGroup();
}

void PPFilterPreset::loadChromaticAberration(QSettings& s)
{
    s.beginGroup("CHROMATIC_ABERRATION");
    m_chromaticAberration.enabled = s.value("ENABLED", 1).toBool();
    m_chromaticAberration.samples = s.value("SAMPLES", 5).toInt();
    
    QStringList latDisp = s.value("LATERAL_DISPERSION", "0.005,0.005").toString().split(',');
    if (latDisp.size() >= 2) {
        m_chromaticAberration.lateralDispersionX = latDisp[0].toFloat();
        m_chromaticAberration.lateralDispersionY = latDisp[1].toFloat();
    }

    QStringList uniDisp = s.value("UNIFORM_DISPERSION", "0.0005,0.0005").toString().split(',');
    if (uniDisp.size() >= 2) {
        m_chromaticAberration.uniformDispersionX = uniDisp[0].toFloat();
        m_chromaticAberration.uniformDispersionY = uniDisp[1].toFloat();
    }
    s.endGroup();
}

void PPFilterPreset::loadVignetting(QSettings& s)
{
    s.beginGroup("VIGNETTING");
    m_vignetting.strength = s.value("STRENGTH", 0.035f).toFloat();
    m_vignetting.fovDependence = s.value("FOV_DEPENDENCE", 0.0f).toFloat();
    s.endGroup();
}

void PPFilterPreset::loadGlare(QSettings& s)
{
    s.beginGroup("GLARE");
    m_glare.enabled = s.value("ENABLED", 1).toBool();
    m_glare.quality = s.value("QUALITY", 3).toInt();
    m_glare.ghost = s.value("GHOST", 1).toBool();
    m_glare.afterImage = s.value("AFTER_IMAGE", 0).toBool();
    m_glare.precision = s.value("PRECISION", 0).toInt();
    m_glare.anamorphic = s.value("ANAMORPHIC", 0).toBool();
    m_glare.luminance = s.value("LUMINANCE", 1.6f).toFloat();
    m_glare.shape = s.value("SHAPE", 11).toInt();
    m_glare.blur = s.value("BLUR", 1).toBool();
    m_glare.threshold = s.value("THRESHOLD", 5.0f).toFloat();
    m_glare.brightPass = s.value("BRIGHT_PASS", 0).toBool();
    s.endGroup();
}

void PPFilterPreset::loadGodRays(QSettings& s)
{
    s.beginGroup("GODRAYS");
    m_godRays.enabled = s.value("ENABLED", 1).toBool();
    m_godRays.useSunLight = s.value("USE_SUN_LIGHT", 1).toBool();
    m_godRays.diffractionRing = s.value("DIFFRACTION_RING", 0.25f).toFloat();
    m_godRays.diffractionRingRadius = s.value("DIFFRACTION_RING_RADIUS", 5.0f).toFloat();
    m_godRays.diffractionRingAttenuation = s.value("DIFFRACTION_RING_ATTENUATION", 0.1f).toFloat();
    m_godRays.length = s.value("LENGTH", 11.0f).toFloat();
    m_godRays.glareRatio = s.value("GLARE_RATIO", 0.005f).toFloat();
    m_godRays.angleAttenuation = s.value("ANGLE_ATTENUATION", 5.0f).toFloat();
    
    QStringList ringColor = s.value("DIFFRACTION_RING_OUTER_COLOR", "0.5,0.5,0.5,0.5").toString().split(',');
    if (ringColor.size() >= 4) {
        m_godRays.diffractionRingOuterColor.setRedF(ringColor[0].toDouble());
        m_godRays.diffractionRingOuterColor.setGreenF(ringColor[1].toDouble());
        m_godRays.diffractionRingOuterColor.setBlueF(ringColor[2].toDouble());
        m_godRays.diffractionRingOuterColor.setAlphaF(ringColor[3].toDouble());
    }
    
    QStringList color = s.value("COLOR", "1,1,1,1").toString().split(',');
    if (color.size() >= 4) {
        m_godRays.color.setRedF(color[0].toDouble());
        m_godRays.color.setGreenF(color[1].toDouble());
        m_godRays.color.setBlueF(color[2].toDouble());
        m_godRays.color.setAlphaF(color[3].toDouble());
    }
    s.endGroup();
}

void PPFilterPreset::loadColorGrading(QSettings& s)
{
    s.beginGroup("COLOR");
    m_colorGrading.hue = s.value("HUE", 0.0f).toFloat();
    m_colorGrading.saturation = s.value("SATURATION", 0.95f).toFloat();
    m_colorGrading.brightness = s.value("BRIGHTNESS", 1.0f).toFloat();
    m_colorGrading.contrast = s.value("CONTRAST", 1.0f).toFloat();
    m_colorGrading.sepia = s.value("SEPIA", 0.0f).toFloat();
    m_colorGrading.colorTemp = s.value("COLOR_TEMP", 6400).toInt();
    m_colorGrading.whiteBalance = s.value("WHITE_BALANCE", 6400).toInt();
    s.endGroup();
}

void PPFilterPreset::saveAutoExposure(QSettings& s) const
{
    s.beginGroup("AUTO_EXPOSURE");
    s.setValue("ENABLED", m_autoExposure.enabled ? 1 : 0);
    s.setValue("DELAY", m_autoExposure.delay);
    s.setValue("MIN", m_autoExposure.minValue);
    s.setValue("MAX", m_autoExposure.maxValue);
    s.setValue("TARGET", m_autoExposure.target);
    s.setValue("METERING_WIDTH", m_autoExposure.meteringWidth);
    s.setValue("METERING_HEIGHT", m_autoExposure.meteringHeight);
    s.setValue("METERING_OFFSET_X", m_autoExposure.meteringOffsetX);
    s.setValue("METERING_OFFSET_Y", m_autoExposure.meteringOffsetY);
    s.setValue("INFLUENCED_BY_GLARE", m_autoExposure.influencedByGlare ? 1 : 0);
    s.endGroup();
}

void PPFilterPreset::saveToneMapping(QSettings& s) const
{
    s.beginGroup("TONEMAPPING");
    s.setValue("HDR", m_toneMapping.hdr ? 1 : 0);
    s.setValue("EXPOSURE", m_toneMapping.exposure);
    s.setValue("GAMMA", m_toneMapping.gamma);
    s.setValue("FUNCTION", m_toneMapping.function);
    s.setValue("MAPPING_FACTOR", m_toneMapping.mappingFactor);
    s.setValue("SCALE_WIDTH", m_toneMapping.scaleWidth);
    s.setValue("SCALE_HEIGHT", m_toneMapping.scaleHeight);
    s.setValue("OFFSET_X", m_toneMapping.offsetX);
    s.setValue("OFFSET_Y", m_toneMapping.offsetY);
    s.endGroup();
}

void PPFilterPreset::saveDOF(QSettings& s) const
{
    s.beginGroup("DOF");
    s.setValue("ENABLED", m_dof.enabled ? 1 : 0);
    s.setValue("QUALITY", m_dof.quality);
    s.setValue("APERTURE_F_NUMBER", m_dof.apertureFNumber);
    s.setValue("IMAGE_SENSOR_HEIGHT", m_dof.imageSensorHeight);
    s.setValue("BASE_FOV", m_dof.baseFOV);
    s.setValue("ADAPTIVE_APERTURE_FACTOR", m_dof.adaptiveApertureFactor);
    s.setValue("APERTURE_PARAMETER", m_dof.apertureParameter);
    s.setValue("APERTURE_FRONT_LEVELS_NUMBER", m_dof.apertureFrontLevels);
    s.setValue("APERTURE_BACK_LEVELS_NUMBER", m_dof.apertureBackLevels);
    s.setValue("BACKGROUND_MASK_THRESHOLD", m_dof.backgroundMaskThreshold);
    s.setValue("EDGE_QUALITY", m_dof.edgeQuality);
    s.setValue("APERTURE_SHAPE", m_dof.apertureShape);
    s.endGroup();
}

void PPFilterPreset::saveChromaticAberration(QSettings& s) const
{
    s.beginGroup("CHROMATIC_ABERRATION");
    s.setValue("ENABLED", m_chromaticAberration.enabled ? 1 : 0);
    s.setValue("SAMPLES", m_chromaticAberration.samples);
    s.setValue("LATERAL_DISPERSION",
        QString("%1,%2").arg(m_chromaticAberration.lateralDispersionX).arg(m_chromaticAberration.lateralDispersionY));
    s.setValue("UNIFORM_DISPERSION",
        QString("%1,%2").arg(m_chromaticAberration.uniformDispersionX).arg(m_chromaticAberration.uniformDispersionY));
    s.endGroup();
}

void PPFilterPreset::saveVignetting(QSettings& s) const
{
    s.beginGroup("VIGNETTING");
    s.setValue("STRENGTH", m_vignetting.strength);
    s.setValue("FOV_DEPENDENCE", m_vignetting.fovDependence);
    s.endGroup();
}

void PPFilterPreset::saveGlare(QSettings& s) const
{
    s.beginGroup("GLARE");
    s.setValue("ENABLED", m_glare.enabled ? 1 : 0);
    s.setValue("QUALITY", m_glare.quality);
    s.setValue("GHOST", m_glare.ghost ? 1 : 0);
    s.setValue("AFTER_IMAGE", m_glare.afterImage ? 1 : 0);
    s.setValue("PRECISION", m_glare.precision);
    s.setValue("ANAMORPHIC", m_glare.anamorphic ? 1 : 0);
    s.setValue("LUMINANCE", m_glare.luminance);
    s.setValue("SHAPE", m_glare.shape);
    s.setValue("BLUR", m_glare.blur ? 1 : 0);
    s.setValue("THRESHOLD", m_glare.threshold);
    s.setValue("BRIGHT_PASS", m_glare.brightPass ? 1 : 0);
    s.endGroup();
}

void PPFilterPreset::saveGodRays(QSettings& s) const
{
    s.beginGroup("GODRAYS");
    s.setValue("ENABLED", m_godRays.enabled ? 1 : 0);
    s.setValue("USE_SUN_LIGHT", m_godRays.useSunLight ? 1 : 0);
    s.setValue("DIFFRACTION_RING", m_godRays.diffractionRing);
    s.setValue("DIFFRACTION_RING_RADIUS", m_godRays.diffractionRingRadius);
    s.setValue("DIFFRACTION_RING_ATTENUATION", m_godRays.diffractionRingAttenuation);
    s.setValue("LENGTH", m_godRays.length);
    s.setValue("GLARE_RATIO", m_godRays.glareRatio);
    s.setValue("ANGLE_ATTENUATION", m_godRays.angleAttenuation);
    s.setValue("DIFFRACTION_RING_OUTER_COLOR",
        QString("%1,%2,%3,%4").arg(m_godRays.diffractionRingOuterColor.redF())
            .arg(m_godRays.diffractionRingOuterColor.greenF())
            .arg(m_godRays.diffractionRingOuterColor.blueF())
            .arg(m_godRays.diffractionRingOuterColor.alphaF()));
    s.setValue("COLOR",
        QString("%1,%2,%3,%4").arg(m_godRays.color.redF())
            .arg(m_godRays.color.greenF())
            .arg(m_godRays.color.blueF())
            .arg(m_godRays.color.alphaF()));
    s.endGroup();
}

void PPFilterPreset::saveColorGrading(QSettings& s) const
{
    s.beginGroup("COLOR");
    s.setValue("HUE", m_colorGrading.hue);
    s.setValue("SATURATION", m_colorGrading.saturation);
    s.setValue("BRIGHTNESS", m_colorGrading.brightness);
    s.setValue("CONTRAST", m_colorGrading.contrast);
    s.setValue("SEPIA", m_colorGrading.sepia);
    s.setValue("COLOR_TEMP", m_colorGrading.colorTemp);
    s.setValue("WHITE_BALANCE", m_colorGrading.whiteBalance);
    s.endGroup();
}

// ── Built-in Preset Library ──────────────────────────────────────────────

QVector<PPFilterPreset::PresetLibraryEntry> PPFilterPreset::builtinPresets()
{
    QVector<PresetLibraryEntry> presets;

    // ── Cinematic ──────────────────────────────────────────────────────
    {
        PresetLibraryEntry e;
        e.name = "Cinematic Warm";
        e.description = "Warm tones with subtle bloom, shallow DOF, and gentle vignette";
        e.category = "cinematic";
        e.toneMapping.exposure = 0.35f;
        e.toneMapping.gamma = 1.1f;
        e.vignetting.strength = 0.08f;
        e.dof.apertureFNumber = 8.0f;
        e.dof.enabled = false;
        e.glare.enabled = true;
        e.glare.luminance = 2.0f;
        e.glare.threshold = 3.0f;
        e.colorGrading.saturation = 1.1f;
        e.colorGrading.contrast = 1.05f;
        e.colorGrading.colorTemp = 5500;
        presets.append(e);
    }
    {
        PresetLibraryEntry e;
        e.name = "Cinematic Cold";
        e.description = "Cool blue tones with high contrast and crisp details";
        e.category = "cinematic";
        e.toneMapping.exposure = 0.30f;
        e.toneMapping.gamma = 1.15f;
        e.vignetting.strength = 0.06f;
        e.colorGrading.saturation = 0.85f;
        e.colorGrading.contrast = 1.15f;
        e.colorGrading.colorTemp = 7500;
        presets.append(e);
    }
    {
        PresetLibraryEntry e;
        e.name = "Film Grain";
        e.description = "Vintage film look with desaturated colors and soft bloom";
        e.category = "cinematic";
        e.toneMapping.exposure = 0.32f;
        e.toneMapping.gamma = 1.2f;
        e.vignetting.strength = 0.10f;
        e.glare.enabled = true;
        e.glare.luminance = 1.5f;
        e.glare.threshold = 4.0f;
        e.colorGrading.saturation = 0.70f;
        e.colorGrading.contrast = 1.0f;
        e.colorGrading.sepia = 0.05f;
        e.colorGrading.colorTemp = 5000;
        presets.append(e);
    }

    // ── Natural ────────────────────────────────────────────────────────
    {
        PresetLibraryEntry e;
        e.name = "Natural Realism";
        e.description = "True-to-life colors with balanced exposure and minimal processing";
        e.category = "natural";
        e.toneMapping.exposure = 0.28f;
        e.toneMapping.gamma = 1.2f;
        e.autoExposure.enabled = true;
        e.autoExposure.target = 0.32f;
        e.colorGrading.saturation = 0.95f;
        e.colorGrading.contrast = 1.0f;
        e.colorGrading.colorTemp = 6400;
        presets.append(e);
    }
    {
        PresetLibraryEntry e;
        e.name = "Bright & Clear";
        e.description = "Slightly elevated exposure with vivid colors for daylight racing";
        e.category = "natural";
        e.toneMapping.exposure = 0.35f;
        e.toneMapping.gamma = 1.1f;
        e.colorGrading.saturation = 1.05f;
        e.colorGrading.brightness = 1.05f;
        e.colorGrading.contrast = 1.02f;
        presets.append(e);
    }
    {
        PresetLibraryEntry e;
        e.name = "Night Vision";
        e.description = "Optimized for night racing: boosted exposure, reduced glare";
        e.category = "natural";
        e.autoExposure.enabled = true;
        e.autoExposure.minValue = 0.4f;
        e.autoExposure.maxValue = 0.8f;
        e.autoExposure.target = 0.45f;
        e.toneMapping.exposure = 0.40f;
        e.toneMapping.gamma = 1.0f;
        e.glare.enabled = false;
        e.colorGrading.saturation = 0.90f;
        e.colorGrading.contrast = 1.1f;
        presets.append(e);
    }

    // ── Performance ────────────────────────────────────────────────────
    {
        PresetLibraryEntry e;
        e.name = "Performance Max";
        e.description = "Disables expensive effects (DOF, glare, god rays) for maximum FPS";
        e.category = "performance";
        e.dof.enabled = false;
        e.glare.enabled = false;
        e.godRays.enabled = false;
        e.autoExposure.enabled = false;
        e.chromaticAberration.enabled = false;
        e.toneMapping.exposure = 0.28f;
        e.toneMapping.gamma = 1.2f;
        presets.append(e);
    }
    {
        PresetLibraryEntry e;
        e.name = "Balanced";
        e.description = "Good visual quality with moderate performance impact";
        e.category = "performance";
        e.dof.enabled = false;
        e.glare.enabled = true;
        e.glare.quality = 2;
        e.godRays.enabled = false;
        e.autoExposure.enabled = true;
        e.toneMapping.exposure = 0.30f;
        e.toneMapping.gamma = 1.15f;
        presets.append(e);
    }

    // ── Vintage ────────────────────────────────────────────────────────
    {
        PresetLibraryEntry e;
        e.name = "Retro 70s";
        e.description = "Warm amber tones with soft contrast reminiscent of 1970s film";
        e.category = "vintage";
        e.toneMapping.exposure = 0.25f;
        e.toneMapping.gamma = 1.3f;
        e.vignetting.strength = 0.12f;
        e.chromaticAberration.enabled = true;
        e.chromaticAberration.lateralDispersionX = 0.003f;
        e.colorGrading.saturation = 0.65f;
        e.colorGrading.contrast = 0.9f;
        e.colorGrading.colorTemp = 4500;
        e.colorGrading.sepia = 0.08f;
        presets.append(e);
    }
    {
        PresetLibraryEntry e;
        e.name = "Bleach Bypass";
        e.description = "Desaturated with high contrast — the iconic film look";
        e.category = "vintage";
        e.toneMapping.exposure = 0.30f;
        e.toneMapping.gamma = 1.0f;
        e.colorGrading.saturation = 0.30f;
        e.colorGrading.contrast = 1.3f;
        e.colorGrading.brightness = 0.95f;
        presets.append(e);
    }

    return presets;
}

bool PPFilterPreset::loadFromLibrary(const QString& presetName)
{
    auto presets = builtinPresets();
    for (const auto& entry : presets) {
        if (entry.name.compare(presetName, Qt::CaseInsensitive) == 0) {
            m_autoExposure = entry.autoExposure;
            m_toneMapping = entry.toneMapping;
            m_dof = entry.dof;
            m_chromaticAberration = entry.chromaticAberration;
            m_vignetting = entry.vignetting;
            m_glare = entry.glare;
            m_godRays = entry.godRays;
            m_colorGrading = entry.colorGrading;
            emit filterChanged();
            return true;
        }
    }
    return false;
}

QStringList PPFilterPreset::libraryPresetNames() const
{
    QStringList names;
    for (const auto& e : builtinPresets())
        names << e.name;
    return names;
}

QStringList PPFilterPreset::libraryPresetCategories() const
{
    QSet<QString> cats;
    for (const auto& e : builtinPresets())
        cats.insert(e.category);
    return QStringList(cats.begin(), cats.end());
}

QStringList PPFilterPreset::libraryPresetsByCategory(const QString& category) const
{
    QStringList names;
    for (const auto& e : builtinPresets()) {
        if (e.category.compare(category, Qt::CaseInsensitive) == 0)
            names << e.name;
    }
    return names;
}
