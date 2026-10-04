#pragma once

#include <QObject>
#include <QStringList>
#include <QVariantMap>
#include <QVariantList>

namespace ks {
namespace audio {

class KsACSndEventBridge : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QStringList categories READ categories CONSTANT)
    Q_PROPERTY(QStringList eventNames READ eventNames CONSTANT)
    Q_PROPERTY(int eventCount READ eventCount CONSTANT)
    Q_PROPERTY(int parameterCount READ parameterCount CONSTANT)

public:
    static KsACSndEventBridge* instance();

    explicit KsACSndEventBridge(QObject* parent = nullptr);
    ~KsACSndEventBridge();

    QStringList categories() const;
    QStringList eventNames() const;
    int eventCount() const;
    int parameterCount() const;

    // Event info
    Q_INVOKABLE QStringList eventsByCategory(const QString& category) const;
    Q_INVOKABLE QString eventDescription(const QString& eventName) const;
    Q_INVOKABLE QString eventCategory(const QString& eventName) const;
    Q_INVOKABLE QString eventPath(const QString& eventName) const;
    Q_INVOKABLE bool eventIs3D(const QString& eventName) const;
    Q_INVOKABLE bool eventLoops(const QString& eventName) const;
    Q_INVOKABLE double eventDefaultVolume(const QString& eventName) const;
    Q_INVOKABLE QStringList eventParameters(const QString& eventName) const;
    Q_INVOKABLE QVariantMap eventInfo(const QString& eventName) const;

    // Parameter info
    Q_INVOKABLE QStringList parameterNames() const;
    Q_INVOKABLE double parameterMin(const QString& paramName) const;
    Q_INVOKABLE double parameterMax(const QString& paramName) const;
    Q_INVOKABLE double parameterDefault(const QString& paramName) const;
    Q_INVOKABLE QString parameterDescription(const QString& paramName) const;
    Q_INVOKABLE QVariantMap parameterInfo(const QString& paramName) const;

    // Bulk data for QML
    Q_INVOKABLE QVariantList allEvents() const;

signals:
    void eventDefsChanged();

private:
    static KsACSndEventBridge* s_instance;
};

} // namespace audio
} // namespace ks
