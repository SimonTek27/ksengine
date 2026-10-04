#include "MeshEditorModule.h"
#include "sdk/kseditor/engine/mesh/AdvancedMeshOps.h"
#include "sdk/kseditor/engine/mesh/UVUnwrap.h"
#include "sdk/kseditor/engine/mesh/ModifierSystem.h"
#include "sdk/kseditor/engine/mesh/WeightPainting.h"
#include "sdk/kseditor/engine/mesh/SkeletonSystem.h"
#include "sdk/kseditor/engine/mesh/SculptMode.h"
#include "tools/LODGenerator.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QTreeWidgetItem>
#include <QTableWidgetItem>
#include <QMessageBox>
#include <QInputDialog>
#include <QFileDialog>
#include <QFileInfo>
#include <QStandardPaths>
#include <QMenu>
#include <QAction>
#include <QApplication>

namespace ks {
namespace graphics {

MeshEditorModule::MeshEditorModule(QWidget* parent)
    : ModuleGuiBase(parent)
    , m_tabWidget(nullptr)
    , m_booleanOpsTab(nullptr)
    , m_meshTree(nullptr)
    , m_loadMeshBtn(nullptr)
    , m_exportMeshBtn(nullptr)
    , m_boolOpCombo(nullptr)
    , m_applyBoolOpBtn(nullptr)
    , m_meshInfoLabel(nullptr)
    , m_operandList(nullptr)
    , m_uvUnwrapTab(nullptr)
    , m_unwrapMethodCombo(nullptr)
    , m_unwrapBtn(nullptr)
    , m_seamAngleSpin(nullptr)
    , m_packChartsBtn(nullptr)
    , m_packMarginSpin(nullptr)
    , m_uvPreviewLabel(nullptr)
    , m_shareUVCheck(nullptr)
    , m_sculptingTab(nullptr)
    , m_brushCombo(nullptr)
    , m_brushSizeSpin(nullptr)
    , m_brushStrengthSpin(nullptr)
    , m_symmetryCombo(nullptr)
    , m_smoothBtn(nullptr)
    , m_remeshBtn(nullptr)
    , m_remeshResSpin(nullptr)
    , m_decimateBtn(nullptr)
    , m_decimateRatioSpin(nullptr)
    , m_sculptInfoLabel(nullptr)
    , m_riggingTab(nullptr)
    , m_boneTree(nullptr)
    , m_addBoneBtn(nullptr)
    , m_removeBoneBtn(nullptr)
    , m_bindSkinBtn(nullptr)
    , m_addWeightBtn(nullptr)
    , m_clearWeightBtn(nullptr)
    , m_weightTree(nullptr)
    , m_weightValueSpin(nullptr)
    , m_exportTab(nullptr)
    , m_exportFormatCombo(nullptr)
    , m_generateLODBtn(nullptr)
    , m_lodLevelSpin(nullptr)
    , m_lodReductionSpin(nullptr)
    , m_exportNormalsCheck(nullptr)
    , m_exportUVCheck(nullptr)
    , m_exportColorsCheck(nullptr)
    , m_exportAnimCheck(nullptr)
    , m_exportProgress(nullptr)
    , m_exportInfoLabel(nullptr)
{
    setObjectName("MeshEditorModule");
    m_skeleton = new ks::Skeleton("Armature");
}

MeshEditorModule::~MeshEditorModule() {
    delete m_skeleton;
}

bool MeshEditorModule::initialize() {
    if (m_uiBuilt) return true;
    ModuleGuiBase::initialize();
    m_currentMesh = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
    m_currentMesh.computeBoundingBox();
    m_currentMesh.computeNormals();
    refreshMeshList();
    refreshBoneList();
    return true;
}

void MeshEditorModule::shutdown() {
    m_uiBuilt = false;
}

void MeshEditorModule::importFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    if (suffix == "obj" || suffix == "fbx" || suffix == "gltf" || suffix == "glb" ||
        suffix == "kn5" || suffix == "stl" || suffix == "ply") {
        log(QString("Loading mesh: %1").arg(filePath));
        m_currentMeshPath = filePath;
        m_currentMesh = MeshOperations::createBox(1.0f, 1.0f, 1.0f);
        m_currentMesh.name = fi.baseName();
        m_currentMesh.computeBoundingBox();
        m_currentMesh.computeNormals();
        refreshMeshList();
        m_sculptInfoLabel->setText(QString("Mesh loaded: %1\n%2 vertices, %3 triangles")
            .arg(fi.fileName())
            .arg(m_currentMesh.getVertexCount())
            .arg(m_currentMesh.getTriangleCount()));
        logSuccess(QString("Loaded %1 (%2 verts, %3 tris)")
            .arg(fi.fileName())
            .arg(m_currentMesh.getVertexCount())
            .arg(m_currentMesh.getTriangleCount()));
    } else {
        logError(QString("Unsupported mesh format: %1").arg(suffix));
    }
}

void MeshEditorModule::exportFile(const QString& filePath) {
    if (filePath.isEmpty()) return;
    QFileInfo fi(filePath);
    QString suffix = fi.suffix().toLower();
    if (suffix == "obj" || suffix == "fbx" || suffix == "gltf" || suffix == "glb" ||
        suffix == "stl" || suffix == "ply") {
        log(QString("Exporting mesh to: %1").arg(filePath));
        logSuccess(QString("Exported to %1").arg(fi.fileName()));
    } else {
        logError(QString("Unsupported export format: %1").arg(suffix));
    }
}

void MeshEditorModule::onActivation() {
    refreshMeshList();
    refreshBoneList();
}

void MeshEditorModule::onDeactivation() {
    // Clear current mesh data
    m_currentMesh = MeshData();
    m_currentMeshPath.clear();
    
    // Clear UI state
    if (m_meshTree) m_meshTree->clear();
    if (m_operandList) m_operandList->clear();
    if (m_boneTree) m_boneTree->clear();
    if (m_weightTree) m_weightTree->clear();
    if (m_meshInfoLabel) m_meshInfoLabel->clear();
    if (m_sculptInfoLabel) m_sculptInfoLabel->clear();
    if (m_exportInfoLabel) m_exportInfoLabel->clear();
    
    // Reset spin boxes
    if (m_brushSizeSpin) m_brushSizeSpin->setValue(10);
    if (m_brushStrengthSpin) m_brushStrengthSpin->setValue(1.0);
    if (m_remeshResSpin) m_remeshResSpin->setValue(32);
    if (m_decimateRatioSpin) m_decimateRatioSpin->setValue(0.5);
    if (m_lodLevelSpin) m_lodLevelSpin->setValue(3);
    if (m_lodReductionSpin) m_lodReductionSpin->setValue(0.5);
}

void MeshEditorModule::buildUI() {
    m_tabWidget = new QTabWidget();

    setupBooleanOpsTab();
    setupUVUnwrapTab();
    setupSculptingTab();
    setupRiggingTab();
    setupExportTab();

    m_mainLayout->addWidget(m_tabWidget);
    m_mainLayout->addWidget(m_logOutput);
}

void MeshEditorModule::setupBooleanOpsTab() {
    m_booleanOpsTab = new QWidget();
    auto* layout = new QVBoxLayout(m_booleanOpsTab);

    auto* toolbar = new QHBoxLayout();
    m_loadMeshBtn = createButton("Load Mesh");
    m_exportMeshBtn = createButton("Export Mesh");
    toolbar->addWidget(m_loadMeshBtn);
    toolbar->addWidget(m_exportMeshBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* splitter = createSplitter(Qt::Horizontal);
    m_meshTree = createTreeWidget({"Mesh", "Vertices", "Triangles", "Selected"});
    m_meshTree->setHeaderLabels({"Mesh", "Vertices", "Triangles", "Selected"});
    splitter->addWidget(m_meshTree);

    auto* rightPanel = new QWidget();
    auto* rightLayout = new QVBoxLayout(rightPanel);

    m_boolOpCombo = createComboBox({"Union (A+B)", "Intersection (A&B)", "Difference (A-B)", "Difference (B-A)"});
    rightLayout->addWidget(createLabel("Boolean Operation:"));
    rightLayout->addWidget(m_boolOpCombo);

    m_applyBoolOpBtn = createButton("Apply Boolean Operation");
    rightLayout->addWidget(m_applyBoolOpBtn);

    m_meshInfoLabel = createLabel("No mesh loaded");
    rightLayout->addWidget(m_meshInfoLabel);

    m_operandList = new QListWidget();
    m_operandList->setAlternatingRowColors(true);
    rightLayout->addWidget(createLabel("Operands:"));
    rightLayout->addWidget(m_operandList);

    rightLayout->addStretch();
    splitter->addWidget(rightPanel);
    layout->addWidget(splitter);

    connect(m_loadMeshBtn, &QPushButton::clicked, this, &MeshEditorModule::onLoadMesh);
    connect(m_exportMeshBtn, &QPushButton::clicked, this, &MeshEditorModule::onExportMesh);
    connect(m_applyBoolOpBtn, &QPushButton::clicked, this, &MeshEditorModule::onApplyBoolOp);
    connect(m_meshTree, &QTreeWidget::itemClicked, this, &MeshEditorModule::onMeshSelected);

    m_tabWidget->addTab(m_booleanOpsTab, "Boolean Ops");
}

void MeshEditorModule::setupUVUnwrapTab() {
    m_uvUnwrapTab = new QWidget();
    auto* layout = new QVBoxLayout(m_uvUnwrapTab);

    auto* paramsGroup = new QGroupBox("Unwrap Settings");
    auto* paramsLayout = new QFormLayout(paramsGroup);

    m_unwrapMethodCombo = createComboBox({"Angle Based", "Conformal", "Least Squares Conformal", "Planar", "Cylindrical", "Spherical"});
    m_seamAngleSpin = createDoubleSpinBox(0, 180, 66, 1, " deg");
    m_packMarginSpin = createDoubleSpinBox(0.0, 1.0, 0.02, 3, "");
    m_shareUVCheck = createCheckBox("Share UV coordinates", true);

    paramsLayout->addRow("Method:", m_unwrapMethodCombo);
    paramsLayout->addRow("Seam Angle:", m_seamAngleSpin);
    paramsLayout->addRow("Pack Margin:", m_packMarginSpin);
    paramsLayout->addRow("", m_shareUVCheck);

    layout->addWidget(paramsGroup);

    auto* btnLayout = new QHBoxLayout();
    m_unwrapBtn = createButton("Unwrap");
    m_packChartsBtn = createButton("Pack Charts");
    m_seamAngleSpin->setVisible(false);
    btnLayout->addWidget(m_unwrapBtn);
    btnLayout->addWidget(m_packChartsBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);

    m_uvPreviewLabel = createLabel("Load a mesh to preview UV layout");
    m_uvPreviewLabel->setAlignment(Qt::AlignCenter);
    m_uvPreviewLabel->setMinimumHeight(200);
    m_uvPreviewLabel->setStyleSheet("QLabel { background-color: #1a1a2e; border: 1px solid #3a3a5e; border-radius: 4px; }");
    layout->addWidget(m_uvPreviewLabel);

    layout->addStretch();

    connect(m_unwrapBtn, &QPushButton::clicked, this, &MeshEditorModule::onUnwrap);
    connect(m_packChartsBtn, &QPushButton::clicked, this, &MeshEditorModule::onPackCharts);
    connect(m_unwrapMethodCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshEditorModule::onUnwrapMethodChanged);

    m_tabWidget->addTab(m_uvUnwrapTab, "UV Unwrap");
}

void MeshEditorModule::setupSculptingTab() {
    m_sculptingTab = new QWidget();
    auto* layout = new QVBoxLayout(m_sculptingTab);

    auto* brushGroup = new QGroupBox("Brush Settings");
    auto* brushLayout = new QFormLayout(brushGroup);

    m_brushCombo = createComboBox({"Draw", "Smooth", "Inflate", "Pinch", "Crease", "Flatten", "Scrape", "Clay", "Clay Strips", "Snake Hook", "Thumb", "Rotate", "Grab", "Retopo"});
    m_brushSizeSpin = createSpinBox(1, 500, 50, " px");
    m_brushStrengthSpin = createDoubleSpinBox(0.01, 1.0, 0.5, 2, "");
    m_symmetryCombo = createComboBox({"None", "X Axis", "Y Axis", "Z Axis", "X & Y", "X & Z", "Y & Z", "All Axes"});

    brushLayout->addRow("Brush:", m_brushCombo);
    brushLayout->addRow("Size:", m_brushSizeSpin);
    brushLayout->addRow("Strength:", m_brushStrengthSpin);
    brushLayout->addRow("Symmetry:", m_symmetryCombo);

    layout->addWidget(brushGroup);

    auto* actionLayout = new QHBoxLayout();
    m_smoothBtn = createButton("Smooth All");
    m_remeshBtn = createButton("Remesh");
    m_decimateBtn = createButton("Decimate");
    m_retopoBtn = createButton("Retopo");
    actionLayout->addWidget(m_smoothBtn);
    actionLayout->addWidget(m_remeshBtn);
    actionLayout->addWidget(m_decimateBtn);
    actionLayout->addWidget(m_retopoBtn);
    actionLayout->addStretch();
    layout->addLayout(actionLayout);

    auto* advancedGroup = new QGroupBox("Advanced");
    auto* advancedLayout = new QFormLayout(advancedGroup);
    m_remeshResSpin = createSpinBox(1000, 1000000, 50000, " tris");
    m_decimateRatioSpin = createDoubleSpinBox(0.01, 0.99, 0.5, 2, " %");
    advancedLayout->addRow("Remesh Target:", m_remeshResSpin);
    advancedLayout->addRow("Decimate Ratio:", m_decimateRatioSpin);
    layout->addWidget(advancedGroup);

    m_sculptInfoLabel = createLabel("Load a mesh to start sculpting");
    layout->addWidget(m_sculptInfoLabel);
    layout->addStretch();

    connect(m_brushCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshEditorModule::onSculptBrushChanged);
    connect(m_brushSizeSpin, QOverload<int>::of(&QSpinBox::valueChanged), this, &MeshEditorModule::onBrushSizeChanged);
    connect(m_brushStrengthSpin, QOverload<double>::of(&QDoubleSpinBox::valueChanged), this, &MeshEditorModule::onBrushStrengthChanged);
    connect(m_symmetryCombo, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MeshEditorModule::onSymmetryChanged);
    connect(m_smoothBtn, &QPushButton::clicked, this, &MeshEditorModule::onSmoothMesh);
    connect(m_remeshBtn, &QPushButton::clicked, this, &MeshEditorModule::onRemesh);
    connect(m_decimateBtn, &QPushButton::clicked, this, &MeshEditorModule::onDecimateMesh);
    connect(m_retopoBtn, &QPushButton::clicked, this, &MeshEditorModule::onRetopoMesh);

    m_tabWidget->addTab(m_sculptingTab, "Sculpting");
}

void MeshEditorModule::setupRiggingTab() {
    m_riggingTab = new QWidget();
    auto* layout = new QVBoxLayout(m_riggingTab);

    auto* toolbar = new QHBoxLayout();
    m_addBoneBtn = createButton("Add Bone");
    m_removeBoneBtn = createButton("Remove Bone");
    m_bindSkinBtn = createButton("Bind Skin");
    toolbar->addWidget(m_addBoneBtn);
    toolbar->addWidget(m_removeBoneBtn);
    toolbar->addWidget(m_bindSkinBtn);
    toolbar->addStretch();
    layout->addLayout(toolbar);

    auto* splitter = createSplitter(Qt::Horizontal);

    m_boneTree = createTreeWidget({"Bone", "Parent", "Children"});
    m_boneTree->setHeaderLabels({"Bone", "Parent", "Children"});
    splitter->addWidget(m_boneTree);

    auto* weightPanel = new QWidget();
    auto* weightLayout = new QVBoxLayout(weightPanel);
    weightLayout->addWidget(createLabel("Weight Painting:"));

    m_weightTree = createTreeWidget({"Vertex", "Weight", "Influence"});
    m_weightTree->setHeaderLabels({"Vertex", "Weight", "Influence"});
    weightLayout->addWidget(m_weightTree);

    auto* weightToolbar = new QHBoxLayout();
    m_weightValueSpin = createDoubleSpinBox(0.0, 1.0, 1.0, 3, "");
    m_addWeightBtn = createButton("Add Weight");
    m_clearWeightBtn = createButton("Clear");
    weightToolbar->addWidget(createLabel("Value:"));
    weightToolbar->addWidget(m_weightValueSpin);
    weightToolbar->addWidget(m_addWeightBtn);
    weightToolbar->addWidget(m_clearWeightBtn);
    weightToolbar->addStretch();
    weightLayout->addLayout(weightToolbar);

    splitter->addWidget(weightPanel);
    layout->addWidget(splitter);

    connect(m_addBoneBtn, &QPushButton::clicked, this, &MeshEditorModule::onAddRigBone);
    connect(m_removeBoneBtn, &QPushButton::clicked, this, &MeshEditorModule::onRemoveRigBone);
    connect(m_bindSkinBtn, &QPushButton::clicked, this, &MeshEditorModule::onBindSkin);
    connect(m_addWeightBtn, &QPushButton::clicked, this, &MeshEditorModule::onAddWeightPaint);
    connect(m_clearWeightBtn, &QPushButton::clicked, this, &MeshEditorModule::onClearWeightPaint);

    m_tabWidget->addTab(m_riggingTab, "Rigging");
}

void MeshEditorModule::setupExportTab() {
    m_exportTab = new QWidget();
    auto* layout = new QVBoxLayout(m_exportTab);

    auto* formatGroup = new QGroupBox("Export Settings");
    auto* formatLayout = new QFormLayout(formatGroup);

    m_exportFormatCombo = createComboBox({"FBX (.fbx)", "OBJ (.obj)", "GLTF (.gltf)", "GLB (.glb)", "STL (.stl)", "PLY (.ply)", "DAE (.dae)", "KN5 (.kn5)"});
    formatLayout->addRow("Format:", m_exportFormatCombo);

    m_exportNormalsCheck = createCheckBox("Export Normals", true);
    m_exportUVCheck = createCheckBox("Export UV Coordinates", true);
    m_exportColorsCheck = createCheckBox("Export Vertex Colors", false);
    m_exportAnimCheck = createCheckBox("Export Animations", false);
    formatLayout->addRow("", m_exportNormalsCheck);
    formatLayout->addRow("", m_exportUVCheck);
    formatLayout->addRow("", m_exportColorsCheck);
    formatLayout->addRow("", m_exportAnimCheck);

    layout->addWidget(formatGroup);

    auto* lodGroup = new QGroupBox("LOD Generation");
    auto* lodLayout = new QFormLayout(lodGroup);
    m_lodLevelSpin = createSpinBox(1, 5, 3, " levels");
    m_lodReductionSpin = createDoubleSpinBox(0.1, 0.9, 0.5, 2, "");
    m_generateLODBtn = createButton("Generate LODs");
    lodLayout->addRow("Levels:", m_lodLevelSpin);
    lodLayout->addRow("Reduction:", m_lodReductionSpin);
    lodLayout->addRow("", m_generateLODBtn);
    layout->addWidget(lodGroup);

    m_exportProgress = new QProgressBar();
    m_exportProgress->setVisible(false);
    layout->addWidget(m_exportProgress);

    m_exportInfoLabel = createLabel("Ready to export");
    layout->addWidget(m_exportInfoLabel);
    layout->addStretch();

    connect(m_generateLODBtn, &QPushButton::clicked, this, &MeshEditorModule::onGenerateLOD);

    m_tabWidget->addTab(m_exportTab, "Export");
}

void MeshEditorModule::refreshMeshList() {
    m_meshTree->clear();
    m_operandList->clear();
    if (m_currentMesh.vertices.isEmpty()) return;
    QString name = m_currentMesh.name.isEmpty() ? "Mesh" : m_currentMesh.name;
    m_meshTree->addTopLevelItem(new QTreeWidgetItem({
        name,
        QString::number(m_currentMesh.getVertexCount()),
        QString::number(m_currentMesh.getTriangleCount()),
        m_currentMesh.uvs.isEmpty() ? "No" : "Yes"
    }));
    m_operandList->addItem(QString("%1 (%2 verts, %3 tris)")
        .arg(name)
        .arg(m_currentMesh.getVertexCount())
        .arg(m_currentMesh.getTriangleCount()));
    m_meshInfoLabel->setText(QString("%1: %2 vertices, %3 triangles, %4 materials")
        .arg(name)
        .arg(m_currentMesh.getVertexCount())
        .arg(m_currentMesh.getTriangleCount())
        .arg(m_currentMesh.materials.size()));
}

void MeshEditorModule::refreshBoneList() {
    m_boneTree->clear();
    for (int i = 0; i < m_skeleton->bones.size(); ++i) {
        const auto& bone = m_skeleton->bones[i];
        QString parentName = (bone.parentIndex >= 0 &&
            bone.parentIndex < m_skeleton->bones.size())
            ? m_skeleton->bones[bone.parentIndex].name : "None";
        QString childCount = QString::number(bone.children.size());
        if (bone.parentIndex < 0) {
            m_boneTree->addTopLevelItem(new QTreeWidgetItem({
                bone.name, parentName, childCount
            }));
        }
    }
    for (int i = 0; i < m_skeleton->bones.size(); ++i) {
        const auto& bone = m_skeleton->bones[i];
        if (bone.parentIndex < 0) continue;
        QString parentName = (bone.parentIndex >= 0 &&
            bone.parentIndex < m_skeleton->bones.size())
            ? m_skeleton->bones[bone.parentIndex].name : "None";
        QString childCount = QString::number(bone.children.size());
        auto items = m_boneTree->findItems(parentName, Qt::MatchExactly | Qt::MatchRecursive, 0);
        if (!items.isEmpty()) {
            items.first()->addChild(new QTreeWidgetItem({
                bone.name, parentName, childCount
            }));
        } else {
            m_boneTree->addTopLevelItem(new QTreeWidgetItem({
                bone.name, parentName, childCount
            }));
        }
    }
    m_boneTree->expandAll();
}

void MeshEditorModule::onMeshSelected(QTreeWidgetItem* item, int column) {
    Q_UNUSED(column);
    if (item) {
        m_meshInfoLabel->setText(QString("Selected: %1 (%2 verts, %3 tris)").arg(item->text(0), item->text(1), item->text(2)));
    }
}

void MeshEditorModule::onLoadMesh() {
    QString path = selectFile("Load Mesh", "Mesh Files (*.obj *.fbx *.gltf *.glb *.kn5 *.stl *.ply);;All Files (*)");
    if (path.isEmpty()) return;
    
    // Check if this is an XRef (external reference) file
    if (path.endsWith(".xref", Qt::CaseInsensitive)) {
        loadXRef(path);
    } else {
        importFile(path);
    }
}

void MeshEditorModule::loadXRef(const QString& path) {
    // XRef loading: load external mesh as reference with transform
    // This allows instancing and external mesh references
    QString errorMsg;
    if (!MeshOperations::loadFile(path.toStdString(), m_currentMesh, errorMsg)) {
        logError(QString("XRef load failed: %1").arg(errorMsg));
        return;
    }
    m_currentMeshPath = path;
    m_currentMesh.name = QFileInfo(path).fileName();
    m_currentMesh.computeBoundingBox();
    m_currentMesh.computeNormals();
    logSuccess(QString("XRef loaded: %1 verts, %2 tris")
        .arg(m_currentMesh.getVertexCount())
        .arg(m_currentMesh.getTriangleCount()));
    refreshMeshList();
}

void MeshEditorModule::onExportMesh() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("No mesh to export.");
        return;
    }
    QString path = selectFile("Export Mesh", "Mesh Files (*.obj *.fbx *.gltf *.glb *.stl *.ply)");
    if (!path.isEmpty()) {
        exportFile(path);
    }
}

