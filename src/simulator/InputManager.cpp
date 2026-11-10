#include "InputManager.h"
#include <cstdio>
#include <cmath>
#include <algorithm>
#include <cstring>

#ifdef _WIN32
#include <windows.h>
#include "devices/xinput/XInputDevice.h"
#include "devices/DirectInputJoystick.h"
#endif

namespace ks {
namespace sim {

InputManager::InputManager() = default;
InputManager::~InputManager() = default;

bool InputManager::initialize() {
#ifdef _WIN32
    m_dinput = std::make_unique<ks::device::DirectInputJoystick>();
    if (m_dinput->initialize()) {
        std::fprintf(stderr, "InputManager: DirectInput device ready\n");
        setMapping(JoystickMapping::wheelDefault());
    } else {
        std::fprintf(stderr, "InputManager: no DirectInput game controller\n");
        m_dinput.reset();
    }

    m_xinput = std::make_unique<ks::device::XInputDevice>();
    if (m_xinput->initialize()) {
        std::fprintf(stderr, "InputManager: XInput pad ready\n");
        if (!m_dinput)
            setMapping(JoystickMapping::gamepadDefault());
    } else {
        m_xinput.reset();
    }
#endif
    m_controlsConfig.loadFromFile("system/cfg/controls.json");
    std::fprintf(stderr, "InputManager: mapping steer=%d throttle=%d brake=%d\n",
                 m_map.steerAxis, m_map.throttleAxis, m_map.brakeAxis);
    return true;
}

double InputManager::curve(double v, double gamma, double deadZone, bool bipolar) {
    if (bipolar) {
        if (std::abs(v) < deadZone) return 0.0;
        double sign = v > 0 ? 1.0 : -1.0;
        double n = (std::abs(v) - deadZone) / (1.0 - deadZone);
        return sign * std::pow(std::clamp(n, 0.0, 1.0), gamma);
    }
    if (v < deadZone) return 0.0;
    double n = (v - deadZone) / (1.0 - deadZone);
    return std::pow(std::clamp(n, 0.0, 1.0), gamma);
}

void InputManager::applyMapping(const double axes[AXIS_COUNT], unsigned buttons) {
    auto axis = [&](int idx) -> double {
        if (idx < 0 || idx >= AXIS_COUNT) return 0.0;
        m_axisRaw[idx] = axes[idx];
        // Calibrazione in corsa: gli estremi dell'asse bersaglio si
        // accumulano mentre il driver muove il volante/pedale.
        if (m_calibrating && idx == m_calibAxis) {
            m_calibMinSeen = std::min(m_calibMinSeen, axes[idx]);
            m_calibMaxSeen = std::max(m_calibMaxSeen, axes[idx]);
        }
        return axes[idx];
    };
    // Corsa utile valutata sul vettore grezzo: min/max della calibrazione
    // ristretta diventano il 0 e il 1 della curva qui sotto.
    auto calUnipolar = [&](double raw, const AxisCalibration& c) {
        if (!c.valid()) return raw;
        return std::clamp((raw - c.min) / (c.max - c.min), 0.0, 1.0);
    };
    // Bipolare (sterzo): center al centro dichiarato; se mai rilevato, il
    // punto intero della corsa. Output ±1 indipendente dall'asimmetria degli
    // estremi del sensore (avvolgimento raw slabile).
    auto calBipolar = [&](double raw, const AxisCalibration& c) {
        if (!c.valid()) return raw;
        const double center = c.centerValid() ? c.center
                                              : (c.min + c.max) * 0.5;
        const double span = raw < center ? (center - c.min) : (c.max - center);
        if (span < 1e-3) return 0.0;
        return std::clamp((raw - center) / span, -1.0, 1.0);
    };

    double st = calBipolar(axis(m_map.steerAxis), m_calib[m_map.steerAxis]);
    st = curve(st, m_map.steerGamma, m_map.deadZone, true) * m_map.steerSensitivity;
    if (m_map.invertSteer) st = -st;
    m_steer = std::clamp(st, -1.0, 1.0);

    if (m_map.throttleIsCombined) {
        double a = calUnipolar(axis(m_map.throttleAxis), m_calib[m_map.throttleAxis]);
        if (a >= 0) {
            m_throttle = curve(a, m_map.throttleGamma, m_map.throttleDeadZone, false) *
                         m_map.throttleSensitivity;
            m_brake = 0;
        } else {
            m_brake = curve(-a, m_map.brakeGamma, m_map.brakeDeadZone, false) *
                      m_map.brakeSensitivity;
            m_throttle = 0;
        }
    } else {
        double th = calUnipolar(axis(m_map.throttleAxis), m_calib[m_map.throttleAxis]);
        double br = calUnipolar(axis(m_map.brakeAxis), m_calib[m_map.brakeAxis]);
        if (th < 0) th = 0;
        if (br < 0) br = 0;
        if (m_map.invertThrottle) th = 1.0 - th;
        if (m_map.invertBrake) br = 1.0 - br;
        m_throttle = curve(th, m_map.throttleGamma, m_map.throttleDeadZone, false) *
                     m_map.throttleSensitivity;
        m_brake = curve(br, m_map.brakeGamma, m_map.brakeDeadZone, false) *
                  m_map.brakeSensitivity;
    }

    if (m_map.clutchAxis >= 0) {
        double c = calUnipolar(axis(m_map.clutchAxis), m_calib[m_map.clutchAxis]);
        if (c < 0) c = 0;
        m_clutch = std::clamp(
            curve(c, 1.0, m_map.clutchDeadZone, false) * m_map.clutchSensitivity, 0.0, 1.0);
    } else if (m_map.clutchButton >= 0) {
        m_clutch = (buttons & (1u << m_map.clutchButton)) ? 1.0 : 0.0;
    } else {
        m_clutch = 0;
    }
    // A keyboard key is a pedal held to the floor: it wins over a pad that may
    // have no clutch at all (and over one that does, as the later input).
    if (keyHeld(m_kb.clutch)) m_clutch = 1.0;

    // Handbrake follows the button while it is held, like the pedal axes follow
    // their own: a latched toggle would leave the brake on after the pad is gone.
    m_handbrake = (m_map.handbrakeButton >= 0 && (buttons & (1u << m_map.handbrakeButton)))
                      ? 1.0 : 0.0;

    bool up = m_map.shiftUpButton >= 0 && (buttons & (1u << m_map.shiftUpButton));
    bool dn = m_map.shiftDownButton >= 0 && (buttons & (1u << m_map.shiftDownButton));
    if (up && !m_prevShiftUpBtn) m_shiftUp = true;
    if (dn && !m_prevShiftDownBtn) m_shiftDown = true;
    m_prevShiftUpBtn = up;
    m_prevShiftDownBtn = dn;

    // H-shifter: prima di tutto un bottone per marcia. Con gearSelectButtonHold
    // il dispositivo tiene premuto il bottone della marcia corrente e rilascia
    // tutto in folle: la marcia è "quale bottone è giù", letta a livello e non
    // a fronte. Senza hold ogni bottone è a fronte (device senza folle
    // stabile): una pressione chiede la marcia, il passaggio a folle avviene
    // quando nessun bottone di marcia resta premuto.
    int held = 0; // 0 = nessuno/folle, 1..7 marce, -1 retro
    for (int i = 0; i < JoystickMapping::kGearButtonCount; ++i) {
        const int b = i < 7 ? m_map.gearButtons[i] : m_map.reverseGearButton;
        if (b >= 0 && (buttons & (1u << b))) {
            held = (i < 7) ? i + 1 : -1;
            break;
        }
    }
    const bool anyGearMapped =
        m_map.reverseGearButton >= 0 ||
        [&] { for (int i = 0; i < 7; ++i) if (m_map.gearButtons[i] >= 0) return true; return false; }();
    if (anyGearMapped) {
        if (m_controlsConfig.gearSelectButtonHold) {
            // Livello: solo quando la marcia letta cambia emettiamo la richiesta.
            if (held != m_prevGearHeld) {
                m_gearRequest = held;
                m_prevGearHeld = held;
            }
        } else {
            // A fronte: solo pressioni verso marce avanti/retro; il rilascio
            // verso "nessuno" non genera folle (il dispositivo non garantisce
            // una posizione di folle distinguibile).
            if (held != 0 && held != m_prevGearHeld) m_gearRequest = held;
            if (held != 0) m_prevGearHeld = held;
        }
    }

    // Filtro analogico: dischiude solo i canali selezionati nel bitmask
    // (1=throttle 2=brake 4=handbrake 8=steering 16=clutch). Il filtro vede
    // il segnale già mappato (curva/deadzone/sensibilità inclusi), perché
    // l'uscita del filtro è l'input del veicolo.
    const auto& cc = m_controlsConfig;
    m_throttle = filterChannel(m_filterThrottle, m_throttle,
                               (cc.analogControlFilter & 1) != 0);
    m_brake = filterChannel(m_filterBrake, m_brake,
                            (cc.analogControlFilter & 2) != 0);
    m_handbrake = filterChannel(m_filterHandbrake, m_handbrake,
                                (cc.analogControlFilter & 4) != 0);
    m_steer = filterChannel(m_filterSteer, m_steer,
                            (cc.analogControlFilter & 8) != 0);
    m_clutch = filterChannel(m_filterClutch, m_clutch,
                             (cc.analogControlFilter & 16) != 0);

    m_rawThrottle = m_throttle;
    m_rawBrake = m_brake;
    m_rawSteer = m_steer;
}

/*
 * Filtro analogico a finestra (analogControlFilter/filterSamples): tiene gli
 * ultimi N campioni del canale, scarta il più basso e il più alto come dice
 * la documentazione rFactor (3-10 campioni) e media i restanti. Finché la
 * finestra non è piena passa il valore tal quale, così il riempimento non
 * introduce alcun ritardo sulla prima pedalata.
 */
double InputManager::filterChannel(AnalogChannel& ch, double sample, bool enabled) {
    const int samples = std::clamp(m_controlsConfig.filterSamples, 3, 10);
    if (!enabled) {
        ch.count = 0;
        return sample;
    }
    ch.buf[ch.head] = sample;
    ch.head = (ch.head + 1) % samples;
    if (ch.count < samples) ++ch.count;
    if (ch.count < samples) return sample; // finestra in riempimento: passthrough
    double lo = ch.buf[0], hi = ch.buf[0], sum = 0.0;
    for (int i = 0; i < samples; ++i) {
        lo = std::min(lo, ch.buf[i]);
        hi = std::max(hi, ch.buf[i]);
        sum += ch.buf[i];
    }
    // Mediano i restanti (sum - min - max), come la descrizione del filtro:
    // con 3 campioni residue ne resta 1, con 10 ne restano 8.
    return (sum - lo - hi) / static_cast<double>(samples - 2);
}

void InputManager::resetFilterChannels() {
    m_filterThrottle = AnalogChannel{};
    m_filterBrake = AnalogChannel{};
    m_filterSteer = AnalogChannel{};
    m_filterClutch = AnalogChannel{};
    m_filterHandbrake = AnalogChannel{};
}

void InputManager::injectAxes(const double axes[AXIS_COUNT], unsigned buttons) {
    std::memcpy(m_axes, axes, sizeof(m_axes));
    m_buttons = buttons;
    m_hasInjected = true;
}

void InputManager::injectAxis(int index, double value) {
    if (index < 0 || index >= AXIS_COUNT) return;
    m_axes[index] = value;
    m_hasInjected = true;
}

void InputManager::injectButton(int index, bool pressed) {
    if (index < 0 || index >= 32) return;
    if (pressed) m_buttons |= (1u << index);
    else m_buttons &= ~(1u << index);
    m_hasInjected = true;
}

void InputManager::processInjected() {
    applyMapping(m_axes, m_buttons);
}

bool InputManager::keyHeld(int vk) const {
    if (vk <= 0) return false;
    auto norm = [](int k) { return (k >= 'a' && k <= 'z') ? k - ('a' - 'A') : k; };
    const int up = norm(vk);
    if (isKeyDown(up)) return true;
    const int low = (up >= 'A' && up <= 'Z') ? up + ('a' - 'A') : up;
    if (isKeyDown(low)) return true;
#ifdef _WIN32
    // The app only forwards the bindings it knows about, and this header has no
    // window procedure: read the key state directly so a rebound pedal works
    // without the app having to know about it.
    return (GetAsyncKeyState(up) & 0x8000) != 0 ||
           (low != up && (GetAsyncKeyState(low) & 0x8000) != 0);
#else
    return false;
#endif
}

int InputManager::calibrationEnd() {
    if (!m_calibrating || m_calibAxis < 0) return -1;
    // Se il centro non è stato catturato esplicitamente, il centro della
    // corsa osservata (media dei due estremi) è la scelta più sicura per
    // asse bipolare; per i pedali (unipolari) il centro non viene usato.
    AxisCalibration c;
    c.min = m_calibMinSeen;
    c.max = m_calibMaxSeen;
    if (m_calibHasCenter) c.center = m_calibCenterSeen;
    else c.center = (c.min + c.max) * 0.5;
    if (!c.valid()) {
        m_calibrating = false;
        m_calibAxis = -1;
        return -1;
    }
    // Il centro deve stare dentro la corsa: altrimenti scartiamo la
    // cattura e ricadiamo sul punto intero.
    if (c.center < c.min || c.center > c.max) c.center = (c.min + c.max) * 0.5;
    m_calib[m_calibAxis] = c;
    m_calibrating = false;
    m_calibAxis = -1;
    m_calibStep = CalibrationStep::Idle;
    return 0;
}

std::string InputManager::saveProfile(const std::string& dir, const std::string& name) const {
    std::error_code ec;
    std::filesystem::create_directories(dir, ec);
    if (ec) return {};
    std::string safe = name;
    for (char& ch : safe)
        if (ch == '/' || ch == '\\' || ch == ':' || ch == '*' || ch == '?' ||
            ch == '"' || ch == '<' || ch == '>' || ch == '|')
            ch = '_';
    if (safe.empty()) return {};
    const std::string path = (std::filesystem::path(dir) / (safe + ".json")).string();
    if (!saveInputProfileJson(path, m_map, m_calib, m_kb)) return {};
    return path;
}

bool InputManager::loadProfile(const std::string& path) {
    return loadInputProfileJson(path, m_map, m_calib, m_kb);
}

void InputManager::update() {
    // dt per i canali a peso (keyboardWeighted): misurato tra due tick.
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTick.time_since_epoch().count() == 0
        ? (1.0 / 60.0)
        : std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;
    const bool weighted = m_controlsConfig.keyboardWeighted != 0;
    const double damping = std::clamp(m_controlsConfig.keyboardDamping, 0.5, 4.0);
    const double rateScale = 1.0 / damping; // 2.0 -> critica, <2 underdamped, >2 overdamped

    m_shiftUp = false;
    m_shiftDown = false;

    // Starter: a rising edge is one crank request. Read on every path (pad,
    // wheel, keyboard) because the branches below return early.
    if (starterPressEdge(keyHeld(m_kb.starter), m_prevStarterKey))
        m_starterRequested = true;

    if (m_hasInjected) {
        processInjected();
        m_hasInjected = false;
        return;
    }

#ifdef _WIN32
    if (m_preferDi && m_dinput && m_dinput->isConnected()) {
        m_dinput->update();
        injectAxes(m_dinput->axes(), m_dinput->buttons());
        processInjected();
        m_hasInjected = false;
        return;
    }
    if (m_xinput && m_xinput->isConnected()) {
        processXInput();
        return;
    }
    if (m_dinput && m_dinput->isConnected()) {
        m_dinput->update();
        injectAxes(m_dinput->axes(), m_dinput->buttons());
        processInjected();
        m_hasInjected = false;
        return;
    }
#endif
    processKeyboard();
}

bool InputManager::hasDirectInput() const {
#ifdef _WIN32
    return m_dinput != nullptr;
#else
    return false;
#endif
}

bool InputManager::isDirectInputConnected() const {
#ifdef _WIN32
    return m_dinput && m_dinput->isConnected();
#else
    return false;
#endif
}

void InputManager::pollMenuNavigation() {
    m_menuKey = 0;
#ifdef _WIN32
    if (!m_xinput || !m_xinput->isConnected()) {
        m_padMenu = ui::PadButtons{};
        return;
    }
    m_xinput->update();
    // Dead zone is the pad's own for steering; the menu wants an unambiguous
    // push, so the stick has to be well past half before it counts at all.
    ui::PadButtons now;
    now.up = m_xinput->dpadUp() || m_xinput->leftStickY() > 0.6;
    now.down = m_xinput->dpadDown() || m_xinput->leftStickY() < -0.6;
    now.left = m_xinput->dpadLeft() || m_xinput->leftStickX() < -0.6;
    now.right = m_xinput->dpadRight() || m_xinput->leftStickX() > 0.6;
    now.confirm = m_xinput->buttonA();
    now.back = m_xinput->buttonB() || m_xinput->buttonStart();
    // One step per press: the previous frame goes back in.
    m_menuKey = ui::padMenuKey(now, m_padMenu);
    m_padMenu = now;
#endif
}

int InputManager::takeMenuKey() {
    const int key = m_menuKey;
    m_menuKey = 0;
    return key;
}

void InputManager::processXInput() {
#ifdef _WIN32
    m_xinput->update();

    double axes[AXIS_COUNT] = {};
    axes[AXIS_LX] = m_xinput->leftStickX();
    axes[AXIS_LY] = m_xinput->leftStickY();
    axes[AXIS_RX] = m_xinput->rightStickX();
    axes[AXIS_RY] = m_xinput->rightStickY();
    axes[AXIS_LT] = m_xinput->leftTrigger();
    axes[AXIS_RT] = m_xinput->rightTrigger();

    unsigned buttons = 0;
    if (m_xinput->buttonA()) buttons |= (1u << 0);
    if (m_xinput->buttonB()) buttons |= (1u << 1);
    if (m_xinput->buttonX()) buttons |= (1u << 2);
    if (m_xinput->buttonY()) buttons |= (1u << 3);
    if (m_xinput->buttonLB()) buttons |= (1u << 4);
    if (m_xinput->buttonRB()) buttons |= (1u << 5);
    if (m_xinput->buttonBack()) buttons |= (1u << 6);
    if (m_xinput->buttonStart()) buttons |= (1u << 7);
    if (m_xinput->dpadUp()) buttons |= (1u << 8);
    if (m_xinput->dpadDown()) buttons |= (1u << 9);
    if (m_xinput->dpadLeft()) buttons |= (1u << 10);
    if (m_xinput->dpadRight()) buttons |= (1u << 11);

    applyMapping(axes, buttons);
    if (m_xinput->dpadUp()) m_shiftUp = true;
    if (m_xinput->dpadDown()) m_shiftDown = true;
#endif
}

bool InputManager::hasXInput() const {
#ifdef _WIN32
    return m_xinput != nullptr;
#else
    return false;
#endif
}

bool InputManager::isXInputConnected() const {
#ifdef _WIN32
    return m_xinput && m_xinput->isConnected();
#else
    return false;
#endif
}

bool InputManager::loadControllerSettings(const std::string& path) {
    if (!m_controller.loadFrom(path, "controller")) return false;
    applyControllerSettings();
    return true;
}

void InputManager::applyControllerSettings() {
    namespace json = ks::engine::json;
    struct AxisMap {
        const char* section;
        double* deadZone;
        double* sensitivity;
    };
    const AxisMap axes[] = {
        {"STEER", &m_map.deadZone, &m_map.steerSensitivity},
        {"THROTTLE", &m_map.throttleDeadZone, &m_map.throttleSensitivity},
        {"BRAKE", &m_map.brakeDeadZone, &m_map.brakeSensitivity},
        {"CLUTCH", &m_map.clutchDeadZone, &m_map.clutchSensitivity},
    };
    // Only the keys a file really carries are applied: absent or out-of-range
    // values leave the mapping alone. 0..0.95 keeps the curve denominator
    // positive; sensitivity is a gain, so 0.1..2.0 stays usable.
    for (const AxisMap& axis : axes) {
        const json::Value* section = m_controller.root.find(axis.section);
        if (section == nullptr || !section->isObject()) continue;
        const double deadZone = section->numberAt("DEADZONE", -1.0);
        if (deadZone >= 0.0 && deadZone < 0.95) *axis.deadZone = deadZone;
        const double sensitivity = section->numberAt("SENSITIVITY", -1.0);
        if (sensitivity >= 0.1 && sensitivity <= 2.0) *axis.sensitivity = sensitivity;
    }
    if (const json::Value* steer = m_controller.root.find("STEER");
        steer && steer->isObject()) {
        if (const json::Value* invert = steer->find("INVERT"); invert && invert->isBool())
            m_map.invertSteer = invert->asBool();
    }
    // H-shifter: un bottone per marcia nella sezione [HSHIFTER] del documento
    // controller (forma rF2: GEAR1..GEAR7 + REVERSE). Valore = codice bottone;
    // assenti o -2 lasciano il mappaggio com'era. La modalità hold resta
    // a gearSelectButtonHold (controls.json), non è una proprietà del documento.
    if (const json::Value* hs = m_controller.root.find("HSHIFTER"); hs && hs->isObject()) {
        for (int i = 0; i < JoystickMapping::kGearButtonCount; ++i) {
            const char* key = (i < 7) ? nullptr : "";
            char kbuf[16];
            if (i < 7) { std::snprintf(kbuf, sizeof(kbuf), "GEAR%d", i + 1); key = kbuf; }
            else { key = "REVERSE"; }
            const int b = hs->intAt(key, -2);
            if (b < -1) continue;
            if (i < 7) m_map.gearButtons[i] = b;
            else m_map.reverseGearButton = b;
        }
    }
}

void InputManager::processKeyboard() {
    // Roadmap 1.4: primaries come from the rebindable mapping; arrows stay
    // fixed as alternates. Key names are matched case-insensitively (the
    // WM_KEYDOWN path inserts the uppercase VK, tests may inject 'w').
    auto norm = [](int k) { return (k >= 'a' && k <= 'z') ? k - ('a' - 'A') : k; };
    auto pressed = [&](int vk) {
        if (vk <= 0) return false;
        const int up = norm(vk);
        if (isKeyDown(up)) return true;
        if (up >= 'A' && up <= 'Z') return isKeyDown(up + ('a' - 'A'));
        return false;
    };

    const double tTarget = (pressed(m_kb.throttle) || pressed(KeyboardMapping::AltThrottle)) ? 1.0 : 0.0;
    const double bTarget = (pressed(m_kb.brake) || pressed(KeyboardMapping::AltBrake)) ? 1.0 : 0.0;
    double sTarget = 0;
    if (pressed(m_kb.steerLeft) || pressed(KeyboardMapping::AltSteerLeft)) sTarget -= 1.0;
    if (pressed(m_kb.steerRight) || pressed(KeyboardMapping::AltSteerRight)) sTarget += 1.0;
    if (m_map.invertSteer) sTarget = -sTarget;
    sTarget = std::clamp(sTarget, -1.0, 1.0);
    const double cTarget = keyHeld(m_kb.clutch) ? 1.0 : 0.0;

    const bool weighted = m_controlsConfig.keyboardWeighted != 0;
    if (weighted) {
        const double damping = std::clamp(m_controlsConfig.keyboardDamping, 0.5, 4.0);
        const double rateScale = 1.0 / damping;
        const double rateTh = m_controlsConfig.keyboardThrottle * 60.0 * rateScale;
        const double rateBr = m_controlsConfig.keyboardBrake * 60.0 * rateScale;
        const double rateSt = m_controlsConfig.keyboardSteering * 60.0 * rateScale;
        const double rateCl = m_controlsConfig.keyboardClutch * 60.0 * rateScale;
        const auto step = [&](double& state, double target, double rate, double dt) {
            const double d = target - state;
            const double maxDelta = rate * dt;
            if (std::abs(d) <= maxDelta) state = target;
            else state += std::copysign(maxDelta, d);
        };
        const double dt = (m_lastTick.time_since_epoch().count() == 0)
            ? (1.0 / 60.0)
            : std::chrono::duration<double>(std::chrono::steady_clock::now() - m_lastTick).count();
        // Il tempo è stato già aggiornato in update(), ma per sicurezza ricadiamo
        // sullo stesso dt misurato lì (m_lastTick conservato).
        step(m_kbwThrottle, tTarget, rateTh, dt);
        step(m_kbwBrake, bTarget, rateBr, dt);
        step(m_kbwSteer, sTarget, rateSt, dt);
        step(m_kbwClutch, cTarget, rateCl, dt);
        m_throttle = m_kbwThrottle;
        m_brake = m_kbwBrake;
        m_steer = m_kbwSteer;
        m_clutch = m_kbwClutch;
    } else {
        m_throttle = tTarget;
        m_brake = bTarget;
        m_steer = sTarget;
        m_clutch = cTarget;
    }

    bool curUp = pressed(m_kb.shiftUp);
    bool curDn = pressed(m_kb.shiftDown);
    if (curUp && !m_prevShiftUpKey) m_shiftUp = true;
    if (curDn && !m_prevShiftDownKey) m_shiftDown = true;
    m_prevShiftUpKey = curUp;
    m_prevShiftDownKey = curDn;

    m_handbrake = pressed(m_kb.handbrake) ? 1.0 : 0.0;

    // Turn signals: rising edge toggles. Pressing left toggles the left
    // indicator (off if already left), pressing right toggles the right one;
    // pressing the other side switches directly.
    bool curLeft = pressed(m_kb.turnSignalLeft);
    bool curRight = pressed(m_kb.turnSignalRight);
    if (curLeft && !m_prevTurnLeftKey) {
        m_turnSignal = (m_turnSignal < 0) ? 0 : -1;
    }
    if (curRight && !m_prevTurnRightKey) {
        m_turnSignal = (m_turnSignal > 0) ? 0 : 1;
    }
    m_prevTurnLeftKey = curLeft;
    m_prevTurnRightKey = curRight;

    m_steer = curve(m_steer, m_map.steerGamma, m_map.deadZone, true);
    m_throttle = std::clamp(m_throttle, 0.0, 1.0);
    m_brake = std::clamp(m_brake, 0.0, 1.0);
    m_steer = std::clamp(m_steer, -1.0, 1.0);

    m_rawThrottle = m_throttle;
    m_rawBrake = m_brake;
    m_rawSteer = m_steer;
}

void InputManager::reset() {
    m_throttle = m_brake = m_steer = m_clutch = m_handbrake = 0;
    m_kbwThrottle = m_kbwBrake = m_kbwSteer = m_kbwClutch = 0.0;
    m_rawThrottle = m_rawBrake = m_rawSteer = 0;
    m_shiftUp = m_shiftDown = false;
    m_gearRequest = 0;
    m_prevGearHeld = 0;
    m_starterRequested = m_prevStarterKey = false;
    resetFilterChannels();
    m_turnSignal = 0;
    m_prevTurnLeftKey = m_prevTurnRightKey = false;
    m_keys.clear();
    m_buttons = 0;
    m_hasInjected = false;
    std::memset(m_axes, 0, sizeof(m_axes));
}

} // namespace sim
} // namespace ks
