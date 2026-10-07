#pragma once
/**
 * Bundles remaining parity wiring: discovery, control API, track limits,
 * weather, PB, session mode, replay helper.
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
#include <memory>
#include <string>
#include <cstdio>
#include <cctype>

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

    void startServices(bool hostAnnounce = false) {
        if (!discoveryStarted) {
            discoveryStarted = discovery.start(hostAnnounce);
        }
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

    void setControlAuthToken(const std::string& token) {
        control.setAuthToken(token);
    }
    void setPersonalBestDirectory(const std::string& dir) {
        pb.setDirectory(dir);
    }
    void setTrackLimitsEnabled(bool on) {
        auto c = trackLimits.config();
        c.enabled = on;
        trackLimits.setConfig(c);
    }
    void setTrackLimitsConfig(const TrackLimitsConfig& c) {
        trackLimits.setConfig(c);
    }

    void setSessionMode(GameSessionMode m) {
        sessionMode = m;
        sessionParams = defaultsForMode(m);
    }

    void setSessionModeFromMenu(const std::string& entry) {
        setSessionMode(modeFromMenuEntry(entry));
    }

    /** Host: publish LAN presence. */
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
                        const std::string& driver, float lapTime,
                        const float sectors[3]) {
        if (pb.submitLap(track, car, driver, lapTime, sectors))
            control.emitEvent("PB track=" + track + " car=" + car +
                              " lap=" + std::to_string(lapTime));
        control.emitEvent("LAP car=0 time=" + std::to_string(lapTime));
    }

    bool loadReplay(const std::string& path) {
        if (!replay.loadReplay(path)) return false;
        replay.startPlayback();
        return true;
    }

    // Optional sinks filled by SimulationLoop
    std::function<void(GameSessionMode, const SessionStartParams&)> onBeginSession;
    std::function<void(uint8_t /*flag*/)> onSetFlag;
    std::function<void(int car, int kind, float value, const std::string& reason)> onPenalty;
    std::function<void(float hours)> onSetTimeOfDay;
    std::function<void(const std::string& preset)> onSetWeather;
    std::function<void(const std::string& path)> onSetupLoad;
    std::function<void(const std::string& path)> onSetupSave;
    std::function<void()> onRequestResults;

private:
    void wireDefaultControlHandlers() {
        control.onCommand = [this](const ControlCommand& cmd) {
            handleControl(cmd);
        };
    }

    void handleControl(const ControlCommand& cmd) {
        const auto& a = cmd.args;
        if (cmd.verb == "SESSION") {
            std::string mode = a.empty() ? "RACE" : a[0];
            setSessionModeFromMenu(mode);
            if (a.size() >= 2)
                sessionParams.totalLaps = std::atoi(a[1].c_str());
            if (onBeginSession) onBeginSession(sessionMode, sessionParams);
            control.sendLine("OK SESSION " + std::string(sessionModeName(sessionMode)));
            return;
        }
        if (cmd.verb == "FLAG" && !a.empty()) {
            std::string f = a[0];
            for (char& c : f) c = (char)toupper((unsigned char)c);
            uint8_t flag = 0;
            if (f == "GREEN") flag = 1;
            else if (f == "YELLOW") flag = 2;
            else if (f == "RED") flag = 3;
            else if (f == "CHECKERED" || f == "CHEQUERED") flag = 4;
            if (onSetFlag) onSetFlag(flag);
            control.sendLine("OK FLAG " + f);
            control.emitEvent("FLAG " + f);
            return;
        }
        if (cmd.verb == "PENALTY" && a.size() >= 2) {
            int car = std::atoi(a[0].c_str());
            std::string kind = a[1];
            float val = a.size() >= 3 ? std::strtof(a[2].c_str(), nullptr) : 0.f;
            std::string reason = a.size() >= 4 ? a[3] : "control";
            int k = 0;
            if (kind == "DT" || kind == "DRIVETHROUGH") k = 1;
            else if (kind == "SG" || kind == "STOPGO") k = 2;
            else k = 3; // time
            if (onPenalty) onPenalty(car, k, val, reason);
            control.sendLine("OK PENALTY");
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
        if (cmd.verb == "SETUP" && a.size() >= 2) {
            if (a[0] == "LOAD" || a[0] == "load") {
                if (onSetupLoad) onSetupLoad(a[1]);
            } else if (a[0] == "SAVE" || a[0] == "save") {
                if (onSetupSave) onSetupSave(a[1]);
            }
            control.sendLine("OK SETUP");
            return;
        }
        if (cmd.verb == "REPLAY" && !a.empty()) {
            if (a[0] == "LOAD" && a.size() >= 2) {
                control.sendLine(loadReplay(a[1]) ? "OK REPLAY" : "ERR REPLAY");
            } else if (a[0] == "PLAY") {
                replay.startPlayback();
                control.sendLine("OK PLAY");
            } else if (a[0] == "STOP") {
                replay.stopPlayback();
                control.sendLine("OK STOP");
            }
            return;
        }
        if (cmd.verb == "CHAT") {
            std::string msg;
            for (size_t i = 0; i < a.size(); ++i) {
                if (i) msg += ' ';
                msg += a[i];
            }
            control.emitEvent("CHAT " + msg);
            control.sendLine("OK CHAT");
            return;
        }
        if (cmd.verb == "RESULT") {
            if (onRequestResults) onRequestResults();
            control.sendLine("OK RESULT");
            return;
        }
        if (cmd.verb == "LIMITS") {
            if (!a.empty()) {
                std::string sub = a[0];
                for (char& c : sub) c = (char)toupper((unsigned char)c);
                if (sub == "OFF") setTrackLimitsEnabled(false);
                else if (sub == "ON") setTrackLimitsEnabled(true);
                else if (sub == "STATUS") {
                    control.sendLine(std::string("OK LIMITS ") +
                        (trackLimits.config().enabled ? "ON" : "OFF") +
                        " reports=" + std::to_string(trackLimits.reportsFor(0)));
                    return;
                }
            }
            control.sendLine(std::string("OK LIMITS ") +
                (trackLimits.config().enabled ? "ON" : "OFF"));
            return;
        }
        if (cmd.verb == "PB") {
            // PB [track] [car]  or  PB LIST [track]
            if (!a.empty() && (a[0] == "LIST" || a[0] == "list")) {
                std::string track = a.size() >= 2 ? a[1] : "default";
                auto board = pb.leaderboard(track, 10);
                control.sendLine("OK PB COUNT " + std::to_string(board.size()));
                for (const auto& r : board) {
                    control.sendLine("PB " + r.carId + " " + std::to_string(r.bestLap) +
                                    " " + r.driver);
                }
                return;
            }
            std::string track = a.size() >= 1 ? a[0] : "default";
            std::string car = a.size() >= 2 ? a[1] : "car";
            auto r = pb.get(track, car);
            control.sendLine("OK PB " + std::to_string(r.bestLap) +
                             " laps=" + std::to_string(r.lapsCompleted));
            return;
        }
        control.sendLine("ERR unknown " + cmd.verb);
    }
};

} // namespace sim
} // namespace ks
