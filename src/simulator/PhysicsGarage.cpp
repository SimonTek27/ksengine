#include "PhysicsGarage.h"
#include "engine/Config/Locale.h"
#include "engine/physics/VehicleSimulator.h"
#include "ui/IconSet.h"
#include "ui/NativeUiTypes.h"
#include "ui/TextRenderer.h"
#include "ui/UiInput.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

namespace ks {
namespace sim {

using ui::Color;
using ui::DrawList;
using ui::Rect;
using ui::TextAlign;
using ui::TextRenderer;
using ui::TextStyle;
using ui::UiInput;
using physics::AeroModel;

std::string PhysicsGarage::tr(const char* key, const char* fallback) const {
    return m_locale != nullptr ? m_locale->tr(key, fallback) : std::string(fallback);
}

namespace {

// The setup screen's own geometry: the physics screen is read the same way.
constexpr float kPad = 56.f;
constexpr float kTabY = 92.f;
constexpr float kTabH = 30.f;
constexpr float kGridTop = 168.f;
constexpr float kRowH = 40.f;
constexpr float kColumnW = 340.f;

const char* tabIcon(const std::string& tab) {
    if (tab == "ENGINE") return "drivetrain";
    if (tab == "AERO") return "setup";
    if (tab == "WINGS") return "setup";
    if (tab == "SUSPENSION") return "suspension";
    if (tab == "DRIVETRAIN") return "drivetrain";
    if (tab == "BRAKES") return "setup";
    if (tab == "TYRES") return "tyre";
    return "generic";
}

void drawDeltaArrow(DrawList& dl, float x, float cy, bool up, const Color& colour) {
    const float h = 6.f, w = 4.5f;
    std::vector<float> points;
    if (up) {
        points = {x, cy + h * 0.5f, x + w, cy - h * 0.5f, x + w * 2.f, cy + h * 0.5f,
                  x, cy + h * 0.5f};
    } else {
        points = {x, cy - h * 0.5f, x + w, cy + h * 0.5f, x + w * 2.f, cy - h * 0.5f,
                  x, cy - h * 0.5f};
    }
    dl.addPolyline(points, colour, 1.4f, true);
}

// --- read-modify-write INI ---------------------------------------------------
// The data files are hand-kept: comments and unknown keys survive a save
// because only the keys the screen owns are replaced (or appended when the
// car never declared them). Keys and sections match case-insensitively; a
// replaced line is rewritten in the canonical AC spelling.
struct IniLine {
    std::string section; // upper-cased, "" above the first header
    std::string raw;     // comments, blanks, headers: kept verbatim
    bool isKey = false;
    std::string key; // upper-cased
    std::string value;
};

std::string upperOf(std::string s) {
    for (char& c : s) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

std::string trimOf(const std::string& s) {
    std::size_t b = s.find_first_not_of(" \t\r");
    if (b == std::string::npos) return std::string();
    std::size_t e = s.find_last_not_of(" \t\r");
    return s.substr(b, e - b + 1);
}

struct IniDoc {
    std::vector<IniLine> lines;
    bool loaded = false;

    static IniDoc read(const std::string& path) {
        IniDoc doc;
        std::ifstream file(path);
        if (!file) return doc;
        doc.loaded = true;
        std::string section;
        std::string line;
        while (std::getline(file, line)) {
            IniLine entry;
            entry.raw = line;
            const std::string t = trimOf(line);
            if (t.size() >= 2 && t.front() == '[' && t.back() == ']') {
                section = upperOf(trimOf(t.substr(1, t.size() - 2)));
                entry.section = section;
            } else if (!t.empty() && t[0] != ';' && t[0] != '#') {
                const std::size_t eq = t.find('=');
                if (eq != std::string::npos) {
                    entry.section = section;
                    entry.isKey = true;
                    entry.key = upperOf(trimOf(t.substr(0, eq)));
                    entry.value = trimOf(t.substr(eq + 1));
                }
            }
            doc.lines.push_back(entry);
        }
        return doc;
    }

    void set(const std::string& section, const std::string& key, const std::string& value) {
        const std::string s = upperOf(section), k = upperOf(key);
        for (IniLine& entry : lines) {
            if (entry.isKey && entry.section == s && entry.key == k) {
                entry.value = value;
                entry.raw = key + "=" + value;
                return;
            }
        }
        // Append inside the section, or open the section at the end.
        for (std::size_t i = lines.size(); i-- > 0;) {
            if (!lines[i].isKey && lines[i].section == s && !lines[i].raw.empty() &&
                lines[i].raw.front() == '[') {
                IniLine entry;
                entry.section = s;
                entry.isKey = true;
                entry.key = k;
                entry.value = value;
                entry.raw = key + "=" + value;
                lines.insert(lines.begin() + static_cast<std::ptrdiff_t>(i + 1), entry);
                return;
            }
        }
        if (!lines.empty() && !lines.back().raw.empty()) {
            IniLine blank;
            blank.section = s;
            lines.push_back(blank);
        }
        IniLine header;
        header.section = s;
        header.raw = "[" + section + "]";
        lines.push_back(header);
        IniLine entry;
        entry.section = s;
        entry.isKey = true;
        entry.key = k;
        entry.value = value;
        entry.raw = key + "=" + value;
        lines.push_back(entry);
    }

    bool has(const std::string& section, const std::string& key) const {
        const std::string s = upperOf(section), k = upperOf(key);
        for (const IniLine& entry : lines)
            if (entry.isKey && entry.section == s && entry.key == k) return true;
        return false;
    }

    /** Drops numbered keys (GEAR_*, WING_*) at or above `keepFrom`, so a
     *  removed gear or wing leaves no stale section behind. A removed wing
     *  takes its whole [WING_n] section (NAME, CHORD, ...) with it, not just
     *  a header. */
    void dropNumberedFrom(const std::string& prefix, int keepFrom) {
        const std::string p = upperOf(prefix);
        std::vector<std::string> dead;
        for (const IniLine& entry : lines) {
            if (entry.raw.empty() || entry.raw.front() != '[') continue;
            if (upperOf(entry.raw).rfind("[WING_", 0) != 0) continue;
            const int n = std::atoi(entry.raw.c_str() + 6);
            if (n >= keepFrom) dead.push_back(entry.section);
        }
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [&](const IniLine& e) {
                                       if (!e.section.empty() &&
                                           std::find(dead.begin(), dead.end(), e.section) !=
                                               dead.end())
                                           return true;
                                       if (e.isKey && e.key.rfind(p, 0) == 0) {
                                           const int n =
                                               std::atoi(e.key.substr(p.size()).c_str());
                                           return n >= keepFrom &&
                                                  e.key == p + std::to_string(n);
                                       }
                                       return false;
                                   }),
                    lines.end());
    }

    /** Drops every spelling of one scalar key (flat and SECTION/KEY), so a
     *  fresh set() below is the only one the loaders find. */
    void dropKeyEverywhere(const std::string& key) {
        const std::string k = upperOf(key);
        lines.erase(std::remove_if(lines.begin(), lines.end(),
                                   [&](const IniLine& e) { return e.isKey && e.key == k; }),
                    lines.end());
    }

