#include "HelpBrowser.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTextBrowser>
#include <QTreeWidget>
#include <QHeaderView>
#include <QSplitter>
#include <QPushButton>
#include <QFile>
#include <QTextStream>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QLabel>
#include <QApplication>
#include <QStyle>

namespace ks {

// ============================================================================
// Help content registry — stores all help topics and their content
// ============================================================================

static QVector<HelpTopic> s_helpTopics;

void HelpContentRegistry::registerTopic(const HelpTopic& topic)
{
    for (int i = 0; i < s_helpTopics.size(); ++i) {
        if (s_helpTopics[i].id == topic.id) {
            s_helpTopics[i] = topic;
            return;
        }
    }
    s_helpTopics.append(topic);
}

QVector<HelpTopic> HelpContentRegistry::allTopics()
{
    return s_helpTopics;
}

HelpTopic HelpContentRegistry::findTopic(const QString& id)
{
    for (const auto& t : s_helpTopics) {
        if (t.id == id) return t;
    }
    return HelpTopic{};
}

QVector<HelpTopic> HelpContentRegistry::topicsByCategory(const QString& category)
{
    QVector<HelpTopic> result;
    for (const auto& t : s_helpTopics) {
        if (t.category == category) result.append(t);
    }
    return result;
}

QStringList HelpContentRegistry::categories()
{
    QStringList cats;
    for (const auto& t : s_helpTopics) {
        if (!cats.contains(t.category)) cats.append(t.category);
    }
    return cats;
}

QString HelpBrowser::categoryForContext(const QString& context)
{
    const QString ctx = context.trimmed().toLower();
    const QStringList cats = HelpContentRegistry::categories();
    if (!ctx.isEmpty()) {
        for (const QString& cat : cats) {
            const QString c = cat.toLower();
            if (c.contains(ctx) || ctx.contains(c)) return cat;
        }
        for (const HelpTopic& t : HelpContentRegistry::allTopics()) {
            if (t.id.toLower().contains(ctx) || t.title.toLower().contains(ctx)) return t.category;
        }
    }
    return cats.isEmpty() ? QString() : cats.first();
}
void HelpContentRegistry::loadDefaults()
{
    if (!s_helpTopics.isEmpty()) return;

    registerTopic({
        "getting-started", "Getting Started", "General",
        "Welcome to ksEditor, a comprehensive Assetto Corsa modding suite.\n\n"
        "To get started:\n"
        "1. Click 'New Project' on the welcome screen\n"
        "2. Choose a project type (Car, Track, Audio)\n"
        "3. Select a template or start from scratch\n"
        "4. Use the module tabs to access editors\n\n"
        "Key shortcuts:\n"
        "  F1     - Help / Documentation\n"
        "  Ctrl+N - New Project\n"
        "  Ctrl+O - Open Project\n"
        "  Ctrl+S - Save Project\n"
        "  Ctrl+Z - Undo\n"
        "  Ctrl+Y - Redo\n"
        "  F12    - Script Console"
    });

    registerTopic({
        "3d-modeler", "3D Modeler", "Modeling",
        "The 3D Modeler provides full-featured mesh editing and scene management.\n\n"
        "Features:\n"
        "- Mesh primitives: Cube, Sphere, Cylinder, Cone, Torus, Plane, Grid\n"
        "- Boolean operations: Union, Difference, Intersection, XOR\n"
        "- UV Mapping: Planar, Cylindrical, Spherical, Box, LSCM Unwrap, Pack\n"
        "- Subdivision surfaces (Catmull-Clark via OpenSubdiv)\n"
        "- Sculpting tools with symmetry\n"
        "- Modifier stack system\n"
        "- Skeleton/Rigging: Humanoid, Quadruped, IK solvers\n"
        "- Shape keys / morph targets\n"
        "- Animation system with timeline editor\n"
        "- Normal map baking, physics mesh generation, collision mesh\n"
        "- LOD generation, Geometry nodes\n"
        "- Car/Track/Character Editor modules\n\n"
        "Supported formats: OBJ, KN5, FBX, GLB, GLTF, STL\n\n"
        "Viewport controls:\n"
        "  Left drag   - Orbit\n"
        "  Right drag  - Pan\n"
        "  Scroll      - Zoom\n"
        "  G           - Gizmo mode (Move/Rotate/Scale)"
    });

    registerTopic({
        "audio-editor", "Audio Editor", "Audio",
        "The Audio Editor (ksAudioStudio) is a full digital audio workstation.\n\n"
        "Features:\n"
        "- Multi-track mixer with channel strips, VU meters, master bus\n"
        "- Effects: EQ, Compressor, Gate, Reverb, Delay, Limiter, Pitch, Saturation\n"
        "- Multiband Compressor with crossover visualization\n"
        "- Convolution Reverb with impulse response presets\n"
        "- Stereo Enhancer (width, mid/side processing)\n"
        "- Tape Emulator (2\", 1\", Cassette, VHS, DAT)\n"
        "- Vocal Processor, Guitar Amp Simulator\n"
        "- Harmonic Generator, Transient Designer\n"
        "- Frequency Analyzer (FFT/1/3 OCT), Oscilloscope, Loudness Meter (EBU R128)\n"
        "- Surround Sound Mixer (5.1/7.1/7.1.2/7.1.4)\n"
        "- VST3/VST2 host, LADSPA host\n"
        "- FMOD bank reader/writer/encrypt\n"
        "- Wwise-compatible soundbank import\n"
        "- AI-driven engine sound generation\n"
        "- Audio format conversion (WAV, MP3, OGG, FLAC, FSB)\n\n"
        "Supported formats: WAV, MP3, OGG, FLAC, FSB, FMOD .bank"
    });

    registerTopic({
        "physics-editor", "Physics Editor", "Physics",
        "The Physics Editor provides vehicle dynamics simulation and tuning.\n\n"
        "Features:\n"
        "- Full vehicle dynamics simulation (software Euler integrator)\n"
        "- Suspension setup: springs, dampers, anti-roll bars\n"
        "- Brake tuning: torque, bias, temperature model\n"
        "- Tire physics: Pacejka tire model, pressure, temperature, wear\n"
        "- Engine/gearbox simulation: power curves, ratios, shift logic\n"
        "- Differential model\n"
        "- Aero configuration: downforce, drag, front/rear split\n"
        "- ERS/Hybrid system: MGU-K, MGU-H, battery\n"
        "- Lap time estimation and validation\n"
        "- Performance profiler\n"
        "- Telemetry recording and analysis\n"
        "- Setup save/load/compare/recommender\n"
        "- Raycasting, sphere overlap, joint management"
    });

    registerTopic({
        "livery-editor", "Livery Editor", "Skinning",
        "The Livery Editor paints car liveries and manages skins.\n\n"
        "Features:\n"
        "- Skin management: create, delete, duplicate, rename\n"
        "- Layer system: decals, paint, texture layers with opacity/blending\n"
        "- Real-time texture painting with brush tools\n"
        "- Color palette with 20 built-in racing colors\n"
        "- License plate generator (22 countries)\n"
        "- Template system (6 built-in livery templates)\n"
        "- DDS export for AC format\n"
        "- Decal import (PNG, JPG, DDS, TGA, BMP, TIFF)\n"
        "- Undo/Redo system (50-level)\n"
        "- 3D car mesh preview with orbit camera\n"
        "- Import/Export skin configuration as JSON"
    });

    registerTopic({
        "showroom-editor", "Showroom Editor", "Preview",
        "The Showroom Editor creates car previews and showcase configurations.\n\n"
        "Features:\n"
        "- Car showroom configuration with camera and lighting\n"
        "- 3D viewport with OBJ/KN5/GLTF mesh loading\n"
        "- Camera management: multiple cameras with position/target/FOV\n"
        "- Light management: directional, point, spot lights\n"
        "- Sun/ambient color and intensity controls\n"
        "- 2D layout preview with camera frustum visualization\n"
        "- Preview generation with configurable resolution\n"
        "- Batch preview generation for all cars in a directory\n"
        "- Config load/save (INI format)\n"
        "- Thumbnail generation (256x256)"
    });

    registerTopic({
        "mod-manager", "Mod Manager", "Content",
        "The Mod Manager handles content organization, packaging, and repair.\n\n"
        "Features:\n"
        "- Install/Uninstall mods\n"
        "- Enable/Disable mods\n"
        "- Dependency checking and conflict resolution\n"
        "- Content repair tool\n"
        "- Mod packaging for distribution\n"
        "- Repository browsing\n"
        "- Version tracking per mod"
    });

    registerTopic({
        "license-plates", "License Plates", "Content",
        "Generate realistic license plates for 22 countries.\n\n"
        "Countries: IT, DE, UK, FR, ES, JP, US, AU, BR, CN, BE, AT,\n"
        "NO, DK, FI, PL, CZ, PT, SE, SK, HR, NL\n\n"
        "Features:\n"
        "- Country-specific format validation\n"
        "- Live preview with 7-segment display\n"
        "- Batch export (DDS, PNG, Livery)\n"
        "- QR code generation\n"
        "- Holographic effect\n"
        "- Gradient rendering\n"
        "- Corner radius and text alignment options"
    });

    registerTopic({
        "display-editor", "Display Editor", "Content",
        "The Display Editor creates dashboard displays for AC instruments.\n\n"
        "Features:\n"
        "- 7-segment, 14-segment, and LCD character displays\n"
        "- Element CRUD: add, remove, update, clear\n"
        "- Data sources: Speed, RPM, Gear, Fuel, Temperature, etc.\n"
        "- Export to INI, Lua, JSON formats\n"
        "- Size presets with custom resolution support\n"
        "- Animation configuration\n"
        "- Physics value binding\n"
        "- Export as image (PNG) with progress rings and animated text"
    });

    registerTopic({
        "font-creator", "Font Creator", "Content",
        "Create bitmap font atlases for AC dashboard displays.\n\n"
        "Features:\n"
        "- 16x16 bitmap glyph canvas\n"
        "- Baseline and cap-height guides\n"
        "- Atlas preview\n"
        "- Export: AC INI, BMFont, PNG\n"
        "- Character range: ASCII 32-126"
    });

    registerTopic({
        "ppfilters-editor", "PP Filters Editor", "Content",
        "Edit Assetto Corsa post-processing filters.\n\n"
        "Features:\n"
        "- Split before/after preview\n"
        "- Live histogram\n"
        "- Tone-curve controls\n"
        "- Parameter sections: Exposure, Color, Tone Curve, Bloom, Lens, DoF\n"
        "- Direct AC export (INI format)\n"
        "- Color grading with lookup tables"
    });

    registerTopic({
        "file-formats", "File Formats", "Reference",
        "ksEditor supports a wide range of file formats.\n\n"
        "3D Models:\n"
        "  OBJ (.obj)     - Import/Export with MTL materials\n"
        "  GLB/GLTF       - Import/Export binary glTF 2.0\n"
        "  FBX            - Import (Autodesk FBX)\n"
        "  STL            - Import/Export (ASCII + binary)\n"
        "  KN5            - Import (Assetto Corsa encrypted)\n"
        "  ACD            - Import (AC data format)\n\n"
        "Audio:\n"
        "  WAV, MP3, OGG, FLAC - Import/Export\n"
        "  FMOD .bank      - Read/Write/Encrypt\n"
        "  Wwise SoundBank - Import\n"
        "  FSB             - Import (FMOD sample bank)\n\n"
        "Config/Data:\n"
        "  JSON / INI / Lua / Python\n"
        "  CSP config, car setups, replay files"
    });

    registerTopic({
        "scripting", "Scripting", "Reference",
        "ksEditor supports JavaScript, Python, and Lua scripting.\n\n"
        "JavaScript Console (F12):\n"
        "  QJSEngine-based with auto-complete and history\n"
        "  Access to editor globals and APIs\n\n"
        "Python:\n"
        "  Scripting host with module reload\n"
        "  Access to Python utility scripts\n\n"
        "Lua:\n"
        "  Lua scripting host for AC config scripts\n"
        "  Lua expression evaluator for dashboard displays\n\n"
        "Macros:\n"
        "  Action-sequence recorder with per-step delay and repeat"
    });

    registerTopic({
        "plugins", "Plugins", "Reference",
        "Plugin architecture for simulator integration.\n\n"
        "Plugin types:\n"
        "  - Native Qt .dll plugins\n"
        "  - Python .py script plugins\n\n"
        "Assetto Corsa Plugin (ksAssettoCorsa):\n"
        "  - Car/track content discovery via KsAssettoCorsaContentPath\n"
        "  - KN5 model parsing and decryption\n"
        "  - INI configuration parsing (KsIni)\n"
        "  - Shared memory telemetry reading\n"
        "  - Custom Shaders Patch (CSP) support\n"
        "  - Workshop module integration\n"
        "  - Setup comparison tools\n\n"
        "Plugin lifecycle: scan, load, unload, enable/disable"
    });
}

// ============================================================================
// HelpBrowser widget
// ============================================================================

HelpBrowser::HelpBrowser(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("ksEditor Help");
    resize(900, 650);
    setMinimumSize(600, 400);

    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);

