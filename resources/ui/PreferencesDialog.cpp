#include "PreferencesDialog.h"
#include "modules/help/HelpBrowser.h"
#include "sys/SettingsManager.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPointer>
#include <QPushButton>
#include <QScrollArea>
#include <QSettings>
#include <QSpinBox>
#include <QStackedWidget>
#include <QVBoxLayout>
#include <QDir>

namespace ks {

// ----------------------------------------------------------------------------
// QString-facing shim over the Qt-free SettingsManager backend
// ----------------------------------------------------------------------------
struct SettingsRef {
    struct Api {
        QString string(const QString& key, const QString& defaultValue = QString()) const {
            return backend().string(key, defaultValue);
        }

        int integer(const QString& key, int defaultValue = 0) const {
            return backend().integer(key, defaultValue);
        }

        bool boolean(const QString& key, bool defaultValue = false) const {
            return backend().boolean(key, defaultValue);
        }

        double real(const QString& key, double defaultValue = 0.0) const {
            return backend().real(key, defaultValue);
        }

        void setValue(const QString& key, const QString& value) const {
            backend().setValue(key, value);
        }

        void setValue(const QString& key, bool value) const {
            backend().setValue(key, value);
        }

        void setValue(const QString& key, int value) const {
            backend().setValue(key, value);
        }

        void setValue(const QString& key, double value) const {
            backend().setValue(key, value);
        }

    private:
        static SettingsManager& backend() { return *globalSettings(); }
    };

    Api* operator->() const {
        static Api api;
        return &api;
    }
};

// ----------------------------------------------------------------------------
// Helpers — Blender-like property rows
// ----------------------------------------------------------------------------

static QWidget* pageShell(QScrollArea*& scrollOut)
{
    auto* outer = new QWidget;
    auto* outerLayout = new QVBoxLayout(outer);
    outerLayout->setContentsMargins(0, 0, 0, 0);
    outerLayout->setSpacing(0);

    auto* scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    scroll->setStyleSheet(QStringLiteral("QScrollArea { background: transparent; }"));

    auto* content = new QWidget;
    content->setStyleSheet(QStringLiteral("background: transparent;"));
    auto* contentLayout = new QVBoxLayout(content);
    contentLayout->setContentsMargins(24, 20, 24, 24);
    contentLayout->setSpacing(16);

    scroll->setWidget(content);
    outerLayout->addWidget(scroll);
    scrollOut = scroll;
    return outer;
}

static QLabel* sectionHeader(const QString& title)
{
    auto* label = new QLabel(title.toUpper());
    label->setStyleSheet(QStringLiteral(
        "QLabel { color: #a1a1aa; font-size: 11px; font-weight: 600;"
        " letter-spacing: 0.6px; padding-bottom: 2px; }"));
    return label;
}

static QWidget* propertyRow(const QString& name, const QString& tooltip,
                            QWidget* control, QFormLayout*& form)
{
    auto* label = new QLabel(name);
    label->setToolTip(tooltip);
    label->setStyleSheet(QStringLiteral(
        "QLabel { color: #d4d4d8; font-size: 12px; background: transparent; }"));
    control->setToolTip(tooltip);
    form->addRow(label, control);
    return control;
}

// ----------------------------------------------------------------------------
// PreferencesDialog
// ----------------------------------------------------------------------------

PreferencesDialog::PreferencesDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle(tr("Preferences"));
    setModal(true);
    resize(860, 580);
    setMinimumSize(720, 440);