    bool write(const std::string& path) const {
        std::ofstream file(path, std::ios::trunc);
        if (!file) return false;
        for (const IniLine& entry : lines) {
            if (entry.isKey) file << entry.key << "=" << entry.value << "\n";
            else file << entry.raw << "\n";
        }
        return static_cast<bool>(file);
    }
};

std::string ftoa(double v, int decimals) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.*f", decimals, v);
    return buffer;
}

} // namespace

// --- document ----------------------------------------------------------------

void PhysicsGarage::attach(physics::VehicleSimulator* vehicle, const std::string& carDir,
                           const std::string& carName) {
    m_vehicle = vehicle;
    m_carDir = carDir;
    m_carName = carName;
    m_savedBases.clear();
    m_status.clear();
    m_dirty = false;
    m_selectedRow = 0;
    rebuildTabs();
    // A fresh car opens on the first page; the builders kept the old tab only
    // when one was already showing.
    m_tab = m_tabs.empty() ? std::string("ENGINE") : m_tabs.front().name;
    m_selectedRow = 0;
    if (m_vehicle == nullptr)
        m_status = "no car loaded: the rows stay empty until one is";
}

void PhysicsGarage::buildTabsKeepSelection() {
    const std::string tab = m_tab;
    const int sel = m_selectedRow;
    const std::string status = m_status;
    rebuildTabs();
    m_tab = tab;
    m_selectedRow = sel;
    m_status = status;
    clampSelection();
}

