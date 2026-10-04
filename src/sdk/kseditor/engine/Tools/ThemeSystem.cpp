#include "ThemeSystem.h"

#include <QApplication>
#include <QPalette>
#include <QStyleFactory>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>
#include <QDir>
#include <QStandardPaths>

namespace ks {

// ─── Theme ───────────────────────────────────────────────────────────────────

Theme::Theme(QObject* parent)
    : QObject(parent)
{}

QColor Theme::color(const QString& name) const
{
    for (const auto& c : m_colors)
        if (c.name == name) return c.color;
    return {};
}

QFont Theme::font(const QString& name) const
{
    for (const auto& f : m_fonts)
        if (f.name == name) return f.font;
    return QApplication::font();
}

bool Theme::loadFromFile(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll()).object();
    m_name = obj["name"].toString();

    m_colors.clear();
    for (const auto& v : obj["colors"].toArray()) {
        QJsonObject co = v.toObject();
        ThemeColor tc;
        tc.name  = co["name"].toString();
        tc.color = QColor(co["value"].toString());
        tc.role  = co["role"].toString();
        m_colors << tc;
    }

    m_fonts.clear();
    for (const auto& v : obj["fonts"].toArray()) {
        QJsonObject fo = v.toObject();
        ThemeFont tf;
        tf.name = fo["name"].toString();
        tf.font = QFont(fo["family"].toString(), fo["size"].toInt());
        tf.role = fo["role"].toString();
        m_fonts << tf;
    }

    m_stylesheet = obj["stylesheet"].toString();
    emit themeChanged();
    return true;
}

bool Theme::saveToFile(const QString& path) const
{
    QJsonObject obj;
    obj["name"] = m_name;

    QJsonArray colors;
    for (const auto& c : m_colors) {
        QJsonObject co;
        co["name"]  = c.name;
        co["value"] = c.color.name();
        co["role"]  = c.role;
        colors.append(co);
    }
    obj["colors"] = colors;

    QJsonArray fonts;
    for (const auto& f : m_fonts) {
        QJsonObject fo;
        fo["name"]   = f.name;
        fo["family"] = f.font.family();
        fo["size"]   = f.font.pointSize();
        fo["role"]   = f.role;
        fonts.append(fo);
    }
    obj["fonts"]      = fonts;
    obj["stylesheet"] = m_stylesheet;

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) return false;
    file.write(QJsonDocument(obj).toJson());
    return true;
}

// ─── ThemeManager ────────────────────────────────────────────────────────────

ThemeManager* ThemeManager::s_instance = nullptr;

ThemeManager* ThemeManager::instance()
{
    if (!s_instance)
        s_instance = new ThemeManager();
    return s_instance;
}

ThemeManager::ThemeManager(QObject* parent)
    : QObject(parent)
{
    buildBuiltinThemes();
}

ThemeManager::~ThemeManager()
{
    s_instance = nullptr;
}