    setStyleSheet(QStringLiteral(
        "QDialog { background-color: #18181b; }"
        "#navList { background-color: #1a1a1e; border: none;"
        "  border-right: 1px solid #27272a; }"
        "#navList::item { color: #a1a1aa; padding: 10px 14px;"
        "  border: none; border-left: 3px solid transparent; }"
        "#navList::item:selected { background-color: #27272a;"
        "  color: #fafafa; border-left-color: #3b82f6; }"
        "#navList::item:hover:!selected { background-color: #212126; color: #e4e4e7; }"
        "#stackHost { background-color: #18181b; }"
        "QPushButton#primaryBtn { background-color: #3b82f6; color: white;"
        "  border: none; border-radius: 4px; padding: 7px 16px; font-weight: 600; }"
        "QPushButton#primaryBtn:hover { background-color: #2563eb; }"
        "QPushButton#secondaryBtn { background-color: #27272a; color: #d4d4d8;"
        "  border: 1px solid #3f3f46; border-radius: 4px; padding: 7px 14px; }"
        "QPushButton#secondaryBtn:hover { background-color: #3f3f46; color: #fafafa; }"
        "QComboBox, QLineEdit, QSpinBox { background-color: #27272a;"
        "  color: #fafafa; border: 1px solid #3f3f46; border-radius: 4px;"
        "  padding: 5px 8px; min-height: 20px; }"
        "QComboBox:focus, QLineEdit:focus, QSpinBox:focus { border-color: #3b82f6; }"
        "QComboBox::drop-down { border: none; width: 22px; }"
        "QComboBox QAbstractItemView { background-color: #27272a; color: #fafafa;"
        "  selection-background-color: #3b82f6; border: 1px solid #3f3f46; }"
        "QCheckBox { color: #d4d4d8; background: transparent; spacing: 8px; }"
        "QCheckBox::indicator { width: 15px; height: 15px; border-radius: 3px;"
        "  border: 1px solid #52525b; background: #27272a; }"
        "QCheckBox::indicator:checked { background: #3b82f6; border-color: #3b82f6; }"
        "QCheckBox::indicator:hover { border-color: #93c5fd; }"
        "QFormLayout { horizontalSpacing: 18px; verticalSpacing: 10px; }"));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // Search field
    auto* searchRow = new QWidget;
    searchRow->setStyleSheet(QStringLiteral("QWidget { background-color: #18181b; }"));
    auto* searchLayout = new QHBoxLayout(searchRow);
    searchLayout->setContentsMargins(16, 10, 16, 4);
    m_search = new QLineEdit;
    m_search->setObjectName(QStringLiteral("settingsSearch"));
    m_search->setPlaceholderText(tr("Search settings…"));
    m_search->setClearButtonEnabled(true);
    searchLayout->addWidget(m_search);
    root->addWidget(searchRow);

    auto* body = new QHBoxLayout;
    body->setContentsMargins(0, 0, 0, 0);
    body->setSpacing(0);

    m_nav = new QListWidget;
    m_nav->setObjectName(QStringLiteral("navList"));
    m_nav->setFixedWidth(200);
    m_nav->setSelectionMode(QAbstractItemView::SingleSelection);

    m_stack = new QStackedWidget;
    m_stack->setObjectName(QStringLiteral("stackHost"));

    body->addWidget(m_nav);
    body->addWidget(m_stack, 1);
    root->addLayout(body, 1);

    auto* footer = new QWidget;
    footer->setStyleSheet(QStringLiteral(
        "QWidget { background-color: #1a1a1e; border-top: 1px solid #27272a; }"));
    auto* footerLayout = new QHBoxLayout(footer);
    footerLayout->setContentsMargins(16, 10, 16, 10);

    auto* statusHint = new QLabel(tr("Changes apply immediately · Save writes to disk"));
    statusHint->setStyleSheet(QStringLiteral(
        "QLabel { color: #71717a; font-size: 11px; background: transparent; }"));
    footerLayout->addWidget(statusHint);
    footerLayout->addStretch();

    auto* restoreBtn = new QPushButton(tr("Restore Defaults"));
    restoreBtn->setObjectName(QStringLiteral("secondaryBtn"));
    auto* saveBtn = new QPushButton(tr("Save Preferences"));
    saveBtn->setObjectName(QStringLiteral("primaryBtn"));
    footerLayout->addWidget(restoreBtn);
    footerLayout->addWidget(saveBtn);
    root->addWidget(footer);

    connect(m_nav, &QListWidget::currentRowChanged, m_stack, &QStackedWidget::setCurrentIndex);
    connect(m_search, &QLineEdit::textChanged, this, &PreferencesDialog::applyFilter);
    connect(saveBtn, &QPushButton::clicked, this, [this]() {
        globalSettings()->sync();
    });
    connect(restoreBtn, &QPushButton::clicked, this, [this]() {
        globalSettings()->resetToDefaults();
        rebuild();
    });
}

void PreferencesDialog::setContext(const QString& context)
{
    if (m_context == context) return;
    m_context = context;
    rebuild();
}

