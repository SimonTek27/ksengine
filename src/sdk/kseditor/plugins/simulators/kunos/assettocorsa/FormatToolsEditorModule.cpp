#include "FormatToolsEditorModule.h"
#include "sdk/kseditor/engine/sys/LogManager.h"
#include "tools/FormatToolsQmlBridge.h"
#include "tools/ImportExportFilters.h"
#include "sdk/kseditor/engine/FileFormat/INIParser.h"
#include "sdk/kseditor/engine/FileFormat/JSONParser.h"
#include "sdk/kseditor/engine/FileFormat/AiSpline.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/KN5Parser.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/acFiles/FBXExporter.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QMainWindow>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDir>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QApplication>
#include <QDropEvent>
#include <QMimeData>
#include <QUrl>
#include <QDragEnterEvent>
#include <QImage>
#include <QProcess>
#include <QToolBar>

namespace ks {

// ── KN5 conversion helpers ────────────────────────────────────────────────
static ::KN5Parser::KN5File meshDataToKN5File(const QVector<MeshData>& meshes)
{
    ::KN5Parser::KN5File kn5;
    kn5.header.magic = ::KN5Parser::KN5_MAGIC;
    kn5.header.version = ::KN5Parser::KN5_VERSION;

    // Default material
    ::KN5Parser::Material mat;
    mat.id = 0;
    mat.name = "DefaultMaterial";
    mat.shaderName = "ksPerPixel";
    mat.type = ::KN5Parser::Material::Type::Normal;
    kn5.materials.append(mat);

    for (const auto& md : meshes) {
        ::KN5Parser::Mesh kn5Mesh;
        kn5Mesh.name = md.name.isEmpty() ? "Mesh" : md.name;

        // Copy positions
        kn5Mesh.positions.resize(md.vertices.size());
        for (int i = 0; i < md.vertices.size(); ++i)
            kn5Mesh.positions[i] = md.vertices[i].position;

        // Copy normals
        kn5Mesh.normals.resize(md.normals.size());
        for (int i = 0; i < md.normals.size(); ++i)
            kn5Mesh.normals[i] = md.normals[i];

        // Copy UVs
        kn5Mesh.uv0.resize(md.uvs.size());
        for (int i = 0; i < md.uvs.size(); ++i)
            kn5Mesh.uv0[i] = md.uvs[i];

        // Copy UV2s
        kn5Mesh.uv1.resize(md.uv2s.size());
        for (int i = 0; i < md.uv2s.size(); ++i)
            kn5Mesh.uv1[i] = md.uv2s[i];

        // Copy tangents
        kn5Mesh.tangents.resize(md.tangents.size());
        for (int i = 0; i < md.tangents.size(); ++i)
            kn5Mesh.tangents[i] = md.tangents[i];

        // Pack index data as quint32
        QByteArray idxData;
        idxData.resize(md.faces.size() * 3 * 4);
        quint32* idxPtr = reinterpret_cast<quint32*>(idxData.data());
        for (const auto& face : md.faces) {
            for (int j = 0; j < face.indices.size() && j < 3; ++j)
                *idxPtr++ = static_cast<quint32>(face.indices[j]);
        }
        kn5Mesh.indexData = idxData;

        // Set up vertex layout
        kn5Mesh.vertexLayout.attributes = {
            {::KN5Parser::AttributeType::Position,  0},
            {::KN5Parser::AttributeType::Normal,   12},
            {::KN5Parser::AttributeType::TexCoord0, 24}
        };
        kn5Mesh.vertexLayout.vertexSize = 32;

        // Bounding box
        if (!md.vertices.isEmpty()) {
            QVector3D minV(1e9f, 1e9f, 1e9f), maxV(-1e9f, -1e9f, -1e9f);
            for (const auto& v : md.vertices) {
                minV.setX(qMin(minV.x(), v.position.x()));
                minV.setY(qMin(minV.y(), v.position.y()));
                minV.setZ(qMin(minV.z(), v.position.z()));
                maxV.setX(qMax(maxV.x(), v.position.x()));
                maxV.setY(qMax(maxV.y(), v.position.y()));
                maxV.setZ(qMax(maxV.z(), v.position.z()));
            }
            kn5Mesh.boundingMin = {minV.x(), minV.y(), minV.z()};
            kn5Mesh.boundingMax = {maxV.x(), maxV.y(), maxV.z()};
            kn5Mesh.boundingRadius = (maxV - minV).length() * 0.5f;
        }

        // Submesh
        ::KN5Parser::SubMesh sm;
        sm.materialIndex = 0;
        sm.vertexOffset = 0;
        sm.vertexCount = static_cast<quint32>(md.vertices.size());
        sm.indexOffset = 0;
        sm.indexCount = static_cast<quint32>(md.faces.size() * 3);
        sm.boundingMin = {kn5Mesh.boundingMin.x, kn5Mesh.boundingMin.y, kn5Mesh.boundingMin.z};
        sm.boundingMax = {kn5Mesh.boundingMax.x, kn5Mesh.boundingMax.y, kn5Mesh.boundingMax.z};
        kn5Mesh.subMeshes.append(sm);

        kn5.meshes.append(kn5Mesh);
    }

    return kn5;
}

// ── Module constructor ─────────────────────────────────────────────────────
FormatToolsEditorModule::FormatToolsEditorModule(QWidget* parent)
    : EditorModule(parent)
    , m_dockWidget(nullptr)
    , m_tabWidget(nullptr)
    , m_logOutput(nullptr)
    , m_progressBar(nullptr)
{
    setAcceptDrops(true);
}

bool FormatToolsEditorModule::initialize()
{
    LOG_INFO("FormatToolsEditorModule", "Initializing Format Tools module");
    return true;
}

void FormatToolsEditorModule::shutdown()
{
    LOG_INFO("FormatToolsEditorModule", "Shutting down Format Tools module");
}

QDockWidget* FormatToolsEditorModule::getOrCreateDockWidget(QMainWindow* mainWindow)
{
    if (!m_dockWidget) {
        m_dockWidget = new QDockWidget("Format Tools", mainWindow);
        m_dockWidget->setAllowedAreas(Qt::AllDockWidgetAreas);
        m_dockWidget->setWidget(this);
    }
    return m_dockWidget;
}

void FormatToolsEditorModule::setupUI()
{
    QVBoxLayout* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(4, 4, 4, 4);
    mainLayout->setSpacing(4);

    setupToolBar(mainLayout);

    m_tabWidget = new QTabWidget(this);

    setupSingleConvTab(m_tabWidget);
    setupBatchConvTab(m_tabWidget);
    setupKn5Tab(m_tabWidget);
    setupValidateTab(m_tabWidget);
    setupTextureTab(m_tabWidget);

    mainLayout->addWidget(m_tabWidget);

    m_progressBar = new QProgressBar(this);
    m_progressBar->setVisible(false);
    m_progressBar->setMaximumHeight(20);
    mainLayout->addWidget(m_progressBar);

    QGroupBox* logGroup = new QGroupBox("Log", this);
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    logLayout->setContentsMargins(2, 2, 2, 2);

    m_logOutput = new QTextEdit(this);
    m_logOutput->setReadOnly(true);
    m_logOutput->setMaximumHeight(120);
    m_logOutput->setStyleSheet("QTextEdit { background: #1a1a1a; color: #c8c8c8; font-family: Consolas; font-size: 10pt; }");
    logLayout->addWidget(m_logOutput);

    QPushButton* clearLogBtn = new QPushButton("Clear Log", this);
    clearLogBtn->setStyleSheet("QPushButton { background: #4a4a4a; color: #fff; border: 1px solid #555; padding: 3px 8px; }");
    connect(clearLogBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onClearLog);
    logLayout->addWidget(clearLogBtn);

    mainLayout->addWidget(logGroup);

    setLayout(mainLayout);

    log("Format Tools module initialized. Drag and drop files to convert.");
}

void FormatToolsEditorModule::setupToolBar(QVBoxLayout* mainLayout)
{
    QToolBar* toolbar = new QToolBar("Format Tools", this);
    toolbar->setIconSize(QSize(16, 16));
    toolbar->setStyleSheet(
        "QToolBar { background: #2d2d2d; border: none; border-bottom: 1px solid #3a3a3a; padding: 2px; spacing: 2px; }"
    );

    auto addBtn = [&](const QString& text, const QString& tooltip, QPushButton** btnOut) {
        QPushButton* btn = new QPushButton(text, toolbar);
        btn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 3px 10px; } QPushButton:hover { background: #4a6a9a; }");
        btn->setToolTip(tooltip);
        toolbar->addWidget(btn);
        if (btnOut) *btnOut = btn;
    };

    QPushButton* kn5ToFbxBtn = nullptr;
    addBtn("KN5 → FBX", "Switch to Single Convert and select KN5→FBX", &kn5ToFbxBtn);
    connect(kn5ToFbxBtn, &QPushButton::clicked, this, [this]() {
        m_tabWidget->setCurrentIndex(0);
        m_formatCombo->setCurrentIndex(0);
        onSelectInputFiles();
    });

    QPushButton* fbxToKn5Btn = nullptr;
    addBtn("FBX → KN5", "Switch to Single Convert and select FBX→KN5", &fbxToKn5Btn);
    connect(fbxToKn5Btn, &QPushButton::clicked, this, [this]() {
        m_tabWidget->setCurrentIndex(0);
        m_formatCombo->setCurrentIndex(1);
        onSelectInputFiles();
    });

    toolbar->addSeparator();

    QPushButton* validateBtn = nullptr;
    addBtn("Validate", "Switch to Validation tab", &validateBtn);
    connect(validateBtn, &QPushButton::clicked, this, [this]() { m_tabWidget->setCurrentIndex(3); });

    QPushButton* extractBtn = nullptr;
    addBtn("Extract KN5", "Switch to KN5 Extraction tab", &extractBtn);
    connect(extractBtn, &QPushButton::clicked, this, [this]() { m_tabWidget->setCurrentIndex(2); });

    toolbar->addSeparator();

    QPushButton* clearBtn = nullptr;
    addBtn("Clear Log", "Clear the log output", &clearBtn);
    connect(clearBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onClearLog);

    mainLayout->insertWidget(0, toolbar);
}

void FormatToolsEditorModule::setupSingleConvTab(QWidget* parent)
{
    QWidget* tab = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QLabel* title = new QLabel("Single File Conversion", tab);
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #ddd;");
    layout->addWidget(title);

    QGroupBox* inputGroup = new QGroupBox("Input File", tab);
    QHBoxLayout* inputLayout = new QHBoxLayout(inputGroup);
    m_selectInputBtn = new QPushButton("Browse...", inputGroup);
    m_selectInputBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_inputFilePath = new QLabel("No file selected", inputGroup);
    m_inputFilePath->setStyleSheet("color: #999;");
    inputLayout->addWidget(m_selectInputBtn);
    inputLayout->addWidget(m_inputFilePath, 1);
    layout->addWidget(inputGroup);

    QGroupBox* formatGroup = new QGroupBox("Target Format", tab);
    QHBoxLayout* formatLayout = new QHBoxLayout(formatGroup);
    m_formatCombo = new QComboBox(formatGroup);
    m_formatCombo->addItems({
        "KN5 → FBX",
        "FBX → KN5",
        "KN5 → glTF",
        "FBX → glTF",
        "JSON → INI",
        "INI → JSON",
        "CSV → AI Line (.ai)",
        "AI Line (.ai) → CSV"
    });
    formatLayout->addWidget(m_formatCombo);
    layout->addWidget(formatGroup);

    QGroupBox* outputGroup = new QGroupBox("Output File", tab);
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    m_selectOutputBtn = new QPushButton("Browse...", outputGroup);
    m_selectOutputBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_outputFilePath = new QLabel("No output location set", outputGroup);
    m_outputFilePath->setStyleSheet("color: #999;");
    outputLayout->addWidget(m_selectOutputBtn);
    outputLayout->addWidget(m_outputFilePath, 1);
    layout->addWidget(outputGroup);

    m_convertBtn = new QPushButton("Convert", tab);
    m_convertBtn->setStyleSheet("QPushButton { background: #3a7a3a; color: #fff; border: 1px solid #4a8a4a; padding: 8px; font-size: 12px; font-weight: bold; }");
    layout->addWidget(m_convertBtn);

    layout->addStretch();
    m_tabWidget->addTab(tab, "Single Convert");

    connect(m_selectInputBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onSelectInputFiles);
    connect(m_selectOutputBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onSelectOutputDir);
    connect(m_convertBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onConvertClicked);
}

void FormatToolsEditorModule::setupBatchConvTab(QWidget* parent)
{
    QWidget* tab = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QLabel* title = new QLabel("Batch Conversion", tab);
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #ddd;");
    layout->addWidget(title);

    QGroupBox* filesGroup = new QGroupBox("Source Files", tab);
    QVBoxLayout* filesLayout = new QVBoxLayout(filesGroup);

    QHBoxLayout* fileBtnLayout = new QHBoxLayout();
    m_addFilesBtn = new QPushButton("Add Files", filesGroup);
    m_addFilesBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_removeFilesBtn = new QPushButton("Remove Selected", filesGroup);
    m_removeFilesBtn->setStyleSheet("QPushButton { background: #8a3a3a; color: #fff; border: 1px solid #9a4a4a; padding: 5px; }");
    m_clearFilesBtn = new QPushButton("Clear All", filesGroup);
    m_clearFilesBtn->setStyleSheet("QPushButton { background: #6a4a3a; color: #fff; border: 1px solid #7a5a4a; padding: 5px; }");
    fileBtnLayout->addWidget(m_addFilesBtn);
    fileBtnLayout->addWidget(m_removeFilesBtn);
    fileBtnLayout->addWidget(m_clearFilesBtn);
    filesLayout->addLayout(fileBtnLayout);

    m_fileList = new QListWidget(filesGroup);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_fileList->setAlternatingRowColors(true);
    m_fileList->setMinimumHeight(120);
    filesLayout->addWidget(m_fileList);
    layout->addWidget(filesGroup);

    QGroupBox* outputGroup = new QGroupBox("Output Directory", tab);
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    m_selectOutputDirBtn = new QPushButton("Browse...", outputGroup);
    m_selectOutputDirBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_batchOutputDir = new QLabel("No output directory set", outputGroup);
    m_batchOutputDir->setStyleSheet("color: #999;");
    outputLayout->addWidget(m_selectOutputDirBtn);
    outputLayout->addWidget(m_batchOutputDir, 1);
    layout->addWidget(outputGroup);

    QGroupBox* formatGroup = new QGroupBox("Target Format", tab);
    QHBoxLayout* formatLayout = new QHBoxLayout(formatGroup);
    m_batchFormatCombo = new QComboBox(formatGroup);
    m_batchFormatCombo->addItems({
        "KN5 → FBX",
        "FBX → KN5",
        "KN5 → glTF",
        "FBX → glTF",
        "Convert to DDS (BC7)",
        "Convert to DDS (BC3)",
        "Convert to PNG",
        "Extract KN5 contents"
    });
    formatLayout->addWidget(m_batchFormatCombo);
    layout->addWidget(formatGroup);

    m_batchConvertBtn = new QPushButton("Start Batch Conversion", tab);
    m_batchConvertBtn->setStyleSheet("QPushButton { background: #3a7a3a; color: #fff; border: 1px solid #4a8a4a; padding: 8px; font-size: 12px; font-weight: bold; }");
    layout->addWidget(m_batchConvertBtn);

    layout->addStretch();
    m_tabWidget->addTab(tab, "Batch Convert");

    connect(m_addFilesBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onAddFilesClicked);
    connect(m_removeFilesBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onRemoveFilesClicked);
    connect(m_clearFilesBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onClearFilesClicked);
    connect(m_selectOutputDirBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onSelectOutputDir);
    connect(m_batchConvertBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onBatchConvertClicked);
}

void FormatToolsEditorModule::setupKn5Tab(QWidget* parent)
{
    QWidget* tab = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QLabel* title = new QLabel("KN5 Archive Tools", tab);
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #ddd;");
    layout->addWidget(title);

    QGroupBox* inputGroup = new QGroupBox("KN5 File", tab);
    QHBoxLayout* inputLayout = new QHBoxLayout(inputGroup);
    m_selectKn5Btn = new QPushButton("Browse...", inputGroup);
    m_selectKn5Btn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_kn5FilePath = new QLabel("No KN5 file selected", inputGroup);
    m_kn5FilePath->setStyleSheet("color: #999;");
    inputLayout->addWidget(m_selectKn5Btn);
    inputLayout->addWidget(m_kn5FilePath, 1);
    layout->addWidget(inputGroup);

    QGroupBox* outputGroup = new QGroupBox("Extract To", tab);
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    m_selectKn5OutputBtn = new QPushButton("Browse...", outputGroup);
    m_selectKn5OutputBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_kn5OutputPath = new QLabel("No output directory set", outputGroup);
    m_kn5OutputPath->setStyleSheet("color: #999;");
    outputLayout->addWidget(m_selectKn5OutputBtn);
    outputLayout->addWidget(m_kn5OutputPath, 1);
    layout->addWidget(outputGroup);

    QGroupBox* optionsGroup = new QGroupBox("Extraction Options", tab);
    QVBoxLayout* optionsLayout = new QVBoxLayout(optionsGroup);
    m_extractTextures = new QCheckBox("Extract Textures (.dds)", optionsGroup);
    m_extractTextures->setChecked(true);
    m_extractModels = new QCheckBox("Extract Models (.fbx)", optionsGroup);
    m_extractModels->setChecked(true);
    optionsLayout->addWidget(m_extractTextures);
    optionsLayout->addWidget(m_extractModels);
    layout->addWidget(optionsGroup);

    m_extractKn5Btn = new QPushButton("Extract KN5 Archive", tab);
    m_extractKn5Btn->setStyleSheet("QPushButton { background: #3a7a3a; color: #fff; border: 1px solid #4a8a4a; padding: 8px; font-size: 12px; font-weight: bold; }");
    layout->addWidget(m_extractKn5Btn);

    layout->addStretch();
    m_tabWidget->addTab(tab, "KN5 Extract");

    connect(m_selectKn5Btn, &QPushButton::clicked, this, &FormatToolsEditorModule::onSelectKn5File);
    connect(m_selectKn5OutputBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
        if (!dir.isEmpty()) {
            m_kn5OutputPath->setText(dir);
            m_kn5OutputPath->setStyleSheet("color: #ddd;");
        }
    });
    connect(m_extractKn5Btn, &QPushButton::clicked, this, &FormatToolsEditorModule::onExtractKn5Clicked);
}

