#include "KsACSndEventBridge.h"
#include "plugins/simulators/kunos/assettocorsa/ksAssettocorsasndeventdefs.h"

namespace ks {
namespace audio {

KsACSndEventBridge* KsACSndEventBridge::s_instance = nullptr;

KsACSndEventBridge* KsACSndEventBridge::instance()
{
    return s_instance;
}

KsACSndEventBridge::KsACSndEventBridge(QObject* parent)
    : QObject(parent)
{
    s_instance = this;
}

KsACSndEventBridge::~KsACSndEventBridge()
{
    s_instance = nullptr;
}

QStringList KsACSndEventBridge::categories() const
{
    return ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().categories();
}

QStringList KsACSndEventBridge::eventNames() const
{
    return ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().eventNames();
}

int KsACSndEventBridge::eventCount() const
{
    return ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().eventNames().size();
}

int KsACSndEventBridge::parameterCount() const
{
    return ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().parameterNames().size();
}

QStringList KsACSndEventBridge::eventsByCategory(const QString& category) const
{
    return ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().eventsByCategory(category);
}

QString KsACSndEventBridge::eventDescription(const QString& eventName) const
{
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    return ev ? ev->description : QString();
}

QString KsACSndEventBridge::eventCategory(const QString& eventName) const
{
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    return ev ? ev->category : QString();
}

QString KsACSndEventBridge::eventPath(const QString& eventName) const
{
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    return ev ? ev->path : QString();
}

bool KsACSndEventBridge::eventIs3D(const QString& eventName) const
{
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    return ev ? ev->is3D : false;
}

bool KsACSndEventBridge::eventLoops(const QString& eventName) const
{
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    return ev ? ev->loops : false;
}

double KsACSndEventBridge::eventDefaultVolume(const QString& eventName) const
{
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    return ev ? static_cast<double>(ev->defaultVolume) : 0.0;
}

QStringList KsACSndEventBridge::eventParameters(const QString& eventName) const
{
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    return ev ? ev->parameters : QStringList();
}

QVariantMap KsACSndEventBridge::eventInfo(const QString& eventName) const
{
    QVariantMap info;
    auto* ev = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getEvent(eventName);
    if (!ev) return info;
    info["name"] = ev->name;
    info["path"] = ev->path;
    info["category"] = ev->category;
    info["description"] = ev->description;
    info["is3D"] = ev->is3D;
    info["loops"] = ev->loops;
    info["defaultVolume"] = static_cast<double>(ev->defaultVolume);
    QVariantList params;
    for (const auto& p : ev->parameters)
        params.append(p);
    info["parameters"] = params;
    return info;
}

QStringList KsACSndEventBridge::parameterNames() const
{
    return ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().parameterNames();
}

double KsACSndEventBridge::parameterMin(const QString& paramName) const
{
    auto* p = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getParameter(paramName);
    return p ? static_cast<double>(p->min) : 0.0;
}

double KsACSndEventBridge::parameterMax(const QString& paramName) const
{
    auto* p = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getParameter(paramName);
    return p ? static_cast<double>(p->max) : 0.0;
}

double KsACSndEventBridge::parameterDefault(const QString& paramName) const
{
    auto* p = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getParameter(paramName);
    return p ? static_cast<double>(p->defaultValue) : 0.0;
}

QString KsACSndEventBridge::parameterDescription(const QString& paramName) const
{
    auto* p = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getParameter(paramName);
    return p ? p->description : QString();
}

QVariantMap KsACSndEventBridge::parameterInfo(const QString& paramName) const
{
    QVariantMap info;
    auto* p = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance().getParameter(paramName);
    if (!p) return info;
    info["name"] = p->name;
    info["min"] = static_cast<double>(p->min);
    info["max"] = static_cast<double>(p->max);
    info["defaultValue"] = static_cast<double>(p->defaultValue);
    info["description"] = p->description;
    return info;
}

QVariantList KsACSndEventBridge::allEvents() const
{
    QVariantList list;
    const auto& defs = ks::plugins::kunos::assettocorsa::ksAssettocorsasndeventdefs::instance();
    for (const auto& name : defs.eventNames()) {
        list.append(eventInfo(name));
    }
    return list;
}

} // namespace audio
} // namespace ks
