#pragma once
/**
 * Bundles discovery, control API, track limits, weather, PB, session, replay.
 */
#include "SessionController.h"
#include "TrackLimitsMonitor.h"
#include "WeatherControl.h"
#include "ServerDiscovery.h"
#include "ExternalControlApi.h"
#include "PersonalBestStore.h"
#include "TrackLayout.h"
#include "ApplySetup.h"
#include "ReplayRecorder.h"
#include "RaceSessionManager.h"
#include "MathTypes.h"
#include <string>
#include <cstdio>
#include <cctype>
#include <functional>

namespace ks {
namespace sim {

class FeatureHub {
public:
    GameSessionMode sessionMode = GameSessionMode::Race;
    SessionStartParams sessionParams = defaultsForMode(GameSessionMode::Race);

    ServerDiscovery discovery;
    ExternalControlApi control;
    TrackLimitsMonitor trackLimits;
    WeatherControl weatherCtrl;
    PersonalBestStore pb;
    TrackLayoutCatalog layouts;
    ReplayRecorder replay;

    bool discoveryStarted = false;
    bool controlStarted = false;

    std::function<void(GameSessionMode, const SessionStartParams&)> onBeginSession;
    std::function<void(int, int, float, const std::string&)> onPenalty;
    std::function<void(const std::string&)> onSetWeather;
    std::function<void(float)> onSetTimeOfDay;
    std::function<void(const std::string&)> onSetupLoad;
    std::function<void(const std::string&)> onSetupSave;
    std::function<void()> onRequestResults;

    void startServices(bool hostAnnounce = false) {
        if (!discoveryStarted) discoveryStarted = discovery.start(hostAnnounce);
        if (!controlStarted) {
            controlStarted = control.start(ExternalControlApi::kDefaultPort);
            wireDefaultControlHandlers();
        }
        pb.setDirectory("user/pb");
    }

    void stopServices() {
        discovery.stop();
        control.stop();
        discoveryStarted = false;
        controlStarted = false;
    }

    void setControlAuthToken(const std::string& token) { control.setAuthToken(token); }
    void setPersonalBestDirectory(const std::string& dir) { pb.setDirectory(dir); }
    void setTrackLimitsEnabled(bool on) {
        auto c = trackLimits.config();
        c.enabled = on;
        trackLimits.setConfig(c);
    }
    void setTrackLimitsConfig(const TrackLimitsConfig& c) { trackLimits.setConfig(c); }

    void setSessionMode(GameSessionMode m) {
        sessionMode = m;
        sessionParams = defaultsForMode(m);
    }
    void setSessionModeFromMenu(const std::string& entry) {
        setSessionMode(modeFromMenuEntry(entry));
    }

    void announceHost(const std::string& name, const std::string& track,
                      uint16_t port, int players, int maxPlayers) {
        discovery.setHostInfo(name, track, port, toNetSessionType(sessionMode),
                              players, maxPlayers);
        if (!discoveryStarted) discoveryStarted = discovery.start(true);
    }

    void tick(float dt, RaceSessionManager* session, int carIndex,
              const vec3& pos, bool onTrackHint) {
        discovery.tick(dt);
        control.poll();
        trackLimits.update(dt, carIndex, pos, onTrackHint, session);
        weatherCtrl.tick(dt);
    }

    void onLapCompleted(const std::string& track, const std::string& car,
                        const std::string& driver, float lapTime, const float* sectors) {
        if (pb.submitLap(track, car, driver, lapTime, sectors))
            control.emitEvent("PB track=" + track + " lap=" + std::to_string(lapTime));
        control.emitEvent("LAP time=" + std::to_string(lapTime));
    }

    bool loadReplay(const std::string& path) { return replay.loadReplay(path); }

private:
    void wireDefaultControlHandlers() {
        control.onCommand = [this](const ControlCommand& cmd) { handleControl(cmd); };
    }

    void handleControl(const ControlCommand& cmd) {
        const auto& a = cmd.args;
        if (cmd.verb == "SESSION") {
            std::string mode = a.empty() ? "RACE" : a[0];
            setSessionModeFromMenu(mode);
            if (a.size() >= 2) sessionParams.totalLaps = std::atoi(a[1].c_str());
            if (onBeginSession) onBeginSession(sessionMode, sessionParams);
            control.sendLine(std::string("OK SESSION ") + sessionModeName(sessionMode));
            return;
        }
        if (cmd.verb == "WEATHER" && !a.empty()) {
            weatherCtrl.applyPreset(a[0]);
            if (onSetWeather) onSetWeather(a[0]);
            control.sendLine("OK WEATHER");
            return;
        }
        if (cmd.verb == "TIME" && !a.empty()) {
            float h = std::strtof(a[0].c_str(), nullptr);
            weatherCtrl.setTimeOfDay(h);
            if (onSetTimeOfDay) onSetTimeOfDay(h);
            control.sendLine("OK TIME");
            return;
        }
        if (cmd.verb == "LIMITS") {
            if (!a.empty()) {
                std::string sub = a[0];
                for (char& c : sub) c = (char)toupper((unsigned char)c);
                if (sub == "OFF") setTrackLimitsEnabled(false);
                else if (sub == "ON") setTrackLimitsEnabled(true);
            }
            control.sendLine(std::string("OK LIMITS ") +
                (trackLimits.config().enabled ? "ON" : "OFF"));
            return;
        }
        if (cmd.verb == "PB") {
            if (!a.empty() && (a[0] == "LIST" || a[0] == "list")) {
                std::string track = a.size() >= 2 ? a[1] : "default";
                auto board = pb.leaderboard(track, 10);
                control.sendLine("OK PB COUNT " + std::to_string(board.size()));
                for (const auto& r : board)
                    control.sendLine("PB " + r.carId + " " + std::to_string(r.bestLap));
                return;
            }
            control.sendLine("OK PB");
            return;
        }
        if (cmd.verb == "REPLAY" && !a.empty()) {
            if (a[0] == "LOAD" && a.size() >= 2)
                control.sendLine(loadReplay(a[1]) ? "OK REPLAY" : "ERR REPLAY");
            else if (a[0] == "PLAY") { replay.startPlayback(); control.sendLine("OK PLAY"); }
            else if (a[0] == "STOP") { replay.stopPlayback(); control.sendLine("OK STOP"); }
            return;
        }
        if (cmd.verb == "RESULT") {
            if (onRequestResults) onRequestResults();
            control.sendLine("OK RESULT");
            return;
        }
        control.sendLine("ERR unknown " + cmd.verb);
    }
};

} // namespace sim
} // namespace ks
