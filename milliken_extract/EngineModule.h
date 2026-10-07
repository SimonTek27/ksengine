#pragma once

#include <string>

namespace ks {
namespace engine {

/**
 * @brief Lightweight engine module interface - no Qt dependency
 *
 * This is the base interface for all engine-level modules. It provides
 * lifecycle management and module identity without coupling to the UI layer.
 *
 * Editor-specific modules should inherit from ks::EditorModule instead,
 * which adds QWidget, dock widget, and project management capabilities.
 */
class EngineModule {
public:
    virtual ~EngineModule() = default;

    // Module identity
    virtual std::string moduleName() const = 0;
    virtual std::string moduleId() const = 0;

    // Lifecycle
    virtual bool initialize() { return true; }
    virtual void shutdown() {}
    virtual bool isInitialized() const { return m_initialized; }

    // Module priority (higher = initialized first)
    virtual int priority() const { return 0; }

protected:
    bool m_initialized = false;
};

} // namespace engine
} // namespace ks