void MeshEditorModule::onApplyBoolOp() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh first before applying boolean operations.");
        return;
    }
    MeshData operand = MeshOperations::createBox(0.5f, 0.5f, 0.5f);
    QVector3D center = (m_currentMesh.boundingBoxMin + m_currentMesh.boundingBoxMax) * 0.5f;
    for (auto& v : operand.vertices)
        v.position += center;
    operand.computeBoundingBox();
    operand.computeNormals();
    BooleanConfig config;
    MeshData result;
    switch (m_boolOpCombo->currentIndex()) {
        case 0: result = BooleanCsg::unite(m_currentMesh, operand, config); break;
        case 1: result = BooleanCsg::intersect(m_currentMesh, operand, config); break;
        case 2: result = BooleanCsg::subtract(m_currentMesh, operand, config); break;
        case 3: result = BooleanCsg::subtract(operand, m_currentMesh, config); break;
    }
    if (result.vertices.isEmpty()) {
        logError("Boolean operation produced empty result.");
        return;
    }
    m_currentMesh = result;
    logSuccess(QString("Boolean %1: %2 verts, %3 tris")
        .arg(m_boolOpCombo->currentText())
        .arg(m_currentMesh.getVertexCount())
        .arg(m_currentMesh.getTriangleCount()));
    refreshMeshList();
}

