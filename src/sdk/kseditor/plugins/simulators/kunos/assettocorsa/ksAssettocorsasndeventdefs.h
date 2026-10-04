#ifndef KSASSETTOCORSASNDEVENTDEFS_H
#define KSASSETTOCORSASNDEVENTDEFS_H

#include <QString>
#include <QStringList>
#include <QMap>
#include <QSet>

namespace ks {
namespace plugins {
namespace kunos {
namespace assettocorsa {

struct ksAssettoCorsaSndEventDef {
    QString path;
    QString name;
    QString category;
    QString description;
    bool is3D;
    bool loops;
    float defaultVolume;
    QStringList parameters;
};

struct ksAssettoCorsaSndParameterDef {
    QString name;
    float min;
    float max;
    float defaultValue;
    QString description;
};

class ksAssettocorsasndeventdefs {
public:
    static const ksAssettocorsasndeventdefs& instance() {
        static ksAssettocorsasndeventdefs defs;
        return defs;
    }

    static QString carEventPath(const QString& carId, const QString& eventName) {
        return QString("/cars/%1/%2").arg(carId, eventName);
    }

    const ksAssettoCorsaSndEventDef* getEvent(const QString& name) const {
        auto it = m_events.constFind(name);
        return it != m_events.constEnd() ? &(it.value()) : nullptr;
    }

    const ksAssettoCorsaSndEventDef* getEventByPath(const QString& path) const {
        for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
            if (it.value().path == path) return &(it.value());
        }
        return nullptr;
    }

    QStringList eventNames() const { return m_events.keys(); }
    QStringList eventPaths() const {
        QStringList paths;
        for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
            paths.append(it.value().path);
        }
        return paths;
    }

    QStringList eventsByCategory(const QString& category) const {
        QStringList names;
        for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
            if (it.value().category == category) names.append(it.value().name);
        }
        return names;
    }

    const ksAssettoCorsaSndParameterDef* getParameter(const QString& name) const {
        auto it = m_parameters.constFind(name);
        return it != m_parameters.constEnd() ? &(it.value()) : nullptr;
    }

    QStringList parameterNames() const { return m_parameters.keys(); }

    QStringList categories() const {
        QSet<QString> cats;
        for (auto it = m_events.constBegin(); it != m_events.constEnd(); ++it) {
            cats.insert(it.value().category);
        }
        return cats.values();
    }

    static QStringList defaultCarEvents() {
        return {"engine_int", "engine_ext", "turbo", "limiter", "door", "horn"};
    }

    static QString soundbankPath(const QString& carDir, const QString& carId) {
        return QString("%1/sfx/%2.bank").arg(carDir, carId);
    }

    static QString guidsPath(const QString& carDir) {
        return QString("%1/sfx/GUIDs.txt").arg(carDir);
    }

    static QString globalGuidsPath(const QString& acRoot) {
        return QString("%1/content/sfx/GUIDs.txt").arg(acRoot);
    }

private:
    QMap<QString, ksAssettoCorsaSndEventDef> m_events;
    QMap<QString, ksAssettoCorsaSndParameterDef> m_parameters;