void FormatToolsEditorModule::setupValidateTab(QWidget* parent)
{
    QWidget* tab = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QLabel* title = new QLabel("INI / JSON Validation", tab);
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #ddd;");
    layout->addWidget(title);

    QGroupBox* inputGroup = new QGroupBox("File to Validate", tab);
    QHBoxLayout* inputLayout = new QHBoxLayout(inputGroup);
    m_selectValidateBtn = new QPushButton("Browse...", inputGroup);
    m_selectValidateBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_validateFilePath = new QLabel("No file selected", inputGroup);
    m_validateFilePath->setStyleSheet("color: #999;");
    inputLayout->addWidget(m_selectValidateBtn);
    inputLayout->addWidget(m_validateFilePath, 1);
    layout->addWidget(inputGroup);

    QGroupBox* typeGroup = new QGroupBox("Validation Type", tab);
    QHBoxLayout* typeLayout = new QHBoxLayout(typeGroup);
    m_validateTypeCombo = new QComboBox(typeGroup);
    m_validateTypeCombo->addItems({"Auto-detect", "JSON", "INI", "Lua Script"});
    typeLayout->addWidget(m_validateTypeCombo);
    layout->addWidget(typeGroup);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_validateBtn = new QPushButton("Validate File", tab);
    m_validateBtn->setStyleSheet("QPushButton { background: #3a7a3a; color: #fff; border: 1px solid #4a8a4a; padding: 8px; font-size: 12px; font-weight: bold; }");
    btnLayout->addWidget(m_validateBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    layout->addStretch();
    m_tabWidget->addTab(tab, "Validate");

    connect(m_selectValidateBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onSelectValidateFile);
    connect(m_validateBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onValidateClicked);
}

void FormatToolsEditorModule::setupTextureTab(QWidget* parent)
{
    QWidget* tab = new QWidget(parent);
    QVBoxLayout* layout = new QVBoxLayout(tab);
    layout->setContentsMargins(8, 8, 8, 8);
    layout->setSpacing(6);

    QLabel* title = new QLabel("Texture Conversion", tab);
    title->setStyleSheet("font-size: 13px; font-weight: bold; color: #ddd;");
    layout->addWidget(title);

    QGroupBox* inputGroup = new QGroupBox("Texture File", tab);
    QHBoxLayout* inputLayout = new QHBoxLayout(inputGroup);
    m_selectTextureBtn = new QPushButton("Browse...", inputGroup);
    m_selectTextureBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_textureFilePath = new QLabel("No texture selected", inputGroup);
    m_textureFilePath->setStyleSheet("color: #999;");
    inputLayout->addWidget(m_selectTextureBtn);
    inputLayout->addWidget(m_textureFilePath, 1);
    layout->addWidget(inputGroup);

    QGroupBox* outputGroup = new QGroupBox("Output Location", tab);
    QHBoxLayout* outputLayout = new QHBoxLayout(outputGroup);
    m_selectTextureOutputBtn = new QPushButton("Browse...", outputGroup);
    m_selectTextureOutputBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 5px; }");
    m_textureOutputPath = new QLabel("Same as input (auto)", outputGroup);
    m_textureOutputPath->setStyleSheet("color: #999;");
    outputLayout->addWidget(m_selectTextureOutputBtn);
    outputLayout->addWidget(m_textureOutputPath, 1);
    layout->addWidget(outputGroup);

    QHBoxLayout* btnLayout = new QHBoxLayout();
    m_ddsToPngBtn = new QPushButton("DDS → PNG", tab);
    m_ddsToPngBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 8px; font-size: 12px; font-weight: bold; }");
    m_pngToDdsBtn = new QPushButton("PNG → DDS", tab);
    m_pngToDdsBtn->setStyleSheet("QPushButton { background: #3a5a8a; color: #fff; border: 1px solid #4a6a9a; padding: 8px; font-size: 12px; font-weight: bold; }");
    btnLayout->addWidget(m_ddsToPngBtn);
    btnLayout->addWidget(m_pngToDdsBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    layout->addStretch();
    m_tabWidget->addTab(tab, "Textures");

    connect(m_selectTextureBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onSelectTextureFile);
    connect(m_selectTextureOutputBtn, &QPushButton::clicked, this, [this]() {
        QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
        if (!dir.isEmpty()) {
            m_textureOutputPath->setText(dir);
            m_textureOutputPath->setStyleSheet("color: #ddd;");
        }
    });
    connect(m_ddsToPngBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onDdsToPngClicked);
    connect(m_pngToDdsBtn, &QPushButton::clicked, this, &FormatToolsEditorModule::onPngToDdsClicked);
}

