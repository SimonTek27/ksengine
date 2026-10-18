#pragma once
/** Kinetika GUID tables — adapter, not core engine. */
#include <string>
#include <vector>

namespace ks {
namespace adapters {
namespace assetto_corsa {

class ACGuidsParser {
public:
    bool load(const std::string& /*path*/) { return false; }
    const std::vector<std::string>& guids() const { return m_guids; }
private:
    std::vector<std::string> m_guids;
};

} // namespace assetto_corsa
} // namespace adapters
} // namespace ks