void PhysicsGarage::rebuildTabs() {
    // The attach-time numbers move aside first: syncBases() carries them into
    // the fresh rows, so an added wing does not zero the arrows.
    for (const Tab& t : m_tabs)
        for (const Row& r : t.rows)
            if (!r.isAction) m_savedBases[r.id] = r.base;
    using physics::VehicleSimulator;
    m_tabs.clear();
    if (m_vehicle == nullptr) {
        m_tabs.push_back(Tab{"ENGINE", {}});
        return;
    }
    VehicleSimulator& v = *m_vehicle;

    auto setupRow = [this](const char* id, const char* label, const char* unit, int decimals,
                           double mn, double mx, double st, float VehicleSimulator::SetupParams::*field,
                           const char* help) {
        Row r;
        r.id = id;
        r.label = label;
        r.unit = unit;
        r.decimals = decimals;
        r.min = mn;
        r.max = mx;
        r.step = st;
        r.help = help;
        r.get = [this, field]() -> double {
            if (m_vehicle == nullptr) return 0.0;
            return m_vehicle->setupParams().*field;
        };
        r.set = [this, field, mn, mx](double value) {
            if (m_vehicle == nullptr) return;
            VehicleSimulator::SetupParams p = m_vehicle->setupParams();
            p.*field = static_cast<float>(std::clamp(value, mn, mx));
            m_vehicle->applySetup(p);
            m_dirty = true;
        };
        return r;
    };
    auto scalarRow = [this](const char* id, const char* label, const char* unit, int decimals,
                            double mn, double mx, double st, std::function<double()> get,
                            std::function<void(double)> apply, const char* help) {
        Row r;
        r.id = id;
        r.label = label;
        r.unit = unit;
        r.decimals = decimals;
        r.min = mn;
        r.max = mx;
        r.step = st;
        r.help = help;
        r.get = [this, get]() -> double {
            if (m_vehicle == nullptr) return 0.0;
            return get();
        };
        r.set = [this, apply, mn, mx](double value) {
            if (m_vehicle == nullptr) return;
            apply(std::clamp(value, mn, mx));
            m_dirty = true;
        };
        return r;
    };

    // ENGINE: power, rev limit and tank size, like data/engine.ini [ENGINE].
    {
        Tab tab;
        tab.name = "ENGINE";
        tab.rows.push_back(scalarRow(
            "engine.power", "Power", "kW", 0, 50, 1500, 5,
            [&v]() -> double { return v.enginePowerKw(); },
            [&v](double x) { v.setEnginePower(static_cast<float>(x)); },
            "Peak power: what the engine makes at full throttle"));
        tab.rows.push_back(scalarRow(
            "engine.rpm", "Rev limiter", "rpm", 0, 3000, 20000, 100,
            [&v]() -> double { return v.maxRpm(); },
            [&v](double x) { v.setMaxRpm(static_cast<float>(x)); },
            "Rev limiter: where the ignition cuts"));
        tab.rows.push_back(scalarRow(
            "engine.tank", "Fuel tank", "L", 0, 5, 200, 1,
            [&v]() -> double { return v.fuelTankCapacityL(); },
            [&v](double x) { v.setFuelTankCapacity(static_cast<float>(x)); },
            "Tank size: the litres a full refuel puts on board"));
        m_tabs.push_back(std::move(tab));
    }
    // AERO: the body numbers behind data/aero.ini [AERO].
    {
        Tab tab;
        tab.name = "AERO";
        tab.rows.push_back(scalarRow(
            "aero.cd", "Drag coefficient", "", 3, 0.10, 1.00, 0.005,
            [&v]() -> double { return v.dragCoeff(); },
            [&v](double x) { v.setDragCoeff(x); },
            "Body drag: the resistance of the shape before the wings"));
        tab.rows.push_back(scalarRow(
            "aero.area", "Frontal area", "m2", 2, 0.5, 5.0, 0.05,
            [&v]() -> double { return v.frontalArea(); },
            [&v](double x) { v.setFrontalArea(x); },
            "Frontal area: the square metres the air sees"));
        m_tabs.push_back(std::move(tab));
    }
    // WINGS: every wing by name, with the position the setup screen never
    // shows. Angles mirror into the axle's wing setting so they are felt at
    // once; positions, sizes and gains define the data for the aero model.
    {
        Tab tab;
        tab.name = "WINGS";
        const int count = v.aero().wingCount();
        for (int i = 0; i < count; ++i) {
            char id[64], label[96];
            std::snprintf(id, sizeof(id), "wing.%d.angle", i);
            std::snprintf(label, sizeof(label), "Wing %d angle", i + 1);
            tab.rows.push_back(scalarRow(
                id, label, "deg", 1, 0, 45, 0.5,
                [&v, i]() -> double {
                    return i < v.aero().wingCount() ? v.aero().wing(i).angle : 0.0;
                },
                [this, i](double x) {
                    auto c = m_vehicle->aero().getConfig();
                    if (i >= static_cast<int>(c.wings.size())) return;
                    c.wings[static_cast<std::size_t>(i)].angle = static_cast<float>(x);
                    m_vehicle->aero().setConfig(c);
                    // Felt now: the setup's wing proxy is what the frame reads.
                    VehicleSimulator::SetupParams p = m_vehicle->setupParams();
                    if (c.wings[static_cast<std::size_t>(i)].position[2] > 0.f)
                        p.frontWingDeg = static_cast<float>(x);
                    else
                        p.rearWingDeg = static_cast<float>(x);
                    m_vehicle->applySetup(p);
                    m_dirty = true;
                },
                "Wing angle of attack: mirrored into the axle's wing setting so it is felt at once"));
            const auto wingField = [this, &v, &scalarRow](int w, const char* key, const char* name,
                                          const char* unit, int dec, double mn, double mx,
                                          double st, const char* help) {
                char wid[64], wlabel[96];
                std::snprintf(wid, sizeof(wid), "wing.%d.%s", w, key);
                std::snprintf(wlabel, sizeof(wlabel), "Wing %d %s", w + 1, name);
                return scalarRow(
                    wid, wlabel, unit, dec, mn, mx, st,
                    [&v, w, key]() -> double {
                        if (w >= v.aero().wingCount()) return 0.0;
                        const auto& wing = v.aero().wing(w);
                        const std::string k = key;
                        if (k == "pos.x") return wing.position[0];
                        if (k == "pos.y") return wing.position[1];
                        if (k == "pos.z") return wing.position[2];
                        if (k == "chord") return wing.chord;
                        if (k == "span") return wing.span;
                        if (k == "clgain") return wing.clGain;
                        return wing.cdGain;
                    },
                    [this, w, key](double x) {
                        auto c = m_vehicle->aero().getConfig();
                        if (w >= static_cast<int>(c.wings.size())) return;
                        auto& wing = c.wings[static_cast<std::size_t>(w)];
                        const std::string k = key;
                        const float f = static_cast<float>(x);
                        if (k == "pos.x") wing.position[0] = f;
                        else if (k == "pos.y") wing.position[1] = f;
                        else if (k == "pos.z") wing.position[2] = f;
                        else if (k == "chord") wing.chord = f;
                        else if (k == "span") wing.span = f;
                        else if (k == "clgain") wing.clGain = f;
                        else wing.cdGain = f;
                        m_vehicle->aero().setConfig(c);
                        m_dirty = true;
                    },
                    help);
            };
            tab.rows.push_back(
                wingField(i, "pos.x", "pos X", "m", 2, -3, 3, 0.01,
                          "Wing position, sideways: moves the balance left or right"));
            tab.rows.push_back(
                wingField(i, "pos.y", "pos Y", "m", 2, -1, 1, 0.01,
                          "Wing position, height: ride-height sensitivity reads it"));
            tab.rows.push_back(
                wingField(i, "pos.z", "pos Z", "m", 2, -3, 3, 0.01,
                          "Wing position along the car: ahead of zero counts as front"));
            tab.rows.push_back(
                wingField(i, "chord", "chord", "m", 2, 0.05, 2, 0.01,
                          "Wing chord: depth of the profile, scales its area"));
            tab.rows.push_back(
                wingField(i, "span", "span", "m", 2, 0.1, 2.5, 0.01,
                          "Wing span: width of the profile, scales its area"));
            tab.rows.push_back(
                wingField(i, "clgain", "Cl gain", "x", 2, 0, 3, 0.05,
                          "Lift gain: multiplies the profile's own Cl curve"));
            tab.rows.push_back(
                wingField(i, "cdgain", "Cd gain", "x", 2, 0, 3, 0.05,
                          "Drag gain: multiplies the profile's own Cd curve"));
        }
        Row add;
        add.id = "wing.add";
        add.label = "ADD WING";
        add.isAction = true;
        add.help = "Adds a rear wing with neutral numbers, ready to shape";
        add.fire = [this]() -> std::string {
            if (m_vehicle == nullptr) return "no car loaded";
            auto c = m_vehicle->aero().getConfig();
            AeroModel::Wing w;
            char name[32];
            std::snprintf(name, sizeof(name), "Wing %zu",
                          c.wings.size() + 1);
            w.name = name;
            w.chord = 0.35f;
            w.span = 1.4f;
            w.angle = 8.f;
            w.position[0] = 0.f;
            w.position[1] = 0.35f;
            w.position[2] = -1.8f;
            w.clGain = 0.5f;
            w.cdGain = 0.5f;
            c.wings.push_back(w);
            m_vehicle->aero().setConfig(c);
            buildTabsKeepSelection();
            m_dirty = true;
            return std::string("added ") + name;
        };
        tab.rows.push_back(add);
        Row remove;
        remove.id = "wing.remove";
        remove.label = "REMOVE WING";
        remove.isAction = true;
        remove.help = "Drops the last wing of the list";
        remove.fire = [this]() -> std::string {
            if (m_vehicle == nullptr) return "no car loaded";
            auto c = m_vehicle->aero().getConfig();
            if (c.wings.empty()) return "the car has no wings";
            c.wings.pop_back();
            m_vehicle->aero().setConfig(c);
            buildTabsKeepSelection();
            m_dirty = true;
            return "wing removed";
        };
        tab.rows.push_back(remove);
        m_tabs.push_back(std::move(tab));
    }
    // DRIVETRAIN: final drive, differential and every gear, like
    // data/drivetrain.ini [DRIVETRAIN] and [GEARS].
    {
        Tab tab;
        tab.name = "DRIVETRAIN";
        tab.rows.push_back(setupRow(
            "drive.final", "Final drive", "x", 2, 1.5, 6, 0.01,
            &VehicleSimulator::SetupParams::finalDrive,
            "Final drive: the ratio the gearbox ends on"));
        tab.rows.push_back(setupRow(
            "drive.preload", "Diff preload", "Nm", 0, 0, 200, 1,
            &VehicleSimulator::SetupParams::diffPreloadNm,
            "Differential preload: the torque the plates carry before slip starts"));
        tab.rows.push_back(scalarRow(
            "drive.power", "Diff lock power", "%", 0, 0, 100, 1,
            [&v]() -> double { return v.setupParams().diffPower * 100.0; },
            [this](double x) {
                VehicleSimulator::SetupParams p = m_vehicle->setupParams();
                p.diffPower = static_cast<float>(x / 100.0);
                m_vehicle->applySetup(p);
                m_dirty = true;
            },
            "Differential lock under power: more lock drives both wheels together out of a corner"));
        tab.rows.push_back(scalarRow(
            "drive.coast", "Diff lock coast", "%", 0, 0, 100, 1,
            [&v]() -> double { return v.setupParams().diffCoast * 100.0; },
            [this](double x) {
                VehicleSimulator::SetupParams p = m_vehicle->setupParams();
                p.diffCoast = static_cast<float>(x / 100.0);
                m_vehicle->applySetup(p);
                m_dirty = true;
            },
            "Differential lock under coast: more lock steadies the car when you lift"));
        const std::vector<double> gears = v.gearRatios();
        for (std::size_t gi = 0; gi < gears.size(); ++gi) {
            char gid[64], glabel[64];
            std::snprintf(gid, sizeof(gid), "drive.gear.%zu", gi + 1);
            std::snprintf(glabel, sizeof(glabel), "Gear %zu", gi + 1);
            tab.rows.push_back(scalarRow(
                gid, glabel, "x", 3, 0.3, 5, 0.005,
                [&v, gi]() -> double {
                    const std::vector<double> g = v.gearRatios();
                    return gi < g.size() ? g[gi] : 0.0;
                },
                [this, gi](double x) {
                    std::vector<double> g = m_vehicle->gearRatios();
                    if (gi >= g.size()) return;
                    g[gi] = x;
                    m_vehicle->setGearRatios(g);
                    m_dirty = true;
                },
                "Gear ratio: lower reaches further, higher pulls harder"));
        }
        Row addGear;
        addGear.id = "drive.addgear";
        addGear.label = "ADD GEAR";
        addGear.isAction = true;
        addGear.help = "Appends a gear, a step longer than the last one";
        addGear.fire = [this]() -> std::string {
            if (m_vehicle == nullptr) return "no car loaded";
            std::vector<double> g = m_vehicle->gearRatios();
            if (g.size() >= 10) return "ten gears is all the loader reads";
            g.push_back(g.empty() ? 3.0 : std::max(0.3, g.back() * 0.85));
            m_vehicle->setGearRatios(g);
            buildTabsKeepSelection();
            m_dirty = true;
            return "gear added";
        };
        tab.rows.push_back(addGear);
        Row removeGear;
        removeGear.id = "drive.remgear";
        removeGear.label = "REMOVE GEAR";
        removeGear.isAction = true;
        removeGear.help = "Drops the top gear of the box";
        removeGear.fire = [this]() -> std::string {
            if (m_vehicle == nullptr) return "no car loaded";
            std::vector<double> g = m_vehicle->gearRatios();
            if (g.size() <= 1) return "the box keeps its last gear";
            g.pop_back();
            m_vehicle->setGearRatios(g);
            buildTabsKeepSelection();
            m_dirty = true;
            return "gear removed";
        };
        tab.rows.push_back(removeGear);
        m_tabs.push_back(std::move(tab));
    }
    // BRAKES: what data/brakes.ini [DATA] declares, live on the model.
    {
        Tab tab;
        tab.name = "BRAKES";
        tab.rows.push_back(scalarRow(
            "brake.torque", "Max torque", "Nm", 0, 500, 12000, 50,
            [&v]() -> double { return v.brakeMaxTorque(); },
            [&v](double x) { v.setBrakeMaxTorque(static_cast<float>(x)); },
            "Peak brake torque: the clamp the pads can hold"));
        tab.rows.push_back(scalarRow(
            "brake.front", "Front share", "%", 1, 10, 90, 0.5,
            [&v]() -> double { return v.brakeFrontShare() * 100.0; },
            [&v](double x) { v.setBrakeFrontShare(static_cast<float>(x / 100.0)); },
            "Brake split: the share of torque the front axle takes"));
        m_tabs.push_back(std::move(tab));
    }
    // TYRES: rolling radius and cold pressures, like data/tyres.ini
    // [FRONT]/[REAR] RADIUS and PRESSURE_STATIC.
    {
        Tab tab;
        tab.name = "TYRES";
        tab.rows.push_back(scalarRow(
            "tyre.radius", "Rolling radius", "m", 3, 0.2, 0.6, 0.005,
            [&v]() -> double { return v.wheelRadius(); },
            [&v](double x) { v.setWheelRadius(x); },
            "Rolling radius: the tyre's loaded radius, gears the whole car"));
        const char* names[4] = {"Pressure FL", "Pressure FR", "Pressure RL",
                                "Pressure RR"};
        for (int w = 0; w < 4; ++w) {
            char pid[64];
            std::snprintf(pid, sizeof(pid), "tyre.psi.%d", w);
            tab.rows.push_back(scalarRow(
                pid, names[w], "bar", 2, 1.2, 3.5, 0.05,
                [&v, w]() -> double { return v.setupParams().tirePsi[w]; },
                [this, w](double x) {
                    VehicleSimulator::SetupParams p = m_vehicle->setupParams();
                    p.tirePsi[w] = static_cast<float>(x);
                    m_vehicle->applySetup(p);
                    m_dirty = true;
                },
                "Cold pressure: more stiffens the tyre and bites quicker"));
        }
        m_tabs.push_back(std::move(tab));
    }
    // MASS: what the scales say, and what the tank and the ballast add.
    {
        Tab tab;
        tab.name = "MASS";
        tab.rows.push_back(scalarRow(
            "mass.base", "Dry mass", "kg", 0, 400, 3000, 5,
            [&v]() -> double { return v.baseMass(); },
            [&v](double x) { v.setMass(x); },
            "Dry mass: the car before fuel and ballast; saved to the manifest when the car has one"));
        tab.rows.push_back(setupRow(
            "mass.ballast", "Ballast", "kg", 0, -100, 300, 1,
            &VehicleSimulator::SetupParams::ballastKg,
            "Ballast: dead weight placed to meet the rules"));
        tab.rows.push_back(setupRow(
            "mass.fuel", "Fuel load", "L", 0, 0, 200, 1,
            &VehicleSimulator::SetupParams::fuelL,
            "Fuel load: weight, and weight that leaves the car as the race runs"));
        m_tabs.push_back(std::move(tab));
    }
    // The tab survives a rebuild (an added wing stays on its page); a fresh
    // attach opens on the first one.
    bool haveTab = false;
    for (const Tab& t : m_tabs)
        if (t.name == m_tab) haveTab = true;
    if (!haveTab) m_tab = m_tabs.empty() ? std::string("ENGINE") : m_tabs.front().name;
    clampSelection();
    syncBases();
}

