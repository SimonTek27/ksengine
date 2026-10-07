#include "SimulationLoop.h"
#include "SessionFlow.h"
#include "ReplayRecorder.h"
#include "ApplySetup.h"
#include "FfbOutput.h"
#include "SetupGarage.h"
#include "SetupFile.h"
#include "GarageExit.h"
#include "GarageSpawn.h"
#include "PitLaneQueue.h"
#include "PitLaneCollision.h"
#include "PitLaneRepair.h"
#include "MultiCarManager.h"
#include <cctype>
#include <cstdio>
#include <fstream>
#include <filesystem>
#include <algorithm>
#include <cmath>

namespace {
bool isSafePath(const std::string& path) {
    if (path.empty() || path.size() > 4096) return false;
    if (path.find('\0') != std::string::npos) return false;
    std::string norm = path;
    for (char& c : norm) if (c == '\\') c = '/';
    if (norm.find("/../") != std::string::npos || norm.find("../") == 0 ||
        norm.find("/..") == norm.size() - 3 || norm == "..")
        return false;
    return true;
}
float finiteOr(float v, float fallback) { return std::isfinite(v) ? v : fallback; }
} // namespace

namespace ks::sim {

void SimulationLoop::setupDefaultGarageLayout(int boxCount) {
    boxCount = std::clamp(boxCount, 1, 32);
    WorldPose first;
    first.x = 0.f; first.y = 0.f; first.z = 0.f; first.heading = 0.f;
    m_garageLayout = GarageSpawnPolicy::makeLinearRow(boxCount, first, 6.f, 0.f);
    configurePitAxis(first.x, first.z, m_garageLayout.pitLaneHeading, 120.f);
    if (!m_garageLayout.boxes.empty()) {
        m_garageLayout.boxes[0].occupied = true;
        m_garageExit.bindBox(0, m_garageLayout.boxes[0].pose, m_garageLayout.pitLaneHeading);
#if HAS_VEHICLE_SIM
        snapVehicleToPose(m_vehicle.get(), m_garageLayout.boxes[0].pose);
#endif
    }
    std::fprintf(stderr, "SimulationLoop: garage layout %d boxes\n", boxCount);
}

void SimulationLoop::beginSession(GameSessionMode mode) {
    m_features.setSessionMode(mode);
    const auto& p = m_features.sessionParams;
    m_sessionType = toNetSessionType(mode);
    m_currentLap = 0;
    m_totalLaps = p.totalLaps > 0 ? std::min(p.totalLaps, 200) : (p.sessionTimeSeconds > 0 ? 0 : 5);
    m_timeRemaining = p.sessionTimeSeconds > 0 ? std::min((double)p.sessionTimeSeconds, 86400.0) : 0.0;
    m_sessionPhase = 1;

    m_pitQueue = PitLaneQueue{};
    m_pitCollision = PitLaneCollision{};
    m_pitRepair = PitLaneRepair{};
    m_pitSystemsReady = true;
    m_requestPitService = false;

    if (!m_trackData.directory.empty() && loadGarageFromTrack(m_trackData.directory))
        ;
    else
        setupDefaultGarageLayout(std::max(8, std::min(p.aiCars + 1, 32)));
    if (p.aiCars > 0)
        spawnAiGrid(std::min(p.aiCars, 32));

    applyVehicleSetup();
    m_sessionFlow.reset();
    m_replayMode = false;

    if (mode == GameSessionMode::Qualifying) {
        std::vector<QualyEntry> cars;
        QualyEntry player;
        player.carId = kPlayerCarId;
        player.name = m_carName.empty() ? "Player" : m_carName;
        cars.push_back(player);
        if (m_multiCar) {
            for (const auto& ce : m_multiCar->cars()) {
                if (!ce || ce->isPlayer) continue;
                QualyEntry e;
                e.carId = ce->id;
                e.name = ce->name.empty() ? "AI" : ce->name;
                cars.push_back(e);
            }
        }
        const float qsec = p.qualifyingTimeSeconds > 0 ? float(p.qualifyingTimeSeconds)
                          : (p.sessionTimeSeconds > 0 ? float(p.sessionTimeSeconds) : 600.f);
        m_sessionFlow.startQualifying(qsec, cars);
        m_sessionPhase = 1; // running
    } else if (mode == GameSessionMode::Race) {
        if (p.useGrid && !m_sessionFlow.qualy().empty()) {
            // already have grid from prior qualy
            m_sessionFlow.enterCountdown(5.f);
        } else {
            m_sessionFlow.startRaceDirect(p.totalLaps > 0 ? p.totalLaps : 5, 5.f);
        }
        // Configure RaceSessionManager
        RaceConfig rc;
        rc.sessionType = RaceConfig::SessionType::Race;
        rc.totalLaps = p.totalLaps > 0 ? p.totalLaps : 5;
        rc.sessionTimeSeconds = p.sessionTimeSeconds;
        rc.useGridPositions = p.useGrid;
        m_raceSession.configure(rc);
        m_raceSession.startSession();
        m_raceSession.startCountdown(5.f);
        m_sessionPhase = 1;
    } else {
        // Practice / TT / etc.
        m_sessionPhase = 1;
    }

    if (p.startInGarage) {
        m_garageExit.forceEnterGarage();
        std::fprintf(stderr, "SimulationLoop: session %s starts IN GARAGE\n", sessionModeName(mode));
    } else {
        m_garageExit.forceOnTrack();
    }
}

void SimulationLoop::startFeatureServices(bool hostAnnounce) {
    m_features.startServices(hostAnnounce);
    m_features.onBeginSession = [this](GameSessionMode mode, const SessionStartParams&) {
        beginSession(mode);
    };
    m_features.onSetFlag = [this](uint8_t f) {
        RaceFlag rf = RaceFlag::Green;
        if (f == 2) rf = RaceFlag::Yellow;
        else if (f >= 4) rf = RaceFlag::Checkered;
        setRaceFlag(rf);
    };
    m_features.onPenalty = [this](int car, int kind, float value, const std::string& reason) {
        using PT = Penalty::Type;
        PT ty = PT::TimeAdded;
        if (kind == 1) ty = PT::DriveThrough;
        else if (kind == 2) ty = PT::StopGo;
        m_raceSession.addPenalty(car, ty, value, reason);
        if (kind == 2) m_requestPitService = true;
    };
    m_features.onSetTimeOfDay = [this](float h) { setTimeOfDay(h); };
    m_features.onSetWeather = [this](const std::string& name) {
        auto wp = presetByName(name);
        ks::physics::WeatherState ws;
        ws.ambientTemp = wp.ambientC;
        ws.trackTemp = wp.trackC;
        ws.trackWetness = wp.wetness;
        ws.rainIntensity = wp.rain;
        setWeatherPreset(ws);
        m_features.weatherCtrl.applyPreset(name);
    };
    m_features.onSetupLoad = [this](const std::string& path) {
        if (!m_setupGarage || !isSafePath(path)) return;
        SetupData s = m_setupGarage->setup();
        loadSetupFromFile(s, path);
        m_setupGarage->setSetup(s);
    };
    m_features.onSetupSave = [this](const std::string& path) {
        if (!m_setupGarage || !isSafePath(path)) return;
        saveSetupToFile(m_setupGarage->setup(), path);
    };
}

bool SimulationLoop::loadReplayFile(const std::string& path) {
    if (!isSafePath(path)) return false;
    if (!m_features.loadReplay(path)) return false;
    setReplayMode(true);
    std::fprintf(stderr, "SimulationLoop: replay mode ON (%d frames)\n",
                 m_features.replay.frameCount());
    return true;
}

void SimulationLoop::configurePitAxis(float originX, float originZ, float headingRad, float lengthM) {
    PitAxis axis;
    axis.originX = originX;
    axis.originZ = originZ;
    axis.heading = headingRad;
    m_pitQueue.setAxis(axis);
    m_pitCollision.setAxis(axis);
    PitLaneQueueConfig qc = m_pitQueue.config();
    qc.pitEndAlong = lengthM > 1.f ? std::min(lengthM, 5000.f) : 120.f;
    m_pitQueue.setConfig(qc);
}

void SimulationLoop::snapVehicleToPose(ks::physics::VehicleSimulator* veh, const WorldPose& pose) {
    if (!veh) return;
    auto& st = veh->state();
    st.position.x = finiteOr(pose.x, 0.f);
    st.position.y = finiteOr(pose.y, 0.f);
    st.position.z = finiteOr(pose.z, 0.f);
    st.heading = finiteOr(pose.heading, 0.f);
    st.velocity = {};
    st.angularVelocity = {};
    st.speed = 0.f;
    m_snapHoldSec = kSnapHoldDuration;
    veh->setFrozen(true);
}

bool SimulationLoop::loadGarageFromTrack(const std::string& trackDir) {
    namespace fs = std::filesystem;
    if (!isSafePath(trackDir)) return false;
    const char* candidates[] = {
        "/data/pit_boxes.ini", "/pit_boxes.ini", "/data/garage.ini", "/garage.ini", "/data/pits.ini"
    };
    std::string path;
    for (auto c : candidates) {
        fs::path p = fs::path(trackDir + c);
        if (fs::exists(p)) { path = p.string(); break; }
    }
    if (path.empty()) return false;

    std::ifstream in(path);
    if (!in) return false;
    m_garageLayout.boxes.clear();
    GarageBox box;
    bool have = false;
    std::string line;
    while (std::getline(in, line)) {
        while (!line.empty() && (line.back()=='\r' || line.back()==' ')) line.pop_back();
        if (line.empty() || line[0]==';' || line[0]==';') continue;
        if (line.front()=='[') {
            if (have) { m_garageLayout.boxes.push_back(box); box = GarageBox{}; }
            have = true;
            int idx = 0;
            for (char ch : line) if (ch>='0' && ch<='9') idx = idx*10 + (ch-'0');
            box.index = idx;
            continue;
        }
        auto eq = line.find('=');
        if (eq == std::string::npos || !have) continue;
        auto key = line.substr(0, eq);
        auto val = line.substr(eq+1);
        auto f = [&](const char* k) { return key.find(k) != std::string::npos; };
        try {
            if (f("WORLD_POSITION") || f("POS_X") || key=="X") box.pose.x = std::stof(val);
            else if (f("POS_Y") || key=="Y") box.pose.y = std::stof(val);
            else if (f("POS_Z") || key=="Z") box.pose.z = std::stof(val);
            else if (f("HEADING") || f("YAW")) box.pose.heading = std::stof(val);
            else if (f("PIT_HEADING")) m_garageLayout.pitLaneHeading = std::stof(val);
        } catch (...) {}
    }
    if (have) m_garageLayout.boxes.push_back(box);
    if (m_garageLayout.boxes.empty()) return false;
    if (m_garageLayout.boxes.size() > 32)
        m_garageLayout.boxes.resize(32);
    if (m_garageLayout.pitLaneHeading == 0.f)
        m_garageLayout.pitLaneHeading = m_garageLayout.boxes[0].pose.heading;
    configurePitAxis(m_garageLayout.boxes[0].pose.x, m_garageLayout.boxes[0].pose.z,
                     m_garageLayout.pitLaneHeading, 120.f);
    m_garageExit.bindBox(0, m_garageLayout.boxes[0].pose, m_garageLayout.pitLaneHeading);
    m_garageLayout.boxes[0].occupied = true;
#if HAS_VEHICLE_SIM
    snapVehicleToPose(m_vehicle.get(), m_garageLayout.boxes[0].pose);
#endif
    std::fprintf(stderr, "SimulationLoop: loaded %zu pit boxes from %s\n",
                 m_garageLayout.boxes.size(), path.c_str());
    return true;
}

void SimulationLoop::updatePitLane(float dt) {
    if (m_snapHoldSec > 0.f) {
        m_snapHoldSec = std::max(0.f, m_snapHoldSec - dt);
#if HAS_VEHICLE_SIM
        if (m_vehicle) {
            m_vehicle->setFrozen(true);
            m_vehicle->setThrottle(0);
            m_vehicle->setBrake(1.0);
            m_vehicle->setSteering(0);
            auto& st = m_vehicle->state();
            st.velocity = {};
            st.angularVelocity = {};
            st.speed = 0.f;
        }
#endif
        if (m_snapHoldSec <= 0.f) {
#if HAS_VEHICLE_SIM
            if (m_vehicle) m_vehicle->setFrozen(false);
#endif
        }
    }
    if (!m_pitSystemsReady) m_pitSystemsReady = true;
    m_pitQueue.setSimTime(static_cast<float>(m_simTime));
#if HAS_VEHICLE_SIM
    if (!m_vehicle) { m_pitQueue.update(dt); return; }
    const auto st = m_vehicle->getState();
    const float x = static_cast<float>(st.position.x);
    const float z = static_cast<float>(st.position.z);
    const float speed = static_cast<float>(st.speed);
    PitCarBody body;
    body.carId = kPlayerCarId;
    body.x = x; body.z = z; body.heading = static_cast<float>(st.heading);
    body.vx = static_cast<float>(st.velocity.x);
    body.vz = static_cast<float>(st.velocity.z);
    body.active = true;
    body.invulnerable = (m_garageExit.phase() == GarageExitPhase::InGarage ||
                         m_garageExit.phase() == GarageExitPhase::Preparing ||
                         m_garageExit.phase() == GarageExitPhase::EngineStart);
    m_pitCollision.upsert(body);
    if (m_multiCar) {
        for (const auto& ce : m_multiCar->cars()) {
            if (!ce || !ce->isActive || ce->isPlayer || !ce->vehicle) continue;
            const auto ost = ce->vehicle->getState();
            PitCarBody ob;
            ob.carId = ce->id;
            ob.x = static_cast<float>(ost.position.x);
            ob.z = static_cast<float>(ost.position.z);
            ob.heading = static_cast<float>(ost.heading);
            ob.vx = static_cast<float>(ost.velocity.x);
            ob.vz = static_cast<float>(ost.velocity.z);
            ob.active = true;
            m_pitCollision.upsert(ob);
            m_pitQueue.updateCar(ce->id, ob.x, ob.z, static_cast<float>(ost.speed));
        }
    }
    const auto phase = m_garageExit.phase();
    if (phase == GarageExitPhase::Preparing || phase == GarageExitPhase::BoxClear ||
        phase == GarageExitPhase::RollingOut || phase == GarageExitPhase::PitLane) {
        m_pitQueue.requestLeave(kPlayerCarId, 1, m_garageExit.garageIndex(), true, x, z);
        m_pitQueue.updateCar(kPlayerCarId, x, z, speed);
        if (m_pitQueue.isCleared(kPlayerCarId) && phase == GarageExitPhase::RollingOut)
            m_pitQueue.markMoving(kPlayerCarId);
    }
    if (phase == GarageExitPhase::OnTrack) m_pitQueue.leaveQueue(kPlayerCarId);
    if (phase == GarageExitPhase::Returning) {
        m_pitQueue.requestEnter(kPlayerCarId, 1, m_garageExit.garageIndex(), true, x, z);
        m_pitQueue.updateCar(kPlayerCarId, x, z, speed);
    }
    m_pitQueue.update(dt);
    m_pitCollision.step(dt);
    m_pitCollision.applyToQueue(m_pitQueue);
    const float maxMs = m_pitQueue.suggestedMaxSpeedMs(kPlayerCarId);
    if ((phase == GarageExitPhase::PitLane || phase == GarageExitPhase::RollingOut) &&
        speed > maxMs && maxMs >= 0.f) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(maxMs < 0.5f ? 0.6 : 0.25);
    }
    if (m_pitCollision.damageImpulseFor(kPlayerCarId) > 0.f)
        m_raceSession.addPenalty(kPlayerCarId, Penalty::Type::TimeAdded, 0.f, "pit contact");
#else
    (void)dt; m_pitQueue.update(dt);
#endif
}

