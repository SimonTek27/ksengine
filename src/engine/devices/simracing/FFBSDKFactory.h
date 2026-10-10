#pragma once
#include "KsExport.h"
#include "LogitechFFB.h"
#include "FanatecFFB.h"
#include "SimucubeFFB.h"
#include "ThrustmasterFFB.h"
#include "MozaFFB.h"
#include "SimRacingDevices.h"

namespace ks::device {

// ============================================================================
// FFBBase — Abstract interface for all FFB SDK wrappers
// ============================================================================

class FFBBase {
public:
    virtual ~FFBBase() = default;
    virtual bool initialize() = 0;
    virtual void shutdown() = 0;
    virtual void updateFFB(float torqueNm) = 0;
    virtual bool isSupported() const = 0;
};

// ============================================================================
// FFBLogitechAdapter — Wraps LogitechFFB in FFBBase interface
// ============================================================================
class FFBLogitechAdapter : public FFBBase {
public:
    explicit FFBLogitechAdapter(std::unique_ptr<LogitechFFB> impl)
        : m_impl(std::move(impl)) {}
    bool initialize() override { return m_impl->initialize(); }
    void shutdown() override { m_impl->shutdown(); }
    void updateFFB(float torqueNm) override { m_impl->updateFFB(torqueNm); }
    bool isSupported() const override { return m_impl->isSupported(); }
    LogitechFFB* impl() { return m_impl.get(); }
private:
    std::unique_ptr<LogitechFFB> m_impl;
};

// ============================================================================
// FFBThrustmasterAdapter — Wraps ThrustmasterFFB in FFBBase interface
// ============================================================================
class FFBThrustmasterAdapter : public FFBBase {
public:
    explicit FFBThrustmasterAdapter(std::unique_ptr<ThrustmasterFFB> impl)
        : m_impl(std::move(impl)) {}
    bool initialize() override { return m_impl->initialize(); }
    void shutdown() override { m_impl->shutdown(); }
    void updateFFB(float torqueNm) override { m_impl->updateFFB(torqueNm); }
    bool isSupported() const override { return m_impl->isSupported(); }
    ThrustmasterFFB* impl() { return m_impl.get(); }
private:
    std::unique_ptr<ThrustmasterFFB> m_impl;
};

// ============================================================================
// FFBFanatecAdapter — Wraps FanatecFFB in FFBBase interface
// ============================================================================
class FFBFanatecAdapter : public FFBBase {
public:
    explicit FFBFanatecAdapter(std::unique_ptr<FanatecFFB> impl)
        : m_impl(std::move(impl)) {}
    bool initialize() override { return m_impl->initialize(); }
    void shutdown() override { m_impl->shutdown(); }
    void updateFFB(float torqueNm) override { m_impl->updateFFB(torqueNm); }
    bool isSupported() const override { return m_impl->isSupported(); }
    FanatecFFB* impl() { return m_impl.get(); }
private:
    std::unique_ptr<FanatecFFB> m_impl;
};

// ============================================================================
// FFBSimucubeAdapter — Wraps SimucubeFFB in FFBBase interface
// ============================================================================
class FFBSimucubeAdapter : public FFBBase {
public:
    explicit FFBSimucubeAdapter(std::unique_ptr<SimucubeFFB> impl)
        : m_impl(std::move(impl)) {}
    bool initialize() override { return m_impl->initialize(); }
    void shutdown() override { m_impl->shutdown(); }
    void updateFFB(float torqueNm) override { m_impl->updateFFB(torqueNm); }
    bool isSupported() const override { return m_impl->isSupported(); }
    SimucubeFFB* impl() { return m_impl.get(); }
private:
    std::unique_ptr<SimucubeFFB> m_impl;
};

// ============================================================================
// FFBMozaAdapter — Wraps MozaFFB in FFBBase interface
// ============================================================================
class FFBMozaAdapter : public FFBBase {
public:
    explicit FFBMozaAdapter(std::unique_ptr<MozaFFB> impl)
        : m_impl(std::move(impl)) {}
    bool initialize() override { return m_impl->initialize(); }
    void shutdown() override { m_impl->shutdown(); }
    void updateFFB(float torqueNm) override { m_impl->updateFFB(torqueNm); }
    bool isSupported() const override { return m_impl->isSupported(); }
    MozaFFB* impl() { return m_impl.get(); }
private:
    std::unique_ptr<MozaFFB> m_impl;
};

// ============================================================================
// FFBSDKFactory — Creates the appropriate FFB wrapper based on detected hardware
// ============================================================================

class KSENGINE_API FFBSDKFactory {
public:
    enum class WheelBrand { None, Logitech, Thrustmaster, Fanatec, Simucube, Moza };

    // Auto-detect and create the best available FFB implementation
    static std::unique_ptr<FFBBase> createFFB();

    // Detect which brand of wheel is connected
    static WheelBrand detectWheel();

    // Create a specific brand's FFB implementation
    static std::unique_ptr<FFBBase> createFFBForBrand(WheelBrand brand);

private:
    static WheelBrand detectDirectInputDevices();
};

} // namespace ks::device
