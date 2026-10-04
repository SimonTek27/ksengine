#pragma once

#include "editor/ModuleGuiBase.h"
#include "sdk/kseditor/engine/mesh/MeshOperations.h"
#include <QTabWidget>

namespace ks { class Skeleton; }
class SculptMode;
#include <QTreeWidget>
#include <QTableWidget>
#include <QComboBox>
#include <QPushButton>
#include <QCheckBox>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QSplitter>
#include <QGroupBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QListWidget>
#include <QProgressBar>

namespace ks {
namespace graphics {

class MeshEditorModule : public ModuleGuiBase {
    Q_OBJECT
public:
    explicit MeshEditorModule(QWidget* parent = nullptr);
    ~MeshEditorModule() override;

    bool initialize() override;
    void shutdown() override;

    QString moduleName() const override { return "Mesh Editor"; }
    QString moduleId() const override { return "mesh"; }
    int getModulePriority() const override { return 50; }

    void importFile(const QString& filePath) override;
    void exportFile(const QString& filePath) override;

protected:
    void buildUI() override;
    void onActivation() override;
    void onDeactivation() override;

private slots:
    void onMeshSelected(QTreeWidgetItem* item, int column);
    void onLoadMesh();
    void onExportMesh();
    void onApplyBoolOp();
    void onBoolOpChanged(int index);
    void onUnwrap();
    void onUnwrapMethodChanged(int index);
    void onPackCharts();
    void onSeamAdded();
    void onSculptBrushChanged(int index);
    void onBrushSizeChanged(int value);
    void onBrushStrengthChanged(double value);
    void onSymmetryChanged(int index);
    void onAddRigBone();
    void onRemoveRigBone();
    void onBindSkin();
    void onAddWeightPaint();
    void onClearWeightPaint();
    void onGenerateLOD();
    void onDecimateMesh();
    void onRemesh();
    void onSmoothMesh();
    void onRetopoMesh();
    void onShowContextMenu(const QPoint& pos);

private:
    void setupBooleanOpsTab();
    void setupUVUnwrapTab();
    void setupSculptingTab();
    void setupRiggingTab();
    void setupExportTab();
    void refreshMeshList();
    void refreshBoneList();
    void loadXRef(const QString& path);

    QTabWidget* m_tabWidget = nullptr;

    QWidget* m_booleanOpsTab = nullptr;
    QTreeWidget* m_meshTree = nullptr;
    QPushButton* m_loadMeshBtn = nullptr;
    QPushButton* m_exportMeshBtn = nullptr;
    QComboBox* m_boolOpCombo = nullptr;
    QPushButton* m_applyBoolOpBtn = nullptr;
    QLabel* m_meshInfoLabel = nullptr;
    QListWidget* m_operandList = nullptr;

    QWidget* m_uvUnwrapTab = nullptr;
    QComboBox* m_unwrapMethodCombo = nullptr;
    QPushButton* m_unwrapBtn = nullptr;
    QDoubleSpinBox* m_seamAngleSpin = nullptr;
    QPushButton* m_packChartsBtn = nullptr;
    QDoubleSpinBox* m_packMarginSpin = nullptr;
    QLabel* m_uvPreviewLabel = nullptr;
    QCheckBox* m_shareUVCheck = nullptr;

    QWidget* m_sculptingTab = nullptr;
    QComboBox* m_brushCombo = nullptr;
    QSpinBox* m_brushSizeSpin = nullptr;
    QDoubleSpinBox* m_brushStrengthSpin = nullptr;
    QComboBox* m_symmetryCombo = nullptr;
    QPushButton* m_smoothBtn = nullptr;
    QPushButton* m_remeshBtn = nullptr;
    QSpinBox* m_remeshResSpin = nullptr;
    QPushButton* m_decimateBtn = nullptr;
    QPushButton* m_retopoBtn = nullptr;
    QDoubleSpinBox* m_decimateRatioSpin = nullptr;
    QLabel* m_sculptInfoLabel = nullptr;

    QWidget* m_riggingTab = nullptr;
    QTreeWidget* m_boneTree = nullptr;
    QPushButton* m_addBoneBtn = nullptr;
    QPushButton* m_removeBoneBtn = nullptr;
    QPushButton* m_bindSkinBtn = nullptr;
    QPushButton* m_addWeightBtn = nullptr;
    QPushButton* m_clearWeightBtn = nullptr;
    QTreeWidget* m_weightTree = nullptr;
    QDoubleSpinBox* m_weightValueSpin = nullptr;

    QWidget* m_exportTab = nullptr;
    QComboBox* m_exportFormatCombo = nullptr;
    QPushButton* m_generateLODBtn = nullptr;
    QSpinBox* m_lodLevelSpin = nullptr;
    QDoubleSpinBox* m_lodReductionSpin = nullptr;
    QCheckBox* m_exportNormalsCheck = nullptr;
    QCheckBox* m_exportUVCheck = nullptr;
    QCheckBox* m_exportColorsCheck = nullptr;
    QCheckBox* m_exportAnimCheck = nullptr;
    QProgressBar* m_exportProgress = nullptr;
    QLabel* m_exportInfoLabel = nullptr;

    MeshData m_currentMesh;
    ks::Skeleton* m_skeleton = nullptr;
    SculptMode* m_sculptMode = nullptr;
    QString m_currentMeshPath;
};

} // namespace graphics
} // namespace ks
