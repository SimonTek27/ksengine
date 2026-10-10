#pragma once
#include <string>
namespace ks { namespace engine { namespace assets {
// Installed layout (see cmake/KsInstallLayout.cmake): everything is resolved
// relative to the working directory, which for a normally launched build is
// the directory holding ksim.exe / kssimserver.exe.
class Paths {
public:
    static std::string projectRoot() { return "."; }
    static std::string content() { return "content"; }
    static std::string user() { return "user"; }
    static std::string server() { return "server"; }
    static std::string shaders() { return "system/shaders"; }
    // Install-wide engine settings (JSON, shipped as {}): see Config/EngineSettings.h
    static std::string systemCfg() { return "system/cfg"; }
    /** system/cfg/<file> */
    static std::string systemCfgFile(const std::string& file) {
        return systemCfg() + "/" + file;
    }
    // Post-processing filters (JSON, one file per preset): system/ppfilters/
    // Each file follows the schema "ksengine.ppfilter/1" (see system/ppfilters/*.json)
    static std::string ppFilters() { return "system/ppfilters"; }
    /** system/ppfilters/<file> */
    static std::string ppFiltersFile(const std::string& file) {
        return ppFilters() + "/" + file;
    }
    // Game strings per language (system/i18n/<lang>.json): see Config/Locale.h
    static std::string i18n() { return "system/i18n"; }
    /** system/i18n/<lang>.json */
    static std::string i18nFile(const std::string& lang) {
        return i18n() + "/" + lang + ".json";
    }
};
}}} // namespace