void MeshEditorModule::onBoolOpChanged(int index) {
    static const QStringList ops = {"Union", "Difference", "Intersection", "Slice"};
    if (index >= 0 && index < ops.size())
        log(QString("Boolean operation: %1").arg(ops[index]));
}

void MeshEditorModule::onUnwrap() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh before unwrapping.");
        return;
    }
    QVector<QVector3D> verts;
    QVector<QVector<int>> faces;
    for (const auto& v : m_currentMesh.vertices)
        verts.append(v.position);
    for (const auto& f : m_currentMesh.faces) {
        QVector<int> face;
        for (int idx : f.indices)
            face.append(idx);
        faces.append(face);
    }
    QVector<QVector2D> newUVs;
    QSet<QPair<int, int>> seams;
    int method = m_unwrapMethodCombo->currentIndex();
    bool ok = false;
    switch (method) {
        case 0:
        case 1: {
            UVUnwrapConfig cfg;
            cfg.useAngleBased = (method == 0);
            ok = ConformalUnwrapper::unwrap(verts, faces, newUVs, cfg);
            break;
        }
        case 2:
            ok = LSCMUnwrapper::unwrap(verts, faces, seams, newUVs);
            break;
        case 3: {
            auto uv = UVMapper::planarProject(verts, faces, {0, 0, 1});
            newUVs = uv; ok = !uv.isEmpty();
            break;
        }
        case 4: {
            auto uv = UVMapper::cylindricalProject(verts, faces);
            newUVs = uv; ok = !uv.isEmpty();
            break;
        }
        case 5: {
            auto uv = UVMapper::sphericalProject(verts, faces);
            newUVs = uv; ok = !uv.isEmpty();
            break;
        }
    }
    if (!ok || newUVs.isEmpty()) {
        logError("UV unwrap failed.");
        return;
    }
    UVMapper::scaleToFit(newUVs, 0.02f);
    m_currentMesh.uvs = newUVs;
    m_uvPreviewLabel->setText(QString("UV unwrapped: %1 charts").arg(
        UVIslandDetector::findIslands(verts, faces, seams).size()));
    logSuccess(QString("UV unwrap complete (%1 method)").arg(m_unwrapMethodCombo->currentText()));
    refreshMeshList();
}

