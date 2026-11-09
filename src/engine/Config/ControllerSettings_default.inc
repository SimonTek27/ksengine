#pragma once
// Built-in default document for ControllerSettings: the shape of an rFactor
// Controller.ini, nothing more. The values come from the file the settings are
// merged from (system defaults, then the player's own); inventing a shipped
// profile here would put numbers on screen that no file ever said.
namespace ks {
namespace engine {
namespace config {

inline std::string ControllerSettings::defaultJson() {
    return R"json({
  "_meta": {
    "source": "builtin",
    "note": "document shape only; values come from the merged Controller file"
  },
  "CONTROLLER": {},
  "CONTROL_DEVICES": {},
  "STEER": {},
  "THROTTLE": {},
  "BRAKE": {},
  "GEAR": {},
  "CLUTCH": {},
  "FF": {},
  "ASSISTS": {}
})json";
}

} // namespace config
} // namespace engine
} // namespace ks
