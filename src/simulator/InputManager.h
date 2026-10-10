#pragma once

// controls.json (per-player folder) is parsed with the engine's JSON reader
// and written with its file helpers — see engine/Config/Json.h and
// engine/assets/UserData.h.
#include "engine/Config/Json.h"
#include "engine/assets/UserData.h"

#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <unordered_set>

#ifdef _WIN32
namespace ks { namespace device {
class XInputDevice;
class DirectInputJoystick;
} }
#endif

namespace ks {
namespace sim {

struct JoystickMapping {
    int steerAxis = 0;
    int throttleAxis = 5;
    int brakeAxis = 4;
    int clutchAxis = -1;

    bool invertSteer = false;
    bool invertThrottle = false;
    bool invertBrake = false;
    bool throttleIsCombined = false;

    int shiftUpButton = 5;
    int shiftDownButton = 4;
    int clutchButton = 0;

    double deadZone = 0.12;
    double steerGamma = 1.6;
    double throttleGamma = 1.0;
    double brakeGamma = 1.0;

    static JoystickMapping gamepadDefault() { return JoystickMapping{}; }

    static JoystickMapping wheelDefault() {
        JoystickMapping m;
        m.steerAxis = 0;
        m.throttleAxis = 1;
        m.brakeAxis = 2;
        m.clutchAxis = 3;
        m.steerGamma = 1.2;
        m.deadZone = 0.02;
        m.shiftUpButton = 4;
        m.shiftDownButton = 5;
        return m;
    }

    static JoystickMapping combinedPedal() {
        JoystickMapping m;
        m.throttleIsCombined = true;
        m.throttleAxis = 1;
        m.brakeAxis = -1;
        return m;
    }
};

// Roadmap 1.4 / GAP P2.7: rebindable keyboard driving bindings. The primary
// keys persist to user/<player>/controls.json (the legacy user/keyboard.ini
// is read once for migration); the arrow keys stay fixed as alternates
// so a profile can never lock the player out of throttle/brake/steer.
struct KeyboardMapping {
    int throttle = 'W';
    int brake = 'S';
    int steerLeft = 'A';
    int steerRight = 'D';
    int shiftUp = 'E';
    int shiftDown = 'Q';
    int handbrake = 0x20; // VK_SPACE

