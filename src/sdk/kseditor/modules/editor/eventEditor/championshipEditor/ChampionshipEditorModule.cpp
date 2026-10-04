#include "ChampionshipEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QFileDialog>
#include <QDir>
#include <QTextStream>
#include <QFileInfo>
#include <QJsonArray>
#include <QStackedWidget>
#include <QScrollArea>
#include <QHeaderView>

namespace ks {

ChampionshipEditorModule::ChampionshipEditorModule(QWidget* parent) : EditorModule(parent) {}

bool ChampionshipEditorModule::initialize() { LOG_INFO("ChampionshipEditorModule", "Initialized"); return true; }
void ChampionshipEditorModule::shutdown()
{
    m_championships.clear();
    m_championshipList->clear();
    m_eventList->clear();
    m_selectedChamp = -1;
    m_selectedEvent = -1;
    m_dir.clear();
}

// ── helpers ──────────────────────────────────────────────────────────────────
static QString readIniValue(const QString& content, const QString& section, const QString& key) {
    bool inSection = false;
    for (const QString& raw : content.split('\n')) {
        QString line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            inSection = line.mid(1, line.length() - 2).trimmed().compare(section, Qt::CaseInsensitive) == 0;
            continue;
        }
        if (inSection && line.startsWith(key, Qt::CaseInsensitive)) {
            int eq = line.indexOf('=');
            if (eq >= 0) {
                QString val = line.mid(eq + 1).trimmed();
                int semi = val.indexOf(';');
                if (semi >= 0) val = val.left(semi).trimmed();
                return val;
            }
        }
    }
    return {};
}

static void writeIniSection(QTextStream& out, const QString& section) {
    out << '[' << section << "]\n";
}

static void writeIniLine(QTextStream& out, const QString& key, const QString& value) {
    if (!value.isEmpty()) out << key << '=' << value << '\n';
}

static QMap<QString, QString> readIniSection(const QString& content, const QString& section) {
    QMap<QString, QString> map;
    bool inSection = false;
    for (const QString& raw : content.split('\n')) {
        QString line = raw.trimmed();
        if (line.startsWith('[') && line.endsWith(']')) {
            inSection = line.mid(1, line.length() - 2).trimmed().compare(section, Qt::CaseInsensitive) == 0;
            continue;
        }
        if (inSection && line.contains('=')) {
            int eq = line.indexOf('=');
            QString key = line.left(eq).trimmed();
            QString val = line.mid(eq + 1).trimmed();
            int semi = val.indexOf(';');
            if (semi >= 0) val = val.left(semi).trimmed();
            map[key.toUpper()] = val;
        }
    }
    return map;
}

// ── UI ───────────────────────────────────────────────────────────────────────
QWidget* ChampionshipEditorModule::createChampionshipProps() {
    auto* w = new QWidget();
    auto* l = new QGridLayout(w);
    l->setContentsMargins(4, 4, 4, 4);

    m_champNameEdit = new QLineEdit();
    m_champCodeEdit = new QLineEdit();
    m_champDescEdit = new QTextEdit(); m_champDescEdit->setMaximumHeight(80);
    m_champModelEdit = new QLineEdit();
    m_champRequiresEdit = new QLineEdit();
    m_goalPointsSpin = new QSpinBox(); m_goalPointsSpin->setRange(0, 9999);
    m_goalRankingSpin = new QSpinBox(); m_goalRankingSpin->setRange(0, 999);
    m_goalTier1Spin = new QSpinBox(); m_goalTier1Spin->setRange(0, 999);
    m_goalTier2Spin = new QSpinBox(); m_goalTier2Spin->setRange(0, 999);
    m_goalTier3Spin = new QSpinBox(); m_goalTier3Spin->setRange(0, 999);

    int r = 0;
    l->addWidget(new QLabel(tr("Name:")), r, 0); l->addWidget(m_champNameEdit, r++, 1);
    l->addWidget(new QLabel(tr("Code:")), r, 0); l->addWidget(m_champCodeEdit, r++, 1);
    l->addWidget(new QLabel(tr("Description:")), r, 0); l->addWidget(m_champDescEdit, r++, 1);
    l->addWidget(new QLabel(tr("Model:")), r, 0); l->addWidget(m_champModelEdit, r++, 1);
    l->addWidget(new QLabel(tr("Requires:")), r, 0); l->addWidget(m_champRequiresEdit, r++, 1);

    auto* gb = new QGroupBox(tr("Goals"));
    auto* gl = new QGridLayout(gb);
    gl->addWidget(new QLabel(tr("Points:")), 0, 0); gl->addWidget(m_goalPointsSpin, 0, 1);
    gl->addWidget(new QLabel(tr("Ranking:")), 1, 0); gl->addWidget(m_goalRankingSpin, 1, 1);
    gl->addWidget(new QLabel(tr("Tier 1 (Bronze):")), 2, 0); gl->addWidget(m_goalTier1Spin, 2, 1);
    gl->addWidget(new QLabel(tr("Tier 2 (Silver):")), 3, 0); gl->addWidget(m_goalTier2Spin, 3, 1);
    gl->addWidget(new QLabel(tr("Tier 3 (Gold):")), 4, 0); gl->addWidget(m_goalTier3Spin, 4, 1);
    l->addWidget(gb, r++, 0, 1, 2);

    auto* ptsGb = new QGroupBox(tr("Points for Place"));
    auto* ptsL = new QVBoxLayout(ptsGb);
    m_pointsList = new QListWidget();
    m_pointsValueSpin = new QSpinBox(); m_pointsValueSpin->setRange(0, 9999);
    auto* ptsBtnL = new QHBoxLayout();
    ptsBtnL->addWidget(new QLabel(tr("Value:")));
    ptsBtnL->addWidget(m_pointsValueSpin);
    m_addPointsBtn = new QPushButton(tr("Add"));
    m_removePointsBtn = new QPushButton(tr("Remove"));
    ptsBtnL->addWidget(m_addPointsBtn);
    ptsBtnL->addWidget(m_removePointsBtn);
    ptsL->addWidget(m_pointsList);
    ptsL->addLayout(ptsBtnL);
    l->addWidget(ptsGb, r++, 0, 1, 2);

    auto* champConnections = new QWidget(this);
    champConnections->setVisible(false);
    connect(m_champNameEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_champCodeEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_champDescEdit, &QTextEdit::textChanged, this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_champModelEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_champRequiresEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_goalPointsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_goalRankingSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_goalTier1Spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_goalTier2Spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_goalTier3Spin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onChampionshipPropChanged);
    connect(m_addPointsBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onAddPoints);
    connect(m_removePointsBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onRemovePoints);

    l->setRowStretch(l->rowCount(), 1);
    return w;
}

