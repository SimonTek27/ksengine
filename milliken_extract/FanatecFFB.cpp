#include <algorithm>
#include <string>
#include <cstdio>
#include <cstring>
#include <cmath>
#include "FanatecFFB.h"

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

const std::vector<FanatecFFB::DeviceInfo>& FanatecFFB::knownDevices() {
    static const std::vector<DeviceInfo> devices = {
        {0x0001, WheelModel::CSLDD, "CSL DD", 5.0f},
        {0x0002, WheelModel::CSLDDPlus, "CSL DD+", 8.0f},
        {0x0003, WheelModel::CSLElite, "CSL Elite", 6.0f},
        {0x0004, WheelModel::ClubSportV2, "ClubSport V2", 8.0f},
        {0x0005, WheelModel::ClubSportV3, "ClubSport V3", 9.0f},
        {0x0006, WheelModel::PodiumDD1, "Podium DD1", 20.0f},
        {0x0007, WheelModel::PodiumDD2, "Podium DD2", 25.0f},
        {0x0008, WheelModel::PodiumDDPlus, "Podium DD+", 15.0f},
    };
    return devices;
}

float FanatecFFB::maxTorqueForModel(WheelModel model) {
    for (const auto& dev : knownDevices()) {
        if (dev.model == model) return dev.maxTorque;
    }
    return 10.0f;
}

float FanatecFFB::maxTorqueNm() const {
    return maxTorqueForModel(m_model);
}

FanatecFFB::FanatecFFB() = default;

FanatecFFB::~FanatecFFB() {
    shutdown();
}

bool FanatecFFB::initialize() {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::fprintf(stderr, "FanatecFFB: Initializing (DirectInput8)\n");

#ifdef _WIN32
    HRESULT hr = DirectInput8Create(
        GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
        reinterpret_cast<void**>(&m_dinput), nullptr);
    if (FAILED(hr) || !m_dinput) {
        std::fprintf(stderr, "FanatecFFB: DirectInput8Create failed: 0x%08lx\n", (unsigned long)hr);
        return false;
    }

    if (!enumerateDevice()) {
        std::fprintf(stderr, "FanatecFFB: No Fanatec wheel found\n");
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
    std::fprintf(stderr, "FanatecFFB: Acquired %s\n", modelName().c_str());
    return true;
#else
    std::fprintf(stderr, "FanatecFFB: DirectInput not available on this platform\n");
    return false;
#endif
}

bool FanatecFFB::enumerateDevice() {
#ifdef _WIN32
    if (!m_dinput) return false;
    struct EnumContext { FanatecFFB* self; bool found; } ctx{this, false};

    auto enumCallback = [](const DIDEVICEINSTANCEA* inst, void* ref) -> BOOL {
        auto* ctx = static_cast<EnumContext*>(ref);
        if (LOWORD(inst->guidProduct.Data1) != FanatecFFB::FANATEC_VID) {
            return DIENUM_CONTINUE;
        }
        uint16_t pid = HIWORD(inst->guidProduct.Data1);
        for (const auto& dev : knownDevices()) {
            if (pid == dev.pid) {
                ctx->self->m_productId = dev.pid;
                ctx->self->m_model = dev.model;
                std::fprintf(stderr, "FanatecFFB: Found %s (PID: 0x%04x)\n", dev.name, dev.pid);
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

bool FanatecFFB::createFFBEffect() {
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
        std::fprintf(stderr, "FanatecFFB: CreateEffect failed: 0x%08lx\n", (unsigned long)hr);
    return m_constantEffectRef != nullptr;
#else
    return false;
#endif
}

void FanatecFFB::updateFFB(float torqueNm) {
    if (!m_acquired) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    m_lastTorque = torqueNm;
    float maxNm = maxTorqueNm();
    float normalizedTorque = std::clamp(torqueNm / maxNm, -1.0f, 1.0f);
    updateConstantForce(normalizedTorque);
}

void FanatecFFB::setConstantForce(float magnitude) {
    if (!m_acquired) return;
    std::lock_guard<std::mutex> lock(m_mutex);
    updateConstantForce(std::clamp(magnitude, -1.0f, 1.0f));
}

void FanatecFFB::setSpringForce(float, float, float) {}
void FanatecFFB::setDamperForce(float, float) {}
void FanatecFFB::setFrictionForce(float) {}
void FanatecFFB::setRumble(float, float) {}

void FanatecFFB::updateConstantForce(float forcePercent) {
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

void FanatecFFB::processDIInput() {
#ifdef _WIN32
    if (!m_device) return;
    HRESULT hr = m_device->Poll();
    if (FAILED(hr)) {
        if (FAILED(m_device->Acquire())) return;
        m_device->Poll();
    }
#endif
}

void FanatecFFB::shutdown() {
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

std::string FanatecFFB::modelName() const {
    for (const auto& dev : knownDevices()) {
        if (dev.pid == m_productId) return std::string(dev.name);
    }
    return "Fanatec (Unknown)";
}

} // namespace ks::device