void SimulationLoop::updateGarageExit(float dt) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const auto st = m_vehicle->getState();
    GarageExitInput in;
    in.engineRunning = st.rpm > 200.0;
    in.ignitionOn = true;
    in.speedMs = static_cast<float>(st.speed);
    in.throttle = static_cast<float>(st.throttle);
    in.brake = static_cast<float>(st.brake);
    in.steer = static_cast<float>(st.steering);
    in.posX = static_cast<float>(st.position.x);
    in.posY = static_cast<float>(st.position.y);
    in.posZ = static_cast<float>(st.position.z);
    in.heading = static_cast<float>(st.heading);
    in.pitLaneOpen = true;
    const bool queueBlock = m_pitQueue.shouldBlockGarageExit(kPlayerCarId);
    const bool contactBlock = m_pitCollision.isInContact(kPlayerCarId) &&
        (m_garageExit.phase() == GarageExitPhase::BoxClear ||
         m_garageExit.phase() == GarageExitPhase::RollingOut);
    const bool serviceBlock = m_pitRepair.isBusy();
    in.pathBlocked = queueBlock || contactBlock || serviceBlock;
    if (m_garageExit.phase() == GarageExitPhase::InGarage && st.throttle > 0.2 && !serviceBlock && m_snapHoldSec <= 0.f)
        in.requestLeave = true;
    GarageExitOutput out = m_garageExit.update(dt, in);
    if (out.holdControls || serviceBlock) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(out.snapToBox || serviceBlock ? 1.0 : st.brake);
        if (out.snapToBox || serviceBlock) m_vehicle->setSteering(0);
    }
    if (out.pitLimiterActive && st.speed > out.pitLimiterMaxMs) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(0.4);
    }