QWidget* ChampionshipEditorModule::createEventProps() {
    auto* w = new QWidget();
    auto* l = new QGridLayout(w);
    l->setContentsMargins(4, 4, 4, 4);

    m_evNameEdit = new QLineEdit();
    m_evDescEdit = new QTextEdit(); m_evDescEdit->setMaximumHeight(60);
    m_evTrackEdit = new QLineEdit();
    m_evConfigTrackEdit = new QLineEdit();
    m_evModelEdit = new QLineEdit();
    m_evModelConfigEdit = new QLineEdit();
    m_evSkinEdit = new QLineEdit();
    m_evCarsSpin = new QSpinBox(); m_evCarsSpin->setRange(0, 99);
    m_evAiLevelSpin = new QSpinBox(); m_evAiLevelSpin->setRange(0, 100);
    m_evRaceLapsSpin = new QSpinBox(); m_evRaceLapsSpin->setRange(0, 999);
    m_evPenaltiesSpin = new QSpinBox(); m_evPenaltiesSpin->setRange(0, 3);
    m_evDriftModeCheck = new QCheckBox(tr("Drift Mode"));
    m_evFixedSetupCheck = new QCheckBox(tr("Fixed Setup"));
    m_evSunAngleSpin = new QSpinBox(); m_evSunAngleSpin->setRange(0, 360);
    m_evTimeMultSpin = new QDoubleSpinBox(); m_evTimeMultSpin->setRange(0.1, 24.0); m_evTimeMultSpin->setSingleStep(0.1);
    m_evCloudSpeedSpin = new QDoubleSpinBox(); m_evCloudSpeedSpin->setRange(0.0, 10.0); m_evCloudSpeedSpin->setSingleStep(0.1);
    m_evAmbientSpin = new QSpinBox(); m_evAmbientSpin->setRange(-20, 60);
    m_evRoadSpin = new QSpinBox(); m_evRoadSpin->setRange(-20, 80);
    m_evDynamicTrackSpin = new QSpinBox(); m_evDynamicTrackSpin->setRange(0, 10);
    m_evSessionTypeCombo = new QComboBox();
    m_evSessionTypeCombo->addItems({tr("Practice"), tr("Qualify"), tr("Race"), tr("Hotlap"), tr("Time Attack"), tr("Drift"), tr("Drag")});
    m_evSessionNameEdit = new QLineEdit();
    m_evSpawnSetEdit = new QLineEdit(); m_evSpawnSetEdit->setPlaceholderText("START");
    m_evStartPosSpin = new QSpinBox(); m_evStartPosSpin->setRange(0, 99);
    m_evSessionLapsSpin = new QSpinBox(); m_evSessionLapsSpin->setRange(0, 999);
    m_evDurationSpin = new QSpinBox(); m_evDurationSpin->setRange(0, 999);

    int r = 0;
    l->addWidget(new QLabel(tr("Name:")), r, 0); l->addWidget(m_evNameEdit, r++, 1);
    l->addWidget(new QLabel(tr("Description:")), r, 0); l->addWidget(m_evDescEdit, r++, 1);
    l->addWidget(new QLabel(tr("Track:")), r, 0); l->addWidget(m_evTrackEdit, r++, 1);
    l->addWidget(new QLabel(tr("Config:")), r, 0); l->addWidget(m_evConfigTrackEdit, r++, 1);
    l->addWidget(new QLabel(tr("Car Model:")), r, 0); l->addWidget(m_evModelEdit, r++, 1);
    l->addWidget(new QLabel(tr("Model Config:")), r, 0); l->addWidget(m_evModelConfigEdit, r++, 1);
    l->addWidget(new QLabel(tr("Car Skin:")), r, 0); l->addWidget(m_evSkinEdit, r++, 1);
    l->addWidget(new QLabel(tr("Opponents:")), r, 0); l->addWidget(m_evCarsSpin, r++, 1);
    l->addWidget(new QLabel(tr("AI Level:")), r, 0); l->addWidget(m_evAiLevelSpin, r++, 1);
    l->addWidget(new QLabel(tr("Race Laps:")), r, 0); l->addWidget(m_evRaceLapsSpin, r++, 1);
    l->addWidget(m_evPenaltiesSpin, r, 0); l->addWidget(new QLabel(tr("(Penalties)")), r++, 1);
    l->addWidget(m_evDriftModeCheck, r++, 0, 1, 2);
    l->addWidget(m_evFixedSetupCheck, r++, 0, 1, 2);

    auto* envGb = new QGroupBox(tr("Environment"));
    auto* envL = new QGridLayout(envGb);
    envL->addWidget(new QLabel(tr("Sun Angle:")), 0, 0); envL->addWidget(m_evSunAngleSpin, 0, 1);
    envL->addWidget(new QLabel(tr("Time Mult:")), 1, 0); envL->addWidget(m_evTimeMultSpin, 1, 1);
    envL->addWidget(new QLabel(tr("Cloud Speed:")), 2, 0); envL->addWidget(m_evCloudSpeedSpin, 2, 1);
    envL->addWidget(new QLabel(tr("Ambient \u00b0C:")), 3, 0); envL->addWidget(m_evAmbientSpin, 3, 1);
    envL->addWidget(new QLabel(tr("Road \u00b0C:")), 4, 0); envL->addWidget(m_evRoadSpin, 4, 1);
    envL->addWidget(new QLabel(tr("Dynamic Track:")), 5, 0); envL->addWidget(m_evDynamicTrackSpin, 5, 1);
    l->addWidget(envGb, r++, 0, 1, 2);

    auto* sessGb = new QGroupBox(tr("Session"));
    auto* sessL = new QGridLayout(sessGb);
    sessL->addWidget(new QLabel(tr("Type:")), 0, 0); sessL->addWidget(m_evSessionTypeCombo, 0, 1);
    sessL->addWidget(new QLabel(tr("Name:")), 1, 0); sessL->addWidget(m_evSessionNameEdit, 1, 1);
    sessL->addWidget(new QLabel(tr("Spawn Set:")), 2, 0); sessL->addWidget(m_evSpawnSetEdit, 2, 1);
    sessL->addWidget(new QLabel(tr("Start Pos:")), 3, 0); sessL->addWidget(m_evStartPosSpin, 3, 1);
    sessL->addWidget(new QLabel(tr("Laps:")), 4, 0); sessL->addWidget(m_evSessionLapsSpin, 4, 1);
    sessL->addWidget(new QLabel(tr("Duration (min):")), 5, 0); sessL->addWidget(m_evDurationSpin, 5, 1);
    l->addWidget(sessGb, r++, 0, 1, 2);

    // ── Conditions panel ──
    auto* condGb = new QGroupBox(tr("Conditions"));
    auto* condL = new QVBoxLayout(condGb);
    m_condList = new QListWidget();
    condL->addWidget(m_condList);
    auto* condBtnL = new QHBoxLayout();
    m_addCondBtn = new QPushButton(tr("Add Condition"));
    m_removeCondBtn = new QPushButton(tr("Remove"));
    condBtnL->addWidget(m_addCondBtn);
    condBtnL->addWidget(m_removeCondBtn);
    condBtnL->addStretch();
    condL->addLayout(condBtnL);
    l->addWidget(condGb, r++, 0, 1, 2);

    // ── Opponents table ──
    auto* oppGb = new QGroupBox(tr("Opponents"));
    auto* oppL = new QVBoxLayout(oppGb);
    m_opponentTable = new QTableWidget(0, 4);
    m_opponentTable->setHorizontalHeaderLabels({tr("Name"), tr("Car"), tr("Skin"), tr("Level")});
    m_opponentTable->horizontalHeader()->setStretchLastSection(true);
    m_opponentTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    oppL->addWidget(m_opponentTable);
    auto* oppBtnL = new QHBoxLayout();
    m_addOppBtn = new QPushButton(tr("Add Opponent"));
    m_removeOppBtn = new QPushButton(tr("Remove"));
    oppBtnL->addWidget(m_addOppBtn);
    oppBtnL->addWidget(m_removeOppBtn);
    oppBtnL->addStretch();
    oppL->addLayout(oppBtnL);
    l->addWidget(oppGb, r++, 0, 1, 2);

    auto* evConnections = new QWidget(this);
    evConnections->setVisible(false);
    connect(m_evNameEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evDescEdit, &QTextEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evTrackEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evConfigTrackEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evModelEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evModelConfigEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evSkinEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evCarsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evAiLevelSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evRaceLapsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evPenaltiesSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evDriftModeCheck, &QCheckBox::toggled, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evFixedSetupCheck, &QCheckBox::toggled, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evSunAngleSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evTimeMultSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evCloudSpeedSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evAmbientSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evRoadSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evDynamicTrackSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evSessionTypeCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evSessionNameEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evSpawnSetEdit, &QLineEdit::textChanged, this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evStartPosSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evSessionLapsSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);
    connect(m_evDurationSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &ChampionshipEditorModule::onEventPropChanged);

    l->setRowStretch(l->rowCount(), 1);
    return w;
}

