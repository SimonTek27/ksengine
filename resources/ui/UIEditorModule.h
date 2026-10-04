#pragma once

#include "modules/editor/ModuleGuiBase.h"
#include <QTabWidget>
#include <QTreeWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QLabel>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QLineEdit>
#include <QColorDialog>

namespace ks {
namespace ui {

class UIEditorModule : public ModuleGuiBase {
    Q_OBJECT
public:
    explicit UIEditorModule(QWidget* parent = nullptr);
    ~UIEditorModule() override = default;

    bool initialize() override;
    void shutdown() override;

    QString moduleName() const override { return "UI Editor"; }
    QString moduleId() const override { return "uiEditor"; }
    int getModulePriority() const override { return 55; }

protected:
    void buildUI() override;
    void onActivation() override;
    void onDeactivation() override;

private slots:
    void onThemeSelected(int index);
    void onAccentColorChanged();
    void onFontSizeChanged(int value);
    void onLayoutSelected(int index);
    void onResetLayout();
    void onSaveLayout();
    void onLoadLayout();
    void onNodeGraphSelected();
    void onConfigureShortcuts();
    void onToggleAnimations(bool checked);

private:
    void setupThemeTab();
    void setupLayoutTab();
    void setupShortcutsTab();
    void setupWidgetsTab();
    void populateThemeList();

    QTabWidget* m_tabWidget = nullptr;

    QWidget* m_themeTab = nullptr;
    QComboBox* m_themeCombo = nullptr;
    QPushButton* m_accentColorBtn = nullptr;
    QLabel* m_accentColorPreview = nullptr;
    QSpinBox* m_fontSizeSpin = nullptr;
    QCheckBox* m_animationsCheck = nullptr;
    QCheckBox* m_animationsReduceCheck = nullptr;
    QLabel* m_themePreviewLabel = nullptr;

    QWidget* m_layoutTab = nullptr;
    QComboBox* m_layoutCombo = nullptr;
    QPushButton* m_resetLayoutBtn = nullptr;
    QPushButton* m_saveLayoutBtn = nullptr;
    QPushButton* m_loadLayoutBtn = nullptr;
    QTreeWidget* m_layoutTree = nullptr;

    QWidget* m_shortcutsTab = nullptr;
    QTreeWidget* m_shortcutTree = nullptr;
    QPushButton* m_configureShortcutsBtn = nullptr;
    QLineEdit* m_shortcutSearchInput = nullptr;

    QWidget* m_widgetsTab = nullptr;
    QTreeWidget* m_widgetTree = nullptr;
    QPushButton* m_openNodeGraphBtn = nullptr;
};

} // namespace ui
} // namespace ks
