/**
 * Roadmap 1.4 / GAP P2.7 — rebindable keyboard bindings.
 *
 * Covers the pure logic added to InputManager.h: keyName/keyFromName round
 * trip (letters, function keys, specials, VK<n> fallback), save/load of the
 * Key=Value ini (user/keyboard.ini pattern), and graceful degradation on a
 * missing file or malformed lines/keys.
 */
#include "KsTest.h"
#include "simulator/InputManager.h"

#include <cstdio>
#include <string>

using ks::sim::KeyboardMapping;
using ks::sim::keyFromName;
using ks::sim::keyName;
using ks::sim::loadKeyboardMapping;
using ks::sim::saveKeyboardMapping;

int main() {
    // --- keyName / keyFromName round trip --------------------------------
    const int codes[] = {
        'W', 'S', 'A', 'D', 'E', 'Q', 'Z', 'P', '5', '0',
        0x20, // SPACE
        0x0D, // ENTER
        0x1B, // ESC
        0x25, 0x26, 0x27, 0x28, // arrows
        0x70, 0x73, 0x7B,       // F1, F4, F12
        0x10,                   // SHIFT
        0x60,                   // numpad-0 -> VK96 fallback
        0x2F,                   // '/' -> VK47 fallback
    };
    for (int vk : codes) {
        const std::string n = keyName(vk);
        KS_CHECK(!n.empty());
        KS_CHECK(keyFromName(n) == vk);
    }
    KS_CHECK(keyFromName("w") == 'W');   // case-insensitive
    KS_CHECK(keyFromName("up") == 0x26); // table names too
    KS_CHECK(keyFromName("VK99") == 99); // numeric fallback
    KS_CHECK(keyFromName("") == -1);
    KS_CHECK(keyFromName("GARBAGEKEY") == -1);
    KS_CHECK(keyFromName("F0") == -1);   // F-keys are 1..12 only

    // Defaults must describe the classic layout.
    KeyboardMapping def;
    KS_CHECK(def.throttle == 'W' && def.brake == 'S');
    KS_CHECK(def.steerLeft == 'A' && def.steerRight == 'D');
    KS_CHECK(def.shiftUp == 'E' && def.shiftDown == 'Q');
    KS_CHECK(def.handbrake == 0x20);
    KS_CHECK(KeyboardMapping::AltThrottle == 0x26);

    // --- Save -> load round trip with a non-default mapping --------------
    KeyboardMapping a;
    a.throttle = 'R';
    a.brake = 'F';
    a.steerLeft = 'Z';
    a.steerRight = 0x2F;
    a.shiftUp = 0x60;
    a.shiftDown = 0x73;
    a.handbrake = 0x0D;
    const char* path = "keyboard_mapping_test.ini";
    KS_CHECK(saveKeyboardMapping(path, a));

    KeyboardMapping b; // defaults underneath
    KS_CHECK(loadKeyboardMapping(path, b));
    KS_CHECK(b.throttle == a.throttle);
    KS_CHECK(b.brake == a.brake);
    KS_CHECK(b.steerLeft == a.steerLeft);
    KS_CHECK(b.steerRight == a.steerRight);
    KS_CHECK(b.shiftUp == a.shiftUp);
    KS_CHECK(b.shiftDown == a.shiftDown);
    KS_CHECK(b.handbrake == a.handbrake);

    // --- Missing file: false + mapping untouched -------------------------
    KeyboardMapping c;
    c.throttle = 'X';
    KS_CHECK(!loadKeyboardMapping("no_such_keyboard_map.ini", c));
    KS_CHECK(c.throttle == 'X');

    // --- Malformed lines skipped, valid keys still apply -----------------
    {
        std::FILE* f = std::fopen(path, "w");
        KS_CHECK(f != nullptr);
        if (f) {
            std::fputs("throttle=J\nnot a kv line\nbrake=GARBAGEKEY\n"
                       "steerLeft=v\n# comment=Z\nshiftUp=VK96\n",
                       f);
            std::fclose(f);
        }
    }
    KeyboardMapping d;
    KS_CHECK(loadKeyboardMapping(path, d));
    KS_CHECK(d.throttle == 'J');
    KS_CHECK(d.brake == 'S');      // invalid value -> default kept
    KS_CHECK(d.steerLeft == 'V');   // case-insensitive
    KS_CHECK(d.shiftUp == 0x60);
    std::remove(path);

    return KS_TEST_RESULT("keyboard_mapping_test");
}