QDockWidget* ChampionshipEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow) {
    if (m_dockWidget) return m_dockWidget;
    m_dockWidget = new QDockWidget(tr("Championship Editor"), mainWindow);
    m_dockWidget->setObjectName("ChampionshipEditorDock");

    auto* centralWidget = new QWidget();
    auto* mainLayout = new QHBoxLayout(centralWidget);

    // ── Left panel: championship list ──
    auto* leftPanel = new QWidget();
    auto* leftLayout = new QVBoxLayout(leftPanel);
    leftLayout->setContentsMargins(0, 0, 0, 0);
    auto* leftLabel = new QLabel(tr("<b>Championships</b>"));
    m_championshipList = new QListWidget();
    m_addChampBtn = new QPushButton(tr("Add"));
    m_removeChampBtn = new QPushButton(tr("Remove"));
    auto* champBtns = new QHBoxLayout();
    champBtns->addWidget(m_addChampBtn);
    champBtns->addWidget(m_removeChampBtn);
    leftLayout->addWidget(leftLabel);
    leftLayout->addWidget(m_championshipList);
    leftLayout->addLayout(champBtns);

    // ── Center panel: event list ──
    auto* centerPanel = new QWidget();
    auto* centerLayout = new QVBoxLayout(centerPanel);
    centerLayout->setContentsMargins(0, 0, 0, 0);
    auto* centerLabel = new QLabel(tr("<b>Events</b>"));
    m_eventList = new QListWidget();
    m_addEventBtn = new QPushButton(tr("Add"));
    m_removeEventBtn = new QPushButton(tr("Remove"));
    auto* evBtns = new QHBoxLayout();
    evBtns->addWidget(m_addEventBtn);
    evBtns->addWidget(m_removeEventBtn);
    centerLayout->addWidget(centerLabel);
    centerLayout->addWidget(m_eventList);
    centerLayout->addLayout(evBtns);

    // ── Right panel: properties ──
    m_propsStack = new QStackedWidget();
    m_champPropsWidget = createChampionshipProps();
    m_eventPropsWidget = createEventProps();

    auto* scrollArea = new QScrollArea();
    scrollArea->setWidgetResizable(true);
    scrollArea->setWidget(m_propsStack);
    m_propsStack->addWidget(m_champPropsWidget);
    m_propsStack->addWidget(m_eventPropsWidget);

    mainLayout->addWidget(leftPanel, 1);
    mainLayout->addWidget(centerPanel, 1);
    mainLayout->addWidget(scrollArea, 2);

    auto* vMain = new QVBoxLayout();
    vMain->addLayout(mainLayout);

    auto* actionLayout = new QHBoxLayout();
    m_loadBtn = new QPushButton(tr("Load Directory"));
    m_saveBtn = new QPushButton(tr("Save All"));
    actionLayout->addWidget(m_loadBtn);
    actionLayout->addWidget(m_saveBtn);
    actionLayout->addStretch();
    vMain->addLayout(actionLayout);

    m_statusLabel = new QLabel(tr("Ready"));
    vMain->addWidget(m_statusLabel);

    auto* wrapper = new QWidget();
    wrapper->setLayout(vMain);
    m_dockWidget->setWidget(wrapper);

    // Connections
    connect(m_championshipList, &QListWidget::currentRowChanged, this, &ChampionshipEditorModule::onChampionshipSelected);
    connect(m_eventList, &QListWidget::currentRowChanged, this, &ChampionshipEditorModule::onEventSelected);
    connect(m_loadBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onLoadDir);
    connect(m_saveBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onSaveDir);
    connect(m_addChampBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onAddChampionship);
    connect(m_removeChampBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onRemoveChampionship);
    connect(m_addEventBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onAddEvent);
    connect(m_removeEventBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onRemoveEvent);
    connect(m_addCondBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onAddCondition);
    connect(m_removeCondBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onRemoveCondition);
    connect(m_addPointsBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onAddPoints);
    connect(m_removePointsBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onRemovePoints);
    connect(m_addOppBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onAddOpponent);
    connect(m_removeOppBtn, &QPushButton::clicked, this, &ChampionshipEditorModule::onRemoveOpponent);

    return m_dockWidget;
}

// ── Data loading ──────────────────────────────────────────────────────────────

