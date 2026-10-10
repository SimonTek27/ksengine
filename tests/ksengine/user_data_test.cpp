/**
 * Phase 3 — the JSON + per-user data layer the installed layout runs on.
 *
 * The Qt-free runtime had no JSON support; the install now ships
 * system/cfg/ksengine.json and the runtime owns user/<player>/{controls,
 * settings,stats}.json. This covers:
 *   - engine/Config/Json.h         parse/dump round trip, escapes, errors
 *   - engine/assets/UserData.h     first-run folder creation, name sanitising
 *   - engine/Config/EngineSettings.h  system cfg + user overrides, write-back
 *   - simulator/UserStats.h        stats.json round trip
 *   - simulator/InputManager.h     controls.json round trip
 *
 * Everything runs in a scratch folder under the system temp directory: the
 * test never touches the staged user/ of the build tree.
 */
#include "KsTest.h"
#include "engine/Config/EngineSettings.h"
#include "engine/Config/Json.h"
#include "engine/assets/UserData.h"
#include "simulator/InputManager.h"
#include "simulator/UserStats.h"

#include <filesystem>
#include <string>

namespace fs = std::filesystem;
using ks::engine::config::EngineSettings;
using ks::engine::json::Value;
using ks::engine::assets::UserData;
using ks::engine::assets::UserLayout;
using ks::sim::DriverProfile;
using ks::sim::DriverStats;

