#include "ControlsConfig.h"
#include "engine/Config/Json.h"
#include <fstream>
#include <sstream>

namespace ks {
namespace sim {

ControlsConfig ControlsConfig::defaults() {
    return ControlsConfig{};
}

bool ControlsConfig::loadFromFile(const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    ks::engine::json::Value root;
    std::string err;
    if (!ks::engine::json::parse(ss.str(), root, &err)) return false;
    if (!root.isObject()) return false;

    analogControlFilter = root.intAt("analogControlFilter", analogControlFilter);
    filterSamples = root.intAt("filterSamples", filterSamples);
    resetFfbTime = root.numberAt("resetFfbTime", resetFfbTime);
    fixedRateInputs = root.intAt("fixedRateInputs", fixedRateInputs);
    autoReverse = root.intAt("autoReverse", autoReverse);
    steerRatioSpeed = root.numberAt("steerRatioSpeed", steerRatioSpeed);
    speedSensitiveSteering = root.numberAt("speedSensitiveSteering", speedSensitiveSteering);
    keyboardLayoutOverride = root.intAt("keyboardLayoutOverride", keyboardLayoutOverride);
    keyboardWeighted = root.intAt("keyboardWeighted", keyboardWeighted);
    keyboardDamping = root.numberAt("keyboardDamping", keyboardDamping);
    keyboardSteering = root.numberAt("keyboardSteering", keyboardSteering);
    keyboardThrottle = root.numberAt("keyboardThrottle", keyboardThrottle);
    keyboardBrake = root.numberAt("keyboardBrake", keyboardBrake);
    keyboardClutch = root.numberAt("keyboardClutch", keyboardClutch);
    mimicSteering = root.intAt("mimicSteering", mimicSteering);
    gearSelectButtonHold = root.intAt("gearSelectButtonHold", gearSelectButtonHold);
    alternateNeutralActivation = root.intAt("alternateNeutralActivation", alternateNeutralActivation);
    alternateRearlookActivation = root.intAt("alternateRearlookActivation", alternateRearlookActivation);
    steeringWheelRange = root.numberAt("steeringWheelRange", steeringWheelRange);
    minimumControllerButtons = root.intAt("minimumControllerButtons", minimumControllerButtons);

    hmdPitchExponent = root.numberAt("hmdPitchExponent", hmdPitchExponent);
    hmdYawExponent = root.numberAt("hmdYawExponent", hmdYawExponent);
    hmdRollExponent = root.numberAt("hmdRollExponent", hmdRollExponent);
    hmdLeftRightRange = root.numberAt("hmdLeftRightRange", hmdLeftRightRange);
    hmdUpDownRange = root.numberAt("hmdUpDownRange", hmdUpDownRange);
    hmdForeAftRange = root.numberAt("hmdForeAftRange", hmdForeAftRange);

    freelookMousePitchSpeed = root.numberAt("freelookMousePitchSpeed", freelookMousePitchSpeed);
    freelookMouseYawSpeed = root.numberAt("freelookMouseYawSpeed", freelookMouseYawSpeed);
    freelookKeyboardPitchSpeed = root.numberAt("freelookKeyboardPitchSpeed", freelookKeyboardPitchSpeed);
    freelookKeyboardPitchAccel = root.numberAt("freelookKeyboardPitchAccel", freelookKeyboardPitchAccel);
    freelookKeyboardPitchDecel = root.numberAt("freelookKeyboardPitchDecel", freelookKeyboardPitchDecel);
    freelookKeyboardYawSpeed = root.numberAt("freelookKeyboardYawSpeed", freelookKeyboardYawSpeed);
    freelookKeyboardYawAccel = root.numberAt("freelookKeyboardYawAccel", freelookKeyboardYawAccel);
    freelookKeyboardYawDecel = root.numberAt("freelookKeyboardYawDecel", freelookKeyboardYawDecel);

    freemoveForwardSpeed = root.numberAt("freemoveForwardSpeed", freemoveForwardSpeed);
    freemoveDownSpeed = root.numberAt("freemoveDownSpeed", freemoveDownSpeed);
    freemoveRightSpeed = root.numberAt("freemoveRightSpeed", freemoveRightSpeed);

    ffbGain = root.numberAt("ffbGain", ffbGain);
    ffbForceExponent = root.numberAt("ffbForceExponent", ffbForceExponent);
    ffbEffectsLevel = root.intAt("ffbEffectsLevel", ffbEffectsLevel);
    ffbDamperCoefficient = root.numberAt("ffbDamperCoefficient", ffbDamperCoefficient);
    ffbFrictionCoefficient = root.numberAt("ffbFrictionCoefficient", ffbFrictionCoefficient);

    return true;
}

bool ControlsConfig::saveToFile(const std::string& path) const {
    ks::engine::json::Value root = ks::engine::json::Value::object();
    root.set("analogControlFilter", ks::engine::json::Value::number(analogControlFilter));
    root.set("filterSamples", ks::engine::json::Value::number(filterSamples));
    root.set("resetFfbTime", ks::engine::json::Value::number(resetFfbTime));
    root.set("fixedRateInputs", ks::engine::json::Value::number(fixedRateInputs));
    root.set("autoReverse", ks::engine::json::Value::number(autoReverse));
    root.set("steerRatioSpeed", ks::engine::json::Value::number(steerRatioSpeed));
    root.set("speedSensitiveSteering", ks::engine::json::Value::number(speedSensitiveSteering));
    root.set("keyboardLayoutOverride", ks::engine::json::Value::number(keyboardLayoutOverride));
    root.set("keyboardWeighted", ks::engine::json::Value::number(keyboardWeighted));
    root.set("keyboardDamping", ks::engine::json::Value::number(keyboardDamping));
    root.set("keyboardSteering", ks::engine::json::Value::number(keyboardSteering));
    root.set("keyboardThrottle", ks::engine::json::Value::number(keyboardThrottle));
    root.set("keyboardBrake", ks::engine::json::Value::number(keyboardBrake));
    root.set("keyboardClutch", ks::engine::json::Value::number(keyboardClutch));
    root.set("mimicSteering", ks::engine::json::Value::number(mimicSteering));
    root.set("gearSelectButtonHold", ks::engine::json::Value::number(gearSelectButtonHold));
    root.set("alternateNeutralActivation", ks::engine::json::Value::number(alternateNeutralActivation));
    root.set("alternateRearlookActivation", ks::engine::json::Value::number(alternateRearlookActivation));
    root.set("steeringWheelRange", ks::engine::json::Value::number(steeringWheelRange));
    root.set("minimumControllerButtons", ks::engine::json::Value::number(minimumControllerButtons));
    root.set("hmdPitchExponent", ks::engine::json::Value::number(hmdPitchExponent));
    root.set("hmdYawExponent", ks::engine::json::Value::number(hmdYawExponent));
    root.set("hmdRollExponent", ks::engine::json::Value::number(hmdRollExponent));
    root.set("hmdLeftRightRange", ks::engine::json::Value::number(hmdLeftRightRange));
    root.set("hmdUpDownRange", ks::engine::json::Value::number(hmdUpDownRange));
    root.set("hmdForeAftRange", ks::engine::json::Value::number(hmdForeAftRange));
    root.set("freelookMousePitchSpeed", ks::engine::json::Value::number(freelookMousePitchSpeed));
    root.set("freelookMouseYawSpeed", ks::engine::json::Value::number(freelookMouseYawSpeed));
    root.set("freelookKeyboardPitchSpeed", ks::engine::json::Value::number(freelookKeyboardPitchSpeed));
    root.set("freelookKeyboardPitchAccel", ks::engine::json::Value::number(freelookKeyboardPitchAccel));
    root.set("freelookKeyboardPitchDecel", ks::engine::json::Value::number(freelookKeyboardPitchDecel));
    root.set("freelookKeyboardYawSpeed", ks::engine::json::Value::number(freelookKeyboardYawSpeed));
    root.set("freelookKeyboardYawAccel", ks::engine::json::Value::number(freelookKeyboardYawAccel));
    root.set("freelookKeyboardYawDecel", ks::engine::json::Value::number(freelookKeyboardYawDecel));
    root.set("freemoveForwardSpeed", ks::engine::json::Value::number(freemoveForwardSpeed));
    root.set("freemoveDownSpeed", ks::engine::json::Value::number(freemoveDownSpeed));
    root.set("freemoveRightSpeed", ks::engine::json::Value::number(freemoveRightSpeed));
    root.set("ffbGain", ks::engine::json::Value::number(ffbGain));
    root.set("ffbForceExponent", ks::engine::json::Value::number(ffbForceExponent));
    root.set("ffbEffectsLevel", ks::engine::json::Value::number(ffbEffectsLevel));
    root.set("ffbDamperCoefficient", ks::engine::json::Value::number(ffbDamperCoefficient));
    root.set("ffbFrictionCoefficient", ks::engine::json::Value::number(ffbFrictionCoefficient));

    std::ofstream out(path);
    if (!out) return false;
    out << ks::engine::json::dump(root);
    return true;
}

} // namespace sim
} // namespace ks