    ksAssettocorsasndeventdefs() {
        addEvent("engine_int", "event:/cars/{car}/engine_int", "Engine", "Interior engine sound", true, true, 1.0f,
                 {"rpms", "throttle"});
        addEvent("engine_ext", "event:/cars/{car}/engine_ext", "Engine", "Exterior engine sound", true, true, 1.0f,
                 {"rpms", "throttle"});
        addEvent("turbo", "event:/cars/{car}/turbo", "Engine", "Turbo whistle", true, true, 0.6f,
                 {"boost"});
        addEvent("limiter", "event:/cars/{car}/limiter", "Engine", "Rev limiter", true, false, 0.8f,
                 {"decay"});
        addEvent("door", "event:/cars/{car}/door", "Body", "Door open/close", true, false, 0.7f,
                 {"state"});
        addEvent("horn", "event:/cars/{car}/horn", "Body", "Horn", true, false, 0.9f,
                 {});
        addEvent("backfire_ext", "event:/cars/{car}/backfire_ext", "Backfire", "Backfire sound", true, false, 0.8f,
            { "throttle","event cone angle"});
        addEvent("backfire_int", "event:/cars/{car}/backfire_int", "Backfire", "Interior backfire sound", true, false, 0.8f,
            { "throttle","event cone angle" });
        addEvent("gear_ext", "event:/cars/{car}/gear_ext", "Engine", "Gear change sound (exterior)", true, false, 0.8f,
            { "state", "event cone angle" });
        addEvent("gear_int", "event:/cars/{car}/gear_int", "Engine", "Gear change sound (interior)", true, false, 0.8f,
            { "state" });
        addEvent("gear_grind", "event:/cars/{car}/gear_grind", "Engine", "Gear grind sound", true, false, 0.8f,
            { "timeline" });
        addEvent("skid_ext", "event:/cars/{car}/skid_ext", "Tires", "Skid sound (exterior)", true, false, 0.8f,
            { "timeline", "event cone angle" });
        addEvent("skid_int", "event:/cars/{car}/skid_int", "Tires", "Skid sound (interior)", true, false, 0.8f,
            { "timeline" });
        addEvent("transmission", "event:/cars/{car}/transmission", "Transmission", "Transmission sound", true, true, 0.7f,
            { "timeline", "throttle","drivetrain_speed"});
        addEvent("wheel", "event:/cars/{car}/wheel", "Tires", "Wheel sound", true, false, 0.8f,
            { "timeline" });
        addEvent("wind", "event:/cars/{car}/wind", "Environment", "Wind noise", true, true, 0.5f,
            { "timeline", "air_pressure", "speed" });
        addEvent("bodywork", "event:/cars/{car}/bodywork", "Body", "Bodywork rattles and creaks", true, false, 0.6f,
            { "timeline", "speed" });
        addEvent("starter_ext", "event:/cars/{car}/starter_ext", "Engine", "Starter motor sound (exterior)", true, false, 0.7f,
            { "crank", "start", "killed" });
        addEvent("starter_int", "event:/cars/{car}/starter_int", "Engine", "Starter motor sound (interior)", true, false, 0.7f,
            { "crank", "start", "killed" });
        addEvent("brakes", "event:/cars/{car}/brakes", "Brakes", "Brake squeal and noise", true, true, 0.7f,
            { "speed", "brake", "brake_temp" });
        addEvent("chassis_ext", "event:/cars/{car}/chassis_ext", "Body", "Chassis creaks and collisions (exterior)", true, false, 0.6f,
            { "speed", "kerbL", "kerbR", "strike" });
        addEvent("chassis_int", "event:/cars/{car}/chassis_int", "Body", "Chassis creaks and collisions (interior)", true, false, 0.6f,
            { "speed", "kerbL", "kerbR", "strike" });
        addEvent("hybrid_ext", "event:/cars/{car}/hybrid_ext", "Hybrid", "Hybrid MGU sound (exterior)", true, true, 0.7f,
            { "drivetrain_speed", "throttle", "brake", "deploy", "harvest" });
        addEvent("hybrid_int", "event:/cars/{car}/hybrid_int", "Hybrid", "Hybrid MGU sound (interior)", true, true, 0.7f,
            { "drivetrain_speed", "throttle", "brake", "deploy", "harvest", "slip" });
        addEvent("ignition_ext", "event:/cars/{car}/ignition_ext", "Engine", "Ignition sound (exterior)", true, false, 0.8f,
            { "state" });
        addEvent("ignition_int", "event:/cars/{car}/ignition_int", "Engine", "Ignition sound (interior)", true, false, 0.8f,
            { "state" });
        addEvent("misc_int", "event:/cars/{car}/misc_int", "Engine", "Miscellaneous interior sounds", true, false, 0.6f,
            { "rpms", "throttle", "pit", "antistall", "gearClonk", "gearReverse" });
        addEvent("tractioncontrol_ext", "event:/cars/{car}/tractioncontrol_ext", "Tires", "Traction control sound (exterior)", true, false, 0.7f,
            { "timeline", "event cone angle" });
        addEvent("tractioncontrol_int", "event:/cars/{car}/tractioncontrol_int", "Tires", "Traction control sound (interior)", true, false, 0.7f,
            { "timeline" });
        addEvent("transmission_ext", "event:/cars/{car}/transmission_ext", "Transmission", "Transmission sound (exterior)", true, true, 0.7f,
            { "timeline", "throttle", "drivetrain_speed", "event cone angle" });
        addEvent("turbo_ext", "event:/cars/{car}/turbo_ext", "Engine", "Turbo whistle (exterior)", true, true, 0.6f,
            { "boost", "event cone angle" });

        // CSP extension events (non-car-specific)
        addEvent("rain_amb", "event:/extension_common/rain_amb", "CSP Extension", "Rain ambient loop", true, true, 0.5f, {});
        addEvent("rain_amb_thunder", "event:/extension_common/rain_amb_thunder", "CSP Extension", "Rain thunderclap", true, false, 0.8f, {});
        addEvent("rain_car_ext", "event:/extension_common/rain_car_ext", "CSP Extension", "Rain on car exterior", true, true, 0.6f, {});
        addEvent("rain_car_int", "event:/extension_common/rain_car_int", "CSP Extension", "Rain on car interior", true, true, 0.6f, {});
        addEvent("rain_grass", "event:/extension_common/rain_grass", "CSP Extension", "Rain on grass surface", true, true, 0.5f, {});
        addEvent("rain_gravel", "event:/extension_common/rain_gravel", "CSP Extension", "Rain on gravel surface", true, true, 0.5f, {});
        addEvent("rain_skid_ext", "event:/extension_common/rain_skid_ext", "CSP Extension", "Rain skid exterior", true, false, 0.6f, {});
        addEvent("rain_skid_int", "event:/extension_common/rain_skid_int", "CSP Extension", "Rain skid interior", true, false, 0.6f, {});
        addEvent("turn_signal_ext__off", "event:/extension_common/turn_signal_ext__off", "CSP Extension", "Turn signal exterior tick (off state)", true, false, 0.7f, {});
        addEvent("turn_signal_int__off", "event:/extension_common/turn_signal_int__off", "CSP Extension", "Turn signal interior tick (off state)", true, false, 0.7f, {});
        addEvent("turn_signal_int", "event:/extension_common/turn_signal_int", "CSP Extension", "Turn signal interior tick (on state)", true, false, 0.7f, {});
        addEvent("wiper_car_ext", "event:/extension_common/wiper_car_ext", "CSP Extension", "Wiper exterior sound", true, false, 0.6f, {});
        addEvent("wiper_car_ext_vintage", "event:/extension_common/wiper_car_ext_vintage", "CSP Extension", "Wiper exterior vintage sound", true, false, 0.6f, {});
        addEvent("wiper_car_int", "event:/extension_common/wiper_car_int", "CSP Extension", "Wiper interior sound", true, false, 0.6f, {});
        addEvent("wiper_car_int_vintage", "event:/extension_common/wiper_car_int_vintage", "CSP Extension", "Wiper interior vintage sound", true, false, 0.6f, {});
        addEvent("handbrake_int", "event:/extension_common/handbrake_int", "CSP Extension", "Handbrake interior sound", true, false, 0.7f, {});
        addEvent("external_wind", "event:/extension_common/external_wind", "CSP Extension", "External wind noise", true, true, 0.5f, { "speed" });
        addEvent("csp_surfaces_skid", "event:/csp/surfaces/skid", "CSP Surfaces", "CSP surface skid sound", true, false, 0.7f, {});
        addEvent("csp_surfaces_force", "event:/csp/surfaces/force", "CSP Surfaces", "CSP surface force feedback sound", true, false, 0.6f, {});
        addEvent("csp_surfaces_rocks", "event:/csp/surfaces/rocks", "CSP Surfaces", "CSP surface rocks sound", true, false, 0.6f, {});
        addEvent("csp_surfaces_ice", "event:/csp/surfaces/ice", "CSP Surfaces", "CSP surface ice sound", true, true, 0.5f, {});

        addParameter("timeline", 0.0f, 1.0f, 0.0f, "Timeline position (0-1)");
        addParameter("event cone angle", 0.0f, 360.0f, 0.0f, "Angle between event forward and listener (degrees)");
        addParameter("state", 0.0f, 1.0f, 0.0f, "Door state (0=closed, 1=open)");
        addParameter("rpms", 0.0f, 9000.0f, 1000.0f, "Engine RPM");
        addParameter("throttle", 0.0f, 1.0f, 0.0f, "Throttle position (0-1)");
        addParameter("brake", 0.0f, 1.0f, 0.0f, "Brake position (0-1)");
        addParameter("inflation", 0.0f, 1.0f, 0.0f, "Tire inflation (0-1)");
        addParameter("suspension_damage", 0.0f, 1.0f, 0.0f, "Suspension compression (0-1)");
        addParameter("boost", 0.0f, 3.0f, 0.0f, "Turbo boost pressure (bar)");
        addParameter("bov", 0.0f, 1.0f, 0.0f, "Blow-off valve position (0-1)");
        addParameter("boc-decay", 0.0f, 1.0f, 1.0f, "Blow-off valve decay (1 = fresh hit)");
        addParameter("decay", 0.0f, 1.0f, 1.0f, "Rev limiter decay (1 = fresh hit)");
        addParameter("drivetrain_speed", 0.0f, 300.0f, 0.0f, "Drivetrain speed (km/h)");
        addParameter("air_pressure", 0.0f, 2.0f, 1.0f, "Air pressure (1 = sea level)");
        addParameter("speed", 0.0f, 300.0f, 0.0f, "Vehicle speed (km/h)");
        addParameter("brake_temp", 0.0f, 1000.0f, 0.0f, "Brake disc temperature (Celsius)");
        addParameter("kerbL", 0.0f, 1.0f, 0.0f, "Left wheel kerb vibration (0-1)");
        addParameter("kerbR", 0.0f, 1.0f, 0.0f, "Right wheel kerb vibration (0-1)");
        addParameter("strike", 0.0f, 1.0f, 0.0f, "Chassis collision intensity (0-1)");
        addParameter("crank", 0.0f, 1.0f, 0.0f, "Starter cranking (0=off, 1=on)");
        addParameter("start", 0.0f, 1.0f, 0.0f, "Engine start signal (0=off, 1=on)");
        addParameter("killed", 0.0f, 1.0f, 0.0f, "Engine killed signal (0=off, 1=on)");
        addParameter("deploy", 0.0f, 1.0f, 0.0f, "Hybrid KERS deployment (0-1)");
        addParameter("harvest", 0.0f, 1.0f, 0.0f, "Hybrid energy recovery level (0-1)");
        addParameter("slip", 0.0f, 1.0f, 0.0f, "Wheel slip ratio (0-1)");
        addParameter("pit", 0.0f, 1.0f, 0.0f, "Pit limiter active (0=off, 1=on)");
        addParameter("antistall", 0.0f, 1.0f, 0.0f, "Anti-stall active (0=off, 1=on)");
        addParameter("gearClonk", 0.0f, 1.0f, 0.0f, "Gear clonk indicator (0-1)");
        addParameter("gearReverse", 0.0f, 1.0f, 0.0f, "Reverse gear indicator (0=off, 1=on)");
    }

    void addEvent(const QString& name, const QString& path, const QString& category,
                  const QString& desc, bool is3D, bool loops, float vol, const QStringList& params) {
        ksAssettoCorsaSndEventDef def;
        def.name = name;
        def.path = path;
        def.category = category;
        def.description = desc;
        def.is3D = is3D;
        def.loops = loops;
        def.defaultVolume = vol;
        def.parameters = params;
        m_events[name] = def;
    }

    void addParameter(const QString& name, float min, float max, float def, const QString& desc) {
        ksAssettoCorsaSndParameterDef param;
        param.name = name;
        param.min = min;
        param.max = max;
        param.defaultValue = def;
        param.description = desc;
        m_parameters[name] = param;
    }
};

} // namespace assettocorsa
} // namespace kunos
} // namespace plugins
} // namespace ks

#endif // KSASSETTOCORSASNDEVENTDEFS_H