#else
    (void)dt;
#endif
}

void SimulationLoop::updatePitRepair(float dt) {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    applyDamageEffects();
    const auto st = m_vehicle->getState();
    const auto phase = m_garageExit.phase();
    PitRepairInput in;
    in.inGarageBox = (phase == GarageExitPhase::InGarage || phase == GarageExitPhase::Returning);
    in.speedMs = static_cast<float>(st.speed);
    in.fuelL = static_cast<float>(st.fuel);
    in.fuelCapacityL = 100.f;
    in.targetFuelL = 100.f;
    in.wantTyres = true; in.wantBody = true; in.wantSuspension = true; in.wantAero = true;
    in.requestService = m_requestPitService && in.inGarageBox;
    PitRepairOutput out = m_pitRepair.update(dt, in, m_damage, phase);
    if (out.holdCar) {
        m_vehicle->setThrottle(0); m_vehicle->setBrake(1.0); m_vehicle->setSteering(0);
    }
    if (out.completedThisFrame) {
        m_requestPitService = false;
        m_vehicle->damage().repairPartial(1.f);
        std::fprintf(stderr, "SimulationLoop: pit service COMPLETE\n");
    }
#else
    (void)dt;
#endif
}

void SimulationLoop::spawnAiGrid(int count) {
    if (count <= 0) return;
    count = std::min(count, 32);
    if (!m_multiCar) m_multiCar = std::make_unique<MultiCarManager>();
    std::vector<int> toRemove;
    for (const auto& ce : m_multiCar->cars())
        if (ce && !ce->isPlayer) toRemove.push_back(ce->id);
    for (int id : toRemove) m_multiCar->removeCar(id);
    const int boxes = static_cast<int>(m_garageLayout.boxes.size());
    for (int i = 0; i < count; ++i) {
        const int boxIdx = (i + 1) % std::max(1, boxes);
        WorldPose pose;
        if (boxes > 0) {
            pose = m_garageLayout.boxes[boxIdx].pose;
            m_garageLayout.boxes[boxIdx].occupied = true;
        } else {
            pose.x = 6.f * float(i + 1); pose.heading = m_garageLayout.pitLaneHeading;
        }
        char name[32];
        std::snprintf(name, sizeof(name), "AI_%02d", i + 1);
        const int id = m_multiCar->addCar("ai_car", name, vec3{pose.x, pose.y, pose.z}, false);
        if (auto* ce = m_multiCar->getCar(id)) {
            ce->transform = mat4();
            ce->transform(0, 3) = pose.x;
            ce->transform(1, 3) = pose.y;
            ce->transform(2, 3) = pose.z;
            snapVehicleToPose(ce->vehicle.get(), pose);
        }
    }
    m_aiCarCount = count;
}

