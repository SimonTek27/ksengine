#pragma once
/** Kinetika / CSP adapter — not core engine. */
#include <string>
#include <unordered_map>

namespace ks {
namespace adapters {
namespace assetto_corsa {

/** Parses Custom Shaders Patch style config (community content). */
class CspConfigParser {
public:
    bool load(const std::string& path);
    std::string get(const std::string& key, const std::string& def = "") const;
    void set(const std::string& key, const std::string& value);

private:
    std::unordered_map<std::string, std::string> m_vals;
};

} // namespace assetto_corsa
} // namespace adapters
} // namespace ks