void PhysicsGarage::syncBases() {
    for (Tab& t : m_tabs)
        for (Row& r : t.rows) {
            if (r.isAction) continue;
            const auto it = m_savedBases.find(r.id);
            if (it != m_savedBases.end()) {
                r.base = it->second;
            } else if (r.get) {
                r.base = r.get();
                m_savedBases[r.id] = r.base;
            }
        }
}

std::vector<const PhysicsGarage::Row*> PhysicsGarage::rows() const {
    std::vector<const Row*> out;
    if (m_tab.empty()) return out;
    for (const Tab& t : m_tabs) {
        if (t.name != m_tab) continue;
        for (const Row& r : t.rows) out.push_back(&r);
    }
    return out;
}

void PhysicsGarage::selectTabByName(const std::string& name) {
    for (const Tab& t : m_tabs)
        if (t.name == name) {
            selectTab(name);
            return;
        }
}

void PhysicsGarage::selectTab(const std::string& tab) {
    m_tab = tab;
    m_selectedRow = 0;
}

void PhysicsGarage::nextTab(int direction) {
    if (m_tabs.empty()) return;
    int at = 0;
    for (std::size_t i = 0; i < m_tabs.size(); ++i)
        if (m_tabs[i].name == m_tab) at = static_cast<int>(i);
    const int n = static_cast<int>(m_tabs.size());
    at = ((at + direction) % n + n) % n;
    selectTab(m_tabs[static_cast<std::size_t>(at)].name);
}

void PhysicsGarage::clampSelection() {
    const int count = static_cast<int>(rows().size());
    if (count <= 0) {
        m_selectedRow = 0;
        return;
    }
    m_selectedRow = std::max(0, std::min(m_selectedRow, count - 1));
}

void PhysicsGarage::adjustSelected(int direction) {
    const std::vector<const Row*> current = rows();
    if (current.empty()) return;
    const int at = std::max(0, std::min(m_selectedRow, static_cast<int>(current.size()) - 1));
    Row& row = const_cast<Row&>(*current[static_cast<std::size_t>(at)]);
    if (row.isAction) {
        if (row.fire) m_status = row.fire();
        return;
    }
    if (!row.set || row.step <= 0.0) return;
    const double now = row.get ? row.get() : row.min;
    double next = now + direction * row.step;
    next = row.min + std::round((next - row.min) / row.step) * row.step;
    row.set(std::clamp(next, row.min, row.max));
}