void SimulationLoop::applyDamageEffects() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    m_vehicle->setEnginePower(260.0 * std::max(0.15, (double)m_vehicle->damage().powerMultiplier()));
#endif
}


void SimulationLoop::applyVehicleSetup() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    SetupData s;
    if (m_setupGarage)
        s = m_setupGarage->setup();
    applySetupToVehicle(s, m_vehicle.get());
    std::fprintf(stderr, "SimulationLoop: setup applied (fuel=%.1f bias=%.2f wings=%.1f/%.1f)\n",
                 s.fuel, s.brakeBias, s.frontWingAngle, s.rearWingAngle);
#endif
}

void SimulationLoop::updateForceFeedback() {
#if HAS_VEHICLE_SIM
    if (!m_vehicle || !m_ffb) return;
    if (m_vehicle->isFrozen()) return;
    m_ffb->update(*m_vehicle, 1.0f);
#endif
}


void SimulationLoop::updateSessionFlow(float dt) {
    if (m_replayMode) return;
    const auto prev = m_sessionFlow.phase();
    m_sessionFlow.tick(dt);

    // Feed qualy laps from race session timing
    if (m_sessionFlow.phase() == SessionFlowPhase::Qualifying) {
        const auto& t = m_raceSession.timing();
        if (t.lastLapTime > 1.f && t.valid)
            m_sessionFlow.reportQualyLap(kPlayerCarId, t.lastLapTime);
    }

    // When grid published → apply positions
    if (prev != SessionFlowPhase::GridPublished &&
        m_sessionFlow.phase() == SessionFlowPhase::GridPublished) {
        buildGridFromQualifying();
    }

    // Countdown: hold player
#if HAS_VEHICLE_SIM
    if (m_vehicle && m_sessionFlow.blocksDrive()) {
        m_vehicle->setThrottle(0);
        m_vehicle->setBrake(1.0);
        if (m_sessionFlow.phase() == SessionFlowPhase::Countdown ||
            m_sessionFlow.phase() == SessionFlowPhase::GridPublished)
            m_vehicle->setFrozen(true);
    }
    if (m_vehicle && prev == SessionFlowPhase::Countdown &&
        m_sessionFlow.phase() == SessionFlowPhase::Race) {
        m_vehicle->setFrozen(false);
        m_garageExit.forceOnTrack();
    }
#endif

    // Race finished via RaceSessionManager
    if (m_sessionFlow.phase() == SessionFlowPhase::Race) {
        // When RSM ends, finalize
        // RaceSessionManager sets inactive on end — detect via remaining laps
        if (m_totalLaps > 0 && m_currentLap >= m_totalLaps)
            finalizeRaceResults();
    }
}