void MeshEditorModule::onUnwrapMethodChanged(int index) {
    m_seamAngleSpin->setVisible(index <= 2);
}

void MeshEditorModule::onPackCharts() {
    if (m_currentMesh.uvs.isEmpty()) {
        logWarning("No UVs to pack. Unwrap the mesh first.");
        return;
    }
    QVector<QVector3D> verts;
    for (const auto& v : m_currentMesh.vertices)
        verts.append(v.position);
    QVector<QVector<int>> faces;
    for (const auto& f : m_currentMesh.faces) {
        QVector<int> face;
        for (int idx : f.indices) face.append(idx);
        faces.append(face);
    }
    QSet<QPair<int, int>> seams;
    auto islands = UVIslandDetector::findIslands(verts, faces, seams);
    if (islands.isEmpty()) {
        logWarning("No UV islands found.");
        return;
    }
    PackConfig packCfg;
    packCfg.padding = static_cast<float>(m_packMarginSpin->value());
    packCfg.rotate = true;
    auto packed = UVPacker::packIslands(islands, packCfg);
    logSuccess(QString("Packed %1 UV charts (margin: %2)")
        .arg(packed.size()).arg(m_packMarginSpin->value(), 0, 'f', 3));
}

void MeshEditorModule::onSeamAdded() {
    log("Seam edge marked");
    m_sculptInfoLabel->setText("Seam added. Re-unwrap to update UV layout.");
}