void FormatToolsEditorModule::onActivation()
{
    LOG_INFO("FormatToolsEditorModule", "Format Tools activated");
    if (!m_tabWidget) {
        setupUI();
    }
}

void FormatToolsEditorModule::onDeactivation()
{
    LOG_INFO("FormatToolsEditorModule", "Format Tools deactivated");
}

void FormatToolsEditorModule::importFile(const QString& filePath)
{
    if (filePath.isEmpty()) return;

    QFileInfo info(filePath);
    QString ext = info.suffix().toLower();

    log(QString("Importing: %1").arg(filePath));

    if (ext == "kn5") {
        m_kn5FilePath->setText(filePath);
        m_kn5FilePath->setStyleSheet("color: #ddd;");
        m_tabWidget->setCurrentIndex(2);
    } else if (ext == "json" || ext == "ini" || ext == "lua") {
        m_validateFilePath->setText(filePath);
        m_validateFilePath->setStyleSheet("color: #ddd;");
        m_tabWidget->setCurrentIndex(3);
    } else if (ext == "dds" || ext == "png") {
        m_textureFilePath->setText(filePath);
        m_textureFilePath->setStyleSheet("color: #ddd;");
        m_tabWidget->setCurrentIndex(4);
    } else {
        m_inputFilePath->setText(filePath);
        m_inputFilePath->setStyleSheet("color: #ddd;");
        m_tabWidget->setCurrentIndex(0);
    }

    LOG_INFO("FormatToolsEditorModule", QString("Imported: %1").arg(filePath));
}

void FormatToolsEditorModule::exportFile(const QString& filePath)
{
    log(QString("Export triggered for: %1").arg(filePath));
    m_outputFilePath->setText(filePath);
    m_outputFilePath->setStyleSheet("color: #ddd;");
    LOG_INFO("FormatToolsEditorModule", QString("Export target: %1").arg(filePath));
}