void PhysicsGarage::resetToBase() {
    if (m_vehicle == nullptr) return;
    // Every row back to the number the car opened with. Wings and gears added
    // or removed meanwhile stay: counts are structure, not numbers.
    for (const Tab& t : m_tabs)
        for (const Row& r : t.rows) {
            if (r.isAction || !r.set) continue;
            const auto it = m_savedBases.find(r.id);
            if (it != m_savedBases.end()) r.set(it->second);
        }
    m_dirty = false;
    buildTabsKeepSelection();
    m_status = tr("physics.status.reset", "back to the numbers the car opened with");
}

bool PhysicsGarage::setRowValue(const std::string& id, double value) {
    for (Tab& t : m_tabs)
        for (Row& r : t.rows) {
            if (r.isAction || r.id != id || !r.set) continue;
            const double c = std::clamp(value, r.min, r.max);
            const double steps = r.step > 0.0 ? std::round((c - r.min) / r.step) : 0.0;
            r.set(r.step > 0.0 ? std::clamp(r.min + steps * r.step, r.min, r.max) : c);
            m_dirty = true;
            return true;
        }
    return false;
}

void PhysicsGarage::setVisible(bool v) {
    m_visible = v;
    if (v) {
        m_selectedRow = 0;
        clampSelection();
    }
}

void PhysicsGarage::update(float dt) {
    const float target = m_visible ? 1.f : 0.f;
    const float speed = 6.f;
    if (m_fadeIn < target) m_fadeIn = std::min(m_fadeIn + dt * speed, target);
    else if (m_fadeIn > target) m_fadeIn = std::max(m_fadeIn - dt * speed, target);
    const float slideTarget = m_visible ? 0.f : -48.f;
    m_slide += (slideTarget - m_slide) * std::min(1.f, dt * 10.f);
}

void PhysicsGarage::render(int width, int height) {
    (void)width;
    (void)height;
}

bool PhysicsGarage::handleKeyPress(int key) {
    if (!m_visible && m_fadeIn < 0.5f) return false;
    const int count = static_cast<int>(rows().size());
    switch (key) {
    case 0x26: // UP
        if (count > 0) m_selectedRow = (m_selectedRow - 1 + count) % count;
        return true;
    case 0x28: // DOWN
        if (count > 0) m_selectedRow = (m_selectedRow + 1) % count;
        return true;
    case 0x25: // LEFT
        adjustSelected(-1);
        return true;
    case 0x27: // RIGHT
        adjustSelected(+1);
        return true;
    case 0x09: // TAB
    case 'E': case 'e':
        nextTab(+1);
        return true;
    case 'Q': case 'q':
        nextTab(-1);
        return true;
    case 0xBB: case 0x6B: // + keypad
        adjustSelected(+1);
        return true;
    case 0xBD: case 0x6D: // - keypad
        adjustSelected(-1);
        return true;
    case 'R': case 'r':
        // RESET: every row back to what the car opened with.
        resetToBase();
        return true;
    case 0x0D: // ENTER saves through the car's own data files
    {
        const std::vector<const Row*> current = rows();
        if (!current.empty()) {
            const int at = std::max(0, std::min(m_selectedRow,
                                               static_cast<int>(current.size()) - 1));
            const Row& row = *current[static_cast<std::size_t>(at)];
            if (row.isAction && row.fire) {
                m_status = row.fire();
                return true;
            }
        }
        m_status = saveToCarFiles();
        return true;
    }
    case 0x1B: // ESC
        setVisible(false);
        return true;
    default:
        break;
    }
    return false;
}

bool PhysicsGarage::handleKeyRelease(int) { return false; }

