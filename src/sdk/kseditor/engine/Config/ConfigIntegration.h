#pragma once

#include "ConfigLoader.h"
#include "PPFilterPreset.h"
#include "../Graphics/ShaderParamRegistry.h"
#include <QString>

class KsIntegration
{
public:
    static bool initialize(const QString& systemPath);
    
    static KsConfigLoader* config() { return &KsConfigLoader::instance(); }
    
    static ks::ShaderParamRegistry* shaderRegistry() { return &ks::ShaderParamRegistry::instance(); }
    
    static QStringList availablePPFilters() {
        return KsConfigLoader::instance().availablePPFilters();
    }
    
    static bool loadPPFilter(const QString& name) {
        return KsConfigLoader::instance().loadPPFilter(name);
    }
    
    static PPFilterPreset* getCurrentPPFilter() {
        return s_currentPPFilter;
    }
    
    static void setCurrentPPFilter(PPFilterPreset* filter) {
        if (s_currentPPFilter && s_currentPPFilter != filter)
            delete s_currentPPFilter;
        s_currentPPFilter = filter;
    }
    
    static bool initializeSystems(const QString& systemPath);
    
private:
    static PPFilterPreset* s_currentPPFilter;
};