int main() {
    std::error_code ec;
    const fs::path root = fs::temp_directory_path() / "ks_user_data_test";
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);

    // --- JSON: round trip ---------------------------------------------------
    {
        Value v = Value::object();
        v.set("name", Value::string("sim \"quoted\" \\ slash\n"));
        v.set("num", Value::number(0.8));
        v.set("int", Value::number(5));
        v.set("flag", Value::boolean(true));
        v.set("nothing", Value::null());
        Value list = Value::array();
        list.append(Value::number(1));
        list.append(Value::string("due"));
        v.set("list", std::move(list));

        const std::string text = ks::engine::json::dump(v);
        Value back;
        std::string err;
        KS_CHECK(ks::engine::json::parse(text, back, &err));
        KS_CHECK(back.stringAt("name", "") == "sim \"quoted\" \\ slash\n");
        KS_CHECK_NEAR(back.numberAt("num", 0.0), 0.8, 1e-12);
        KS_CHECK(back.intAt("int", 0) == 5);
        KS_CHECK(back.boolAt("flag", false));
        KS_CHECK(back.find("nothing") && back.find("nothing")->isNull());
        const Value* listBack = back.find("list");
        KS_CHECK(listBack && listBack->isArray() && listBack->size() == 2);
        KS_CHECK(listBack && listBack->item(1) && listBack->item(1)->asString() == "due");
        KS_CHECK(back.find("absent") == nullptr);

        // Compact output parses back to the same document, and re-dumping it
        // pretty gives byte-identical text (a file written twice is stable).
        Value compact;
        KS_CHECK(ks::engine::json::parse(ks::engine::json::dump(v, 0), compact, &err));
        KS_CHECK(ks::engine::json::dump(compact) == text);

        // \uXXXX, including a surrogate pair, decodes to UTF-8.
        Value u;
        KS_CHECK(ks::engine::json::parse("{\"u\":\"\\u00e9\\ud83d\\ude00\"}", u, &err));
        KS_CHECK(u.stringAt("u", "") == "\xC3\xA9\xF0\x9F\x98\x80");

        // Malformed input fails loudly instead of yielding a partial value.
        KS_CHECK(!ks::engine::json::parse("{\"a\": }", back, &err) && !err.empty());
        KS_CHECK(!ks::engine::json::parse("{\"a\":1} trailing", back, &err));
        KS_CHECK(!ks::engine::json::parse("", back, &err));
        KS_CHECK(!ks::engine::json::parse("{\"a\":[1,2}", back, &err));
    }

    // --- player folder name ------------------------------------------------
    {
        const auto n = [](const char* s) { return UserData::playerFolderName(s); };
        KS_CHECK(n("Mario") == "Mario");
        // "Ünïcode" in UTF-8 (split so \xAFc is not read as one escape)
        const std::string unicode = "\xC3\x9Cn\xC3\xAF" "code";
        KS_CHECK(n(unicode.c_str()) == unicode);            // UTF-8 bytes are legal
        KS_CHECK(n("") == "Driver");
        KS_CHECK(n("   ") == "Driver");
        KS_CHECK(n(" ... ") == "Driver");
        KS_CHECK(n("..") == "Driver");
        KS_CHECK(n("CON") == "Driver");
        KS_CHECK(n("con.txt") == "Driver");        // reserved stem, any suffix
        KS_CHECK(n("COM1") == "Driver");
        KS_CHECK(n("LPT9") == "Driver");
        KS_CHECK(n("a/b\\c") == "a_b_c");          // path separators
        KS_CHECK(n("V:iva?*") == "V_iva__");       // drive/illegal chars
        KS_CHECK(n(" trailing . ") == "trailing"); // trimmed after folding
        KS_CHECK(n(std::string(200, 'x').c_str()).size() == 64);
    }

    // --- first-run layout --------------------------------------------------
    UserLayout first;
    {
        first = UserData::ensure(root.string(), "Mario");
        KS_CHECK(first.ok);
        KS_CHECK(first.created);
        KS_CHECK(fs::is_directory(first.player));
        KS_CHECK(fs::is_directory(first.screenshots));
        KS_CHECK(fs::is_directory(first.replay));
        KS_CHECK(fs::is_directory(first.telemetry));
        KS_CHECK(fs::is_regular_file(first.controls));
        KS_CHECK(fs::is_regular_file(first.settings));
        KS_CHECK(fs::is_regular_file(first.stats));

        // Shipped state of every JSON file: an empty object, no keys.
        std::string text;
        KS_CHECK(UserData::readFile(first.controls, text) && text == "{}\n");
        KS_CHECK(UserData::readFile(first.settings, text) && text == "{}\n");
        KS_CHECK(UserData::readFile(first.stats, text) && text == "{}\n");

        // Second run: nothing to create, nothing overwritten.
        KS_CHECK(UserData::writeFile(first.controls, "{\"throttle\":\"R\"}\n"));
        UserLayout second = UserData::ensure(root.string(), "Mario");
        KS_CHECK(second.ok && !second.created);
        KS_CHECK(second.player == first.player);
        KS_CHECK(UserData::readFile(second.controls, text) && text == "{\"throttle\":\"R\"}\n");

        // A missing file reads as false, an unwritable path writes as false.
        KS_CHECK(!UserData::readFile((root / "nope.json").string(), text));
    }

    // --- controls.json -----------------------------------------------------
    {
        ks::sim::KeyboardMapping a;
        a.throttle = 'R';
        a.brake = 'F';
        a.steerLeft = 'Z';
        a.steerRight = 0x2F;
        a.shiftUp = 0x60;
        a.shiftDown = 0x73;
        a.handbrake = 0x0D;
        KS_CHECK(ks::sim::saveKeyboardMappingJson(first.controls, a));

        ks::sim::KeyboardMapping b;
        KS_CHECK(ks::sim::loadKeyboardMappingJson(first.controls, b));
        KS_CHECK(b.throttle == a.throttle && b.brake == a.brake);
        KS_CHECK(b.steerLeft == a.steerLeft && b.steerRight == a.steerRight);
        KS_CHECK(b.shiftUp == a.shiftUp && b.shiftDown == a.shiftDown);
        KS_CHECK(b.handbrake == a.handbrake);

        // The shipped "{}": no bindings read, mapping untouched — this is the
        // "migrate the legacy ini" signal SimulatorApp acts on.
        KS_CHECK(UserData::writeFile(first.controls, "{}\n"));
        ks::sim::KeyboardMapping c;
        c.throttle = 'X';
        KS_CHECK(!ks::sim::loadKeyboardMappingJson(first.controls, c));
        KS_CHECK(c.throttle == 'X');

        // Malformed JSON degrades the same way (false, mapping untouched).
        KS_CHECK(UserData::writeFile(first.controls, "{ not json"));
        KS_CHECK(!ks::sim::loadKeyboardMappingJson(first.controls, c));
        KS_CHECK(c.throttle == 'X');
    }

    // --- settings.json: system cfg + user overrides + write-back ----------
    {
        const std::string systemPath = (root / "ksengine.json").string();
        const std::string userPath = (root / "settings.json").string();
        const std::string outPath = (root / "settings_out.json").string();

        // Install-wide file: timestep + audio.
        EngineSettings system;
        system.physicsFixedDt = 1.0 / 60.0;
        system.audioMaster = 0.5f;
        system.hasPhysicsFixedDt = true;
        system.hasAudioMaster = true;
        KS_CHECK(system.save(systemPath));

        // Player file: assists only.
        Value user = Value::object();
        Value assist = Value::object();
        assist.set("tc", Value::number(3));
        assist.set("abs", Value::number(2));
        user.set("assist", std::move(assist));
        KS_CHECK(UserData::writeFile(userPath, ks::engine::json::dump(user)));

        EngineSettings merged;
        KS_CHECK(merged.load(systemPath));
        merged.clearPresence(); // only the player's own keys are remembered
        KS_CHECK(merged.load(userPath));
        KS_CHECK_NEAR(merged.physicsFixedDt, 1.0 / 60.0, 1e-12);
        KS_CHECK_NEAR(merged.audioMaster, 0.5, 1e-6);
        KS_CHECK(merged.assistTc == 3 && merged.assistAbs == 2);
        KS_CHECK(merged.hasAssistTc && merged.hasAssistAbs);
        KS_CHECK(!merged.hasPhysicsFixedDt && !merged.hasAudioMaster);

        // Write-back reports exactly what the player's file carried: the
        // system/cfg values must not be promoted into it.
        KS_CHECK(merged.save(outPath));
        EngineSettings back;
        KS_CHECK(back.load(outPath));
        KS_CHECK(!back.hasPhysicsFixedDt && !back.hasAudioMaster);
        KS_CHECK(back.hasAssistTc && back.assistTc == 3 && back.assistAbs == 2);
        KS_CHECK_NEAR(back.physicsFixedDt, 1.0 / 120.0, 1e-12); // default again

        // The shipped "{}" and a missing file both leave the defaults alone.
        EngineSettings def;
        KS_CHECK(def.load(first.settings));
        KS_CHECK(!def.hasPhysicsFixedDt && def.assistTc == 0);
        KS_CHECK_NEAR(def.physicsFixedDt, 1.0 / 120.0, 1e-12);
        KS_CHECK(!def.load((root / "absent.json").string()));

        // Wrong-typed / out-of-range values are ignored, not applied.
        KS_CHECK(UserData::writeFile(userPath,
                                     "{\"physics\":{\"fixedDt\":0},"
                                     "\"assist\":{\"tc\":999},"
                                     "\"audio\":{\"master\":\"loud\"}}"));
        EngineSettings guarded;
        KS_CHECK(guarded.load(userPath));
        KS_CHECK(!guarded.hasPhysicsFixedDt);
        KS_CHECK(!guarded.hasAssistTc);
        KS_CHECK(!guarded.hasAudioMaster);
        KS_CHECK_NEAR(guarded.physicsFixedDt, 1.0 / 120.0, 1e-12);
        KS_CHECK_NEAR(guarded.audioMaster, 0.8, 1e-6);

        // A malformed file must not wipe what was already merged in.
        KS_CHECK(UserData::writeFile(userPath, "{ oops"));
        EngineSettings broken;
        KS_CHECK(!broken.load(userPath));
        KS_CHECK_NEAR(broken.physicsFixedDt, 1.0 / 120.0, 1e-12);
    }

    // --- stats.json --------------------------------------------------------
    {
        DriverProfile p;
        p.wins = 3;
        p.poles = 2;
        p.podiums = 5;
        p.totalRaces = 9;
        p.bestLapTime = 91.5f;
        const DriverStats snapshot = ks::sim::driverStatsFromProfile(p, 7);
        KS_CHECK(ks::sim::saveDriverStats(first.stats, snapshot));

        DriverProfile fresh; // profile as a new run starts
        DriverStats in;
        KS_CHECK(ks::sim::loadDriverStats(first.stats, in));
        KS_CHECK(in.pbRecords == 7);
        ks::sim::applyDriverStats(in, fresh);
        KS_CHECK(fresh.wins == 3 && fresh.poles == 2 && fresh.podiums == 5);
        KS_CHECK(fresh.totalRaces == 9);
        KS_CHECK_NEAR(fresh.bestLapTime, 91.5, 1e-4);

        // "{}" is a successful load that resets nothing.
        DriverStats keep;
        keep.wins = 4;
        keep.bestLapTime = 12.0f;
        KS_CHECK(UserData::writeFile(first.stats, "{}\n"));
        KS_CHECK(ks::sim::loadDriverStats(first.stats, keep));
        KS_CHECK(keep.wins == 4);
        KS_CHECK_NEAR(keep.bestLapTime, 12.0, 1e-4);
    }

    fs::remove_all(root, ec);
    return KS_TEST_RESULT("user_data_test");
}