void FormatToolsEditorModule::onConvertClicked()
{
    QString inputPath = m_inputFilePath->text();
    if (inputPath.isEmpty() || inputPath == "No file selected") {
        logError("No input file selected");
        return;
    }

    QString outputPath = m_outputFilePath->text();
    if (outputPath.isEmpty() || outputPath == "No output location set") {
        outputPath = QFileInfo(inputPath).absolutePath();
    }

    QString format = m_formatCombo->currentText();
    log(QString("Converting: %1 → [%2]").arg(inputPath, format));

    if (format == "JSON → INI") {
        JSONParser json;
        if (!json.load(inputPath)) {
            logError("Failed to load JSON file: " + json.lastError());
            return;
        }

        // Determine output path
        QFileInfo fi(inputPath);
        if (QFileInfo(outputPath).isDir()) {
            outputPath = QDir(outputPath).filePath(fi.completeBaseName() + ".ini");
        }

        INIParser ini;
        QVariantMap root = json.toMap();
        for (auto it = root.begin(); it != root.end(); ++it) {
            if (it.value().type() == QVariant::Map) {
                QVariantMap section = it.value().toMap();
                for (auto sit = section.begin(); sit != section.end(); ++sit) {
                    ini.setValue(it.key(), sit.key(), sit.value());
                }
            } else {
                ini.setValue("General", it.key(), it.value());
            }
        }

        if (ini.save(outputPath)) {
            log("JSON → INI conversion completed: " + QFileInfo(outputPath).fileName());
        } else {
            logError("Failed to write INI file");
        }
        return;
    }

    if (format == "INI → JSON") {
        INIParser ini;
        if (!ini.load(inputPath)) {
            logError("Failed to load INI file");
            return;
        }

        QFileInfo fi(inputPath);
        if (QFileInfo(outputPath).isDir()) {
            outputPath = QDir(outputPath).filePath(fi.completeBaseName() + ".json");
        }

        JSONParser json;
        QVariantMap root;
        for (const QString& section : ini.sections()) {
            QVariantMap sectionMap;
            for (const QString& key : ini.keys(section)) {
                sectionMap[key] = ini.value(section, key);
            }
            root[section] = sectionMap;
        }
        json.setRoot(root);

        if (json.savePretty(outputPath)) {
            log("INI → JSON conversion completed: " + QFileInfo(outputPath).fileName());
        } else {
            logError("Failed to write JSON file");
        }
        return;
    }

    if (format == "CSV → AI Line (.ai)") {
        QFileInfo fi(inputPath);
        if (QFileInfo(outputPath).isDir()) {
            outputPath = QDir(outputPath).filePath(fi.completeBaseName() + ".ai");
        }

        // Parse CSV: x,y,z,[curvature],[speed]
        QFile file(inputPath);
        if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
            logError("Cannot open CSV file");
            return;
        }

        ks::ai::AiSpline spline;
        spline.name = fi.completeBaseName();
        QTextStream in(&file);
        int lineNum = 0;
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            lineNum++;
            if (line.isEmpty() || line.startsWith('#')) continue;

            QStringList parts = line.split(',');
            if (parts.size() < 3) {
                logError(QString("Line %1: expected at least 3 columns (x,y,z)").arg(lineNum));
                continue;
            }

            ks::ai::AiPoint pt;
            bool ok = false;
            pt.position.x = parts[0].trimmed().toFloat(&ok);
            if (!ok) { logError(QString("Line %1: invalid X").arg(lineNum)); continue; }
            pt.position.y = parts[1].trimmed().toFloat(&ok);
            if (!ok) { logError(QString("Line %1: invalid Y").arg(lineNum)); continue; }
            pt.position.z = parts[2].trimmed().toFloat(&ok);
            if (!ok) { logError(QString("Line %1: invalid Z").arg(lineNum)); continue; }
            if (parts.size() > 3) pt.curvature = parts[3].trimmed().toFloat();
            if (parts.size() > 4) pt.speed = parts[4].trimmed().toFloat();
            spline.points.append(pt);
        }
        file.close();

        if (spline.points.isEmpty()) {
            logError("No valid points found in CSV");
            return;
        }

        ks::ai::AiSplineGrid grid;
        grid.trackName = spline.name;
        grid.splines.append(spline);

        if (ks::ai::AiFileWriter::write(outputPath, grid)) {
            log(QString("CSV → AI conversion completed: %1 points").arg(spline.points.size()));
        } else {
            logError("Failed to write AI spline file");
        }
        return;
    }

    if (format == "AI Line (.ai) → CSV") {
        QFileInfo fi(inputPath);
        if (QFileInfo(outputPath).isDir()) {
            outputPath = QDir(outputPath).filePath(fi.completeBaseName() + ".csv");
        }

        ks::ai::AiSplineGrid grid;
        if (!ks::ai::AiFileReader::read(inputPath, grid)) {
            logError("Failed to load AI spline file");
            return;
        }

        QFile file(outputPath);
        if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
            logError("Cannot write CSV file");
            return;
        }

        QTextStream out(&file);
        out << "# x,y,z,curvature,speed\n";
        int totalPoints = 0;
        for (const auto& spline : grid.splines) {
            for (const auto& pt : spline.points) {
                out << pt.position.x << "," << pt.position.y << "," << pt.position.z << ","
                    << pt.curvature << "," << pt.speed << "\n";
                totalPoints++;
            }
        }
        file.close();

        log(QString("AI → CSV conversion completed: %1 points from %2 spline(s)")
            .arg(totalPoints).arg(grid.splines.size()));
        return;
    }

    if (format == "KN5 → FBX") {
        QFileInfo fi(inputPath);
        if (QFileInfo(outputPath).isDir())
            outputPath = QDir(outputPath).filePath(fi.completeBaseName() + ".fbx");

        log("Reading KN5: " + inputPath);
        QString parseErr;
        auto kn5File = ::KN5Parser::KN5ParserImpl::parse(inputPath, &parseErr);
        if (!kn5File.isValid()) {
            logError("Failed to parse KN5: " + parseErr);
            return;
        }

        QVector<MeshData> meshes;
        meshes.reserve(kn5File.meshes.size());
        for (const auto& km : kn5File.meshes) {
            MeshData md;
            md.name = km.name;
            md.vertices.reserve(km.positions.size());
            md.normals = km.normals;
            md.uvs = QVector<QVector2D>(km.uv0.size());
            for (int uvi = 0; uvi < km.uv0.size(); ++uvi)
                md.uvs[uvi] = km.uv0[uvi];
            for (const auto& p : km.positions) {
                Vertex v;
                v.position = p;
                md.vertices.append(v);
            }
            const quint32* idxData = reinterpret_cast<const quint32*>(km.indexData.constData());
            int totalIdx = km.indexData.size() / 4;
            for (int i = 0; i + 2 < totalIdx; i += 3) {
                Face f;
                f.indices = {static_cast<int>(idxData[i]), static_cast<int>(idxData[i+1]), static_cast<int>(idxData[i+2])};
                md.faces.append(f);
            }
            if (!md.vertices.isEmpty())
                meshes.append(md);
        }

        if (meshes.isEmpty()) {
            logError("No meshes found in KN5 file");
            return;
        }

        FBXExportSettings fbxSettings = FBXExporter::getDefaultExportSettings();
        if (FBXExporter::exportToFBX(outputPath, meshes, fbxSettings))
            log(QString("KN5 → FBX: %1 meshes exported to %2").arg(meshes.size()).arg(QFileInfo(outputPath).fileName()));
        else
            logError("KN5 → FBX conversion failed");
        return;
    }

    if (format == "FBX → KN5") {
        QFileInfo fi(inputPath);
        if (QFileInfo(outputPath).isDir())
            outputPath = QDir(outputPath).filePath(fi.completeBaseName() + ".kn5");

        log("Reading FBX: " + inputPath);
        QVector<MeshData> meshes;
        FBXImportSettings fbxImportSettings = FBXImporter::getDefaultImportSettings();
        if (!FBXImporter::importFromFBX(inputPath, meshes, fbxImportSettings)) {
            logError("Failed to parse FBX file");
            return;
        }

        if (meshes.isEmpty()) {
            logError("No meshes found in FBX file");
            return;
        }

        ::KN5Parser::KN5File kn5File = meshDataToKN5File(meshes);
        if (::KN5Parser::KN5ParserImpl::write(outputPath, kn5File))
            log(QString("FBX → KN5: %1 meshes exported to %2").arg(meshes.size()).arg(QFileInfo(outputPath).fileName()));
        else
            logError("FBX → KN5 conversion failed");
        return;
    }

    if (format == "KN5 → glTF" || format == "FBX → glTF") {
        bool isKn5 = (format == "KN5 → glTF");
        QString fbxPath = outputPath;
        if (fbxPath.endsWith(".glb", Qt::CaseInsensitive) || fbxPath.endsWith(".gltf", Qt::CaseInsensitive))
            fbxPath = QFileInfo(fbxPath).absolutePath() + "/" + QFileInfo(fbxPath).completeBaseName() + "_intermediate.fbx";

        if (isKn5) {
            log("Step 1: Converting KN5 → FBX intermediate...");
            QVector<MeshData> meshes;
            ::KN5Parser::KN5File kn5File = ::KN5Parser::KN5ParserImpl::parse(inputPath);
            if (kn5File.meshes.isEmpty()) {
                logError("Failed to load KN5 file");
                return;
            }
            for (const auto& km : kn5File.meshes) {
                MeshData md;
                md.name = km.name;
                for (const auto& p : km.positions) {
                    Vertex v;
                    v.position = p;
                    md.vertices.append(v);
                }
                md.normals = QVector<QVector3D>(km.normals.begin(), km.normals.end());
                md.uvs = QVector<QVector2D>(km.uv0.size());
                for (int uvi = 0; uvi < km.uv0.size(); ++uvi)
                    md.uvs[uvi] = km.uv0[uvi];
                const quint32* idxData = reinterpret_cast<const quint32*>(km.indexData.constData());
                int totalIdx = km.indexData.size() / 4;
                for (int i = 0; i + 2 < totalIdx; i += 3) {
                    Face f;
                    f.indices = {static_cast<int>(idxData[i]), static_cast<int>(idxData[i+1]), static_cast<int>(idxData[i+2])};
                    md.faces.append(f);
                }
                if (!md.vertices.isEmpty()) meshes.append(md);
            }
            if (meshes.isEmpty()) { logError("No meshes found in KN5"); return; }
            FBXExportSettings fbxSettings = FBXExporter::getDefaultExportSettings();
            if (!FBXExporter::exportToFBX(fbxPath, meshes, fbxSettings)) {
                logError("KN5 → FBX intermediate failed");
                return;
            }
            log(QString("KN5 → FBX intermediate: %1 meshes → %2").arg(meshes.size()).arg(QFileInfo(fbxPath).fileName()));
        }

        // Step 2: FBX → glTF via external tool
        log("Step 2: Converting FBX → glTF...");
        log("  Looking for FBX2glTF on PATH...");
        QProcess proc;
        proc.setProcessChannelMode(QProcess::MergedChannels);
        proc.start("FBX2glTF", {"--input", fbxPath, "--output", outputPath, "--binary"});
        if (proc.waitForFinished(120000)) {
            QString out = QString::fromUtf8(proc.readAllStandardOutput());
            if (!out.isEmpty()) log(out);
            if (proc.exitCode() == 0) {
                log(QString("glTF export completed: %1").arg(QFileInfo(outputPath).fileName()));
                return;
            }
            log("FBX2glTF failed, trying Blender-based conversion...");
        } else {
            proc.kill();
            log("FBX2glTF timed out, trying Blender-based conversion...");
        }

        // Fallback: Blender CLI
        QString blenderPath = "blender";
        QString blendScript = "import bpy; import sys; bpy.ops.import_scene.fbx(filepath=sys.argv[5]); bpy.ops.export_scene.gltf(filepath=sys.argv[6], export_format='GLB')";
        proc.start(blenderPath, {"--background", "--python-expr", blendScript, "--", fbxPath, outputPath});
        if (proc.waitForFinished(120000)) {
            QString out = QString::fromUtf8(proc.readAllStandardOutput());
            if (!out.isEmpty()) log(out);
            if (proc.exitCode() == 0) {
                log(QString("glTF export via Blender completed: %1").arg(QFileInfo(outputPath).fileName()));
                return;
            }
            logError("Blender-based glTF export also failed");
        } else {
            proc.kill();
            logError("Blender timed out");
        }

        logError("glTF export requires FBX2glTF or Blender on PATH");
        log("Install FBX2glTF: https://github.com/facebookincubator/FBX2glTF/releases");
        log("Or install Blender and ensure 'blender' is on PATH");
        return;
    }

    {
        QJsonObject options;
        options["input"] = inputPath;
        options["output"] = outputPath;

        if (auto* filter = ks::ExportFilter::instance()) {
            QJsonObject filterOpts;
            filterOpts["format"] = format;
            if (filter->applyFilter(inputPath, outputPath, filterOpts)) {
                log("Conversion completed successfully");
            } else {
                logError("Conversion failed");
            }
        } else {
            log("No ExportFilter plugin available for this format");
            log("Supported formats: JSON↔INI, CSV↔AI");
        }
    }
}