static ChampionshipEvent loadEvent(const QString& dir) {
    ChampionshipEvent ev;
    QFile f(dir + "/event.ini");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return ev;
    QString c = f.readAll();
    f.close();

    auto section = [&](const QString& s) { return readIniSection(c, s); };

    QMap<QString, QString> eventSec = section("EVENT");
    ev.name = eventSec["NAME"];
    ev.description = eventSec["DESCRIPTION"];

    QMap<QString, QString> raceSec = section("RACE");
    ev.track = raceSec["TRACK"];
    ev.configTrack = raceSec["CONFIG_TRACK"];
    ev.model = raceSec["MODEL"];
    ev.modelConfig = raceSec["MODEL_CONFIG"];
    ev.cars = raceSec.value("CARS", "1").toInt();
    ev.aiLevel = raceSec.value("AI_LEVEL", "100").toInt();
    ev.raceLaps = raceSec.value("RACE_LAPS", "0").toInt();
    ev.penalties = raceSec.value("PENALTIES", "1").toInt();
    ev.driftMode = raceSec.value("DRIFT_MODE", "0").toInt() != 0;
    ev.fixedSetup = raceSec.value("FIXED_SETUP", "0").toInt() != 0;

    QMap<QString, QString> carSec = section("CAR_0");
    ev.skin = carSec["SKIN"];

    QMap<QString, QString> lightSec = section("LIGHTING");
    ev.sunAngle = lightSec.value("SUN_ANGLE", "48").toInt();
    ev.timeMult = lightSec.value("TIME_MULT", "1").toDouble();
    ev.cloudSpeed = lightSec.value("CLOUD_SPEED", "0.2").toDouble();

    QMap<QString, QString> tempSec = section("TEMPERATURE");
    ev.ambientTemp = tempSec.value("AMBIENT", "26").toInt();
    ev.roadTemp = tempSec.value("ROAD", "32").toInt();

    QMap<QString, QString> dtSec = section("DYNAMIC_TRACK");
    ev.dynamicTrackPreset = dtSec.value("PRESET", "5").toInt();

    QMap<QString, QString> sessSec = section("SESSION_0");
    ev.sessionName = sessSec["NAME"];
    ev.sessionType = sessSec.value("TYPE", "3").toInt();
    ev.spawnSet = sessSec.value("SPAWN_SET", "START");
    ev.startingPosition = sessSec.value("STARTING_POSITION", "0").toInt();
    ev.sessionLaps = sessSec.value("LAPS", "0").toInt();
    ev.durationMinutes = sessSec.value("DURATION_MINUTES", "0").toInt();

    for (int i = 0; i < 3; ++i) {
        QMap<QString, QString> condSec = section(QString("CONDITION_%1").arg(i));
        if (condSec.isEmpty()) break;
        ConditionGoal g;
        g.type = condSec.value("TYPE", "POSITION");
        g.objective = condSec.value("OBJECTIVE", "0").toInt();
        ev.conditions.append(g);
    }

    return ev;
}

static void saveEvent(const ChampionshipEvent& ev, const QString& dir) {
    QDir().mkpath(dir);
    QFile f(dir + "/event.ini");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);

    writeIniSection(o, "EVENT");
    writeIniLine(o, "NAME", ev.name);
    writeIniLine(o, "DESCRIPTION", ev.description);
    o << '\n';

    writeIniSection(o, "RACE");
    writeIniLine(o, "TRACK", ev.track);
    writeIniLine(o, "CONFIG_TRACK", ev.configTrack);
    writeIniLine(o, "MODEL", ev.model);
    writeIniLine(o, "MODEL_CONFIG", ev.modelConfig);
    writeIniLine(o, "CARS", QString::number(ev.cars));
    writeIniLine(o, "AI_LEVEL", QString::number(ev.aiLevel));
    if (ev.raceLaps > 0) writeIniLine(o, "RACE_LAPS", QString::number(ev.raceLaps));
    writeIniLine(o, "PENALTIES", QString::number(ev.penalties));
    writeIniLine(o, "DRIFT_MODE", QString::number(ev.driftMode ? 1 : 0));
    writeIniLine(o, "FIXED_SETUP", QString::number(ev.fixedSetup ? 1 : 0));
    o << '\n';

    if (!ev.skin.isEmpty()) {
        writeIniSection(o, "CAR_0");
        writeIniLine(o, "MODEL", "-");
        writeIniLine(o, "SKIN", ev.skin);
        o << '\n';
    }

    writeIniSection(o, "LIGHTING");
    writeIniLine(o, "SUN_ANGLE", QString::number(ev.sunAngle));
    writeIniLine(o, "TIME_MULT", QString::number(ev.timeMult));
    writeIniLine(o, "CLOUD_SPEED", QString::number(ev.cloudSpeed));
    o << '\n';

    writeIniSection(o, "TEMPERATURE");
    writeIniLine(o, "AMBIENT", QString::number(ev.ambientTemp));
    writeIniLine(o, "ROAD", QString::number(ev.roadTemp));
    o << '\n';

    writeIniSection(o, "DYNAMIC_TRACK");
    writeIniLine(o, "PRESET", QString::number(ev.dynamicTrackPreset));
    o << '\n';

    writeIniSection(o, "SESSION_0");
    writeIniLine(o, "NAME", ev.sessionName);
    writeIniLine(o, "TYPE", QString::number(ev.sessionType));
    writeIniLine(o, "SPAWN_SET", ev.spawnSet);
    if (ev.startingPosition > 0) writeIniLine(o, "STARTING_POSITION", QString::number(ev.startingPosition));
    if (ev.sessionLaps > 0) writeIniLine(o, "LAPS", QString::number(ev.sessionLaps));
    if (ev.durationMinutes > 0) writeIniLine(o, "DURATION_MINUTES", QString::number(ev.durationMinutes));
    o << '\n';

    for (int i = 0; i < ev.conditions.size(); ++i) {
        writeIniSection(o, QString("CONDITION_%1").arg(i));
        writeIniLine(o, "TYPE", ev.conditions[i].type);
        writeIniLine(o, "OBJECTIVE", QString::number(ev.conditions[i].objective));
        o << '\n';
    }

    f.close();
}