void PreferencesDialog::showSettings(QWidget* parent, const QString& context)
{
    static QPointer<PreferencesDialog> dialog;
    if (!dialog) {
        dialog = new PreferencesDialog(parent);
        dialog->setAttribute(Qt::WA_DeleteOnClose, false);
    }
    dialog->setContext(context);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

void PreferencesDialog::rebuild()
{
    const int previous = m_nav->currentRow();

    m_nav->clear();
    while (m_stack->count() > 0) {
        QWidget* w = m_stack->widget(0);
        m_stack->removeWidget(w);
        w->deleteLater();
    }

    // Always: General (options that fit all apps)
    m_nav->addItem(new QListWidgetItem(tr("General")));
    m_stack->addWidget(createGeneralPage());

    // Only when a section is running: module-specific page
    const QString section = HelpBrowser::categoryForContext(m_context);
    if (!section.isEmpty()) {
        m_nav->addItem(new QListWidgetItem(section));
        m_stack->addWidget(createSectionPage(section));
    }

    const int row = (previous >= 0 && previous < m_nav->count()) ? previous : 0;
    m_nav->setCurrentRow(row);

    applyFilter(m_search->text());
}

void PreferencesDialog::applyFilter(const QString& text)
{
    const QString needle = text.trimmed();

    for (int p = 0; p < m_stack->count(); ++p) {
        QWidget* page = m_stack->widget(p);
        if (!page) continue;

        auto* scroll = page->findChild<QScrollArea*>();
        QWidget* content = scroll ? scroll->widget() : nullptr;
        if (!content || !content->layout()) continue;

        QLayout* layout = content->layout();
        QList<QWidget*> pendingLabels; // section headers / intro labels awaiting a verdict

        for (int i = 0; i < layout->count(); ++i) {
            QLayoutItem* item = layout->itemAt(i);
            if (!item) continue;

            if (auto* form = qobject_cast<QFormLayout*>(item->layout())) {
                bool anyVisible = false;
                for (int r = 0; r < form->rowCount(); ++r) {
                    bool match = needle.isEmpty();
                    if (!match) {
                        if (auto* li = form->itemAt(r, QFormLayout::LabelRole)) {
                            if (auto* lab = qobject_cast<QLabel*>(li->widget())) {
                                match = lab->text().contains(needle, Qt::CaseInsensitive)
                                     || lab->toolTip().contains(needle, Qt::CaseInsensitive);
                            }
                        }
                        if (!match) {
                            if (auto* fi = form->itemAt(r, QFormLayout::FieldRole)) {
                                if (QWidget* field = fi->widget()) {
                                    match = field->toolTip().contains(needle,
                                                                        Qt::CaseInsensitive);
                                }
                            }
                        }
                    }
                    form->setRowVisible(r, match);
                    anyVisible = anyVisible || match;
                }

                const bool sectionVisible = needle.isEmpty() || anyVisible;
                for (QWidget* label : pendingLabels) {
                    label->setVisible(sectionVisible);
                }
                pendingLabels.clear();
                continue;
            }

            if (auto* lbl = qobject_cast<QLabel*>(item->widget())) {
                pendingLabels.append(lbl);
            }
        }

        for (QWidget* label : pendingLabels) {
            label->setVisible(needle.isEmpty());
        }
    }
}

QWidget* PreferencesDialog::createGeneralPage()
{
    QScrollArea* scroll = nullptr;
    auto* page = pageShell(scroll);
    auto* layout = qobject_cast<QVBoxLayout*>(scroll->widget()->layout());
    SettingsRef s;

    // --- Interface ---
    layout->addWidget(sectionHeader(tr("Interface")));

    auto* interfaceForm = new QFormLayout;
    interfaceForm->setContentsMargins(0, 0, 0, 0);

    auto* theme = new QComboBox;
    theme->addItems({tr("Dark"), tr("Light")});
    theme->setCurrentText(s->string(QStringLiteral("theme"), QStringLiteral("dark"))
                              .compare(QLatin1String("light"), Qt::CaseInsensitive) == 0
                          ? tr("Light") : tr("Dark"));
    QObject::connect(theme, &QComboBox::currentTextChanged, this, [s](const QString& text) {
        s->setValue(QStringLiteral("theme"),
                    text == QLatin1String("Light") ? QStringLiteral("light")
                                                   : QStringLiteral("dark"));
    });
    propertyRow(tr("Theme"), tr("Application color theme"), theme, interfaceForm);

    auto* language = new QComboBox;
    language->addItems({QStringLiteral("en"), QStringLiteral("it"), QStringLiteral("de"),
                        QStringLiteral("fr"), QStringLiteral("es")});
    language->setCurrentText(s->string(QStringLiteral("language"), QStringLiteral("en")));
    QObject::connect(language, &QComboBox::currentTextChanged, this,
                     [s](const QString& code) { s->setValue(QStringLiteral("language"), code); });
    propertyRow(tr("Language"), tr("UI language code"), language, interfaceForm);

    layout->addLayout(interfaceForm);
    layout->addSpacing(8);

    // --- System ---
    layout->addWidget(sectionHeader(tr("System")));

    auto* systemForm = new QFormLayout;
    systemForm->setContentsMargins(0, 0, 0, 0);

    auto* projectPathRow = new QWidget;
    auto* projectPathLayout = new QHBoxLayout(projectPathRow);
    projectPathLayout->setContentsMargins(0, 0, 0, 0);
    projectPathLayout->setSpacing(6);
    auto* projectPath = new QLineEdit(
        s->string(QStringLiteral("project/defaultProjectPath")));
    auto* browseProject = new QPushButton(tr("…"));
    browseProject->setFixedWidth(32);
    browseProject->setObjectName(QStringLiteral("secondaryBtn"));
    QObject::connect(browseProject, &QPushButton::clicked, this, [s, projectPath]() {
        const QString dir = QFileDialog::getExistingDirectory(
            nullptr, tr("Select Default Project Folder"), projectPath->text());
        if (!dir.isEmpty()) {
            projectPath->setText(dir);
            s->setValue(QStringLiteral("project/defaultProjectPath"), dir);
        }
    });
    QObject::connect(projectPath, &QLineEdit::editingFinished, this, [s, projectPath]() {
        s->setValue(QStringLiteral("project/defaultProjectPath"), projectPath->text());
    });
    projectPathLayout->addWidget(projectPath, 1);
    projectPathLayout->addWidget(browseProject);
    propertyRow(tr("Default project folder"),
                tr("Where new projects are created by default"),
                projectPathRow, systemForm);

    auto* recentLimit = new QSpinBox;
    recentLimit->setRange(1, 50);
    recentLimit->setValue(s->integer(QStringLiteral("project/recentProjectsLimit"), 10));
    QObject::connect(recentLimit, &QSpinBox::valueChanged, this, [s](int v) {
        s->setValue(QStringLiteral("project/recentProjectsLimit"), v);
    });
    propertyRow(tr("Recent projects"),
                tr("Maximum number of recent projects in the File menu"),
                recentLimit, systemForm);

    QSettings appSettings(QStringLiteral("ksEditor"), QStringLiteral("ksEditorQt"));
    auto* acPathRow = new QWidget;
    auto* acPathLayout = new QHBoxLayout(acPathRow);
    acPathLayout->setContentsMargins(0, 0, 0, 0);
    acPathLayout->setSpacing(6);
    auto* acPath = new QLineEdit(appSettings.value(QStringLiteral("acPath")).toString());
    auto* browseAc = new QPushButton(tr("…"));
    browseAc->setFixedWidth(32);
    browseAc->setObjectName(QStringLiteral("secondaryBtn"));
    QObject::connect(browseAc, &QPushButton::clicked, this, [acPath]() {
        const QString dir = QFileDialog::getExistingDirectory(
            nullptr, tr("Select Assetto Corsa Installation"), acPath->text());
        if (!dir.isEmpty()) {
            acPath->setText(dir);
            QSettings settings(QStringLiteral("ksEditor"), QStringLiteral("ksEditorQt"));
            settings.setValue(QStringLiteral("acPath"), dir);
        }
    });
    QObject::connect(acPath, &QLineEdit::editingFinished, this, [acPath]() {
        QSettings settings(QStringLiteral("ksEditor"), QStringLiteral("ksEditorQt"));
        settings.setValue(QStringLiteral("acPath"), acPath->text());
    });
    acPathLayout->addWidget(acPath, 1);
    acPathLayout->addWidget(browseAc);
    propertyRow(tr("Assetto Corsa path"),
                tr("Root folder of the Assetto Corsa installation"),
                acPathRow, systemForm);

    layout->addLayout(systemForm);
    layout->addSpacing(8);

    // --- Editing ---
    layout->addWidget(sectionHeader(tr("Editing")));

    auto* editForm = new QFormLayout;
    editForm->setContentsMargins(0, 0, 0, 0);

    auto* autoSave = new QCheckBox;
    autoSave->setChecked(s->boolean(QStringLiteral("editor/autoSave"), true));
    QObject::connect(autoSave, &QCheckBox::toggled, this, [s](bool on) {
        s->setValue(QStringLiteral("editor/autoSave"), on);
    });
    propertyRow(tr("Auto-save"), tr("Save open projects automatically"),
                autoSave, editForm);

    auto* autoSaveInterval = new QSpinBox;
    autoSaveInterval->setRange(30, 3600);
    autoSaveInterval->setSuffix(tr(" s"));
    autoSaveInterval->setValue(
        s->integer(QStringLiteral("editor/autoSaveInterval"), 300));
    QObject::connect(autoSaveInterval, &QSpinBox::valueChanged, this, [s](int v) {
        s->setValue(QStringLiteral("editor/autoSaveInterval"), v);
    });
    propertyRow(tr("Auto-save interval"),
                tr("Seconds between automatic saves"),
                autoSaveInterval, editForm);

    auto* lineNumbers = new QCheckBox;
    lineNumbers->setChecked(s->boolean(QStringLiteral("editor/showLineNumbers"), true));
    QObject::connect(lineNumbers, &QCheckBox::toggled, this, [s](bool on) {
        s->setValue(QStringLiteral("editor/showLineNumbers"), on);
    });
    propertyRow(tr("Show line numbers"),
                tr("Display line numbers in text and code editors"),
                lineNumbers, editForm);

    auto* tabSize = new QSpinBox;
    tabSize->setRange(1, 16);
    tabSize->setValue(s->integer(QStringLiteral("editor/tabSize"), 4));
    QObject::connect(tabSize, &QSpinBox::valueChanged, this, [s](int v) {
        s->setValue(QStringLiteral("editor/tabSize"), v);
    });
    propertyRow(tr("Tab width"),
                tr("Number of spaces per tab stop"),
                tabSize, editForm);

    auto* fontSize = new QSpinBox;
    fontSize->setRange(8, 32);
    fontSize->setSuffix(tr(" pt"));
    fontSize->setValue(s->integer(QStringLiteral("editor/fontSize"), 12));
    QObject::connect(fontSize, &QSpinBox::valueChanged, this, [s](int v) {
        s->setValue(QStringLiteral("editor/fontSize"), v);
    });
    propertyRow(tr("Editor font size"),
                tr("Font size used by text and code editors"),
                fontSize, editForm);

    layout->addLayout(editForm);

    // --- Export ---
    layout->addWidget(sectionHeader(tr("Export")));

    auto* exportForm = new QFormLayout;
    exportForm->setContentsMargins(0, 0, 0, 0);

    auto* compression = new QSpinBox;
    compression->setRange(0, 9);
    compression->setValue(s->integer(QStringLiteral("export/compressionLevel"), 5));
    QObject::connect(compression, &QSpinBox::valueChanged, this, [s](int v) {
        s->setValue(QStringLiteral("export/compressionLevel"), v);
    });
    propertyRow(tr("Compression level"),
                tr("0 = none, 9 = maximum (archives / packed assets)"),
                compression, exportForm);

    auto* exportNormals = new QCheckBox;
    exportNormals->setChecked(s->boolean(QStringLiteral("export/exportNormals"), true));
    QObject::connect(exportNormals, &QCheckBox::toggled, this, [s](bool on) {
        s->setValue(QStringLiteral("export/exportNormals"), on);
    });
    propertyRow(tr("Export normals"),
                tr("Include normals when exporting meshes"),
                exportNormals, exportForm);

    auto* optimizeMeshes = new QCheckBox;
    optimizeMeshes->setChecked(s->boolean(QStringLiteral("export/optimizeMeshes"), true));
    QObject::connect(optimizeMeshes, &QCheckBox::toggled, this, [s](bool on) {
        s->setValue(QStringLiteral("export/optimizeMeshes"), on);
    });
    propertyRow(tr("Optimize meshes"),
                tr("Run mesh optimization on export"),
                optimizeMeshes, exportForm);

    layout->addLayout(exportForm);
    layout->addStretch();
    return page;
}

QWidget* PreferencesDialog::createSectionPage(const QString& category)
{
    QScrollArea* scroll = nullptr;
    auto* page = pageShell(scroll);
    auto* layout = qobject_cast<QVBoxLayout*>(scroll->widget()->layout());
    SettingsRef s;

    layout->addWidget(sectionHeader(category));

    auto* intro = new QLabel(
        tr("Options that apply only while the %1 section is active.").arg(category));
    intro->setWordWrap(true);
    intro->setStyleSheet(QStringLiteral(
        "QLabel { color: #71717a; font-size: 11px; background: transparent; }"));
    layout->addWidget(intro);

    auto* form = new QFormLayout;
    form->setContentsMargins(0, 4, 0, 0);

    if (category == QLatin1String("Modeling")) {
        auto* grid = new QCheckBox;
        grid->setChecked(s->boolean(QStringLiteral("3dview/showGrid"), true));
        QObject::connect(grid, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("3dview/showGrid"), on);
        });
        propertyRow(tr("Show grid"), tr("Display the floor grid in the 3D viewport"),
                    grid, form);

        auto* gridSize = new QSpinBox;
        gridSize->setRange(1, 100);
        gridSize->setValue(qRound(s->real(QStringLiteral("3dview/gridSize"), 1.0)));
        QObject::connect(gridSize, &QSpinBox::valueChanged, this, [s](int v) {
            s->setValue(QStringLiteral("3dview/gridSize"), double(v));
        });
        propertyRow(tr("Grid size"), tr("Grid cell size in the 3D viewport"),
                    gridSize, form);

        auto* aa = new QCheckBox;
        aa->setChecked(s->boolean(QStringLiteral("3dview/antiAliasing"), true));
        QObject::connect(aa, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("3dview/antiAliasing"), on);
        });
        propertyRow(tr("Anti-aliasing"), tr("Smooth mesh edges in the viewport"),
                    aa, form);

        auto* shadows = new QComboBox;
        shadows->addItems({tr("Off"), tr("Low"), tr("Medium"), tr("High")});
        shadows->setCurrentIndex(
            qBound(0, s->integer(QStringLiteral("3dview/shadowQuality"), 2), 3));
        QObject::connect(shadows, &QComboBox::currentIndexChanged, this, [s](int i) {
            s->setValue(QStringLiteral("3dview/shadowQuality"), i);
        });
        propertyRow(tr("Shadow quality"), tr("Viewport shadow quality preset"),
                    shadows, form);
    } else if (category == QLatin1String("Physics")) {
        auto* telemetryRate = new QSpinBox;
        telemetryRate->setRange(10, 240);
        telemetryRate->setSuffix(tr(" Hz"));
        telemetryRate->setValue(s->integer(QStringLiteral("physics/telemetryRate"), 60));
        QObject::connect(telemetryRate, &QSpinBox::valueChanged, this, [s](int v) {
            s->setValue(QStringLiteral("physics/telemetryRate"), v);
        });
        propertyRow(tr("Telemetry rate"),
                    tr("Samples per second when recording telemetry"),
                    telemetryRate, form);

        auto* setupAutosave = new QCheckBox;
        setupAutosave->setChecked(s->boolean(QStringLiteral("physics/setupAutoSave"), true));
        QObject::connect(setupAutosave, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("physics/setupAutoSave"), on);
        });
        propertyRow(tr("Auto-save setups"),
                    tr("Save physics setups when switching tabs"),
                    setupAutosave, form);

        auto* showForces = new QCheckBox;
        showForces->setChecked(s->boolean(QStringLiteral("physics/showForceVectors"), false));
        QObject::connect(showForces, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("physics/showForceVectors"), on);
        });
        propertyRow(tr("Show force vectors"),
                    tr("Draw suspension and aero force vectors in the viewer"),
                    showForces, form);
    } else if (category == QLatin1String("Audio")) {
        auto* sampleRate = new QComboBox;
        sampleRate->addItems({QStringLiteral("44100"), QStringLiteral("48000"),
                              QStringLiteral("96000")});
        const QString rate =
            s->string(QStringLiteral("audio/sampleRate"), QStringLiteral("48000"));
        sampleRate->setCurrentText(rate);
        QObject::connect(sampleRate, &QComboBox::currentTextChanged, this,
                         [s](const QString& v) { s->setValue(QStringLiteral("audio/sampleRate"), v); });
        propertyRow(tr("Sample rate"),
                    tr("Project sample rate in Hz"),
                    sampleRate, form);

        auto* normalize = new QCheckBox;
        normalize->setChecked(s->boolean(QStringLiteral("audio/normalizeOnImport"), true));
        QObject::connect(normalize, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("audio/normalizeOnImport"), on);
        });
        propertyRow(tr("Normalize on import"),
                    tr("Peak-normalize assets when importing banks"),
                    normalize, form);

        auto* bankFormat = new QComboBox;
        bankFormat->addItems({QStringLiteral("wav"), QStringLiteral("ogg"),
                              QStringLiteral("fsb")});
        bankFormat->setCurrentText(
            s->string(QStringLiteral("audio/bankFormat"), QStringLiteral("wav")));
        QObject::connect(bankFormat, &QComboBox::currentTextChanged, this,
                         [s](const QString& v) { s->setValue(QStringLiteral("audio/bankFormat"), v); });
        propertyRow(tr("Bank format"),
                    tr("Default container written by Build Banks"),
                    bankFormat, form);
    } else if (category == QLatin1String("Skinning")) {
        auto* dpi = new QSpinBox;
        dpi->setRange(72, 600);
        dpi->setValue(s->integer(QStringLiteral("paint/exportDpi"), 300));
        QObject::connect(dpi, &QSpinBox::valueChanged, this, [s](int v) {
            s->setValue(QStringLiteral("paint/exportDpi"), v);
        });
        propertyRow(tr("Export DPI"), tr("Resolution hint for livery exports"),
                    dpi, form);

        auto* premult = new QCheckBox;
        premult->setChecked(s->boolean(QStringLiteral("paint/premultiplyAlpha"), true));
        QObject::connect(premult, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("paint/premultiplyAlpha"), on);
        });
        propertyRow(tr("Premultiply alpha"),
                    tr("Bake alpha into RGB on export"),
                    premult, form);
    } else if (category == QLatin1String("Preview")) {
        auto* fov = new QSpinBox;
        fov->setRange(30, 120);
        fov->setSuffix(QStringLiteral("°"));
        fov->setValue(s->integer(QStringLiteral("Viewport/FOV"), 75));
        QObject::connect(fov, &QSpinBox::valueChanged, this, [s](int v) {
            s->setValue(QStringLiteral("Viewport/FOV"), v);
        });
        propertyRow(tr("Showroom FOV"),
                    tr("Default camera field of view in the showroom"),
                    fov, form);

        auto* msaa = new QComboBox;
        msaa->addItems({QStringLiteral("0"), QStringLiteral("2"),
                        QStringLiteral("4"), QStringLiteral("8")});
        msaa->setCurrentText(QString::number(s->integer(QStringLiteral("Viewport/MSAA"), 4)));
        QObject::connect(msaa, &QComboBox::currentTextChanged, this,
                         [s](const QString& v) { s->setValue(QStringLiteral("Viewport/MSAA"), v.toInt()); });
        propertyRow(tr("MSAA"), tr("Multisample anti-aliasing samples"),
                    msaa, form);
    } else if (category == QLatin1String("Content")) {
        auto* validate = new QCheckBox;
        validate->setChecked(s->boolean(QStringLiteral("modmanager/validateOnInstall"), true));
        QObject::connect(validate, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("modmanager/validateOnInstall"), on);
        });
        propertyRow(tr("Validate on install"),
                    tr("Check archive structure when installing mods"),
                    validate, form);

        auto* backup = new QCheckBox;
        backup->setChecked(s->boolean(QStringLiteral("modmanager/backupBeforeReplace"), true));
        QObject::connect(backup, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("modmanager/backupBeforeReplace"), on);
        });
        propertyRow(tr("Backup before replace"),
                    tr("Keep a copy of replaced content files"),
                    backup, form);
    } else if (category == QLatin1String("Reference")) {
        auto* luacheck = new QCheckBox;
        luacheck->setChecked(s->boolean(QStringLiteral("script/lintOnSave"), true));
        QObject::connect(luacheck, &QCheckBox::toggled, this, [s](bool on) {
            s->setValue(QStringLiteral("script/lintOnSave"), on);
        });
        propertyRow(tr("Lint on save"),
                    tr("Run script checks when a file is saved"),
                    luacheck, form);
    } else {
        auto* note = new QLabel(tr("No options registered for this section yet."));
        note->setStyleSheet(QStringLiteral(
            "QLabel { color: #71717a; font-size: 12px; background: transparent; }"));
        form->addRow(note);
    }

    layout->addLayout(form);
    layout->addStretch();
    return page;
}

} // namespace ks