void PhysicsGarage::build(DrawList& dl, TextRenderer& text, int width, int height,
                          UiInput* input) {
    update(1.f / 60.f);
    const float a = m_fadeIn;
    if (a <= 0.01f) return;

    auto sty = [&](const Color& c, float scale, bool bold = false) {
        (void)bold;
        TextStyle s;
        s.color = c;
        s.scale = scale;
        s.shadow = true;
        s.outline = false;
        return s;
    };
    auto rightX = [&text](float xRight, const std::string& s, float scale) {
        return xRight - text.measureWidth(s, scale);
    };

    const float slide = m_slide;
    const float w = static_cast<float>(width);
    const float h = static_cast<float>(height);
    const float usableW = w * 0.70f;
    const float setupW = usableW;

    // The room stays behind the screen, dimmed enough that the numbers read
    // over it: the setup screen paints the same page over its own showroom.
    dl.addRectFilled({0, 0, w, h}, Color::rgba(0.02f, 0.03f, 0.045f, 0.96f * a));

    // --- header -------------------------------------------------------------
    text.draw(dl, kPad + slide, 26.f, tr("physics.title", "PHYSICS DATA"),
              sty(Color::rgba(1, 1, 1, a), 1.9f, true));
    const std::string car = m_carName.empty() ? tr("physics.vehicle", "Vehicle") : m_carName;
    text.draw(dl, kPad + slide, 62.f, car, sty(Color::rgba(0.62f, 0.68f, 0.76f, a), 1.1f));
    // LIVE: every row edits the car at once; the star says files lag behind.
    const std::string live = tr("physics.live", "LIVE");
    text.draw(dl, rightX(w - kPad - (m_dirty ? text.measureWidth("*", 1.3f) + 4.f : 0.f),
                          live, 1.05f),
              30.f, live, sty(Color::rgba(0.55f, 0.85f, 0.55f, a), 1.05f, true));
    if (m_dirty)
        text.draw(dl, w - kPad - text.measureWidth("*", 1.3f), 29.f, "*",
                  sty(Color::rgba(0.95f, 0.68f, 0.20f, a), 1.3f, true));

    // --- tabs ---------------------------------------------------------------
    float tabX = kPad + slide;
    std::string clickedTab;
    constexpr float kIconSize = 20.f;
    constexpr float kIconGap = 8.f;
    const float tabStopX = usableW;
    for (const Tab& tab : m_tabs) {
        if (tabX + (kIconSize + kIconGap + text.measureWidth(tab.name, 1.0f) + 26.f) >
            tabStopX)
            break;
        const float labelW = text.measureWidth(tab.name, 1.0f);
        const float tw = kIconSize + kIconGap + labelW + 26.f;
        const Rect tr{tabX, kTabY, tw, kTabH};
        const bool on = (tab.name == m_tab);
        const bool hovered = input != nullptr && input->isHovering(tr);
        if (on)
            dl.addRectFilled(tr, Color::rgba(0.90f, 0.62f, 0.12f, 0.95f * a));
        else
            dl.addRectFilled(tr, Color::rgba(1, 1, 1, (hovered ? 0.12f : 0.06f) * a));
        const Color ink = on ? Color::rgba(0.06f, 0.05f, 0.02f, a)
                             : Color::rgba(0.72f, 0.76f, 0.82f, a);
        const ui::SvgIcon* glyph = ui::icons::get(tabIcon(tab.name));
        if (glyph != nullptr)
            glyph->draw(dl, tabX + 11.f, kTabY + (kTabH - kIconSize) * 0.5f, kIconSize, ink,
                        on ? 1.3f : 1.15f);
        text.draw(dl, tabX + 11.f + kIconSize + kIconGap, kTabY + 8.f, tab.name,
                  sty(ink, 1.0f, on));
        if (hovered && input->state().leftReleased()) clickedTab = tab.name;
        tabX += tw + 6.f;
    }
    if (!clickedTab.empty()) selectTab(clickedTab);

    dl.addRectFilled({kPad, kTabY + kTabH + 6.f, w - kPad * 2.f, 1.f},
                     Color::rgba(1, 1, 1, 0.10f * a));

    // --- the car between the columns ---------------------------------------
    const float carCx = (w * 0.75f) * 0.5f;
    const float carCy = kGridTop + 118.f;
    const float bodyW = 116.f, bodyH = 268.f;
    dl.addRectFilled({carCx - bodyW * 0.5f, carCy - bodyH * 0.5f, bodyW, bodyH},
                     Color::rgba(1, 1, 1, 0.035f * a));
    dl.addRect({carCx - bodyW * 0.5f, carCy - bodyH * 0.5f, bodyW, bodyH},
               Color::rgba(1, 1, 1, 0.14f * a), 1.f);
    dl.addRectFilled({carCx - bodyW * 0.36f, carCy - bodyH * 0.22f, bodyW * 0.72f,
                      bodyH * 0.10f},
                     Color::rgba(1, 1, 1, 0.10f * a));
    const float wheelW = 34.f, wheelH = 84.f;
    const float dx = bodyW * 0.5f + wheelW * 0.35f;
    const float dy = bodyH * 0.5f - wheelH * 0.55f;
    for (int corner = 0; corner < 4; ++corner) {
        const bool left = (corner == 0 || corner == 2);
        const bool front = (corner < 2);
        const float wx = carCx + (left ? -dx : dx) - wheelW * 0.5f;
        const float wy = carCy + (front ? -dy : dy) - wheelH * 0.5f;
        dl.addRectFilled({wx, wy, wheelW, wheelH}, Color::rgba(1, 1, 1, 0.13f * a));
    }

    // --- the grid -----------------------------------------------------------
    const std::vector<const Row*> current = rows();
    const int n = static_cast<int>(current.size());
    const int half = (n + 1) / 2;
    const Row* hoveredRow = nullptr;
    const Row* sel = (m_selectedRow >= 0 && m_selectedRow < n)
                         ? current[static_cast<std::size_t>(m_selectedRow)]
                         : nullptr;
    for (int i = 0; i < n; ++i) {
        const Row& row = *current[static_cast<std::size_t>(i)];
        const int col = (i < half) ? 0 : 1;
        const int slot = (col == 0) ? i : i - half;
        const float rx = (col == 0) ? kPad + 12.f : setupW - kPad - 12.f - kColumnW;
        const float rw = kColumnW;
        const float ry = kGridTop + slot * kRowH;
        const float rh = kRowH - 8.f;
        if (ry + rh > h - 120.f) continue; // never under the help line
        const bool isSelected = (i == m_selectedRow);
        const bool hovered =
            input != nullptr && input->isHovering(Rect{rx, ry, rw, rh});
        if (hovered) hoveredRow = &row;
        if (isSelected || hovered)
            dl.addRectFilled({rx - 6.f, ry - 2.f, rw + 12.f, rh + 4.f},
                             Color::rgba(1, 1, 1, (isSelected ? 0.08f : 0.04f) * a));
        if (isSelected)
            dl.addRectFilled({rx - 6.f, ry - 2.f, 3.f, rh + 4.f},
                             Color::rgba(0.90f, 0.62f, 0.12f, a));

        const float labelY = ry + 1.f;
        text.draw(dl, rx, labelY, row.label,
                  sty(isSelected ? Color::rgba(1, 1, 1, a)
                                 : Color::rgba(0.72f, 0.76f, 0.82f, a),
                      isSelected ? 1.05f : 0.98f, isSelected));

        std::string value;
        if (row.isAction) {
            value = ">";
        } else {
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), "%.*f", row.decimals,
                          row.get ? row.get() : 0.0);
            value = buffer;
            if (row.unit[0] != 0) value += std::string(" ") + row.unit;
        }
        const float valueX = rightX(rx + rw, value, 1.05f);
        text.draw(dl, valueX, labelY, value,
                  sty(row.isAction ? Color::rgba(0.90f, 0.62f, 0.12f, a)
                      : isSelected  ? Color::rgba(1, 1, 1, a)
                                    : Color::rgba(0.86f, 0.90f, 0.95f, a),
                      1.05f, isSelected));

        // How far this row is from what the car opened with.
        if (!row.isAction) {
            const double now = row.get ? row.get() : 0.0;
            const double delta = now - row.base;
            if (std::fabs(delta) > 1e-9) {
                char deltaText[32];
                std::snprintf(deltaText, sizeof(deltaText), "%s%.*f", delta > 0 ? "+" : "-",
                              row.decimals, std::fabs(delta));
                const Color tint = Color::rgba(0.95f, 0.68f, 0.20f, a);
                const float textW = text.measureWidth(deltaText, 0.9f);
                const float markerRight = valueX - 8.f;
                drawDeltaArrow(dl, markerRight - textW - 8.f, labelY + 6.f, delta > 0, tint);
                text.draw(dl, markerRight - textW, labelY, deltaText, sty(tint, 0.9f));
            }
            char factory[80];
            std::snprintf(factory, sizeof(factory), "factory %.*f %s", row.decimals, row.base,
                          row.unit.c_str());
            text.draw(dl, rx, ry + 17.f, factory,
                      sty(Color::rgba(0.42f, 0.48f, 0.56f, a), 0.8f));
            if (row.max > row.min) {
                const double now2 = row.get ? row.get() : row.min;
                const float fraction =
                    static_cast<float>((now2 - row.min) / (row.max - row.min));
                const float barY = ry + rh - 6.f;
                dl.addRectFilled({rx, barY, rw, 4.f}, Color::rgba(1, 1, 1, 0.10f * a));
                const float fillW = rw * std::max(0.f, std::min(1.f, fraction));
                dl.addRectFilled({rx, barY, fillW, 4.f},
                                 isSelected ? Color::rgba(0.90f, 0.62f, 0.12f, a)
                                            : Color::rgba(0.55f, 0.68f, 0.80f, 0.75f * a));
            }
        }

        if (input != nullptr && input->isHovering(Rect{rx, ry, rw, rh}) &&
            input->state().leftReleased()) {
            m_selectedRow = i;
            if (row.isAction) {
                if (row.fire) m_status = row.fire();
            } else if (row.set && row.max > row.min) {
                const float clickX = input->state().x;
                const double relative =
                    (static_cast<double>(clickX) - rx) / static_cast<double>(rw);
                const double raw =
                    row.min + (row.max - row.min) * std::max(0.0, std::min(1.0, relative));
                const double stepped =
                    row.min + std::round((raw - row.min) / row.step) * row.step;
                row.set(std::clamp(stepped, row.min, row.max));
            }
        }
    }

    // --- help panel (right third) -------------------------------------------
    const float helpPanelX = usableW + 4.f;
    const float helpPanelW = (w - usableW) - kPad;
    dl.addRectFilled({helpPanelX, kGridTop, helpPanelW, h - kGridTop - 100.f},
                     Color::rgba(0.03f, 0.04f, 0.05f, 0.55f * a));
    dl.addRect({helpPanelX, kGridTop, helpPanelW, h - kGridTop - 100.f},
               Color::rgba(1, 1, 1, 0.10f * a), 1.f);
    const Row* helpEntry = hoveredRow != nullptr ? hoveredRow : sel;
    if (helpEntry != nullptr) {
        text.draw(dl, helpPanelX + 8.f, kGridTop + 8.f, helpEntry->label,
                  sty(Color::rgba(0.90f, 0.62f, 0.12f, a), 1.05f, true));
        std::string help = helpEntry->help;
        if (!helpEntry->isAction) {
            char range[128];
            std::snprintf(range, sizeof(range), "  %.*f .. %.*f %s, step %.*f",
                          helpEntry->decimals, helpEntry->min, helpEntry->decimals,
                          helpEntry->max, helpEntry->unit.c_str(), helpEntry->decimals,
                          helpEntry->step);
            help += range;
        }
        float hy = kGridTop + 30.f;
        std::string line;
        for (char c : help) {
            if (c == ' ') {
                if (text.measureWidth(line + " " + std::string(1, c), 0.85f) > helpPanelW - 16.f) {
                    text.draw(dl, helpPanelX + 8.f, hy, line,
                              sty(Color::rgba(0.72f, 0.76f, 0.82f, a), 0.85f));
                    hy += 14.f;
                    line.clear();
                } else if (!line.empty()) line += " ";
            } else line.push_back(c);
        }
        if (!line.empty())
            text.draw(dl, helpPanelX + 8.f, hy, line,
                      sty(Color::rgba(0.72f, 0.76f, 0.82f, a), 0.85f));
    }

    // --- help line and footer ------------------------------------------------
    if (helpEntry != nullptr) {
        dl.addRectFilled({kPad, h - 116.f, w - kPad * 2.f, 1.f},
                         Color::rgba(1, 1, 1, 0.10f * a));
        text.draw(dl, kPad, h - 100.f, helpEntry->label,
                  sty(Color::rgba(0.90f, 0.62f, 0.12f, a), 1.05f, true));
        const float helpX = kPad + text.measureWidth(helpEntry->label, 1.05f) + 16.f;
        std::string help = helpEntry->help;
        const float available = w - kPad - helpX;
        if (text.measureWidth(help, 0.98f) > available) {
            while (help.size() > 4 && text.measureWidth(help + "...", 0.98f) > available)
                help.pop_back();
            help += "...";
        }
        text.draw(dl, helpX, h - 100.f, help,
                  sty(Color::rgba(0.70f, 0.76f, 0.82f, a), 0.98f));
    }
    if (!m_status.empty())
        text.draw(dl, kPad, h - 76.f, m_status, sty(Color::rgba(0.55f, 0.72f, 0.62f, a), 0.95f));
    const std::string hints = tr(
        "physics.hint.main",
        "UP/DOWN row    LEFT/RIGHT adjust    Q/E tab    R reset    ENTER save the data files    "
        "ESC close");
    text.draw(dl, kPad, h - 44.f, hints, sty(Color::rgba(0.50f, 0.55f, 0.62f, a), 0.92f));
}