// ── Batch KN5 ↔ FBX helpers ──────────────────────────────────────────────
bool FormatToolsEditorModule::convertKn5ToFbxBatch(const QString& inputPath, const QString& outputPath)
{
    QString parseErr;
    auto kn5File = ::KN5Parser::KN5ParserImpl::parse(inputPath, &parseErr);
    if (!kn5File.isValid()) {
        logError("  Failed to parse KN5: " + parseErr);
        return false;
    }

    QVector<MeshData> meshes;
    meshes.reserve(kn5File.meshes.size());
    for (const auto& km : kn5File.meshes) {
        MeshData md;
        md.name = km.name;
        md.vertices.reserve(km.positions.size());
        md.normals = km.normals;
        md.uvs = QVector<QVector2D>(km.uv0.size());
        for (int uvi = 0; uvi < km.uv0.size(); ++uvi)
            md.uvs[uvi] = km.uv0[uvi];
        for (const auto& p : km.positions) {
            Vertex v;
            v.position = p;
            md.vertices.append(v);
        }
        const quint32* idxData = reinterpret_cast<const quint32*>(km.indexData.constData());
        int totalIdx = km.indexData.size() / 4;
        for (int i = 0; i + 2 < totalIdx; i += 3) {
            Face f;
            f.indices = {static_cast<int>(idxData[i]), static_cast<int>(idxData[i+1]), static_cast<int>(idxData[i+2])};
            md.faces.append(f);
        }
        if (!md.vertices.isEmpty())
            meshes.append(md);
    }

    if (meshes.isEmpty()) {
        logError("  No meshes found in KN5 file");
        return false;
    }

    FBXExportSettings fbxSettings = FBXExporter::getDefaultExportSettings();
    if (FBXExporter::exportToFBX(outputPath, meshes, fbxSettings)) {
        log(QString("  KN5 → FBX: %1 meshes exported").arg(meshes.size()));
        return true;
    }
    logError("  KN5 → FBX conversion failed");
    return false;
}

bool FormatToolsEditorModule::convertFbxToKn5Batch(const QString& inputPath, const QString& outputPath)
{
    QVector<MeshData> meshes;
    FBXImportSettings fbxImportSettings = FBXImporter::getDefaultImportSettings();
    if (!FBXImporter::importFromFBX(inputPath, meshes, fbxImportSettings)) {
        logError("  Failed to parse FBX file");
        return false;
    }

    if (meshes.isEmpty()) {
        logError("  No meshes found in FBX file");
        return false;
    }

    ::KN5Parser::KN5File kn5File = meshDataToKN5File(meshes);
    if (::KN5Parser::KN5ParserImpl::write(outputPath, kn5File)) {
        log(QString("  FBX → KN5: %1 meshes exported").arg(meshes.size()));
        return true;
    }
    logError("  FBX → KN5 conversion failed");
    return false;
}

