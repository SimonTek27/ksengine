#pragma once
/**
 * Engine settings, persisted as JSON.
 *
 * Two files share this shape and are merged in order, later wins per key:
 *
 *   system/cfg/ksengine.json     install-wide defaults, shipped as "{}"
 *   user/<player>/settings.json  per-player overrides, created as "{}"
 *
 * A key neither file carries keeps its compiled-in default, which is why the
 * shipped empty files are complete no-ops: nothing changes unless somebody
 * edits them. Only the keys a file actually carries are remembered (has*)
 * and only those are written back, so a round trip never invents settings
 * nobody chose.
 *
 * Who consumes what (src/simulator/SimulatorApp.cpp):
 *   physics.fixedDt  -> ks::Engine::setFixedDt()   fixed simulation step
 *   audio.master     -> SimulatorAudio::setMasterVolume()
 *   assist.tc/abs    -> VehicleSimulator::applySetup() at start-up
 *
 * Header-only: read by ksim and by the engine itself, so no state crosses
 * the DLL boundary.
 */
#include "Json.h"
#include "assets/UserData.h"

#include <cmath>
#include <cstdio>
#include <string>

namespace ks {
namespace engine {
namespace config {

struct EngineSettings {
    // --- values (compiled-in defaults) ------------------------------------
    /** Fixed simulation step in seconds; ks::Engine accepts 1e-5..1.0. */
    double physicsFixedDt = 1.0 / 120.0;
    /** Master audio gain, 0..2 (SimulatorAudio's own default is 0.8). */
    float audioMaster = 0.8f;
    /** Driver aids pushed into the vehicle setup at start-up, 0..12. */
    int assistTc = 0;
    int assistAbs = 0;

    // --- presence: did the file carry the key at all? ---------------------
    bool hasPhysicsFixedDt = false;
    bool hasAudioMaster = false;
    bool hasAssistTc = false;
    bool hasAssistAbs = false;

    /** Merge a parsed document: object members only, wrong-typed values are
     *  ignored (a settings file can never inject an out-of-range assist or a
     *  degenerate timestep). */
    void merge(const json::Value& root) {
        if (!root.isObject()) return;
        if (const json::Value* physics = root.find("physics"); physics && physics->isObject()) {
            if (const json::Value* v = physics->find("fixedDt");
                v && v->isNumber() && v->asNumber() > 1.0e-5 && v->asNumber() < 1.0) {
                physicsFixedDt = v->asNumber();
                hasPhysicsFixedDt = true;
            }
        }
        if (const json::Value* audio = root.find("audio"); audio && audio->isObject()) {
            if (const json::Value* v = audio->find("master");
                v && v->isNumber() && v->asNumber() >= 0.0 && v->asNumber() <= 2.0) {
                audioMaster = static_cast<float>(v->asNumber());
                hasAudioMaster = true;
            }
        }
        if (const json::Value* assist = root.find("assist"); assist && assist->isObject()) {
            if (const json::Value* v = assist->find("tc"); v && v->isNumber() &&
                v->asNumber() >= 0.0 && v->asNumber() <= 12.0) {
                assistTc = static_cast<int>(std::llround(v->asNumber()));
                hasAssistTc = true;
            }
            if (const json::Value* v = assist->find("abs"); v && v->isNumber() &&
                v->asNumber() >= 0.0 && v->asNumber() <= 12.0) {
                assistAbs = static_cast<int>(std::llround(v->asNumber()));
                hasAssistAbs = true;
            }
        }
    }

    /**
     * Forget which keys a file carried (the values are kept). Used when
     * merging in order — system/cfg first, user file second — so that save()
     * later reports exactly the keys the *user's* file had, and a default
     * that only exists in system/cfg is never promoted into user/<player>/.
     */
    void clearPresence() {
        hasPhysicsFixedDt = false;
        hasAudioMaster = false;
        hasAssistTc = false;
        hasAssistAbs = false;
    }

    /**
     * Merge whatever `path` carries into *this. False when the file is
     * missing or malformed (a parse failure is reported on stderr; the
     * values already held are left untouched, so system/cfg defaults
     * survive a broken user file).
     */
    bool load(const std::string& path) {
        std::string text;
        if (!assets::UserData::readFile(path, text)) return false;
        json::Value root;
        std::string error;
        if (!json::parse(text, root, &error)) {
            std::fprintf(stderr, "EngineSettings: %s: %s\n", path.c_str(), error.c_str());
            return false;
        }
        merge(root);
        return true;
    }

    /** Only the keys this object carries — see the header comment. */
    json::Value toJson() const {
        json::Value root = json::Value::object();
        if (hasPhysicsFixedDt) {
            json::Value physics = json::Value::object();
            physics.set("fixedDt", json::Value::number(physicsFixedDt));
            root.set("physics", std::move(physics));
        }
        if (hasAudioMaster) {
            json::Value audio = json::Value::object();
            audio.set("master", json::Value::number(static_cast<double>(audioMaster)));
            root.set("audio", std::move(audio));
        }
        if (hasAssistTc || hasAssistAbs) {
            json::Value assist = json::Value::object();
            if (hasAssistTc) assist.set("tc", json::Value::number(assistTc));
            if (hasAssistAbs) assist.set("abs", json::Value::number(assistAbs));
            root.set("assist", std::move(assist));
        }
        return root;
    }

    /** Write back the carried keys (creating the folder if needed). */
    bool save(const std::string& path) const {
        return assets::UserData::writeFile(path, json::dump(toJson()));
    }
};

} // namespace config
} // namespace engine
} // namespace ks