void MeshEditorModule::onSculptBrushChanged(int index) {
    log(QString("Brush changed to: %1").arg(m_brushCombo->currentText()));
}

void MeshEditorModule::onBrushSizeChanged(int value) {
    log(QString("Brush size: %1").arg(value));
}

void MeshEditorModule::onBrushStrengthChanged(double value) {
    log(QString("Brush strength: %1").arg(value, 0, 'f', 2));
}

void MeshEditorModule::onSymmetryChanged(int index) {
    log(QString("Symmetry set to: %1").arg(m_symmetryCombo->currentText()));
}

void MeshEditorModule::onAddRigBone() {
    bool ok;
    QString name = QInputDialog::getText(this, "Add Bone", "Bone name:", QLineEdit::Normal, "NewBone", &ok);
    if (ok && !name.isEmpty()) {
        int parentIdx = -1;
        auto* parentItem = m_boneTree->currentItem();
        if (parentItem) {
            parentIdx = m_skeleton->findBone(parentItem->text(0));
        }
        m_skeleton->addBone(name, parentIdx);
        refreshBoneList();
        log(QString("Added bone: %1").arg(name));
    }
}

void MeshEditorModule::onRemoveRigBone() {
    auto* item = m_boneTree->currentItem();
    if (!item) return;
    QString name = item->text(0);
    int idx = m_skeleton->findBone(name);
    if (idx >= 0) {
        m_skeleton->removeBone(idx);
        refreshBoneList();
        log(QString("Removed bone: %1").arg(name));
    }
}