void FormatToolsEditorModule::onBatchConvertClicked()
{
    if (m_fileList->count() == 0) {
        logError("No files added to batch");
        return;
    }

    QString outputDir = m_batchOutputDir->text();
    if (outputDir.isEmpty() || outputDir == "No output directory set") {
        logError("No output directory selected");
        return;
    }

    QString format = m_batchFormatCombo->currentText();
    int total = m_fileList->count();

    log(QString("Starting batch conversion: %1 files → %2").arg(total).arg(format));
    m_progressBar->setVisible(true);
    m_progressBar->setMaximum(total);
    m_progressBar->setValue(0);

    QDir().mkpath(outputDir);
    int converted = 0;
    int failed = 0;
    int skipped = 0;

    for (int i = 0; i < total; ++i) {
        QString inputPath = m_fileList->item(i)->data(Qt::UserRole).toString();
        QFileInfo info(inputPath);

        if (!info.exists()) {
            logError(QString("[%1/%2] File not found: %3").arg(i + 1).arg(total).arg(info.fileName()));
            failed++;
            m_progressBar->setValue(i + 1);
            QApplication::processEvents();
            continue;
        }

        QString ext;
        bool isKn5ToFbx = format.contains("KN5") && format.contains("FBX");
        bool isFbxToKn5 = format.contains("FBX") && format.contains("KN5");
        if (format.contains("FBX") && !isKn5ToFbx && !isFbxToKn5) ext = ".fbx";
        else if (format.contains("glTF")) ext = ".glb";
        else if (format.contains("BC7")) ext = ".dds";
        else if (format.contains("BC3")) ext = ".dds";
        else if (format.contains("PNG")) ext = ".png";
        else if (isKn5ToFbx) ext = ".fbx";
        else if (isFbxToKn5) ext = ".kn5";
        else ext = info.suffix();

        QString outputPath = QDir(outputDir).filePath(info.completeBaseName() + ext);

        // If same file, skip
        if (inputPath == outputPath) {
            log(QString("[%1/%2] %3 → skipped (same file)").arg(i + 1).arg(total).arg(info.fileName()));
            skipped++;
            m_progressBar->setValue(i + 1);
            QApplication::processEvents();
            continue;
        }

        log(QString("[%1/%2] %3 → %4").arg(i + 1).arg(total).arg(info.fileName(), QFileInfo(outputPath).fileName()));

        bool success = false;
        QString sourceExt = info.suffix().toLower();

        bool isGltf = format.contains("glTF");
        
        // Handle KN5 ↔ FBX conversions in batch mode
        if (isKn5ToFbx) {
            success = convertKn5ToFbxBatch(inputPath, outputPath);
        } else if (isFbxToKn5) {
            success = convertFbxToKn5Batch(inputPath, outputPath);
        } else if (isGltf) {
            // glTF export requires FBX intermediate + external tool
            log("  glTF export: use KN5 → FBX batch first, then convert FBX → glTF with external tool (e.g. Blender, FBX2glTF)");
            success = false;
        } else {
            // Try plugin conversion first
            if (auto* filter = ks::ExportFilter::instance()) {
                QJsonObject opts;
                opts["format"] = format;
                if (filter->applyFilter(inputPath, outputPath, opts)) {
                    success = true;
                }
            }

            // Fallback: simple copy for format-preserving operations
            if (!success && sourceExt == "kn5" && format == "Extract KN5 contents") {
                logError("  KN5 extraction requires the Assetto Corsa plugin DLL");
            } else if (!success && sourceExt == ext) {
                if (QFile::copy(inputPath, outputPath)) {
                    success = true;
                }
            }
        }

        if (success) {
            converted++;
        } else if (sourceExt != ext) {
            // Different format and no plugin available
            logError(QString("  Format conversion not supported: .%1 → .%2").arg(sourceExt, ext));
            failed++;
        } else {
            logError("  File copy failed");
            failed++;
        }

        m_progressBar->setValue(i + 1);
        QApplication::processEvents();
    }

    log(QString("Batch: %1 converted, %2 failed, %3 skipped").arg(converted).arg(failed).arg(skipped));
    m_progressBar->setVisible(false);
}

void FormatToolsEditorModule::onSelectInputFiles()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Select Input File(s)",
        QString(), "All Supported (*.kn5 *.fbx *.dds *.png *.json *.ini *.csv *.ai);;All Files (*)");
    if (!files.isEmpty()) {
        m_inputFilePath->setText(files.first());
        m_inputFilePath->setStyleSheet("color: #ddd;");
    }
}

void FormatToolsEditorModule::onSelectOutputDir()
{
    QString dir = QFileDialog::getExistingDirectory(this, "Select Output Directory");
    if (!dir.isEmpty()) {
        m_outputFilePath->setText(dir);
        m_outputFilePath->setStyleSheet("color: #ddd;");
    }
}

void FormatToolsEditorModule::onAddFilesClicked()
{
    QStringList files = QFileDialog::getOpenFileNames(this, "Add Files to Batch",
        QString(), "All Supported (*.kn5 *.fbx *.dds *.png *.json *.ini);;All Files (*)");
    for (const QString& file : files) {
        QFileInfo info(file);
        QListWidgetItem* item = new QListWidgetItem(info.fileName(), m_fileList);
        item->setData(Qt::UserRole, file);
        item->setToolTip(file);
    }
    log(QString("Added %1 file(s) to batch").arg(files.size()));
}

void FormatToolsEditorModule::onRemoveFilesClicked()
{
    QList<QListWidgetItem*> selected = m_fileList->selectedItems();
    for (auto* item : selected) {
        delete item;
    }
}

void FormatToolsEditorModule::onClearFilesClicked()
{
    m_fileList->clear();
    log("Cleared batch file list");
}

void FormatToolsEditorModule::onClearLog()
{
    m_logOutput->clear();
}

void FormatToolsEditorModule::onToolChanged(int index)
{
    m_tabWidget->setCurrentIndex(index);
}

void FormatToolsEditorModule::onExtractKn5Clicked()
{
    QString kn5Path = m_kn5FilePath->text();
    if (kn5Path.isEmpty() || kn5Path == "No KN5 file selected") {
        logError("No KN5 file selected");
        return;
    }

    QString outputDir = m_kn5OutputPath->text();
    if (outputDir.isEmpty() || outputDir == "No output directory set") {
        outputDir = QFileInfo(kn5Path).absolutePath();
    }

    log(QString("Extracting KN5: %1").arg(kn5Path));
    log(QString("Output: %1").arg(outputDir));

    QDir().mkpath(outputDir);

    // Try to use the KN5 parser if available
    // KN5 reading requires ksAssettoCorsa plugin — check via plugin manager
    log("KN5 is Assetto Corsa's encrypted model archive format");
    log("Extraction requires ksAssettoCorsa.dll plugin to be loaded in the editor");

    // Fallback: try 7-Zip with KN5 plugin
    QProcess proc;
    proc.start("7z", {"l", kn5Path});
    if (proc.waitForFinished(5000) && proc.exitCode() == 0) {
        log("7-Zip can read this KN5 as an archive, attempting extraction...");
        QProcess extract;
        extract.start("7z", {"x", kn5Path, "-o" + outputDir, "-y"});
        if (extract.waitForFinished(30000) && extract.exitCode() == 0) {
            log("Extraction via 7-Zip completed to: " + outputDir);
            return;
        }
    }

    log("Alternative: use ksEditor's import feature in 3D Modeler to load KN5 files");
    logError("KN5 extraction not available — plugin not loaded or file is encrypted");
}

void FormatToolsEditorModule::onValidateClicked()
{
    QString filePath = m_validateFilePath->text();
    if (filePath.isEmpty() || filePath == "No file selected") {
        logError("No file selected for validation");
        return;
    }

    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logError(QString("Cannot open file: %1").arg(filePath));
        return;
    }

    QString content = QString::fromUtf8(file.readAll());
    file.close();

    int validateType = m_validateTypeCombo->currentIndex();
    if (validateType == 0) {
        QFileInfo info(filePath);
        QString ext = info.suffix().toLower();
        if (ext == "json") validateType = 1;
        else if (ext == "ini") validateType = 2;
        else if (ext == "lua") validateType = 3;
    }

    int lineCount = content.count('\n') + 1;
    int charCount = content.size();

    log(QString("Validating: %1").arg(filePath));
    log(QString("  Lines: %1, Characters: %2").arg(lineCount).arg(charCount));

    bool valid = false;
    QStringList errors;
    QStringList lines = content.split('\n');

    if (validateType == 1) {
        QJsonParseError parseError;
        QJsonDocument::fromJson(content.toUtf8(), &parseError);
        valid = (parseError.error == QJsonParseError::NoError);
        if (!valid) {
            errors << QString("JSON error at offset %1: %2").arg(parseError.offset).arg(parseError.errorString());
        } else {
            log("  JSON syntax: VALID");
        }
    } else if (validateType == 2) {
        valid = true;
        for (int i = 0; i < lines.size(); ++i) {
            QString trimmed = lines[i].trimmed();
            if (!trimmed.isEmpty() && !trimmed.startsWith(';') && !trimmed.startsWith('#')
                && !trimmed.startsWith('[') && !trimmed.contains('=')) {
                errors << QString("Line %1: Invalid INI syntax: %2").arg(i + 1).arg(trimmed.left(50));
                valid = false;
            }
        }
        if (valid) log("  INI syntax: VALID");
    } else if (validateType == 3) {
        // Basic Lua syntax validation: balanced keywords and parentheses
        int depthDoEnd = 0, depthIfEnd = 0, depthFunctionEnd = 0, depthParen = 0;
        QStringList keywordStack;
        for (int i = 0; i < lines.size(); ++i) {
            QString trimmed = lines[i].trimmed();
            if (trimmed.isEmpty() || trimmed.startsWith("--")) continue;
            QString lower = trimmed.toLower();

            // Remove string literals to avoid false matches
            QString stripped;
            bool inStr = false;
            for (QChar ch : lower) {
                if (ch == '"' || ch == '\'') { inStr = !inStr; continue; }
                if (!inStr) stripped += ch;
            }

            if (stripped.contains("function")) depthFunctionEnd++;
            if (stripped.contains("end")) {
                if (depthFunctionEnd > 0) depthFunctionEnd--;
                else if (depthIfEnd > 0) depthIfEnd--;
                else if (depthDoEnd > 0) depthDoEnd--;
            }
            if (stripped.contains("if") && !stripped.contains("endif")) depthIfEnd++;
            if (stripped.contains("do")) depthDoEnd++;

            for (QChar ch : lower) {
                if (ch == '(') depthParen++;
                if (ch == ')') depthParen--;
            }
            if (depthParen < 0) {
                errors << QString("Line %1: Unbalanced parentheses").arg(i + 1);
                valid = false;
            }
        }
        if (depthFunctionEnd != 0) {
            errors << QString("Unmatched 'function' (depth: %1)").arg(depthFunctionEnd);
            valid = false;
        }
        if (depthIfEnd != 0) {
            errors << QString("Unmatched 'if' (depth: %1)").arg(depthIfEnd);
            valid = false;
        }
        if (depthDoEnd != 0) {
            errors << QString("Unmatched 'do' (depth: %1)").arg(depthDoEnd);
            valid = false;
        }
        if (depthParen != 0) {
            errors << QString("Unbalanced parentheses across file (depth: %1)").arg(depthParen);
            valid = false;
        }
        log("  Lua validation: " + QString(valid ? "PASSED" : "FAILED"));
        log("  Lines: " + QString::number(lineCount));
    }

    if (!valid) {
        logError(QString("Validation FAILED with %1 error(s)").arg(errors.size()));
        for (const QString& err : errors) {
            logError("  " + err);
        }
    } else {
        log("Validation PASSED");
    }
}