    QSplitter* splitter = new QSplitter(Qt::Horizontal, this);

    // Sidebar: topic tree
    m_treeWidget = new QTreeWidget(this);
    m_treeWidget->setHeaderHidden(true);
    m_treeWidget->setMinimumWidth(220);
    m_treeWidget->setMaximumWidth(350);
    m_treeWidget->setIndentation(12);
    m_treeWidget->setAnimated(true);
    splitter->addWidget(m_treeWidget);

    // Main: content browser
    QWidget* rightSide = new QWidget(this);
    QVBoxLayout* rightLayout = new QVBoxLayout(rightSide);
    rightLayout->setContentsMargins(0, 0, 0, 0);

    m_textBrowser = new QTextBrowser(this);
    m_textBrowser->setOpenExternalLinks(true);
    m_textBrowser->setReadOnly(true);
    rightLayout->addWidget(m_textBrowser, 1);

    QHBoxLayout* buttonLayout = new QHBoxLayout();
    buttonLayout->addStretch();
    QPushButton* closeBtn = new QPushButton("Close", this);
    connect(closeBtn, &QPushButton::clicked, this, &QWidget::close);
    buttonLayout->addWidget(closeBtn);
    rightLayout->addLayout(buttonLayout);

    splitter->addWidget(rightSide);
    splitter->setStretchFactor(0, 1);
    splitter->setStretchFactor(1, 3);
    mainLayout->addWidget(splitter);