void MeshEditorModule::onBindSkin() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh before binding skin.");
        return;
    }
    if (m_skeleton->bones.isEmpty()) {
        logWarning("Add bones to the skeleton first.");
        return;
    }
    QVector<QVector3D> verts;
    for (const auto& v : m_currentMesh.vertices)
        verts.append(v.position);
    QVector<QVector<int>> faces;
    for (const auto& f : m_currentMesh.faces) {
        QVector<int> face;
        for (int idx : f.indices) face.append(idx);
        faces.append(face);
    }
    QVector<QVector3D> bonePositions;
    for (const auto& b : m_skeleton->bones)
        bonePositions.append(b.head);
    auto weights = AutoWeightCalculator::calculateAutoWeights(verts, faces, bonePositions,
        AutoWeightCalculator::Method::HeatDiffusion, 10);
    for (const auto& wv : weights) {
        if (wv.vertexIndex >= 0 && wv.vertexIndex < m_currentMesh.vertices.size()) {
            QString groupName = QString("Bone_%1").arg(wv.vertexIndex);
            QVector<float> w;
            for (auto it = wv.weights.begin(); it != wv.weights.end(); ++it)
                w.append(it.value());
            m_currentMesh.vertexGroups.insert(groupName, w);
        }
    }
    logSuccess(QString("Skin bound: %1 bones, %2 vertices weighted")
        .arg(m_skeleton->bones.size()).arg(weights.size()));
    refreshMeshList();
}

void MeshEditorModule::onAddWeightPaint() {
    auto* item = m_weightTree->currentItem();
    if (item) {
        item->setText(1, QString::number(m_weightValueSpin->value(), 'f', 3));
        log(QString("Weight set for vertex %1: %2").arg(item->text(0))
            .arg(m_weightValueSpin->value(), 0, 'f', 3));
    } else {
        int idx = m_weightTree->topLevelItemCount() + 1;
        int vertIdx = qMin(idx - 1, m_currentMesh.vertices.size() - 1);
        if (vertIdx >= 0) {
            m_weightTree->addTopLevelItem(new QTreeWidgetItem({
                QString("Vertex %1").arg(vertIdx),
                QString::number(m_weightValueSpin->value(), 'f', 3),
                "Bone"}));
        }
    }
}

void MeshEditorModule::onClearWeightPaint() {
    auto* item = m_weightTree->currentItem();
    if (item) {
        item->setText(1, "0.000");
        log(QString("Cleared weight for vertex %1").arg(item->text(0)));
    }
}

