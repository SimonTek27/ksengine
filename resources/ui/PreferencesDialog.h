#pragma once

#include <QDialog>
#include <QString>

class QListWidget;
class QStackedWidget;
class QWidget;
class QLineEdit;

namespace ks {

// Blender-style Preferences: left category rail + right property pages.
// Always shows "General"; adds a module-specific section only when a context runs.
class PreferencesDialog : public QDialog {
    Q_OBJECT
public:
    explicit PreferencesDialog(QWidget* parent = nullptr);

    void setContext(const QString& context);

    static void showSettings(QWidget* parent, const QString& context = QString());

private:
    void rebuild();
    void applyFilter(const QString& text);
    QWidget* createGeneralPage();
    QWidget* createSectionPage(const QString& category);

    QListWidget* m_nav = nullptr;
    QStackedWidget* m_stack = nullptr;
    QLineEdit* m_search = nullptr;
    QString m_context;
};

} // namespace ks
