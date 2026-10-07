#include <algorithm>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "ThrustmasterFFB.h"

#ifdef _WIN32
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#include <objbase.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace ks::device {

const std::vector<ThrustmasterFFB::DeviceInfo>& ThrustmasterFFB::knownDevices() {
    static const std::vector<DeviceInfo> devices = {
        {0xB65D, WheelModel::T300, "T300 RS (PC mode)"},
        {0xB65E, WheelModel::T300, "T300 RS (PS3 mode)"},
        {0xB662, WheelModel::T300, "T300 RS (PS4 mode)"},
        {0xB661, WheelModel::T150, "T150 (PC mode)"},
        {0xB663, WheelModel::T150, "T150 (PS3 mode)"},
        {0xB688, WheelModel::TSPC, "TS-PC Racer"},
        {0xB669, WheelModel::TGT, "T-GT"},
        {0xB66A, WheelModel::TGT, "T-GT II"},
        {0xB653, WheelModel::T80, "T80"},
        {0xB655, WheelModel::TMX, "TMX"},
    };
    return devices;
}

ThrustmasterFFB::ThrustmasterFFB() = default;

ThrustmasterFFB::~ThrustmasterFFB() {
    shutdown();
}

bool ThrustmasterFFB::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::fprintf(stderr, "ThrustmasterFFB: Initializing (DirectInput8)\n");

#ifdef _WIN32
    HRESULT hr = DirectInput8Create(
        GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
        reinterpret_cast<void**>(&m_dinput), nullptr);
    if (FAILED(hr) || !m_dinput) {
        std::fprintf(stderr, "ThrustmasterFFB: DirectInput8Create failed: 0x%08lx\n", (unsigned long)hr);
        return false;
    }

    if (!enumerateDevice()) {
        std::fprintf(stderr, "ThrustmasterFFB: No Thrustmaster wheel found\n");
        shutdown();
        return false;
    }

    hr = m_device->SetCooperativeLevel(GetForegroundWindow(), DISCL_EXCLUSIVE | DISCL_FOREGROUND);
    if (FAILED(hr)) { shutdown(); return false; }

    hr = m_device->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) { shutdown(); return false; }

    hr = m_device->Acquire();
    if (FAILED(hr)) { shutdown(); return false; }

    m_fsbInitialized = createFFBEffect();
    m_acquired = true;
    std::fprintf(stderr, "ThrustmasterFFB: Acquired %s\n", modelName().c_str());
    return true;
#else
    std::fprintf(stderr, "ThrustmasterFFB: DirectInput not available on this platform\n");
    return false;
#endif
}

bool ThrustmasterFFB::enumerateDevice() {
#ifdef _WIN32
    if (!m_dinput) return false;
    struct EnumContext { ThrustmasterFFB* self; bool found; } ctx{this, false};

    auto enumCallback = [](const DIDEVICEINSTANCEA* inst, void* ref) -> BOOL {
        auto* ctx = static_cast<EnumContext*>(ref);
        if (LOWORD(inst->guidProduct.Data1) != ThrustmasterFFB::THRUSTMASTER_VID) {
            return DIENUM_CONTINUE;
        }
        uint16_t pid = HIWORD(inst->guidProduct.Data1);
        for (const auto& dev : knownDevices()) {
            if (pid == dev.pid) {
                ctx->self->m_productId = dev.pid;
                ctx->self->m_model = dev.model;
                std::fprintf(stderr, "ThrustmasterFFB: Found %s (PID: 0x%04x)\n", dev.name, dev.pid);
                break;
            }
        }
        HRESULT hr = ctx->self->m_dinput->CreateDevice(inst->guidInstance, &ctx->self->m_device, nullptr);
        if (SUCCEEDED(hr) && ctx->self->m_device) {
            ctx->found = true;
            return DIENUM_STOP;
        }
        return DIENUM_CONTINUE;
    };

    m_dinput->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCallback, &ctx,
                          DIEDFL_ATTACHEDONLY | DIEDFL_FORCEFEEDBACK);
    return ctx.found;
#else
    return false;
#endif
}