void FormatToolsEditorModule::onSelectValidateFile()
{
    QString file = QFileDialog::getOpenFileName(this, "Select File to Validate",
        QString(), "All Supported (*.json *.ini *.lua);;All Files (*)");
    if (!file.isEmpty()) {
        m_validateFilePath->setText(file);
        m_validateFilePath->setStyleSheet("color: #ddd;");

        QFileInfo info(file);
        QString ext = info.suffix().toLower();
        if (ext == "json") m_validateTypeCombo->setCurrentIndex(1);
        else if (ext == "ini") m_validateTypeCombo->setCurrentIndex(2);
        else if (ext == "lua") m_validateTypeCombo->setCurrentIndex(3);
        else m_validateTypeCombo->setCurrentIndex(0);
    }
}

// ── DDS helpers ─────────────────────────────────────────────────────────
#pragma pack(push, 1)
struct DDSHeader {
    uint32_t magic = 0x20534444; // "DDS "
    uint32_t size = 124;
    uint32_t flags = 0x100F;     // DDSD_CAPS|HEIGHT|WIDTH|PIXELFORMAT|LINEARSIZE
    uint32_t height;
    uint32_t width;
    uint32_t pitchOrLinearSize;
    uint32_t depth = 0;
    uint32_t mipMapCount = 0;
    uint32_t reserved1[11] = {};
    uint32_t pfSize = 32;
    uint32_t pfFlags = 0x41;     // DDPF_RGB | DDPF_ALPHAPIXELS
    uint32_t fourCC = 0;
    uint32_t rgbBitCount = 32;
    uint32_t rBitMask = 0x00FF0000;
    uint32_t gBitMask = 0x0000FF00;
    uint32_t bBitMask = 0x000000FF;
    uint32_t aBitMask = 0xFF000000;
    uint32_t caps = 0x1000;      // DDSCAPS_TEXTURE
    uint32_t caps2 = 0;
    uint32_t caps3 = 0;
    uint32_t caps4 = 0;
    uint32_t reserved2 = 0;
};
#pragma pack(pop)

static bool writeUncompressedDDS(const QString& path, const QImage& img)
{
    QImage argb = img.convertToFormat(QImage::Format_ARGB32);
    if (argb.isNull()) return false;

    DDSHeader hdr;
    hdr.width = argb.width();
    hdr.height = argb.height();
    hdr.pitchOrLinearSize = argb.width() * argb.height() * 4;

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(reinterpret_cast<const char*>(&hdr), sizeof(hdr));

    // Write pixels as BGRA (DDS native byte order)
    for (int y = 0; y < argb.height(); ++y) {
        const QRgb* line = reinterpret_cast<const QRgb*>(argb.constScanLine(y));
        for (int x = 0; x < argb.width(); ++x) {
            QRgb px = line[x];
            uint8_t bgra[4] = { (uint8_t)qBlue(px), (uint8_t)qGreen(px), (uint8_t)qRed(px), (uint8_t)qAlpha(px) };
            f.write(reinterpret_cast<const char*>(bgra), 4);
        }
    }
    return true;
}

static QImage readUncompressedDDS(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};

    DDSHeader hdr;
    if (f.read(reinterpret_cast<char*>(&hdr), sizeof(hdr)) != sizeof(hdr))
        return {};
    if (hdr.magic != 0x20534444 || hdr.width == 0 || hdr.height == 0)
        return {};

    // Only support uncompressed 32-bit DDS
    bool isUncompressed = (hdr.fourCC == 0) && (hdr.rgbBitCount == 32);
    bool isDXT = !isUncompressed && (hdr.fourCC == 0x31545844 || hdr.fourCC == 0x33545844 ||
                                      hdr.fourCC == 0x35545844 || hdr.fourCC == 0x37545844);

    if (isDXT) {
        // Decompress BCn (S3TC) compressed DDS
        auto decodeBC1 = [](const quint8* block, int x, int y, QImage& img, int imgW, int imgH) {
            quint16 c0 = block[0] | (block[1] << 8);
            quint16 c1 = block[2] | (block[3] << 8);
            quint8 r0 = ((c0 >> 11) & 0x1F) << 3, g0 = ((c0 >> 5) & 0x3F) << 2, b0 = (c0 & 0x1F) << 3;
            quint8 r1 = ((c1 >> 11) & 0x1F) << 3, g1 = ((c1 >> 5) & 0x3F) << 2, b1 = (c1 & 0x1F) << 3;
            quint8 colors[4][3];
            colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
            colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;
            if (c0 > c1) {
                colors[2][0] = (2*r0 + r1)/3; colors[2][1] = (2*g0 + g1)/3; colors[2][2] = (2*b0 + b1)/3;
                colors[3][0] = (r0 + 2*r1)/3; colors[3][1] = (g0 + 2*g1)/3; colors[3][2] = (b0 + 2*b1)/3;
            } else {
                colors[2][0] = (r0 + r1)/2; colors[2][1] = (g0 + g1)/2; colors[2][2] = (b0 + b1)/2;
                colors[3][0] = 0; colors[3][1] = 0; colors[3][2] = 0;
            }
            quint32 indices = block[4] | (block[5] << 8) | (block[6] << 16) | (block[7] << 24);
            for (int j = 0; j < 4; ++j) {
                for (int i = 0; i < 4; ++i) {
                    int px = x + i, py = y + j;
                    if (px < imgW && py < imgH) {
                        int idx = (indices >> (2 * (j*4 + i))) & 3;
                        img.setPixel(px, py, qRgba(colors[idx][0], colors[idx][1], colors[idx][2], (idx == 3 && c0 <= c1) ? 0 : 255));
                    }
                }
            }
        };
        auto decodeBC3 = [](const quint8* block, int x, int y, QImage& img, int imgW, int imgH) {
            quint8 alpha[8];
            alpha[0] = block[0]; alpha[1] = block[1];
            if (alpha[0] > alpha[1]) {
                for (int k = 2; k < 8; ++k) alpha[k] = ((8-k)*alpha[0] + (k-1)*alpha[1]) / 7;
            } else {
                for (int k = 2; k < 6; ++k) alpha[k] = ((6-k)*alpha[0] + (k-1)*alpha[1]) / 5;
                alpha[6] = 0; alpha[7] = 255;
            }
            quint64 alphaBits = 0;
            for (int i = 0; i < 6; ++i) alphaBits |= (static_cast<quint64>(block[2+i]) << (i*8));
            // BC3 color = BC1 with 4-color mode
            quint16 c0 = block[8] | (block[9] << 8);
            quint16 c1 = block[10] | (block[11] << 8);
            quint8 r0 = ((c0 >> 11) & 0x1F) << 3, g0 = ((c0 >> 5) & 0x3F) << 2, b0 = (c0 & 0x1F) << 3;
            quint8 r1 = ((c1 >> 11) & 0x1F) << 3, g1 = ((c1 >> 5) & 0x3F) << 2, b1 = (c1 & 0x1F) << 3;
            quint8 colors[4][3];
            colors[0][0] = r0; colors[0][1] = g0; colors[0][2] = b0;
            colors[1][0] = r1; colors[1][1] = g1; colors[1][2] = b1;
            colors[2][0] = (2*r0 + r1)/3; colors[2][1] = (2*g0 + g1)/3; colors[2][2] = (2*b0 + b1)/3;
            colors[3][0] = (r0 + 2*r1)/3; colors[3][1] = (g0 + 2*g1)/3; colors[3][2] = (b0 + 2*b1)/3;
            quint32 colorIdx = block[12] | (block[13] << 8) | (block[14] << 16) | (block[15] << 24);
            for (int j = 0; j < 4; ++j) {
                for (int i = 0; i < 4; ++i) {
                    int px = x + i, py = y + j;
                    if (px < imgW && py < imgH) {
                        int ai = (alphaBits >> (3 * (j*4 + i))) & 7;
                        int ci = (colorIdx >> (2 * (j*4 + i))) & 3;
                        img.setPixel(px, py, qRgba(colors[ci][0], colors[ci][1], colors[ci][2], alpha[ai]));
                    }
                }
            }
        };

        uint32_t blockW = qMax(1u, (hdr.width + 3) / 4);
        uint32_t blockH = qMax(1u, (hdr.height + 3) / 4);
        QImage img(hdr.width, hdr.height, QImage::Format_ARGB32);
        uint32_t blockSize = (hdr.fourCC == 0x31545844) ? 8 : 16; // BC1=8, BC3/BC5/BC7=16
        for (uint32_t by = 0; by < blockH; ++by) {
            for (uint32_t bx = 0; bx < blockW; ++bx) {
                quint8 block[16];
                if (f.read(reinterpret_cast<char*>(block), blockSize) != blockSize) return {};
                if (blockSize < 16) memset(block + 8, 0, 8);
                int x = bx * 4, y = by * 4;
                switch (hdr.fourCC) {
                    case 0x31545844: decodeBC1(block, x, y, img, hdr.width, hdr.height); break; // DXT1/BC1
                    case 0x33545844: decodeBC3(block, x, y, img, hdr.width, hdr.height); break; // DXT3/BC2
                    case 0x35545844: // DXT5/BC3
                    case 0x37545844: decodeBC3(block, x, y, img, hdr.width, hdr.height); break; // BC7 (approximate as BC3)
                    default: break;
                }
            }
        }
        return img;
    }
    if (!isUncompressed) return {};

    QImage img(hdr.width, hdr.height, QImage::Format_ARGB32);
    for (uint32_t y = 0; y < hdr.height; ++y) {
        QRgb* line = reinterpret_cast<QRgb*>(img.scanLine(y));
        for (uint32_t x = 0; x < hdr.width; ++x) {
            uint8_t bgra[4];
            if (f.read(reinterpret_cast<char*>(bgra), 4) != 4) return {};
            line[x] = qRgba(bgra[2], bgra[1], bgra[0], bgra[3]);
        }
    }
    return img;
}

