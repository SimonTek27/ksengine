#ifndef ACGUIDSPARSER_H
#define ACGUIDSPARSER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>

namespace ks {
namespace fileformat {

struct GUIDMapping {
    QString guid;
    QString eventType;
    QString eventPath;
    QString bankPath;
};

class ACGuidsParser : public QObject {
    Q_OBJECT
public:
    explicit ACGuidsParser(QObject* parent = nullptr);

    bool parseFile(const QString& filePath);
    bool parseFromString(const QString& content);

    QStringList eventPaths() const { return m_eventPaths; }
    QStringList bankPaths() const { return m_bankPaths; }
    QMap<QString, QString> guidToEvent() const { return m_guidToEvent; }
    QMap<QString, QString> guidToBank() const { return m_guidToBank; }
    QString eventForGUID(const QString& guid) const {
        return m_guidToEvent.value(guid);
    }
    QString bankForGUID(const QString& guid) const {
        return m_guidToBank.value(guid);
    }

    QStringList carEventPaths(const QString& carId) const;
    QStringList carBankPaths(const QString& carId) const;

    static QString globalGuidsPath(const QString& acRoot);
    static QString carGuidsPath(const QString& carDir);

    void mergeWith(const ACGuidsParser& other);
    void remapCarId(const QString& oldId, const QString& newId);

signals:
    void parsed(int eventCount, int bankCount);

private:
    QStringList m_eventPaths;
    QStringList m_bankPaths;
    QMap<QString, QString> m_guidToEvent;
    QMap<QString, QString> m_guidToBank;

    static QRegularExpression& guidRegex() {
        static QRegularExpression re(
            "\\{(\\w{8}(?:-\\w{4}){3}-\\w{12})\\}\\s+"
            "(event|bank):/(.+?)(?:\\s|$)");
        return re;
    }
};

} // namespace fileformat
} // namespace ks

#endif // ACGUIDSPARSER_H
