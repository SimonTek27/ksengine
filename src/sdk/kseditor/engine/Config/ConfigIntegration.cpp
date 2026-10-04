#include "ConfigIntegration.h"

PPFilterPreset* KsIntegration::s_currentPPFilter = nullptr;

bool KsIntegration::initialize(const QString& systemPath)
{
    KsConfigLoader::instance().setSystemPath(systemPath);
    KsConfigLoader::instance().loadAll();
    return true;
}

bool KsIntegration::initializeSystems(const QString& systemPath)
{
    initialize(systemPath);
    
    if (loadPPFilter("default")) {
        auto* preset = new PPFilterPreset("default");
        QSettings settings(systemPath + "cfg/ppfilters/default.ini", QSettings::IniFormat);
        preset->loadFromSettings(&settings);
        setCurrentPPFilter(preset);
    }
    
    return true;
}