void FormatToolsEditorModule::onDdsToPngClicked()
{
    QString inputPath = m_textureFilePath->text();
    if (inputPath.isEmpty() || inputPath == "No file selected") {
        logError("No texture file selected");
        return;
    }

    QFileInfo info(inputPath);
    if (info.suffix().toLower() != "dds") {
        logError("Selected file is not a DDS texture");
        return;
    }

    QString outputPath = m_textureOutputPath->text();
    if (outputPath.isEmpty() || outputPath == "Same as input (auto)" || outputPath == "No output directory set") {
        outputPath = info.absolutePath();
    }

    if (QFileInfo(outputPath).isDir()) {
        outputPath = QDir(outputPath).filePath(info.completeBaseName() + ".png");
    }

    log(QString("Converting DDS → PNG: %1").arg(inputPath));
    log(QString("Output: %1").arg(outputPath));

    QImage img = readUncompressedDDS(inputPath);
    if (!img.isNull()) {
        if (img.save(outputPath, "PNG")) {
            log(QString("DDS → PNG conversion completed: %1x%2").arg(img.width()).arg(img.height()));
        } else {
            logError("Failed to save PNG");
        }
        return;
    }

    // Fallback: try Qt's image loader (in case user has a DDS plugin)
    img = QImage(inputPath);
    if (!img.isNull()) {
        if (img.save(outputPath, "PNG")) {
            log(QString("DDS → PNG conversion completed (via Qt image loader): %1x%2").arg(img.width()).arg(img.height()));
            return;
        }
    }

    log("DDS file is in an unrecognised format (not BC1/BC3/BC7 or uncompressed)");
    log("Try: 7-Zip to extract, then convert with Paint.NET, GIMP, or texconv");
    logError("DDS → PNG conversion failed — unsupported DDS format");
}

void FormatToolsEditorModule::onPngToDdsClicked()
{
    QString inputPath = m_textureFilePath->text();
    if (inputPath.isEmpty() || inputPath == "No file selected") {
        logError("No texture file selected");
        return;
    }

    QFileInfo info(inputPath);
    if (info.suffix().toLower() != "png") {
        logError("Selected file is not a PNG texture");
        return;
    }

    QString outputPath = m_textureOutputPath->text();
    if (outputPath.isEmpty() || outputPath == "Same as input (auto)" || outputPath == "No output directory set") {
        outputPath = info.absolutePath();
    }

    if (QFileInfo(outputPath).isDir()) {
        outputPath = QDir(outputPath).filePath(info.completeBaseName() + ".dds");
    }

    log(QString("Converting PNG → DDS: %1").arg(inputPath));
    log(QString("Output: %1").arg(outputPath));

    QImage img(inputPath);
    if (img.isNull()) {
        logError("Failed to load source PNG");
        return;
    }

    if (writeUncompressedDDS(outputPath, img)) {
        log(QString("PNG → uncompressed DDS conversion completed: %1x%2").arg(img.width()).arg(img.height()));
        log("Note: Use TexConv or NVTT for BC7/BC3 compressed DDS output");
    } else {
        logError("Failed to write DDS file");
    }
}

void FormatToolsEditorModule::onSelectKn5File()
{
    QString file = QFileDialog::getOpenFileName(this, "Select KN5 File",
        QString(), "KN5 Archives (*.kn5);;All Files (*)");
    if (!file.isEmpty()) {
        m_kn5FilePath->setText(file);
        m_kn5FilePath->setStyleSheet("color: #ddd;");
    }
}

void FormatToolsEditorModule::onSelectTextureFile()
{
    QString file = QFileDialog::getOpenFileName(this, "Select Texture File",
        QString(), "Textures (*.dds *.png *.tga *.bmp *.jpg);;All Files (*)");
    if (!file.isEmpty()) {
        m_textureFilePath->setText(file);
        m_textureFilePath->setStyleSheet("color: #ddd;");

        QFileInfo info(file);
        QString ext = info.suffix().toLower();
        if (ext == "dds") {
            m_textureOutputPath->setText(info.absolutePath());
            m_textureOutputPath->setStyleSheet("color: #ddd;");
        } else if (ext == "png") {
            m_textureOutputPath->setText(info.absolutePath());
            m_textureOutputPath->setStyleSheet("color: #ddd;");
        }
    }
}

QStringList FormatToolsEditorModule::collectFiles(const QStringList& extensions)
{
    QStringList result;
    for (int i = 0; i < m_fileList->count(); ++i) {
        QString path = m_fileList->item(i)->data(Qt::UserRole).toString();
        QFileInfo info(path);
        if (extensions.isEmpty() || extensions.contains(info.suffix().toLower())) {
            result.append(path);
        }
    }
    return result;
}

void FormatToolsEditorModule::log(const QString& msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logOutput->append(QString("[%1] %2").arg(timestamp, msg));
    LOG_INFO("FormatToolsEditorModule", msg);
}

void FormatToolsEditorModule::logError(const QString& msg)
{
    QString timestamp = QDateTime::currentDateTime().toString("HH:mm:ss");
    m_logOutput->append(QString("[%1] <span style='color:#ff6b6b;'>ERROR: %2</span>").arg(timestamp, msg));
    LOG_ERROR("FormatToolsEditorModule", msg);
}

} // namespace ks