static ChampionshipEntry loadChampionship(const QString& dir) {
    ChampionshipEntry entry;
    entry.dirName = QDir(dir).dirName();

    QFile f(dir + "/series.ini");
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) return entry;
    QString c = f.readAll();
    f.close();

    QMap<QString, QString> series = readIniSection(c, "SERIES");
    entry.name = series["NAME"];
    entry.code = series["CODE"];
    entry.description = series["DESCRIPTION"];
    entry.model = series["MODEL"];
    entry.requires = series["REQUIRES"];

    QString pointsStr = series["POINTS"];
    if (!pointsStr.isEmpty() && pointsStr != "0") {
        for (const QString& p : pointsStr.split(',')) {
            bool ok;
            int v = p.trimmed().toInt(&ok);
            if (ok) entry.pointsForPlace.append(v);
        }
    }

    QMap<QString, QString> goals = readIniSection(c, "GOALS");
    entry.goals.points = goals.value("POINTS", "0").toInt();
    entry.goals.ranking = goals.value("RANKING", "0").toInt();
    entry.goals.tier1 = goals.value("TIER1", "0").toInt();
    entry.goals.tier2 = goals.value("TIER2", "0").toInt();
    entry.goals.tier3 = goals.value("TIER3", "0").toInt();

    // Load opponents
    QFile opp(dir + "/opponents.ini");
    if (opp.open(QIODevice::ReadOnly | QIODevice::Text)) {
        QString oc = opp.readAll();
        opp.close();
        for (int i = 1; i <= 100; ++i) {
            QMap<QString, QString> ai = readIniSection(oc, QString("AI%1").arg(i));
            if (ai.isEmpty()) break;
            OpponentDriver d;
            d.name = ai["NAME"];
            d.car = ai["CAR"];
            d.skin = ai["SKIN"];
            d.level = ai.value("LEVEL", "100").toInt();
            d.modelConfig = ai["MODEL_CONFIG"];
            entry.opponents.append(d);
        }
    }

    // Load events
    QDir d(dir);
    for (const QString& sub : d.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (sub.startsWith("event", Qt::CaseInsensitive)) {
            entry.events.append(loadEvent(d.absoluteFilePath(sub)));
        }
    }

    return entry;
}

void ChampionshipEditorModule::loadDirToUI() {
    m_championships.clear();
    m_championshipList->clear();
    m_eventList->clear();
    m_selectedChamp = -1;
    m_selectedEvent = -1;
    m_propsStack->setCurrentIndex(0);

    QDir dir(m_dir);
    int loaded = 0;
    for (const QString& sub : dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        if (QFileInfo::exists(dir.absoluteFilePath(sub + "/series.ini"))) {
            m_championships.append(loadChampionship(dir.absoluteFilePath(sub)));
            m_championshipList->addItem(sub);
            ++loaded;
        }
    }
    m_statusLabel->setText(tr("Loaded %1 championships").arg(loaded));
}

void ChampionshipEditorModule::saveChampionship(const ChampionshipEntry& entry, const QString& dir) {
    QDir().mkpath(dir);

    // series.ini
    QFile f(dir + "/series.ini");
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) return;
    QTextStream o(&f);
    writeIniSection(o, "SERIES");
    writeIniLine(o, "CODE", entry.code);
    writeIniLine(o, "NAME", entry.name);
    writeIniLine(o, "DESCRIPTION", entry.description);
    writeIniLine(o, "REQUIRES", entry.requires);
    if (!entry.pointsForPlace.isEmpty()) {
        QStringList ps;
        for (int p : entry.pointsForPlace) ps.append(QString::number(p));
        writeIniLine(o, "POINTS", ps.join(","));
    } else {
        writeIniLine(o, "POINTS", "0");
    }
    writeIniLine(o, "MODEL", entry.model);
    o << '\n';

    writeIniSection(o, "GOALS");
    writeIniLine(o, "POINTS", QString::number(entry.goals.points));
    writeIniLine(o, "RANKING", QString::number(entry.goals.ranking));
    writeIniLine(o, "TIER1", QString::number(entry.goals.tier1));
    writeIniLine(o, "TIER2", QString::number(entry.goals.tier2));
    writeIniLine(o, "TIER3", QString::number(entry.goals.tier3));
    f.close();

    // opponents.ini
    if (!entry.opponents.isEmpty()) {
        QFile of(dir + "/opponents.ini");
        if (of.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream oo(&of);
            for (int i = 0; i < entry.opponents.size(); ++i) {
                writeIniSection(oo, QString("AI%1").arg(i + 1));
                writeIniLine(oo, "NAME", entry.opponents[i].name);
                writeIniLine(oo, "CAR", entry.opponents[i].car);
                writeIniLine(oo, "SKIN", entry.opponents[i].skin);
                writeIniLine(oo, "LEVEL", QString::number(entry.opponents[i].level));
                writeIniLine(oo, "MODEL_CONFIG", entry.opponents[i].modelConfig);
                oo << '\n';
            }
            of.close();
        }
    }

    // events
    for (int i = 0; i < entry.events.size(); ++i) {
        saveEvent(entry.events[i], dir + QString("/event%1").arg(i + 1));
    }
}

void ChampionshipEditorModule::saveAll() {
    if (m_dir.isEmpty()) return;
    QDir dir(m_dir);
    for (const auto& champ : m_championships) {
        saveChampionship(champ, dir.absoluteFilePath(champ.dirName));
    }
    m_statusLabel->setText(tr("Saved %1 championships to %2").arg(m_championships.size()).arg(m_dir));
}

// ── Property population ──────────────────────────────────────────────────────

void ChampionshipEditorModule::populateChampionshipProps(int index) {
    if (index < 0 || index >= m_championships.size()) return;
    const auto& c = m_championships[index];
    m_champNameEdit->setText(c.name);
    m_champCodeEdit->setText(c.code);
    m_champDescEdit->setText(c.description);
    m_champModelEdit->setText(c.model);
    m_champRequiresEdit->setText(c.requires);
    m_goalPointsSpin->setValue(c.goals.points);
    m_goalRankingSpin->setValue(c.goals.ranking);
    m_goalTier1Spin->setValue(c.goals.tier1);
    m_goalTier2Spin->setValue(c.goals.tier2);
    m_goalTier3Spin->setValue(c.goals.tier3);
    m_pointsList->clear();
    for (int p : c.pointsForPlace)
        m_pointsList->addItem(QString::number(p));
}

