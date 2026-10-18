#pragma once
/**
 * DEPRECATED in engine — CSP is Kinetika content, not core engine.
 * Use: #include "adapters/assetto_corsa/CspConfigParser.h"
 *      ks::adapters::assetto_corsa::CspConfigParser
 */
#include "../../adapters/assetto_corsa/CspConfigParser.h"

namespace ks {
namespace engine {
namespace config {
// Transitional alias so old includes still resolve to the adapter type.
using CspConfigParser = ks::adapters::assetto_corsa::CspConfigParser;
} // namespace config
} // namespace engine
} // namespace ks