bool ThrustmasterFFB::createFFBEffect() {
#ifdef _WIN32
    if (!m_device) return false;
    m_constantEffect = new DIEFFECT;
    std::memset(m_constantEffect, 0, sizeof(DIEFFECT));
    m_constantEffect->dwSize = sizeof(DIEFFECT);
    m_constantEffect->dwFlags = DIEFF_CARTESIAN | DIEFF_POLAR;
    m_constantEffect->dwDuration = INFINITE;
    m_constantEffect->dwGain = DI_FFNOMINALMAX;
    m_constantEffect->dwTriggerButton = DIEB_NOTRIGGER;
    m_constantEffect->cAxes = 1;
    m_constantEffect->rgdwAxes = new DWORD[1]{DIJOFS_X};
    m_constantEffect->rglDirection = new LONG[1]{0};

    HRESULT hr = m_device->CreateEffect(
        GUID_ConstantForce, m_constantEffect,
        reinterpret_cast<IDirectInputEffect**>(&m_constantEffectRef), nullptr);
    if (FAILED(hr))
        std::fprintf(stderr, "ThrustmasterFFB: CreateEffect failed: 0x%08lx\n", (unsigned long)hr);
    return m_constantEffectRef != nullptr;
#else
    return false;
#endif
}

void ThrustmasterFFB::updateFFB(float torqueNm) {
    if (!m_acquired) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastTorque = torqueNm;
    float maxTorqueNm = 5.0f;
    float normalizedTorque = std::clamp(torqueNm / maxTorqueNm, -1.0f, 1.0f);
    updateConstantForce(normalizedTorque);
}

void ThrustmasterFFB::setConstantForce(float magnitude) {
    if (!m_acquired) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    updateConstantForce(std::clamp(magnitude, -1.0f, 1.0f));
}

void ThrustmasterFFB::setSpringForce(float, float, float) {}
void ThrustmasterFFB::setDamperForce(float, float) {}
void ThrustmasterFFB::setFrictionForce(float) {}
void ThrustmasterFFB::setRumble(float, float) {}
void ThrustmasterFFB::setMotorTemperature(float) {}
void ThrustmasterFFB::setBoostLevel(float) {}
void ThrustmasterFFB::updatePeriodicEffect(float, float) {}

void ThrustmasterFFB::updateConstantForce(float forcePercent) {
#ifdef _WIN32
    if (!m_constantEffectRef) return;
    auto* effect = static_cast<IDirectInputEffect*>(m_constantEffectRef);
    if (!effect) return;
    LONG force = static_cast<LONG>(forcePercent * DI_FFNOMINALMAX);
    m_constantEffect->rglDirection[0] = (force >= 0) ? 1 : -1;
    m_constantEffect->dwGain = static_cast<DWORD>(std::abs(force));
    HRESULT hr = effect->SetParameters(m_constantEffect, DIEP_DIRECTION | DIEP_GAIN);
    if (hr == DIERR_INPUTLOST) {
        m_device->Acquire();
        effect->SetParameters(m_constantEffect, DIEP_DIRECTION | DIEP_GAIN);
    }
    effect->Start(1, 0);
#else
    (void)forcePercent;
#endif
}

void ThrustmasterFFB::processDIInput() {
#ifdef _WIN32
    if (!m_device) return;
    HRESULT hr = m_device->Poll();
    if (FAILED(hr)) {
        if (FAILED(m_device->Acquire())) return;
        m_device->Poll();
    }
#endif
}

void ThrustmasterFFB::shutdown() {
    std::lock_guard<std::mutex> lock(m_mutex);
#ifdef _WIN32
    if (m_constantEffectRef) {
        static_cast<IDirectInputEffect*>(m_constantEffectRef)->Release();
        m_constantEffectRef = nullptr;
    }
    if (m_damperEffectRef) {
        static_cast<IDirectInputEffect*>(m_damperEffectRef)->Release();
        m_damperEffectRef = nullptr;
    }
    if (m_constantEffect) {
        delete[] m_constantEffect->rgdwAxes;
        delete[] m_constantEffect->rglDirection;
        delete m_constantEffect;
        m_constantEffect = nullptr;
    }
    if (m_damperEffect) {
        delete[] m_damperEffect->rgdwAxes;
        delete[] m_damperEffect->rglDirection;
        delete m_damperEffect;
        m_damperEffect = nullptr;
    }
    if (m_device) { m_device->Unacquire(); m_device->Release(); m_device = nullptr; }
    if (m_dinput) { m_dinput->Release(); m_dinput = nullptr; }
#endif
    m_acquired = false;
    m_fsbInitialized = false;
    m_model = WheelModel::Unknown;
    m_productId = 0;
}

std::string ThrustmasterFFB::modelName() const {
    for (const auto& dev : knownDevices()) {
        if (dev.pid == m_productId) return std::string(dev.name);
    }
    return "Thrustmaster (Unknown)";
}

} // namespace ks::device