void ChampionshipEditorModule::populateEventProps(int champIdx, int eventIdx) {
    if (champIdx < 0 || champIdx >= m_championships.size()) return;
    if (eventIdx < 0 || eventIdx >= m_championships[champIdx].events.size()) return;
    const auto& e = m_championships[champIdx].events[eventIdx];
    m_evNameEdit->setText(e.name);
    m_evDescEdit->setText(e.description);
    m_evTrackEdit->setText(e.track);
    m_evConfigTrackEdit->setText(e.configTrack);
    m_evModelEdit->setText(e.model);
    m_evModelConfigEdit->setText(e.modelConfig);
    m_evSkinEdit->setText(e.skin);
    m_evCarsSpin->setValue(e.cars);
    m_evAiLevelSpin->setValue(e.aiLevel);
    m_evRaceLapsSpin->setValue(e.raceLaps);
    m_evPenaltiesSpin->setValue(e.penalties);
    m_evDriftModeCheck->setChecked(e.driftMode);
    m_evFixedSetupCheck->setChecked(e.fixedSetup);
    m_evSunAngleSpin->setValue(e.sunAngle);
    m_evTimeMultSpin->setValue(e.timeMult);
    m_evCloudSpeedSpin->setValue(e.cloudSpeed);
    m_evAmbientSpin->setValue(e.ambientTemp);
    m_evRoadSpin->setValue(e.roadTemp);
    m_evDynamicTrackSpin->setValue(e.dynamicTrackPreset);
    m_evSessionTypeCombo->setCurrentIndex(qBound(0, e.sessionType - 1, 6));
    m_evSessionNameEdit->setText(e.sessionName);
    m_evSpawnSetEdit->setText(e.spawnSet);
    m_evStartPosSpin->setValue(e.startingPosition);
    m_evSessionLapsSpin->setValue(e.sessionLaps);
    m_evDurationSpin->setValue(e.durationMinutes);

    // Populate conditions list
    m_condList->clear();
    for (int i = 0; i < e.conditions.size(); ++i) {
        const auto& c = e.conditions[i];
        m_condList->addItem(tr("Cond %1: %2 = %3").arg(i + 1).arg(c.type).arg(c.objective));
    }

    // Populate opponents table
    const auto& champ = m_championships[champIdx];
    m_opponentTable->setRowCount(champ.opponents.size());
    for (int i = 0; i < champ.opponents.size(); ++i) {
        const auto& op = champ.opponents[i];
        m_opponentTable->setItem(i, 0, new QTableWidgetItem(op.name));
        m_opponentTable->setItem(i, 1, new QTableWidgetItem(op.car));
        m_opponentTable->setItem(i, 2, new QTableWidgetItem(op.skin));
        m_opponentTable->setItem(i, 3, new QTableWidgetItem(QString::number(op.level)));
    }
}

// ── Slots ────────────────────────────────────────────────────────────────────

void ChampionshipEditorModule::onChampionshipSelected(int row) {
    if (row < 0 || row >= m_championships.size()) {
        m_selectedChamp = -1;
        m_eventList->clear();
        m_propsStack->setCurrentIndex(0);
        return;
    }
    m_selectedChamp = row;
    m_selectedEvent = -1;

    m_eventList->blockSignals(true);
    m_eventList->clear();
    const auto& c = m_championships[row];
    for (int i = 0; i < c.events.size(); ++i)
        m_eventList->addItem(tr("Event %1: %2").arg(i + 1).arg(c.events[i].name));
    m_eventList->blockSignals(false);

    m_propsStack->setCurrentIndex(0);
    populateChampionshipProps(row);
}

void ChampionshipEditorModule::onEventSelected(int row) {
    if (m_selectedChamp < 0 || row < 0 || row >= m_championships[m_selectedChamp].events.size()) {
        m_selectedEvent = -1;
        m_propsStack->setCurrentIndex(0);
        populateChampionshipProps(m_selectedChamp);
        return;
    }
    m_selectedEvent = row;
    m_propsStack->setCurrentIndex(1);
    populateEventProps(m_selectedChamp, row);
}

void ChampionshipEditorModule::onLoadDir() {
    QString d = QFileDialog::getExistingDirectory(this, tr("Open career/championship directory"));
    if (!d.isEmpty()) { m_dir = d; loadDirToUI(); }
}

void ChampionshipEditorModule::onSaveDir() { saveAll(); }

void ChampionshipEditorModule::onAddChampionship() {
    ChampionshipEntry e;
    e.dirName = QString("series%1").arg(m_championships.size());
    e.name = tr("New Championship");
    e.code = "NEW";
    m_championships.append(e);
    m_championshipList->addItem(e.dirName);
    m_championshipList->setCurrentRow(m_championships.size() - 1);
    m_statusLabel->setText(tr("Added new championship"));
}

void ChampionshipEditorModule::onRemoveChampionship() {
    if (m_selectedChamp < 0) return;
    m_championships.removeAt(m_selectedChamp);
    delete m_championshipList->takeItem(m_selectedChamp);
    m_selectedChamp = -1;
    m_eventList->clear();
    m_statusLabel->setText(tr("Removed championship"));
}

void ChampionshipEditorModule::onAddEvent() {
    if (m_selectedChamp < 0) return;
    ChampionshipEvent e;
    e.name = tr("New Event");
    e.track = "ks_vallelunga";
    e.model = "abarth500";
    e.sessionName = tr("Race");
    m_championships[m_selectedChamp].events.append(e);
    int idx = m_championships[m_selectedChamp].events.size() - 1;
    m_eventList->addItem(tr("Event %1: %2").arg(idx + 1).arg(e.name));
    m_eventList->setCurrentRow(idx);
    m_statusLabel->setText(tr("Added new event"));
}

void ChampionshipEditorModule::onRemoveEvent() {
    if (m_selectedChamp < 0 || m_selectedEvent < 0) return;
    m_championships[m_selectedChamp].events.removeAt(m_selectedEvent);
    delete m_eventList->takeItem(m_selectedEvent);
    m_selectedEvent = -1;
    m_propsStack->setCurrentIndex(0);
    if (m_selectedChamp >= 0) populateChampionshipProps(m_selectedChamp);
    m_statusLabel->setText(tr("Removed event"));
}

void ChampionshipEditorModule::onChampionshipPropChanged() {
    if (m_selectedChamp < 0) return;
    auto& c = m_championships[m_selectedChamp];
    c.name = m_champNameEdit->text();
    c.code = m_champCodeEdit->text();
    c.description = m_champDescEdit->toPlainText();
    c.model = m_champModelEdit->text();
    c.requires = m_champRequiresEdit->text();
    c.goals.points = m_goalPointsSpin->value();
    c.goals.ranking = m_goalRankingSpin->value();
    c.goals.tier1 = m_goalTier1Spin->value();
    c.goals.tier2 = m_goalTier2Spin->value();
    c.goals.tier3 = m_goalTier3Spin->value();
    c.pointsForPlace.clear();
    for (int i = 0; i < m_pointsList->count(); ++i)
        c.pointsForPlace.append(m_pointsList->item(i)->text().toInt());
    m_championshipList->currentItem()->setText(c.name.isEmpty() ? c.dirName : c.name);
}

void ChampionshipEditorModule::onEventPropChanged() {
    if (m_selectedChamp < 0 || m_selectedEvent < 0) return;
    auto& e = m_championships[m_selectedChamp].events[m_selectedEvent];
    e.name = m_evNameEdit->text();
    e.description = m_evDescEdit->toPlainText();
    e.track = m_evTrackEdit->text();
    e.configTrack = m_evConfigTrackEdit->text();
    e.model = m_evModelEdit->text();
    e.modelConfig = m_evModelConfigEdit->text();
    e.skin = m_evSkinEdit->text();
    e.cars = m_evCarsSpin->value();
    e.aiLevel = m_evAiLevelSpin->value();
    e.raceLaps = m_evRaceLapsSpin->value();
    e.penalties = m_evPenaltiesSpin->value();
    e.driftMode = m_evDriftModeCheck->isChecked();
    e.fixedSetup = m_evFixedSetupCheck->isChecked();
    e.sunAngle = m_evSunAngleSpin->value();
    e.timeMult = m_evTimeMultSpin->value();
    e.cloudSpeed = m_evCloudSpeedSpin->value();
    e.ambientTemp = m_evAmbientSpin->value();
    e.roadTemp = m_evRoadSpin->value();
    e.dynamicTrackPreset = m_evDynamicTrackSpin->value();
    e.sessionType = m_evSessionTypeCombo->currentIndex() + 1;
    e.sessionName = m_evSessionNameEdit->text();
    e.spawnSet = m_evSpawnSetEdit->text();
    e.startingPosition = m_evStartPosSpin->value();
    e.sessionLaps = m_evSessionLapsSpin->value();
    e.durationMinutes = m_evDurationSpin->value();
    m_eventList->currentItem()->setText(tr("Event %1: %2").arg(m_selectedEvent + 1).arg(e.name));
}