    static constexpr int AltThrottle = 0x26;   // VK_UP
    static constexpr int AltBrake = 0x28;      // VK_DOWN
    static constexpr int AltSteerLeft = 0x25;  // VK_LEFT
    static constexpr int AltSteerRight = 0x27; // VK_RIGHT
};

namespace kb_detail {
struct KeyName { int vk; const char* name; };
inline const KeyName* table() {
    static const KeyName t[] = {
        {0x20, "SPACE"},  {0x0D, "ENTER"},  {0x1B, "ESC"},    {0x09, "TAB"},
        {0x25, "LEFT"},   {0x26, "UP"},     {0x27, "RIGHT"},  {0x28, "DOWN"},
        {0x10, "SHIFT"},  {0x11, "CTRL"},   {0x12, "ALT"},
        {0x24, "HOME"},   {0x23, "END"},    {0x21, "PGUP"},   {0x22, "PGDN"},
        {0x2D, "INSERT"}, {0x2E, "DELETE"},
        {0, nullptr},
    };
    return t;
}
} // namespace kb_detail

/** Stable name for a virtual-key code: letters/digits as-is (W, 5), F1-F12,
 *  the kb_detail table (SPACE, UP, ...), else VK<code>. */
inline std::string keyName(int vk) {
    if (vk >= 'A' && vk <= 'Z') return std::string(1, static_cast<char>(vk));
    if (vk >= '0' && vk <= '9') return std::string(1, static_cast<char>(vk));
    if (vk >= 0x70 && vk <= 0x7B) return "F" + std::to_string(vk - 0x6F);
    for (const auto* e = kb_detail::table(); e->name; ++e)
        if (e->vk == vk) return e->name;
    return "VK" + std::to_string(vk);
}

/** Inverse of keyName() (case-insensitive); -1 when not recognised. */
inline int keyFromName(std::string name) {
    for (char& c : name)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    if (name.size() == 1) {
        const char c = name[0];
        if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) return c;
        return -1;
    }
    if (name.size() >= 2 && name[0] == 'F') {
        const int f = std::atoi(name.c_str() + 1);
        if (f >= 1 && f <= 12) return 0x70 + (f - 1);
    }
    for (const auto* e = kb_detail::table(); e->name; ++e)
        if (name == e->name) return e->vk;
    if (name.compare(0, 2, "VK") == 0) {
        const int vk = std::atoi(name.c_str() + 2);
        if (vk > 0 && vk < 256) return vk;
    }
    return -1;
}

/** Persist to the legacy Key=Value ini (user/keyboard.ini). False if
 *  unwritable. Kept for migration: loading it once feeds controls.json. */
inline bool saveKeyboardMapping(const std::string& path, const KeyboardMapping& m) {
    // Mirror PersonalBestStore: create the containing directory (user/) first.
    {
        const auto slash = path.find_last_of("/\\");
        if (slash != std::string::npos) {
            std::error_code ec;
            std::filesystem::create_directories(path.substr(0, slash), ec);
        }
    }
    std::FILE* f = std::fopen(path.c_str(), "w");
    if (!f) return false;
    std::fprintf(f,
        "# ksengine keyboard bindings (roadmap 1.4)\n"
        "throttle=%s\nbrake=%s\nsteerLeft=%s\nsteerRight=%s\n"
        "shiftUp=%s\nshiftDown=%s\nhandbrake=%s\n",
        keyName(m.throttle).c_str(), keyName(m.brake).c_str(),
        keyName(m.steerLeft).c_str(), keyName(m.steerRight).c_str(),
        keyName(m.shiftUp).c_str(), keyName(m.shiftDown).c_str(),
        keyName(m.handbrake).c_str());
    std::fclose(f);
    return true;
}

/**
 * Load over an existing mapping: a missing file returns false and leaves it
 * untouched; malformed lines and unknown key names are skipped individually
 * (the field keeps whatever the mapping already held).
 */
inline bool loadKeyboardMapping(const std::string& path, KeyboardMapping& m) {
    std::FILE* f = std::fopen(path.c_str(), "r");
    if (!f) return false;
    char line[128];
    while (std::fgets(line, sizeof(line), f)) {
        std::string s(line);
        while (!s.empty() && (s.back() == '\n' || s.back() == '\r' || s.back() == ' '))
            s.pop_back();
        if (s.empty() || s[0] == '#') continue;
        const auto eq = s.find('=');
        if (eq == std::string::npos) continue;
        const int vk = keyFromName(s.substr(eq + 1));
        if (vk < 0) continue;
        const std::string key = s.substr(0, eq);
        if (key == "throttle") m.throttle = vk;
        else if (key == "brake") m.brake = vk;
        else if (key == "steerLeft") m.steerLeft = vk;
        else if (key == "steerRight") m.steerRight = vk;
        else if (key == "shiftUp") m.shiftUp = vk;
        else if (key == "shiftDown") m.shiftDown = vk;
        else if (key == "handbrake") m.handbrake = vk;
    }
    std::fclose(f);
    return true;
}

// --- controls.json ---------------------------------------------------------
// The installed layout's per-player file (user/<player>/controls.json, see
// assets::UserData): same key names as the ini above, values are the same
// key names keyName() produces. JSON so the whole user/ surface reads the
// same way, and per-player so two profiles never share bindings.

/** Persist to controls.json. False if unwritable. */
inline bool saveKeyboardMappingJson(const std::string& path, const KeyboardMapping& m) {
    namespace json = ks::engine::json;
    json::Value root = json::Value::object();
    root.set("throttle", json::Value::string(keyName(m.throttle)));
    root.set("brake", json::Value::string(keyName(m.brake)));
    root.set("steerLeft", json::Value::string(keyName(m.steerLeft)));
    root.set("steerRight", json::Value::string(keyName(m.steerRight)));
    root.set("shiftUp", json::Value::string(keyName(m.shiftUp)));
    root.set("shiftDown", json::Value::string(keyName(m.shiftDown)));
    root.set("handbrake", json::Value::string(keyName(m.handbrake)));
    return ks::engine::assets::UserData::writeFile(path, json::dump(root));
}

/**
 * Load over an existing mapping. Missing file, malformed JSON and a
 * populated-but-keyless "{}" (the state UserData::ensure() ships) all return
 * false with `m` untouched; a recognised binding applies only if
 * keyFromName() understands its value, and the result is true only when at
 * least one binding was applied — that is how the caller tells "no bindings
 * here yet" from "here are the bindings", and migrates user/keyboard.ini.
 */
inline bool loadKeyboardMappingJson(const std::string& path, KeyboardMapping& m) {
    namespace json = ks::engine::json;
    std::string text;
    if (!ks::engine::assets::UserData::readFile(path, text)) return false;
    json::Value root;
    std::string error;
    if (!json::parse(text, root, &error)) {
        std::fprintf(stderr, "controls.json: %s: %s\n", path.c_str(), error.c_str());
        return false;
    }
    if (!root.isObject()) return false;
    int applied = 0;
    const auto apply = [&](const char* key, int& field) {
        const json::Value* v = root.find(key);
        if (!v || !v->isString()) return;
        const int vk = keyFromName(v->asString());
        if (vk < 0) return;
        field = vk;
        ++applied;
    };
    apply("throttle", m.throttle);
    apply("brake", m.brake);
    apply("steerLeft", m.steerLeft);
    apply("steerRight", m.steerRight);
    apply("shiftUp", m.shiftUp);
    apply("shiftDown", m.shiftDown);
    apply("handbrake", m.handbrake);
    return applied > 0;
}

class InputManager {
public:
    static constexpr int KEY_UP = 0x26;
    static constexpr int KEY_DOWN = 0x28;
    static constexpr int KEY_LEFT = 0x25;
    static constexpr int KEY_RIGHT = 0x27;