void SimulationLoop::buildGridFromQualifying() {
    const auto& q = m_sessionFlow.qualy();
    if (q.empty()) return;
    // Place player/AI on grid using garage boxes if available
    const int boxes = static_cast<int>(m_garageLayout.boxes.size());
    for (const auto& e : q) {
        const int boxIdx = std::min(e.gridPos - 1, std::max(0, boxes - 1));
        WorldPose pose;
        if (boxes > 0)
            pose = m_garageLayout.boxes[static_cast<size_t>(boxIdx)].pose;
        else {
            pose.x = float(e.gridPos) * 6.f;
            pose.heading = m_garageLayout.pitLaneHeading;
        }
#if HAS_VEHICLE_SIM
        if (e.carId == kPlayerCarId && m_vehicle) {
            snapVehicleToPose(m_vehicle.get(), pose);
            m_vehicle->setFrozen(true);
        } else if (m_multiCar) {
            if (auto* ce = m_multiCar->getCar(e.carId))
                snapVehicleToPose(ce->vehicle.get(), pose);
        }
#endif
        m_raceSession.setGridPosition(e.carId, e.gridPos);
    }
    std::fprintf(stderr, "SimulationLoop: grid applied from qualifying\n");
}

void SimulationLoop::finalizeRaceResults() {
    if (m_sessionFlow.phase() == SessionFlowPhase::Finished) return;
    std::vector<RaceResultEntry> rows;
    for (const auto& st : m_raceSession.standings()) {
        RaceResultEntry r;
        r.carId = st.carIndex;
        r.name = st.driverName.empty() ? st.carName : st.driverName;
        r.laps = st.currentLap;
        r.totalTime = st.totalTime;
        r.bestLap = st.bestLapTime;
        r.finished = st.finished && !st.disqualified;
        r.dnf = !r.finished;
        rows.push_back(r);
    }
    if (rows.empty()) {
        RaceResultEntry r;
        r.carId = kPlayerCarId;
        r.name = m_carName.empty() ? "Player" : m_carName;
        r.laps = m_currentLap;
        r.finished = true;
        rows.push_back(r);
    }
    m_sessionFlow.finishRace(rows);
    m_sessionPhase = 3; // finished
    if (onSessionStateChanged)
        onSessionStateChanged(m_sessionType, m_sessionPhase, m_currentLap, m_totalLaps, m_timeRemaining);
}

void SimulationLoop::setReplayMode(bool on) {
    m_replayMode = on;
#if HAS_VEHICLE_SIM
    if (m_vehicle) m_vehicle->setFrozen(on);
#endif
}

void SimulationLoop::updateReplayPlayback(float dt) {
    auto& rep = m_features.replay;
    if (!m_replayMode) return;
    rep.updatePlayback(dt);
    if (!rep.isPlaying() && rep.isFinished()) {
        m_replayMode = false;
        return;
    }
#if HAS_VEHICLE_SIM
    if (!m_vehicle) return;
    const ReplayFrame f = rep.getPlaybackFrame();
    auto& st = m_vehicle->state();
    st.position.x = f.position.x;
    st.position.y = f.position.y;
    st.position.z = f.position.z;
    st.heading = f.rotation.y;
    st.speed = f.speed;
    st.rpm = f.rpm;
    st.gear = f.gear;
    st.throttle = f.throttle;
    st.brake = f.brake;
    st.steering = f.steering;
    st.velocity = {};
    m_vehicle->setFrozen(true);
#endif
}

} // namespace ks::sim