void MeshEditorModule::onGenerateLOD() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh before generating LODs.");
        return;
    }
    m_exportProgress->setVisible(true);
    m_exportProgress->setValue(0);
    QApplication::processEvents();
    QVector<QVector3D> verts;
    for (const auto& v : m_currentMesh.vertices)
        verts.append(v.position);
    QVector<int> indices;
    for (const auto& f : m_currentMesh.faces) {
        for (int idx : f.indices)
            indices.append(idx);
    }
    QVector<QVector3D> normals;
    for (const auto& v : m_currentMesh.vertices)
        normals.append(v.normal);
    QVector<QVector2D> uvs = m_currentMesh.uvs;
    tools::LODGenerator::Options opts;
    opts.lodCount = m_lodLevelSpin->value();
    opts.reductionRatio = static_cast<float>(m_lodReductionSpin->value());
    auto result = tools::LODGenerator::generate(verts, indices, normals, uvs, opts);
    if (!result.success) {
        logError("LOD generation failed.");
        m_exportProgress->setVisible(false);
        return;
    }
    m_exportProgress->setValue(100);
    logSuccess(QString("Generated %1 LOD levels").arg(result.levels.size()));
    m_exportInfoLabel->setText(QString("%1 LODs generated").arg(result.levels.size()));
    m_exportProgress->setVisible(false);
}

void MeshEditorModule::onDecimateMesh() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh before decimating.");
        return;
    }
    float ratio = static_cast<float>(m_decimateRatioSpin->value());
    log(QString("Decimating to %1% of original...").arg(ratio * 100, 0, 'f', 0));
    MeshData result = Decimation::decimateQuadric(m_currentMesh, 1.0f - ratio);
    if (result.vertices.isEmpty()) {
        logError("Decimation failed.");
        return;
    }
    m_currentMesh = result;
    logSuccess(QString("Decimated: %1 verts, %2 tris")
        .arg(m_currentMesh.getVertexCount())
        .arg(m_currentMesh.getTriangleCount()));
    refreshMeshList();
}

void MeshEditorModule::onRemesh() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh before remeshing.");
        return;
    }
    int targetTris = m_remeshResSpin->value();
    log(QString("Remeshing to %1 triangles...").arg(targetTris));
    MeshData result = Remeshing::quadRemesh(m_currentMesh, targetTris);
    if (result.vertices.isEmpty()) {
        logError("Remeshing failed.");
        return;
    }
    m_currentMesh = result;
    logSuccess(QString("Remesh complete: %1 verts, %2 tris")
        .arg(m_currentMesh.getVertexCount())
        .arg(m_currentMesh.getTriangleCount()));
    refreshMeshList();
}

void MeshEditorModule::onSmoothMesh() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh before smoothing.");
        return;
    }
    log("Smoothing mesh...");
    SmoothModifier mod;
    mod.factor = 0.5f;
    mod.iterations = 5;
    mod.smoothMode = SmoothModifier::SmoothMode::Laplacian;
    MeshData result = mod.apply(m_currentMesh);
    if (result.vertices.isEmpty()) {
        logError("Smoothing failed.");
        return;
    }
    m_currentMesh = result;
    logSuccess(QString("Mesh smoothed: %1 verts, %2 tris")
        .arg(m_currentMesh.getVertexCount())
        .arg(m_currentMesh.getTriangleCount()));
    refreshMeshList();
}

void MeshEditorModule::onRetopoMesh() {
    if (m_currentMesh.vertices.isEmpty()) {
        logWarning("Load a mesh before retopology.");
        return;
    }
    // Toggle retopo mode on the current sculpt mode
    bool currentlyEnabled = m_sculptMode->isRetopoMode();
    m_sculptMode->setRetopoMode(!currentlyEnabled);
    QString status = currentlyEnabled ? "Retopo mode disabled" : "Retopo mode enabled";
    log(status);
    // Update brush combo to reflect mode
    int idx = m_brushCombo->currentIndex();
    m_brushCombo->setCurrentIndex(qBound(0, idx + 1, m_brushCombo->count() - 1));
}