std::string PhysicsGarage::saveToCarFiles() {
    if (m_vehicle == nullptr) return tr("physics.save.noCar", "no car loaded: nowhere to write");
    if (m_carDir.empty()) return tr("physics.save.noFolder", "no car folder: nowhere to write");
    namespace fs = std::filesystem;
    const fs::path dataDir = fs::path(m_carDir) / "data";
    if (!fs::is_directory(dataDir))
        return tr("physics.save.noData", "the car has no data/ folder to write");
    using physics::VehicleSimulator;
    VehicleSimulator& v = *m_vehicle; // aero() is a live handle, never const
    const VehicleSimulator::SetupParams p = v.setupParams();
    std::vector<std::string> written;
    std::vector<std::string> failed;
    const auto commit = [&](const std::string& name, IniDoc& doc) {
        const std::string path = (dataDir / name).string();
        if (doc.write(path)) {
            written.push_back(name);
            std::fprintf(stderr, "[physics] written to %s\n", path.c_str());
        } else {
            failed.push_back(name);
        }
    };
    // engine.ini [ENGINE]: the three numbers its loader reads.
    {
        IniDoc doc = IniDoc::read((dataDir / "engine.ini").string());
        doc.set("ENGINE", "POWER", ftoa(v.enginePowerKw(), 0));
        doc.set("ENGINE", "LIMITER", ftoa(v.maxRpm(), 0));
        doc.set("ENGINE", "FUEL_CAPACITY", ftoa(v.fuelTankCapacityL(), 0));
        commit("engine.ini", doc);
    }
    // aero.ini: body numbers plus every wing, positions included. The car
    // keeps its other keys; a removed wing takes its section with it.
    {
        IniDoc doc = IniDoc::read((dataDir / "aero.ini").string());
        doc.set("AERO", "CD", ftoa(v.dragCoeff(), 3));
        doc.set("AERO", "AREA", ftoa(v.frontalArea(), 2));
        const auto cfg = v.aero().getConfig();
        for (std::size_t i = 0; i < cfg.wings.size(); ++i) {
            const auto& wing = cfg.wings[i];
            const std::string section = "WING_" + std::to_string(i);
            doc.set(section, "NAME", wing.name.empty() ? section : wing.name);
            doc.set(section, "CHORD", ftoa(wing.chord, 2));
            doc.set(section, "SPAN", ftoa(wing.span, 2));
            doc.set(section, "ANGLE", ftoa(wing.angle, 1));
            doc.set(section, "CL_GAIN", ftoa(wing.clGain, 2));
            doc.set(section, "CD_GAIN", ftoa(wing.cdGain, 2));
            char pos[64];
            std::snprintf(pos, sizeof(pos), "%s,%s,%s", ftoa(wing.position[0], 3).c_str(),
                          ftoa(wing.position[1], 3).c_str(), ftoa(wing.position[2], 3).c_str());
            doc.set(section, "POSITION", pos);
        }
        doc.dropNumberedFrom("WING_", static_cast<int>(cfg.wings.size()));
        commit("aero.ini", doc);
    }
    // drivetrain.ini: FINAL plus GEAR_1..N in one section, so the loader never
    // sees a stale top gear next to the new set.
    {
        IniDoc doc = IniDoc::read((dataDir / "drivetrain.ini").string());
        doc.dropKeyEverywhere("FINAL");
        doc.dropKeyEverywhere("FINAL_DRIVE");
        const std::vector<double> gears = v.gearRatios();
        doc.dropNumberedFrom("GEAR_", 1);
        const std::string section = doc.has("GEARS", "GEAR_1") ? "GEARS" : "DRIVETRAIN";
        doc.set(section, "FINAL", ftoa(v.finalDrive(), 2));
        for (std::size_t i = 0; i < gears.size(); ++i)
            doc.set(section, "GEAR_" + std::to_string(i + 1), ftoa(gears[i], 3));
        commit("drivetrain.ini", doc);
    }
    // brakes.ini [DATA]: the two numbers its loader reads.
    {
        IniDoc doc = IniDoc::read((dataDir / "brakes.ini").string());
        doc.set("DATA", "MAX_TORQUE", ftoa(v.brakeMaxTorque(), 0));
        doc.set("DATA", "FRONT_SHARE", ftoa(v.brakeFrontShare(), 3));
        commit("brakes.ini", doc);
    }
    // suspensions.ini: FRONT/REAR in SI (the file's units), ARB per axle, and
    // the GEOMETRY block the setup screen does not own.
    {
        IniDoc doc = IniDoc::read((dataDir / "suspension.ini").string());
        if (!doc.loaded) doc = IniDoc::read((dataDir / "suspensions.ini").string());
        std::string suspName = "suspension.ini";
        if (!fs::exists(dataDir / suspName) && fs::exists(dataDir / "suspensions.ini"))
            suspName = "suspensions.ini";
        const auto axle = [&](const char* section, float springNmm, float rideMm, float camber,
                              float toeDeg, float bump, float fastBump, float rebound,
                              float fastRebound) {
            doc.set(section, "SPRING_RATE", ftoa(springNmm * 1000.0, 0));
            doc.set(section, "ROD_LENGTH", ftoa(rideMm / 1000.0, 4));
            doc.set(section, "STATIC_CAMBER", ftoa(camber, 1));
            doc.set(section, "TOE_OUT", ftoa(toeDeg * 2.5e-6, 8));
            doc.set(section, "DAMP_BUMP", ftoa(bump, 0));
            doc.set(section, "DAMP_FAST_BUMP", ftoa(fastBump, 0));
            doc.set(section, "DAMP_REBOUND", ftoa(rebound, 0));
            doc.set(section, "DAMP_FAST_REBOUND", ftoa(fastRebound, 0));
        };
        axle("FRONT", p.springRateFront, p.rideHeightFrontMm, p.camberFrontDeg, p.toeFrontDeg,
             p.damperBumpNs, p.damperFastBumpNs, p.damperReboundNs, p.damperFastReboundNs);
        axle("REAR", p.springRateRear, p.rideHeightRearMm, p.camberRearDeg, p.toeRearDeg,
             p.damperBumpNs, p.damperFastBumpNs, p.damperReboundNs, p.damperFastReboundNs);
        doc.set("ARB", "FRONT", ftoa(p.arbFront, 0));
        doc.set("ARB", "REAR", ftoa(p.arbRear, 0));
        doc.set("GEOMETRY", "ROLL_CENTER_FRONT", ftoa(p.rollCenterFrontM, 3));
        doc.set("GEOMETRY", "ROLL_CENTER_REAR", ftoa(p.rollCenterRearM, 3));
        doc.set("GEOMETRY", "CG_HEIGHT", ftoa(p.cgHeightM, 3));
        doc.set("GEOMETRY", "FRONT_WEIGHT", ftoa(p.frontWeightFrac, 3));
        doc.set("GEOMETRY", "ROLL_STEER_FRONT", ftoa(p.rollSteerFront, 3));
        doc.set("GEOMETRY", "ROLL_STEER_REAR", ftoa(p.rollSteerRear, 3));
        doc.set("GEOMETRY", "LAT_COMPLIANCE_FRONT", ftoa(p.latComplianceFront, 0));
        doc.set("GEOMETRY", "LAT_COMPLIANCE_REAR", ftoa(p.latComplianceRear, 0));
        doc.set("GEOMETRY", "ALIGN_COMPLIANCE_FRONT", ftoa(p.alignComplianceFront, 0));
        doc.set("GEOMETRY", "ALIGN_COMPLIANCE_REAR", ftoa(p.alignComplianceRear, 0));
        doc.set("GEOMETRY", "WHEELBASE", ftoa(v.wheelBase(), 3));
        doc.set("GEOMETRY", "TRACK", ftoa(v.trackWidth(), 3));
        commit(suspName, doc);
    }
    // tyres.ini: radius always; pressures only where the car declares them
    // (the setup screen owns the rows, the file keeps its spelling).
    {
        IniDoc doc = IniDoc::read((dataDir / "tyres.ini").string());
        doc.set("FRONT", "RADIUS", ftoa(v.wheelRadius(), 3));
        doc.set("REAR", "RADIUS", ftoa(v.wheelRadius(), 3));
        if (doc.has("FRONT", "PRESSURE_STATIC"))
            doc.set("FRONT", "PRESSURE_STATIC", ftoa(p.tirePsi[0] / 0.0689476, 1));
        if (doc.has("REAR", "PRESSURE_STATIC"))
            doc.set("REAR", "PRESSURE_STATIC", ftoa(p.tirePsi[2] / 0.0689476, 1));
        commit("tyres.ini", doc);
    }
    // physics.json: the dry mass, and only when the car ships a manifest to
    // carry it. The manifest has no brake/drivetrain/aero keys the files above
    // already wrote, so the number lands without touching anything else.
    {
        std::string manifest;
        for (const char* candidate : {"data/physics.json", "physics.json"}) {
            const std::string path = (fs::path(m_carDir) / candidate).string();
            if (fs::exists(path)) {
                manifest = path;
                break;
            }
        }
        if (!manifest.empty()) {
            std::ifstream in(manifest);
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            char mass[32];
            std::snprintf(mass, sizeof(mass), "%.0f", v.baseMass());
            bool done = false;
            for (const char* key : {"\"massKg\"", "\"mass\"", "\"MASS\""}) {
                const std::size_t at = text.find(key);
                if (at == std::string::npos) continue;
                const std::size_t colon = text.find(':', at + std::strlen(key));
                if (colon == std::string::npos) continue;
                std::size_t num = text.find_first_of("-0123456789", colon + 1);
                if (num == std::string::npos) continue;
                const std::size_t end = text.find_first_not_of("-0123456789.eE+", num);
                text.replace(num, end == std::string::npos ? end : end - num, mass);
                done = true;
                break;
            }
            if (!done) {
                const std::size_t car = text.find("\"car\"");
                const std::size_t open =
                    car == std::string::npos ? car : text.find('{', car);
                if (open != std::string::npos)
                    text.insert(open + 1, std::string("\n    \"massKg\": ") + mass + ",");
                else
                    manifest.clear(); // nowhere honest to put it: skip the file
            }
            if (!manifest.empty()) {
                std::ofstream out(manifest, std::ios::trunc);
                if (out) {
                    out << text;
                    written.push_back(fs::path(manifest).filename().string());
                } else {
                    failed.push_back(fs::path(manifest).filename().string());
                }
            }
        }
    }
    // The rows that ride on the setup document instead (wing angles the proxy
    // reads, diff, fuel, ballast) persist through the setup screen's own save.
    m_dirty = failed.empty() ? false : m_dirty;
    if (!failed.empty()) {
        std::string report = "could not write ";
        for (std::size_t i = 0; i < failed.size(); ++i)
            report += (i ? ", " : "") + failed[i];
        return report;
    }
    std::string report = "saved ";
    for (std::size_t i = 0; i < written.size(); ++i)
        report += (i ? ", " : "") + written[i];
    report += " (setup-owned rows: wing angles, diff, fuel, ballast)";
    return report;
}

} // namespace sim
} // namespace ks