void ChampionshipEditorModule::importFile(const QString& f) {
    QFileInfo fi(f);
    if (fi.isDir()) { m_dir = f; loadDirToUI(); }
}

void ChampionshipEditorModule::exportFile(const QString& f) {
    if (!f.isEmpty()) { m_dir = f; saveAll(); }
}

void ChampionshipEditorModule::onActivation() { if (m_statusLabel) m_statusLabel->setText(tr("Active")); }
void ChampionshipEditorModule::onDeactivation() { if (m_statusLabel) m_statusLabel->setText(tr("Inactive")); }
void ChampionshipEditorModule::setupUi() { if (m_statusLabel) m_statusLabel->setText(tr("UI Ready")); }

QWidget* ChampionshipEditorModule::createEventConditions(int champIdx, int eventIdx)
{
    if (champIdx < 0 || champIdx >= m_championships.size()) return new QWidget();
    if (eventIdx < 0 || eventIdx >= m_championships[champIdx].events.size()) return new QWidget();

    auto* widget = new QWidget();
    auto* layout = new QVBoxLayout(widget);
    auto* label = new QLabel(tr("Event Conditions"));
    layout->addWidget(label);

    auto& ev = m_championships[champIdx].events[eventIdx];
    for (int i = 0; i < ev.conditions.size(); ++i) {
        auto* row = new QHBoxLayout();
        auto* typeCombo = new QComboBox();
        typeCombo->addItems({"TIME", "POINTS", "POSITION", "AI"});
        typeCombo->setCurrentText(ev.conditions[i].type);
        auto* objSpin = new QSpinBox();
        objSpin->setRange(0, 9999);
        objSpin->setValue(ev.conditions[i].objective);
        row->addWidget(typeCombo);
        row->addWidget(objSpin);
        layout->addLayout(row);
    }

    auto* addBtn = new QPushButton(tr("Add Condition"));
    layout->addWidget(addBtn);
    layout->addStretch();
    return widget;
}

QJsonObject ChampionshipEditorModule::serializeProject() const
{
    QJsonObject data;
    data["dir"] = m_dir;
    data["selectedChamp"] = m_selectedChamp;
    data["selectedEvent"] = m_selectedEvent;

    QJsonArray champsArray;
    for (const auto& champ : m_championships) {
        QJsonObject champObj;
        champObj["dirName"] = champ.dirName;
        champObj["name"] = champ.name;
        champObj["code"] = champ.code;
        champObj["description"] = champ.description;
        champObj["model"] = champ.model;
        champObj["requires"] = champ.requires;

        QJsonObject goalsObj;
        goalsObj["points"] = champ.goals.points;
        goalsObj["ranking"] = champ.goals.ranking;
        goalsObj["tier1"] = champ.goals.tier1;
        goalsObj["tier2"] = champ.goals.tier2;
        goalsObj["tier3"] = champ.goals.tier3;
        champObj["goals"] = goalsObj;

        QJsonArray ppArray;
        for (int p : champ.pointsForPlace) ppArray.append(p);
        champObj["pointsForPlace"] = ppArray;

        QJsonArray eventsArray;
        for (const auto& ev : champ.events) {
            QJsonObject evObj;
            evObj["name"] = ev.name;
            evObj["description"] = ev.description;
            evObj["track"] = ev.track;
            evObj["configTrack"] = ev.configTrack;
            evObj["model"] = ev.model;
            evObj["modelConfig"] = ev.modelConfig;
            evObj["skin"] = ev.skin;
            evObj["cars"] = ev.cars;
            evObj["aiLevel"] = ev.aiLevel;
            evObj["raceLaps"] = ev.raceLaps;
            evObj["penalties"] = ev.penalties;
            evObj["driftMode"] = ev.driftMode;
            evObj["fixedSetup"] = ev.fixedSetup;
            evObj["sunAngle"] = ev.sunAngle;
            evObj["timeMult"] = ev.timeMult;
            evObj["cloudSpeed"] = ev.cloudSpeed;
            evObj["ambientTemp"] = ev.ambientTemp;
            evObj["roadTemp"] = ev.roadTemp;
            evObj["dynamicTrackPreset"] = ev.dynamicTrackPreset;
            evObj["sessionName"] = ev.sessionName;
            evObj["sessionType"] = ev.sessionType;
            evObj["spawnSet"] = ev.spawnSet;
            evObj["startingPosition"] = ev.startingPosition;
            evObj["sessionLaps"] = ev.sessionLaps;
            evObj["durationMinutes"] = ev.durationMinutes;

            QJsonArray condArray;
            for (const auto& c : ev.conditions) {
                QJsonObject cObj;
                cObj["type"] = c.type;
                cObj["objective"] = c.objective;
                condArray.append(cObj);
            }
            evObj["conditions"] = condArray;
            eventsArray.append(evObj);
        }
        champObj["events"] = eventsArray;

        QJsonArray opponentsArray;
        for (const auto& op : champ.opponents) {
            QJsonObject opObj;
            opObj["name"] = op.name;
            opObj["car"] = op.car;
            opObj["skin"] = op.skin;
            opObj["level"] = op.level;
            opObj["modelConfig"] = op.modelConfig;
            opponentsArray.append(opObj);
        }
        champObj["opponents"] = opponentsArray;
        champsArray.append(champObj);
    }
    data["championships"] = champsArray;
    return data;
}