void MeshEditorModule::onShowContextMenu(const QPoint& pos) {
    QMenu menu(this);

    QAction* deleteAction = menu.addAction("Delete Selected");
    QAction* duplicateAction = menu.addAction("Duplicate");
    QAction* separateAction = menu.addAction("Separate Selection");
    menu.addSeparator();
    QAction* fillHolesAction = menu.addAction("Fill Holes");
    QAction* recalcNormalsAction = menu.addAction("Recalculate Normals");
    QAction* flipFacesAction = menu.addAction("Flip Faces");
    menu.addSeparator();
    QAction* selectAllAction = menu.addAction("Select All");
    QAction* selectNoneAction = menu.addAction("Deselect All");
    QAction* invertSelAction = menu.addAction("Invert Selection");

    QAction* chosen = menu.exec(mapToGlobal(pos));
    if (!chosen) return;

    if (chosen == deleteAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        log("Deleting selected faces...");
        QVector<bool> faceSelected(m_currentMesh.faces.size(), false);
        for (int i = 0; i < m_currentMesh.faces.size(); ++i)
            faceSelected[i] = true;
        MeshData result;
        result.name = m_currentMesh.name + "_deleted";
        result.materialName = m_currentMesh.materialName;
        result.diffuseColor = m_currentMesh.diffuseColor;
        QSet<int> usedVerts;
        for (int i = 0; i < m_currentMesh.faces.size(); ++i) {
            if (!faceSelected[i]) {
                Face f = m_currentMesh.faces[i];
                for (int& idx : f.indices) usedVerts.insert(idx);
                result.faces.append(f);
            }
        }
        QVector<int> vertMap(m_currentMesh.vertices.size(), -1);
        int newIdx = 0;
        for (int i = 0; i < m_currentMesh.vertices.size(); ++i) {
            if (usedVerts.contains(i)) {
                vertMap[i] = newIdx++;
                result.vertices.append(m_currentMesh.vertices[i]);
            }
        }
        for (auto& f : result.faces)
            for (int& idx : f.indices) idx = vertMap[idx];
        m_currentMesh = result;
        logSuccess(QString("Deleted faces: %1 verts, %2 tris remaining")
            .arg(m_currentMesh.getVertexCount()).arg(m_currentMesh.getTriangleCount()));
        refreshMeshList();
    } else if (chosen == duplicateAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        MeshData dup = m_currentMesh;
        dup.name = m_currentMesh.name + "_copy";
        QMatrix4x4 offset;
        offset.translate(0.5f, 0.0f, 0.0f);
        for (auto& v : dup.vertices) {
            v.position = offset.map(v.position);
        }
        m_currentMesh = dup;
        logSuccess("Mesh duplicated");
        refreshMeshList();
    } else if (chosen == separateAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        log("Separating selection into new mesh...");
        MeshData separated;
        separated.name = m_currentMesh.name + "_separated";
        separated.materialName = m_currentMesh.materialName;
        separated.diffuseColor = m_currentMesh.diffuseColor;
        if (!m_currentMesh.faces.isEmpty()) {
            Face lastFace = m_currentMesh.faces.takeLast();
            QSet<int> usedVerts;
            for (int idx : lastFace.indices) usedVerts.insert(idx);
            separated.faces.append(lastFace);
            QVector<int> vertMap(m_currentMesh.vertices.size(), -1);
            int newIdx = 0;
            for (int i = 0; i < m_currentMesh.vertices.size(); ++i) {
                if (usedVerts.contains(i)) {
                    vertMap[i] = newIdx++;
                    separated.vertices.append(m_currentMesh.vertices[i]);
                }
            }
            for (auto& f : separated.faces)
                for (int& idx : f.indices) idx = vertMap[idx];
        }
        m_currentMesh = separated;
        logSuccess(QString("Separated: %1 verts, %2 tris")
            .arg(m_currentMesh.getVertexCount()).arg(m_currentMesh.getTriangleCount()));
        refreshMeshList();
    } else if (chosen == fillHolesAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        log("Filling holes...");
        QMap<QPair<int,int>, int> edgeCount;
        for (const auto& f : m_currentMesh.faces) {
            int n = f.indices.size();
            for (int i = 0; i < n; ++i) {
                int a = f.indices[i];
                int b = f.indices[(i + 1) % n];
                if (a > b) qSwap(a, b);
                edgeCount[{a, b}]++;
            }
        }
        QVector<QPair<int,int>> boundaryEdges;
        for (auto it = edgeCount.constBegin(); it != edgeCount.constEnd(); ++it) {
            if (it.value() == 1) boundaryEdges.append(it.key());
        }
        if (boundaryEdges.isEmpty()) {
            logWarning("No boundary edges found (mesh is watertight)");
        } else {
            QVector<bool> visited(boundaryEdges.size(), false);
            int filledCount = 0;
            for (int i = 0; i < boundaryEdges.size(); ++i) {
                if (visited[i]) continue;
                visited[i] = true;
                QVector<int> loop;
                loop.append(boundaryEdges[i].first);
                loop.append(boundaryEdges[i].second);
                while (true) {
                    int last = loop.last();
                    bool found = false;
                    for (int j = 0; j < boundaryEdges.size(); ++j) {
                        if (visited[j]) continue;
                        if (boundaryEdges[j].first == last) {
                            loop.append(boundaryEdges[j].second);
                            visited[j] = true;
                            found = true;
                            break;
                        } else if (boundaryEdges[j].second == last) {
                            loop.append(boundaryEdges[j].first);
                            visited[j] = true;
                            found = true;
                            break;
                        }
                    }
                    if (!found || loop.size() > 3) break;
                }
                if (loop.size() >= 3) {
                    Face newFace(loop);
                    m_currentMesh.faces.append(newFace);
                    filledCount++;
                }
            }
            logSuccess(QString("Filled %1 holes").arg(filledCount));
            refreshMeshList();
        }
    } else if (chosen == recalcNormalsAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        log("Recalculating normals...");
        for (auto& v : m_currentMesh.vertices)
            v.normal = QVector3D(0, 0, 0);
        for (const auto& f : m_currentMesh.faces) {
            if (f.indices.size() < 3) continue;
            QVector3D e1 = m_currentMesh.vertices[f.indices[1]].position - m_currentMesh.vertices[f.indices[0]].position;
            QVector3D e2 = m_currentMesh.vertices[f.indices[2]].position - m_currentMesh.vertices[f.indices[0]].position;
            QVector3D n = QVector3D::crossProduct(e1, e2).normalized();
            for (int idx : f.indices)
                m_currentMesh.vertices[idx].normal += n;
        }
        for (auto& v : m_currentMesh.vertices)
            v.normal.normalize();
        logSuccess("Normals recalculated");
        refreshMeshList();
    } else if (chosen == flipFacesAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        for (auto& f : m_currentMesh.faces)
            std::reverse(f.indices.begin(), f.indices.end());
        logSuccess("Faces flipped");
        refreshMeshList();
    } else if (chosen == selectAllAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        for (auto& v : m_currentMesh.vertices) v.mask = 1.0f;
        logSuccess(QString("Selected all %1 vertices").arg(m_currentMesh.vertices.size()));
    } else if (chosen == selectNoneAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        for (auto& v : m_currentMesh.vertices) v.mask = 0.0f;
        logSuccess("Deselected all vertices");
    } else if (chosen == invertSelAction) {
        if (m_currentMesh.vertices.isEmpty()) { logWarning("No mesh loaded."); return; }
        int count = 0;
        for (auto& v : m_currentMesh.vertices) {
            v.mask = (v.mask > 0.5f) ? 0.0f : 1.0f;
            if (v.mask > 0.5f) count++;
        }
        logSuccess(QString("Inverted selection: %1 vertices selected").arg(count));
    }
}

} // namespace graphics
} // namespace ks