    static constexpr int AXIS_LX = 0;
    static constexpr int AXIS_LY = 1;
    static constexpr int AXIS_RX = 2;
    static constexpr int AXIS_RY = 3;
    static constexpr int AXIS_LT = 4;
    static constexpr int AXIS_RT = 5;
    static constexpr int AXIS_COUNT = 8;

    InputManager();
    ~InputManager();

    bool initialize();
    void update();

    double throttle() const { return m_throttle; }
    double brake() const { return m_brake; }
    double steer() const { return m_steer; }
    double clutch() const { return m_clutch; }

    double rawThrottle() const { return m_rawThrottle; }
    double rawBrake() const { return m_rawBrake; }
    double rawSteer() const { return m_rawSteer; }

    bool shiftUp() const { return m_shiftUp; }
    bool shiftDown() const { return m_shiftDown; }

    bool hasXInput() const;
    bool isXInputConnected() const;
    bool hasDirectInput() const;
    bool isDirectInputConnected() const;

    void setPreferDirectInput(bool v) { m_preferDi = v; }
    bool preferDirectInput() const { return m_preferDi; }

    void setKeyDown(int key) { m_keys.insert(key); }
    void setKeyUp(int key) { m_keys.erase(key); }
    bool isKeyDown(int key) const { return m_keys.count(key) > 0; }

    void setMapping(const JoystickMapping& m) { m_map = m; }
    const JoystickMapping& mapping() const { return m_map; }
    void setSteerGamma(double g) { m_map.steerGamma = g; }
    void setDeadZone(double dz) { m_map.deadZone = dz; }
    void setInvertSteer(bool i) { m_map.invertSteer = i; }

    void setKeyboardMapping(const KeyboardMapping& m) { m_kb = m; }
    const KeyboardMapping& keyboardMapping() const { return m_kb; }

    void injectAxes(const double axes[AXIS_COUNT], unsigned buttons);
    void injectAxis(int index, double value);
    void injectButton(int index, bool pressed);

    void reset();

private:
    void processKeyboard();
    void processXInput();
    void processInjected();
    void applyMapping(const double axes[AXIS_COUNT], unsigned buttons);
    static double curve(double v, double gamma, double deadZone, bool bipolar);

    double m_throttle = 0, m_brake = 0, m_steer = 0, m_clutch = 0;
    double m_rawThrottle = 0, m_rawBrake = 0, m_rawSteer = 0;

    bool m_shiftUp = false, m_shiftDown = false;
    bool m_prevShiftUpKey = false, m_prevShiftDownKey = false;
    bool m_prevShiftUpBtn = false, m_prevShiftDownBtn = false;

    JoystickMapping m_map = JoystickMapping::gamepadDefault();
    KeyboardMapping m_kb;

    double m_axes[AXIS_COUNT] = {};
    unsigned m_buttons = 0;
    bool m_hasInjected = false;

    std::unordered_set<int> m_keys;

#ifdef _WIN32
    std::unique_ptr<ks::device::XInputDevice> m_xinput;
    std::unique_ptr<ks::device::DirectInputJoystick> m_dinput;
    bool m_preferDi = true;
#else
    bool m_preferDi = false;
#endif
};

} // namespace sim
} // namespace ks
