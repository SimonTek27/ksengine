#include "DirectInputJoystick.h"
#include <cstdio>
#include <cstring>
#include <algorithm>
#include <cmath>

#ifdef _WIN32
#define DIRECTINPUT_VERSION 0x0800
#include <windows.h>
#include <dinput.h>
#pragma comment(lib, "dinput8.lib")
#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "ole32.lib")
#endif

namespace ks {
namespace device {

#ifdef _WIN32
struct EnumCtx {
    DirectInputJoystick* self;
    unsigned preferredVid;
    bool found;
    DIDEVICEINSTANCEA chosen{};
};

static BOOL CALLBACK enumCb(const DIDEVICEINSTANCEA* inst, void* ref) {
    auto* ctx = static_cast<EnumCtx*>(ref);
    // Prefer joystick / gamectrl
    ctx->chosen = *inst;
    ctx->found = true;
    if (ctx->preferredVid != 0) {
        unsigned vid = LOWORD(inst->guidProduct.Data1);
        if (vid == ctx->preferredVid)
            return DIENUM_STOP;
        return DIENUM_CONTINUE;
    }
    return DIENUM_STOP; // first device
}
#endif

std::vector<DirectInputJoystick::DeviceInfo> DirectInputJoystick::enumerate() {
    std::vector<DeviceInfo> out;
#ifdef _WIN32
    IDirectInput8A* di = nullptr;
    if (FAILED(DirectInput8Create(GetModuleHandle(nullptr), DIRECTINPUT_VERSION,
                                  IID_IDirectInput8A, reinterpret_cast<void**>(&di), nullptr)) || !di)
        return out;

    struct ListCtx { std::vector<DeviceInfo>* out; };
    ListCtx lc{&out};
    auto cb = [](const DIDEVICEINSTANCEA* inst, void* ref) -> BOOL {
        auto* lc = static_cast<ListCtx*>(ref);
        DeviceInfo info;
        info.name = inst->tszInstanceName;
        info.vendorId = LOWORD(inst->guidProduct.Data1);
        info.productId = HIWORD(inst->guidProduct.Data1);
        lc->out->push_back(info);
        return DIENUM_CONTINUE;
    };
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, cb, &lc, DIEDFL_ATTACHEDONLY);
    di->Release();
#endif
    return out;
}

bool DirectInputJoystick::initialize(unsigned preferredVid) {
#ifdef _WIN32
    m_preferredVid = preferredVid;
    HRESULT hr = DirectInput8Create(
        GetModuleHandle(nullptr), DIRECTINPUT_VERSION, IID_IDirectInput8A,
        reinterpret_cast<void**>(&m_dinput), nullptr);
    if (FAILED(hr) || !m_dinput) {
        std::fprintf(stderr, "DirectInputJoystick: DirectInput8Create failed\n");
        return false;
    }
    if (!createDevice()) {
        shutdown();
        return false;
    }
    return true;
#else
    (void)preferredVid;
    return false;
#endif
}

bool DirectInputJoystick::createDevice() {
#ifdef _WIN32
    auto* di = static_cast<IDirectInput8A*>(m_dinput);
    EnumCtx ctx{this, m_preferredVid, false, {}};
    di->EnumDevices(DI8DEVCLASS_GAMECTRL, enumCb, &ctx, DIEDFL_ATTACHEDONLY);
    if (!ctx.found) {
        std::fprintf(stderr, "DirectInputJoystick: no game controller\n");
        return false;
    }

    HRESULT hr = di->CreateDevice(ctx.chosen.guidInstance,
                                  reinterpret_cast<IDirectInputDevice8A**>(&m_device), nullptr);
    if (FAILED(hr) || !m_device) {
        std::fprintf(stderr, "DirectInputJoystick: CreateDevice failed\n");
        return false;
    }

    auto* dev = static_cast<IDirectInputDevice8A*>(m_device);
    hr = dev->SetDataFormat(&c_dfDIJoystick2);
    if (FAILED(hr)) {
        std::fprintf(stderr, "DirectInputJoystick: SetDataFormat failed\n");
        return false;
    }

    hr = dev->SetCooperativeLevel(GetForegroundWindow(), DISCL_FOREGROUND | DISCL_NONEXCLUSIVE);
    if (FAILED(hr)) {
        // try without window
        hr = dev->SetCooperativeLevel(GetDesktopWindow(), DISCL_BACKGROUND | DISCL_NONEXCLUSIVE);
    }

    // Axis ranges -1000..1000
    DIPROPRANGE range;
    range.diph.dwSize = sizeof(DIPROPRANGE);
    range.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    range.diph.dwHow = DIPH_BYOFFSET;
    range.lMin = -1000;
    range.lMax = 1000;
    for (DWORD off : {DIJOFS_X, DIJOFS_Y, DIJOFS_Z, DIJOFS_RX, DIJOFS_RY, DIJOFS_RZ,
                      DIJOFS_SLIDER(0), DIJOFS_SLIDER(1)}) {
        range.diph.dwObj = off;
        dev->SetProperty(DIPROP_RANGE, &range.diph);
    }

    DIPROPDWORD dead;
    dead.diph.dwSize = sizeof(DIPROPDWORD);
    dead.diph.dwHeaderSize = sizeof(DIPROPHEADER);
    dead.diph.dwHow = DIPH_DEVICE;
    dead.diph.dwObj = 0;
    dead.dwData = 0; // we apply deadzone in InputManager
    dev->SetProperty(DIPROP_DEADZONE, &dead.diph);

    hr = dev->Acquire();
    if (FAILED(hr)) {
        std::fprintf(stderr, "DirectInputJoystick: Acquire failed (0x%08lx)\n", (unsigned long)hr);
        // still mark partial
    }

    m_info.name = ctx.chosen.tszInstanceName;
    m_info.vendorId = LOWORD(ctx.chosen.guidProduct.Data1);
    m_info.productId = HIWORD(ctx.chosen.guidProduct.Data1);
    m_info.hasForceFeedback = (ctx.chosen.dwDevType & DIDEVTYPE_HID) != 0;
    m_acquired = true;

    std::fprintf(stderr, "DirectInputJoystick: acquired \"%s\" VID=%04X PID=%04X\n",
                 m_info.name.c_str(), m_info.vendorId, m_info.productId);
    return true;
#else
    return false;
#endif
}

void DirectInputJoystick::releaseDevice() {
#ifdef _WIN32
    if (m_device) {
        auto* dev = static_cast<IDirectInputDevice8A*>(m_device);
        dev->Unacquire();
        dev->Release();
        m_device = nullptr;
    }
    if (m_dinput) {
        static_cast<IDirectInput8A*>(m_dinput)->Release();
        m_dinput = nullptr;
    }
#endif
    m_acquired = false;
}

void DirectInputJoystick::shutdown() {
    releaseDevice();
    std::memset(m_axes, 0, sizeof(m_axes));
    m_buttons = 0;
}

void DirectInputJoystick::update() {
#ifdef _WIN32
    if (!m_device || !m_acquired) return;
    auto* dev = static_cast<IDirectInputDevice8A*>(m_device);

    HRESULT hr = dev->Poll();
    if (FAILED(hr)) {
        hr = dev->Acquire();
        if (FAILED(hr)) {
            m_acquired = false;
            return;
        }
        dev->Poll();
    }

    DIJOYSTATE2 js{};
    hr = dev->GetDeviceState(sizeof(js), &js);
    if (FAILED(hr)) return;

    auto normBipolar = [](LONG v) -> double {
        return std::clamp(v / 1000.0, -1.0, 1.0);
    };
    auto normUnipolar = [](LONG v) -> double {
        // some devices report 0..1000, others -1000..1000 for pedals
        double n = v / 1000.0;
        if (n < 0) n = (n + 1.0) * 0.5; // map -1..1 → 0..1
        return std::clamp(n, 0.0, 1.0);
    };

    // Map DI axes → InputManager layout:
    // 0 LX, 1 LY, 2 RX, 3 RY, 4 LT/brake, 5 RT/throttle, 6 slider0, 7 slider1
    m_axes[0] = normBipolar(js.lX);
    m_axes[1] = normBipolar(js.lY);
    m_axes[2] = normBipolar(js.lRx);
    m_axes[3] = normBipolar(js.lRy);
    m_axes[4] = normUnipolar(js.lZ);   // often brake or clutch
    m_axes[5] = normUnipolar(js.lRz);  // often throttle
    m_axes[6] = normUnipolar(js.rglSlider[0]);
    m_axes[7] = normUnipolar(js.rglSlider[1]);

    m_buttons = 0;
    for (int i = 0; i < 32; ++i) {
        if (js.rgbButtons[i] & 0x80)
            m_buttons |= (1u << i);
    }

    m_info.numButtons = 32;
    m_info.numAxes = 8;
#else
    (void)0;
#endif
}

} // namespace device
} // namespace ks
