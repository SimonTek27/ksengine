#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QJsonObject>
#include <QColor>
#include <QFont>

namespace ks {

struct ThemeColor {
    QString name;
    QColor color;
    QString role;
};

struct ThemeFont {
    QString name;
    QFont font;
    QString role;
};

class Theme : public QObject
{
    Q_OBJECT

public:
    explicit Theme(QObject* parent = nullptr);
    ~Theme() = default;

    QString getName() const { return m_name; }
    void setName(const QString& name) { m_name = name; }

    QColor color(const QString& name) const;
    QFont font(const QString& name) const;

    void setColors(const QVector<ThemeColor>& colors) { m_colors = colors; }
    void setFonts(const QVector<ThemeFont>& fonts) { m_fonts = fonts; }

    void setStylesheet(const QString& ss) { m_stylesheet = ss; }
    QString getStylesheet() const { return m_stylesheet; }

    bool loadFromFile(const QString& path);
    bool saveToFile(const QString& path) const;

signals:
    void themeChanged();

private:
    QString m_name;
    QVector<ThemeColor> m_colors;
    QVector<ThemeFont> m_fonts;
    QString m_stylesheet;
};

class ThemeManager : public QObject
{
    Q_OBJECT

public:
    static ThemeManager* instance();

    Theme* getTheme(const QString& name) const;
    Theme* getCurrentTheme() const;
    QStringList getAvailableThemes() const;

    bool applyTheme(const QString& name);
    bool applyTheme(Theme* theme);

    bool loadThemeFromFile(const QString& path);
    bool saveThemeToFile(const QString& name, const QString& path) const;

signals:
    void themeChanged(const QString& name);

private:
    ThemeManager(QObject* parent = nullptr);
    ~ThemeManager();
    Q_DISABLE_COPY(ThemeManager)

    void buildBuiltinThemes();

    static ThemeManager* s_instance;

    QMap<QString, Theme*> m_themes;
    Theme* m_currentTheme = nullptr;
    QString m_currentThemeName;
};

} // namespace ks