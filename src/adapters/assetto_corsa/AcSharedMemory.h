#pragma once
/**
 * AC-compatible shared memory layout (adapter only).
 * Windows: Local\\acpmf_physics | graphics | static
 * Linux:   /acpmf_physics etc. via POSIX shm_open
 */
#include <cstdint>
#include <cstring>
#include <string>
#include <cstdio>

namespace ks {
namespace ac {

#pragma pack(push, 4)

struct AcPhysicsPage {
    int packetId = 0;
    float gas = 0;
    float brake = 0;
    float fuel = 0;
    int gear = 0;
    int rpms = 0;
    float steerAngle = 0;
    float speedKmh = 0;
    float velocity[3] = {};
    float accG[3] = {};
    float wheelSlip[4] = {};
    float wheelLoad[4] = {};
    float wheelsPressure[4] = {};
    float wheelAngularSpeed[4] = {};
    float tyreWear[4] = {};
    float tyreDirtyLevel[4] = {};
    float tyreCoreTemperature[4] = {};
    float camberRAD[4] = {};
    float suspensionTravel[4] = {};
    float drs = 0;
    float tc = 0;
    float heading = 0;
    float pitch = 0;
    float roll = 0;
    float cgHeight = 0;
    float carDamage[5] = {};
    int numberOfTyresOut = 0;
    int pitLimiterOn = 0;
    float abs = 0;
    float kersCharge = 0;
    float kersInput = 0;
    int autoShifterOn = 0;
    float rideHeight[2] = {};
    float turboBoost = 0;
    float ballast = 0;
    float airDensity = 1.225f;
    float airTemp = 25.f;
    float roadTemp = 30.f;
    float localAngularVel[3] = {};
    float finalFF = 0;
    float performanceMeter = 0;
    int engineBrake = 0;
    int ersRecoveryLevel = 0;
    int ersPowerLevel = 0;
    int ersHeatCharging = 0;
    int ersIsCharging = 0;
    float kersCurrentKJ = 0;
    int drsAvailable = 0;
    int drsEnabled = 0;
    float brakeTemp[4] = {};
    float clutch = 0;
    float tyreTempI[4] = {};
    float tyreTempM[4] = {};
    float tyreTempO[4] = {};
    int isAIControlled = 0;
    float tyreContactPoint[4][3] = {};
    float tyreContactNormal[4][3] = {};
    float tyreContactHeading[4][3] = {};
    float brakeBias = 0.55f;
    float localVelocity[3] = {};
};

struct AcGraphicsPage {
    int packetId = 0;
    int status = 0;
    int session = 0;
    wchar_t currentTime[15] = {};
    wchar_t lastTime[15] = {};
    wchar_t bestTime[15] = {};
    wchar_t split[15] = {};
    int completedLaps = 0;
    int position = 0;
    int iCurrentTime = 0;
    int iLastTime = 0;
    int iBestTime = 0;
    float sessionTimeLeft = 0;
    float distanceTraveled = 0;
    int isInPit = 0;
    int currentSectorIndex = 0;
    int lastSectorTime = 0;
    int numberOfLaps = 0;
    wchar_t tyreCompound[33] = {};
    float replayTimeMultiplier = 1.f;
    float normalizedCarPosition = 0;
    float carCoordinates[3] = {};
    float penaltyTime = 0;
    int flag = 0;
    int idealLineOn = 0;
    int isInPitLane = 0;
    float surfaceGrip = 1.f;
    int mandatoryPitDone = 0;
    float windSpeed = 0;
    float windDirection = 0;
};

struct AcStaticPage {
    wchar_t smVersion[15] = {};
    wchar_t acVersion[15] = {};
    int numberOfSessions = 0;
    int numCars = 1;
    wchar_t carModel[33] = {};
    wchar_t track[33] = {};
    wchar_t playerName[33] = {};
    wchar_t playerSurname[33] = {};
    wchar_t playerNick[33] = {};
    int sectorCount = 3;
    float maxTorque = 0;
    float maxPower = 0;
    int maxRpm = 0;
    float maxFuel = 0;
    float suspensionMaxTravel[4] = {};
    float tyreRadius[4] = {};
    float maxTurboBoost = 0;
    float deprecated_1 = 0;
    float deprecated_2 = 0;
    int penaltiesEnabled = 0;
    float aidFuelRate = 1.f;
    float aidTireRate = 1.f;
    float aidMechanicalDamage = 1.f;
    int aidAllowTyreBlankets = 0;
    float aidStability = 0;
    int aidAutoClutch = 0;
    int aidAutoBlip = 0;
    int hasDRS = 0;
    int hasERS = 0;
    int hasKERS = 0;
    float kersMaxJ = 0;
    int engineBrakeSettingsCount = 0;
    int ersPowerControllerCount = 0;
    float trackSPlineLength = 0;
    wchar_t trackConfiguration[33] = {};
    float ersMaxJ = 0;
    int isTimedRace = 0;
    int hasExtraLap = 0;
    wchar_t carId[33] = {};
    int neutralGearIsCoastOn = 0;
};

#pragma pack(pop)

struct AcLiveInput {
    float throttle = 0, brake = 0, clutch = 0, steer = 0;
    float speedMs = 0;
    float rpm = 0;
    int gear = 1;
    float fuel = 50.f;
    float velocity[3] = {};
    float accG[3] = {};
    float localAngularVel[3] = {};
    float heading = 0, pitch = 0, roll = 0;
    float wheelSlip[4] = {};
    float wheelLoad[4] = {3500, 3500, 3500, 3500};
    float tyreTemp[4] = {80, 80, 80, 80};
    float tyreWear[4] = {};
    float tyrePressure[4] = {2.2f, 2.2f, 2.0f, 2.0f};
    float brakeTemp[4] = {200, 200, 200, 200};
    float suspensionTravel[4] = {};
    float camber[4] = {};
    float finalFF = 0;
    float surfaceGrip = 1.f;
    float airTemp = 25.f, roadTemp = 30.f;
    float airDensity = 1.225f;
    float carX = 0, carY = 0, carZ = 0;
    float normalizedSpline = 0;
    float distanceTraveled = 0;
    int completedLaps = 0;
    int position = 1;
    int currentSector = 0;
    int iCurrentTimeMs = 0;
    int iLastTimeMs = 0;
    int iBestTimeMs = 0;
    int lastSectorTimeMs = 0;
    int sessionType = 2;
    int status = 2;
    bool inPit = false;
    bool pitLimiter = false;
    int flag = 0;
    float brakeBias = 0.55f;
    std::string carModel;
    std::string trackName;
    std::string playerName;
    std::string tyreCompound = "D";
    int maxRpm = 8500;
    float maxFuel = 100.f;
    int sectorCount = 3;
    int totalLaps = 0;
    float sessionTimeLeft = 0;
    float trackSplineLength = 0;
    float carDamage[5] = {};
    float damageOverall = 0.f;
    float engineHealth = 1.f;
    float powerMult = 1.f;
    float dragMult = 1.f;
    float downforceMult = 1.f;
    int damageWarning = 0;
    bool engineSeized = false;
};

/**
 * Maps ks::sim::RaceFlag numeric codes (None=0 Green=1 Yellow=2 Blue=3
 * White=4 Black=5 Checkered=6 Meatball=7 SafetyCar=8) to AC_FLAG_TYPE
 * (SPageFileGraphic.flag): NO=0 BLUE=1 YELLOW=2 BLACK=3 WHITE=4
 * CHECKERED=5 PENALTY=6. AC has no green/SC code: green = NO_FLAG,
 * safety car = yellow; meatball = penalty flag (closest match).
 */
inline int ksRaceFlagToAcFlag(int raceFlag)
{
    switch (raceFlag) {
        case 2: return 2; // Yellow     -> AC_YELLOW_FLAG
        case 3: return 1; // Blue       -> AC_BLUE_FLAG
        case 4: return 4; // White      -> AC_WHITE_FLAG
        case 5: return 3; // Black      -> AC_BLACK_FLAG
        case 6: return 5; // Checkered  -> AC_CHECKERED_FLAG
        case 7: return 6; // Meatball   -> AC_PENALTY_FLAG
        case 8: return 2; // SafetyCar  -> AC_YELLOW_FLAG
        default: return 0; // None/Green -> AC_NO_FLAG
    }
}

inline void formatAcTime(wchar_t* dst, size_t n, int ms) {
    if (!dst || n < 2) return;
    if (ms <= 0) {
        dst[0] = L'-'; dst[1] = L'-'; dst[2] = 0;
        return;
    }
    const int m = ms / 60000;
    const int s = (ms / 1000) % 60;
    const int frac = ms % 1000;
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%d:%02d.%03d", m, s, frac);
    size_t i = 0;
    for (; i + 1 < n && buf[i]; ++i)
        dst[i] = static_cast<wchar_t>(static_cast<unsigned char>(buf[i]));
    dst[i] = 0;
}

inline void fillPhysics(AcPhysicsPage& p, const AcLiveInput& in) {
    p.packetId++;
    p.gas = in.throttle;
    p.brake = in.brake;
    p.clutch = in.clutch;
    p.fuel = in.fuel;
    if (in.gear < 0)
        p.gear = 0;
    else if (in.gear == 0)
        p.gear = 1;
    else
        p.gear = in.gear + 1;
    p.rpms = static_cast<int>(in.rpm);
    p.steerAngle = in.steer;
    p.speedKmh = in.speedMs * 3.6f;
    for (int i = 0; i < 3; ++i) {
        p.velocity[i] = in.velocity[i];
        p.accG[i] = in.accG[i];
        p.localAngularVel[i] = in.localAngularVel[i];
        p.localVelocity[i] = in.velocity[i];
    }
    for (int i = 0; i < 4; ++i) {
        p.wheelSlip[i] = in.wheelSlip[i];
        p.wheelLoad[i] = in.wheelLoad[i];
        p.wheelsPressure[i] = in.tyrePressure[i];
        p.tyreCoreTemperature[i] = in.tyreTemp[i];
        p.tyreTempI[i] = in.tyreTemp[i];
        p.tyreTempM[i] = in.tyreTemp[i];
        p.tyreTempO[i] = in.tyreTemp[i];
        p.tyreWear[i] = in.tyreWear[i];
        p.brakeTemp[i] = in.brakeTemp[i];
        p.suspensionTravel[i] = in.suspensionTravel[i];
        p.camberRAD[i] = in.camber[i];
    }
    p.heading = in.heading;
    p.pitch = in.pitch;
    p.roll = in.roll;
    p.finalFF = in.finalFF;
    p.airTemp = in.airTemp;
    p.roadTemp = in.roadTemp;
    p.airDensity = in.airDensity;
    p.pitLimiterOn = in.pitLimiter ? 1 : 0;
    p.brakeBias = in.brakeBias;
    for (int i = 0; i < 5; ++i)
        p.carDamage[i] = in.carDamage[i];
}

inline void fillGraphics(AcGraphicsPage& g, const AcLiveInput& in) {
    g.packetId++;
    g.status = in.status;
    g.session = in.sessionType;
    g.completedLaps = in.completedLaps;
    g.position = in.position;
    g.iCurrentTime = in.iCurrentTimeMs;
    g.iLastTime = in.iLastTimeMs;
    g.iBestTime = in.iBestTimeMs;
    g.lastSectorTime = in.lastSectorTimeMs;
    g.currentSectorIndex = in.currentSector;
    g.numberOfLaps = in.totalLaps;
    g.sessionTimeLeft = in.sessionTimeLeft;
    g.isInPit = in.inPit ? 1 : 0;
    g.isInPitLane = in.inPit ? 1 : 0;
    g.surfaceGrip = in.surfaceGrip;
    g.normalizedCarPosition = in.normalizedSpline;
    g.distanceTraveled = in.distanceTraveled;
    g.carCoordinates[0] = in.carX;
    g.carCoordinates[1] = in.carY;
    g.carCoordinates[2] = in.carZ;
    g.flag = in.flag;
    formatAcTime(g.currentTime, 15, in.iCurrentTimeMs);
    formatAcTime(g.lastTime, 15, in.iLastTimeMs);
    formatAcTime(g.bestTime, 15, in.iBestTimeMs);
    {
        size_t i = 0;
        for (; i + 1 < 33 && i < in.tyreCompound.size(); ++i)
            g.tyreCompound[i] = static_cast<wchar_t>(static_cast<unsigned char>(in.tyreCompound[i]));
        g.tyreCompound[i] = 0;
    }
}

inline void fillStatic(AcStaticPage& s, const AcLiveInput& in) {
    auto copyW = [](wchar_t* dst, size_t n, const std::string& src) {
        size_t i = 0;
        for (; i + 1 < n && i < src.size(); ++i)
            dst[i] = static_cast<wchar_t>(static_cast<unsigned char>(src[i]));
        dst[i] = 0;
    };
    copyW(s.smVersion, 15, "1.7");
    copyW(s.acVersion, 15, "ksim");
    copyW(s.carModel, 33, in.carModel);
    copyW(s.track, 33, in.trackName);
    copyW(s.playerName, 33, in.playerName.empty() ? "Driver" : in.playerName);
    copyW(s.carId, 33, in.carModel);
    s.numCars = 1;
    s.sectorCount = in.sectorCount;
    s.maxRpm = in.maxRpm;
    s.maxFuel = in.maxFuel;
    s.trackSPlineLength = in.trackSplineLength;
}

} // namespace ac
} // namespace ks