void ThemeManager::buildBuiltinThemes()
{
    // ── Dark ──────────────────────────────────────────────────────────────
    {
        auto* t = new Theme(this);
        t->setName("dark");
        QVector<ThemeColor> cols = {
            {"background",      QColor(28,  28,  35),  "window"},
            {"surface",         QColor(38,  38,  48),  "base"},
            {"surface2",        QColor(50,  50,  62),  "alternateBase"},
            {"text",            QColor(220, 220, 225),  "windowText"},
            {"textMuted",       QColor(130, 130, 145),  ""},
            {"accent",          QColor(220, 60,  60),   "highlight"},
            {"accentText",      QColor(255, 255, 255),  "highlightedText"},
            {"border",          QColor(60,  60,  75),   ""},
            {"error",           QColor(220, 60,  60),   ""},
            {"warning",         QColor(220, 160, 60),   ""},
            {"success",         QColor(80,  200, 120),  ""},
            {"buttonBg",        QColor(55,  55,  68),   "button"},
            {"buttonText",      QColor(220, 220, 225),  "buttonText"},
        };
        t->setColors(cols);
        t->setStylesheet(R"(
QToolTip { background:#1e1e23; color:#dcdce1; border:1px solid #3c3c4b; padding:4px; }
QScrollBar:vertical   { background:#1c1c23; width:10px; margin:0; }
QScrollBar::handle:vertical { background:#464658; border-radius:5px; min-height:24px; }
QScrollBar::handle:vertical:hover { background:#565670; }
QScrollBar::add-line:vertical, QScrollBar::sub-line:vertical { height:0; }
QScrollBar:horizontal { background:#1c1c23; height:10px; }
QScrollBar::handle:horizontal { background:#464658; border-radius:5px; min-width:24px; }
QSplitter::handle { background:#3c3c4b; }
QTabBar::tab { background:#262633; color:#aaaaaf; padding:6px 14px; border-bottom:2px solid transparent; }
QTabBar::tab:selected { color:#dcdce1; border-bottom:2px solid #dc3c3c; }
QTabBar::tab:hover { color:#dcdce1; }
QDockWidget::title { background:#262633; padding:4px 8px; }
QMenuBar { background:#1c1c23; color:#dcdce1; }
QMenuBar::item:selected { background:#3c3c4b; }
QMenu { background:#262633; color:#dcdce1; border:1px solid #3c3c4b; }
QMenu::item:selected { background:#dc3c3c; }
QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background:#1c1c23; color:#dcdce1; border:1px solid #3c3c4b;
    border-radius:3px; padding:3px 6px; selection-background-color:#dc3c3c;
}
QComboBox::drop-down { border:none; }
QPushButton { background:#373748; color:#dcdce1; border:1px solid #464658;
              border-radius:4px; padding:4px 12px; }
QPushButton:hover  { background:#464658; }
QPushButton:pressed{ background:#dc3c3c; }
QGroupBox { border:1px solid #3c3c4b; border-radius:4px; margin-top:8px; padding-top:8px; }
QGroupBox::title { subcontrol-origin:margin; left:8px; color:#aaaaaf; }
QHeaderView::section { background:#262633; color:#aaaaaf; border:none; padding:4px; }
QTreeView, QListView, QTableView { background:#1c1c23; color:#dcdce1;
                                    alternate-background-color:#212130; }
QTreeView::item:selected, QListView::item:selected, QTableView::item:selected {
    background:#dc3c3c; color:#ffffff; }
QStatusBar { background:#1c1c23; color:#888899; }
)");
        m_themes["dark"] = t;
    }

    // ── Light ─────────────────────────────────────────────────────────────
    {
        auto* t = new Theme(this);
        t->setName("light");
        QVector<ThemeColor> cols = {
            {"background", QColor(245, 245, 248), "window"},
            {"surface",    QColor(255, 255, 255), "base"},
            {"text",       QColor(30,  30,  35),  "windowText"},
            {"accent",     QColor(200, 50,  50),  "highlight"},
            {"accentText", QColor(255, 255, 255), "highlightedText"},
        };
        t->setColors(cols);
        m_themes["light"] = t;
    }
}

QStringList ThemeManager::getAvailableThemes() const
{
    QStringList list = m_themes.keys();
    // Append user themes from disk
    QString userDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                      + "/themes";
    for (const auto& fi : QDir(userDir).entryInfoList({"*.json"}, QDir::Files))
        if (!list.contains(fi.baseName())) list << fi.baseName();
    return list;
}

Theme* ThemeManager::getTheme(const QString& name) const
{
    return m_themes.value(name, nullptr);
}

Theme* ThemeManager::getCurrentTheme() const
{
    return m_currentTheme;
}

bool ThemeManager::applyTheme(const QString& name)
{
    // Try built-in first, then load from disk
    Theme* t = m_themes.value(name, nullptr);
    if (!t) {
        QString path = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                       + "/themes/" + name + ".json";
        if (!QFile::exists(path)) {
            qWarning() << "ThemeManager: theme not found:" << name;
            return false;
        }
        t = new Theme(this);
        if (!t->loadFromFile(path)) { delete t; return false; }
        m_themes[name] = t;
    }

    m_currentTheme = t;
    m_currentThemeName = name;

    // Build Qt palette from theme colors
    qApp->setStyle(QStyleFactory::create("Fusion"));

    QPalette pal;
    auto setRole = [&](QPalette::ColorRole role, const QString& colorName) {
        QColor c = t->color(colorName);
        if (c.isValid()) pal.setColor(role, c);
    };
    setRole(QPalette::Window,          "background");
    setRole(QPalette::WindowText,      "text");
    setRole(QPalette::Base,            "surface");
    setRole(QPalette::AlternateBase,   "surface2");
    setRole(QPalette::Text,            "text");
    setRole(QPalette::Button,          "buttonBg");
    setRole(QPalette::ButtonText,      "buttonText");
    setRole(QPalette::Highlight,       "accent");
    setRole(QPalette::HighlightedText, "accentText");
    qApp->setPalette(pal);

    if (!t->getStylesheet().isEmpty())
        qApp->setStyleSheet(t->getStylesheet());

    emit themeChanged(name);
    return true;
}

bool ThemeManager::applyTheme(Theme* theme)
{
    if (!theme) return false;
    return applyTheme(theme->getName());
}

bool ThemeManager::loadThemeFromFile(const QString& path)
{
    auto* t = new Theme(this);
    if (!t->loadFromFile(path)) { delete t; return false; }
    m_themes[t->getName()] = t;
    return true;
}

bool ThemeManager::saveThemeToFile(const QString& name, const QString& path) const
{
    Theme* t = m_themes.value(name, nullptr);
    if (!t) return false;
    return t->saveToFile(path);
}

} // namespace ks
