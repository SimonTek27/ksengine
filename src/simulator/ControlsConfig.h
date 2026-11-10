#pragma once

#include <string>

namespace ks {
namespace sim {

struct ControlsConfig {
    int analogControlFilter = 0;
    int filterSamples = 3;
    double resetFfbTime = 0.0;
    int fixedRateInputs = 1;
    int autoReverse = 0;
    double steerRatioSpeed = 20.0;
    double speedSensitiveSteering = 0.6;
    int keyboardLayoutOverride = 0;
    int keyboardWeighted = 0;
    double keyboardDamping = 1.6;
    double keyboardSteering = 0.19931;
    double keyboardThrottle = 0.49724;
    double keyboardBrake = 0.49724;
    double keyboardClutch = 0.19931;
    int mimicSteering = 0;
    int gearSelectButtonHold = 1;
    int alternateNeutralActivation = 0;
    int alternateRearlookActivation = 1;
    double steeringWheelRange = 270.0;
    int minimumControllerButtons = 2;

    double hmdPitchExponent = 1.0;
    double hmdYawExponent = 1.0;
    double hmdRollExponent = 1.0;
    double hmdLeftRightRange = 0.15;
    double hmdUpDownRange = 0.15;
    double hmdForeAftRange = 0.15;

    double freelookMousePitchSpeed = 0.004;
    double freelookMouseYawSpeed = 0.004;
    double freelookKeyboardPitchSpeed = 1.5;
    double freelookKeyboardPitchAccel = 4.0;
    double freelookKeyboardPitchDecel = 4.0;
    double freelookKeyboardYawSpeed = 1.5;
    double freelookKeyboardYawAccel = 4.0;
    double freelookKeyboardYawDecel = 4.0;

    double freemoveForwardSpeed = 2.0;
    double freemoveDownSpeed = 2.0;
    double freemoveRightSpeed = 2.0;

    double ffbGain = 1.0;
    double ffbForceExponent = 0.8;
    int ffbEffectsLevel = 3;
    double ffbDamperCoefficient = 0.175;
    double ffbFrictionCoefficient = 0.175;

    static ControlsConfig defaults();
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;
};

} // namespace sim
} // namespace ks