void ChampionshipEditorModule::deserializeProject(const QJsonObject& data)
{
    m_dir = data["dir"].toString();
    m_selectedChamp = data["selectedChamp"].toInt(-1);
    m_selectedEvent = data["selectedEvent"].toInt(-1);
    m_championships.clear();

    for (const auto& v : data["championships"].toArray()) {
        QJsonObject champObj = v.toObject();
        ChampionshipEntry champ;
        champ.dirName = champObj["dirName"].toString();
        champ.name = champObj["name"].toString();
        champ.code = champObj["code"].toString();
        champ.description = champObj["description"].toString();
        champ.model = champObj["model"].toString();
        champ.requires = champObj["requires"].toString();

        QJsonObject goalsObj = champObj["goals"].toObject();
        champ.goals.points = goalsObj["points"].toInt();
        champ.goals.ranking = goalsObj["ranking"].toInt();
        champ.goals.tier1 = goalsObj["tier1"].toInt();
        champ.goals.tier2 = goalsObj["tier2"].toInt();
        champ.goals.tier3 = goalsObj["tier3"].toInt();

        for (const auto& pp : champObj["pointsForPlace"].toArray())
            champ.pointsForPlace.append(pp.toInt());

        for (const auto& evV : champObj["events"].toArray()) {
            QJsonObject evObj = evV.toObject();
            ChampionshipEvent ev;
            ev.name = evObj["name"].toString();
            ev.description = evObj["description"].toString();
            ev.track = evObj["track"].toString();
            ev.configTrack = evObj["configTrack"].toString();
            ev.model = evObj["model"].toString();
            ev.modelConfig = evObj["modelConfig"].toString();
            ev.skin = evObj["skin"].toString();
            ev.cars = evObj["cars"].toInt(1);
            ev.aiLevel = evObj["aiLevel"].toInt(100);
            ev.raceLaps = evObj["raceLaps"].toInt();
            ev.penalties = evObj["penalties"].toInt(1);
            ev.driftMode = evObj["driftMode"].toBool();
            ev.fixedSetup = evObj["fixedSetup"].toBool();
            ev.sunAngle = evObj["sunAngle"].toInt(48);
            ev.timeMult = evObj["timeMult"].toDouble(1.0);
            ev.cloudSpeed = evObj["cloudSpeed"].toDouble(0.2);
            ev.ambientTemp = evObj["ambientTemp"].toInt(26);
            ev.roadTemp = evObj["roadTemp"].toInt(32);
            ev.dynamicTrackPreset = evObj["dynamicTrackPreset"].toInt(5);
            ev.sessionName = evObj["sessionName"].toString();
            ev.sessionType = evObj["sessionType"].toInt(3);
            ev.spawnSet = evObj["spawnSet"].toString("START");
            ev.startingPosition = evObj["startingPosition"].toInt();
            ev.sessionLaps = evObj["sessionLaps"].toInt();
            ev.durationMinutes = evObj["durationMinutes"].toInt();

            for (const auto& cV : evObj["conditions"].toArray()) {
                QJsonObject cObj = cV.toObject();
                ConditionGoal c;
                c.type = cObj["type"].toString();
                c.objective = cObj["objective"].toInt();
                ev.conditions.append(c);
            }
            champ.events.append(ev);
        }

        for (const auto& opV : champObj["opponents"].toArray()) {
            QJsonObject opObj = opV.toObject();
            OpponentDriver op;
            op.name = opObj["name"].toString();
            op.car = opObj["car"].toString();
            op.skin = opObj["skin"].toString();
            op.level = opObj["level"].toInt(100);
            op.modelConfig = opObj["modelConfig"].toString();
            champ.opponents.append(op);
        }

        m_championships.append(champ);
    }

    // Populate UI from deserialized data instead of reloading from disk
    m_championshipList->blockSignals(true);
    m_championshipList->clear();
    for (const auto& c : m_championships)
        m_championshipList->addItem(c.name.isEmpty() ? c.dirName : c.name);
    m_championshipList->blockSignals(false);
    m_eventList->clear();
    if (m_selectedChamp >= 0 && m_selectedChamp < m_championships.size()) {
        onChampionshipSelected(m_selectedChamp);
    }
}

void ChampionshipEditorModule::onAddPoints() {
    if (m_selectedChamp < 0) return;
    int val = m_pointsValueSpin->value();
    m_pointsList->addItem(QString::number(val));
    m_championships[m_selectedChamp].pointsForPlace.append(val);
    m_statusLabel->setText(tr("Added points: %1").arg(val));
}

void ChampionshipEditorModule::onRemovePoints() {
    if (m_selectedChamp < 0) return;
    int row = m_pointsList->currentRow();
    if (row < 0) return;
    m_pointsList->model()->removeRow(row);
    if (row < m_championships[m_selectedChamp].pointsForPlace.size())
        m_championships[m_selectedChamp].pointsForPlace.removeAt(row);
    m_statusLabel->setText(tr("Removed points entry"));
}

void ChampionshipEditorModule::onAddOpponent() {
    if (m_selectedChamp < 0) return;
    OpponentDriver op;
    op.name = tr("New Driver");
    op.car = "abarth500";
    op.level = 100;
    m_championships[m_selectedChamp].opponents.append(op);
    int idx = m_championships[m_selectedChamp].opponents.size() - 1;
    m_opponentTable->setRowCount(idx + 1);
    m_opponentTable->setItem(idx, 0, new QTableWidgetItem(op.name));
    m_opponentTable->setItem(idx, 1, new QTableWidgetItem(op.car));
    m_opponentTable->setItem(idx, 2, new QTableWidgetItem(op.skin));
    m_opponentTable->setItem(idx, 3, new QTableWidgetItem(QString::number(op.level)));
    m_statusLabel->setText(tr("Added opponent"));
}

void ChampionshipEditorModule::onRemoveOpponent() {
    if (m_selectedChamp < 0) return;
    int row = m_opponentTable->currentRow();
    if (row < 0 || row >= m_championships[m_selectedChamp].opponents.size()) return;
    m_championships[m_selectedChamp].opponents.removeAt(row);
    for (int i = row; i < m_opponentTable->rowCount() - 1; ++i) {
        for (int c = 0; c < 4; ++c)
            m_opponentTable->setItem(i, c, m_opponentTable->takeItem(i + 1, c));
    }
    m_opponentTable->setRowCount(m_opponentTable->rowCount() - 1);
    m_statusLabel->setText(tr("Removed opponent"));
}

void ChampionshipEditorModule::onAddCondition() {
    if (m_selectedChamp < 0 || m_selectedEvent < 0) return;
    ConditionGoal c;
    c.type = "POSITION";
    c.objective = 1;
    m_championships[m_selectedChamp].events[m_selectedEvent].conditions.append(c);
    m_condList->addItem(tr("Cond %1: %2 = %3")
        .arg(m_condList->count() + 1).arg(c.type).arg(c.objective));
    m_statusLabel->setText(tr("Added condition"));
}

void ChampionshipEditorModule::onRemoveCondition() {
    if (m_selectedChamp < 0 || m_selectedEvent < 0) return;
    int row = m_condList->currentRow();
    if (row < 0) return;
    m_championships[m_selectedChamp].events[m_selectedEvent].conditions.removeAt(row);
    delete m_condList->takeItem(row);
    m_statusLabel->setText(tr("Removed condition"));
}

} // namespace ks