    // Load default content
    HelpContentRegistry::loadDefaults();

    // Populate tree
    buildTopicTree();

    // Connect selection
    connect(m_treeWidget, &QTreeWidget::currentItemChanged,
            this, &HelpBrowser::onTopicSelected);

    // Show default topic
    auto topics = HelpContentRegistry::allTopics();
    if (!topics.isEmpty()) {
        showTopic(topics[0].id);
    }
}

void HelpBrowser::showTopic(const QString& topicId)
{
    HelpTopic topic = HelpContentRegistry::findTopic(topicId);
    if (topic.id.isEmpty()) return;

    m_textBrowser->clear();

    htmlContent = QString(
        "<html><body style='font-family: Segoe UI, sans-serif; font-size: 13px; "
        "color: #ddd; background: #1e1e1e; padding: 20px;'>"
        "<h1 style='color: #4fc3f7; font-size: 22px; border-bottom: 1px solid #333; "
        "padding-bottom: 8px;'>%1</h1>"
        "<p style='color: #888; font-size: 11px; margin-top: -8px;'>Category: %2</p>"
        "<div style='line-height: 1.6;'>%3</div>"
        "</body></html>"
    ).arg(topic.title.toHtmlEscaped(),
          topic.category.toHtmlEscaped(),
          topic.content.toHtmlEscaped().replace("\n", "<br>"));

    m_textBrowser->setHtml(htmlContent);
}

void HelpBrowser::onTopicSelected(QTreeWidgetItem* current, QTreeWidgetItem*)
{
    if (!current) return;
    QString topicId = current->data(0, Qt::UserRole).toString();
    if (!topicId.isEmpty()) {
        showTopic(topicId);
    }
}

void HelpBrowser::buildTopicTree()
{
    m_treeWidget->clear();

    QMap<QString, QTreeWidgetItem*> categoryItems;
    for (const auto& topic : HelpContentRegistry::allTopics()) {
        if (!categoryItems.contains(topic.category)) {
            auto* catItem = new QTreeWidgetItem(m_treeWidget);
            catItem->setText(0, topic.category);
            catItem->setFlags(catItem->flags() | Qt::ItemIsSelectable);
            catItem->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
            categoryItems[topic.category] = catItem;
        }

        auto* topicItem = new QTreeWidgetItem(categoryItems[topic.category]);
        topicItem->setText(0, topic.title);
        topicItem->setData(0, Qt::UserRole, topic.id);
        topicItem->setFlags(topicItem->flags() | Qt::ItemIsSelectable);
    }

    m_treeWidget->expandAll();
}

} // namespace ks