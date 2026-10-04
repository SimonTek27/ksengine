#ifndef KSRPMPROFILE_H
#define KSRPMPROFILE_H

#include <QObject>
#include <QString>
#include <QVector>
#include <QJsonObject>
#include <QJsonDocument>

namespace ks {
namespace audio {

class KSRPMProfile : public QObject {
    Q_OBJECT

public:
    struct RPMPoint {
        int rpm;
        int durationMs;
        bool skip;

        RPMPoint(int r = 1000, int d = 3000, bool s = false)
            : rpm(r), durationMs(d), skip(s) {}
    };

    enum class EngineType {
        Economy4,
        Sport6,
        V8,
        V10,
        V12,
        Flat6,
        Rotary,
        Race
    };

    explicit KSRPMProfile(QObject* parent = nullptr);
    ~KSRPMProfile() = default;

    QString name() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    QString description() const { return m_description; }
    void setDescription(const QString& desc) { m_description = desc; }

    QVector<RPMPoint>& points() { return m_points; }
    const QVector<RPMPoint>& points() const { return m_points; }

    void setPoints(const QVector<RPMPoint>& points) { m_points = points; }
    void addPoint(int rpm, int durationMs = 3000, bool skip = false);
    void removePoint(int index);
    void clearPoints();

    EngineType engineType() const { return m_engineType; }
    void setEngineType(EngineType type);

    int minRPM() const { return m_minRPM; }
    int maxRPM() const { return m_maxRPM; }
    int step() const { return m_step; }

    void setRPMRange(int min, int max, int step);
    void generateLinearRange(int startRPM, int endRPM, int stepRPM, int holdMs = 3000);
    void generateEngineRange(EngineType type, int holdMs = 3000);

    QJsonObject toJson() const;
    void fromJson(const QJsonObject& json);

    bool save(const QString& filePath) const;
    bool load(const QString& filePath);

    static EngineType engineTypeFromString(const QString& str);
    static QString engineTypeToString(EngineType type);
    static KSRPMProfile* createPreset(EngineType type);
    static QVector<KSRPMProfile*> createAllPresets();

signals:
    void nameChanged(const QString& name);
    void pointsChanged();
    void engineTypeChanged(EngineType type);

private:
    void generateFromConfig(int minRPM, int maxRPM, int step, int holdMs);

    QString m_name;
    QString m_description;
    QVector<RPMPoint> m_points;
    EngineType m_engineType = EngineType::Sport6;
    int m_minRPM = 1000;
    int m_maxRPM = 8000;
    int m_step = 500;
};

}
}

#endif
