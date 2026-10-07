#include "FFBSDKFactory.h"
#include <cstdio>

#ifdef _WIN32
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#endif

namespace ks::device {

// ============================================================================
// Auto-detect and create FFB
// ============================================================================

std::unique_ptr<FFBBase> FFBSDKFactory::createFFB() {
    WheelBrand brand = detectWheel();
    if (brand == WheelBrand::None) {
            std::fprintf(stderr, "FFBSDKFactory: No FFB wheel detected\n");
        return nullptr;
    }
    return createFFBForBrand(brand);
}

// ============================================================================
// Detect wheel brand
// ============================================================================

FFBSDKFactory::WheelBrand FFBSDKFactory::detectWheel() {
    // Try DirectInput detection first (works for Logitech, Thrustmaster, and some MOZA)
    WheelBrand brand = detectDirectInputDevices();
    if (brand != WheelBrand::None) return brand;

    // MOZA wheels may not appear in DirectInput — try HID detection
    // Check if any MOZA HID device is connected
    {
        auto impl = std::make_unique<MozaFFB>();
        // Try initialize just to detect — shutdown immediately if found
        // This is safe because MozaFFB::initialize() only opens the HID handle
        if (impl->initialize()) {
            std::fprintf(stderr, "FFBSDKFactory: MOZA wheel detected via HID\n");
            impl->shutdown();
            return WheelBrand::Moza;
        }
    }

    // TODO: Fanatec detection via FDB SDK or USB HID
    // TODO: Simucube detection via TrueDrive TCP scan

    return WheelBrand::None;
}

FFBSDKFactory::WheelBrand FFBSDKFactory::detectDirectInputDevices() {
#ifdef _WIN32
    IDirectInput8A* dinput = nullptr;
    HRESULT hr = DirectInput8Create(
        GetModuleHandle(nullptr),
        DIRECTINPUT_VERSION,
        IID_IDirectInput8A,
        reinterpret_cast<void**>(&dinput),
        nullptr
    );
    if (FAILED(hr) || !dinput) return WheelBrand::None;

    struct DetectContext {
        WheelBrand foundBrand = WheelBrand::None;
    } ctx;

    auto enumCallback = [](const DIDEVICEINSTANCEA* inst, void* ref) -> BOOL {
        auto* ctx = static_cast<DetectContext*>(ref);

        uint16_t vid = LOWORD(inst->guidProduct.Data1);
        uint16_t pid = HIWORD(inst->guidProduct.Data1);

        // Logitech VID
        if (vid == LogitechFFB::LOGITECH_VID) {
            std::fprintf(stderr, "FFBSDKFactory: Logitech device detected (PID: )\n");
            ctx->foundBrand = WheelBrand::Logitech;
            return DIENUM_STOP;
        }

        // Thrustmaster VID
        if (vid == ThrustmasterFFB::THRUSTMASTER_VID) {
            std::fprintf(stderr, "FFBSDKFactory: Thrustmaster device detected (PID: )\n");
            ctx->foundBrand = WheelBrand::Thrustmaster;
            return DIENUM_STOP;
        }

        // MOZA VID
        if (vid == MozaFFB::MOZA_VID) {
            std::fprintf(stderr, "FFBSDKFactory: MOZA device detected (PID: )\n");
            ctx->foundBrand = WheelBrand::Moza;
            return DIENUM_STOP;
        }

        return DIENUM_CONTINUE;
    };

    dinput->EnumDevices(
        DI8DEVCLASS_GAMECTRL,
        enumCallback,
        &ctx,
        DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK
    );

    dinput->Release();
    return ctx.foundBrand;

#else
    return WheelBrand::None;
#endif
}

// ============================================================================
// Create specific brand FFB
// ============================================================================

std::unique_ptr<FFBBase> FFBSDKFactory::createFFBForBrand(WheelBrand brand) {
    switch (brand) {
        case WheelBrand::Logitech: {
            auto impl = std::make_unique<LogitechFFB>();
            if (impl->initialize()) {
            std::fprintf(stderr, "FFBSDKFactory: Logitech FFB initialized\n");
                return std::make_unique<FFBLogitechAdapter>(std::move(impl));
            }
            std::fprintf(stderr, "WARN: FFBSDKFactory: Logitech FFB initialization failed\n");
            return nullptr;
        }
        case WheelBrand::Thrustmaster: {
            auto impl = std::make_unique<ThrustmasterFFB>();
            if (impl->initialize()) {
            std::fprintf(stderr, "FFBSDKFactory: Thrustmaster FFB initialized\n");
                return std::make_unique<FFBThrustmasterAdapter>(std::move(impl));
            }
            std::fprintf(stderr, "WARN: FFBSDKFactory: Thrustmaster FFB initialization failed\n");
            return nullptr;
        }
        case WheelBrand::Fanatec: {
            auto impl = std::make_unique<FanatecFFB>();
            if (impl->initialize()) {
            std::fprintf(stderr, "FFBSDKFactory: Fanatec FFB initialized\n");
                return std::make_unique<FFBFanatecAdapter>(std::move(impl));
            }
            std::fprintf(stderr, "WARN: FFBSDKFactory: Fanatec FFB initialization failed\n");
            return nullptr;
        }
        case WheelBrand::Simucube: {
            auto impl = std::make_unique<SimucubeFFB>();
            if (impl->initialize()) {
            std::fprintf(stderr, "FFBSDKFactory: Simucube FFB initialized\n");
                return std::make_unique<FFBSimucubeAdapter>(std::move(impl));
            }
            std::fprintf(stderr, "WARN: FFBSDKFactory: Simucube FFB initialization failed\n");
            return nullptr;
        }
        case WheelBrand::Moza: {
            auto impl = std::make_unique<MozaFFB>();
            if (impl->initialize()) {
            std::fprintf(stderr, "FFBSDKFactory: MOZA FFB initialized\n");
                return std::make_unique<FFBMozaAdapter>(std::move(impl));
            }
            std::fprintf(stderr, "WARN: FFBSDKFactory: MOZA FFB initialization failed\n");
            return nullptr;
        }
        case WheelBrand::None:
        default:
            return nullptr;
    }
}

} // namespace ks::device
