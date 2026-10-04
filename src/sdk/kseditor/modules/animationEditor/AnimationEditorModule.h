#pragma once

#include "editor/ModuleGuiBase.h"
#include "sdk/kseditor/engine/animation/AnimationSystem.h"

#include <QTreeWidget>
#include <QTableWidget>
#include <QDoubleSpinBox>
#include <QComboBox>
#include <QPushButton>
#include <QLabel>
#include <QSlider>
#include <QSpinBox>
#include <QCheckBox>
#include <QSplitter>
#include <QTabWidget>
#include <QGroupBox>
#include <QFormLayout>
#include <QScrollArea>
#include <QColorDialog>
#include <QMenu>
#include <QAction>

namespace ks {

class AnimationEditorModule : public ModuleGuiBase {
    Q_OBJECT
public:
    explicit AnimationEditorModule(QWidget* parent = nullptr);
    ~AnimationEditorModule() override = default;

    bool initialize() override;
    void shutdown() override;

    QString moduleName() const override { return "Animation Editor"; }
    QString moduleId() const override { return "animation"; }
    int getModulePriority() const override { return 40; }

    void importFile(const QString& filePath) override;
    void exportFile(const QString& filePath) override;

protected:
    void buildUI() override;
    void onActivation() override;
    void onDeactivation() override;

private slots:
    void onNewAnimation();
    void onDeleteAnimation();
    void onDuplicateAnimation();
    void onAddKeyframe();
    void onRemoveKeyframe();
    void onPlayPause();
    void onStop();
    void onLoopToggled(bool);
    void onTimeChanged(double value);
    void onFrameChanged(int frame);
    void onAnimationSelected(QTreeWidgetItem* item, int column);
    void onKeyframeSelected(int row, int column);
    void onInterpolationChanged(int index);
    void onAddTrack();
    void onRemoveTrack();
    void onAddState();
    void onRemoveState();
    void onAddTransition();
    void onBlendTreeNodeAdded();
    void onExportAnimation();
    void onImportAnimation();
    void onShowContextMenu(const QPoint& pos);
    void onStateContextMenu(const QPoint& pos);
    void onTransitionContextMenu(const QPoint& pos);

private:
    void setupTimelineTab();
    void setupStateMachineTab();
    void setupBlendTreeTab();
    void setupPropertiesTab();
    void setupCurvesTab();
    void refreshAnimationList();
    void refreshKeyframeTable();
    void refreshStateList();
    void updateTimelineScrubber();
    void playAnimation();
    void pauseAnimation();
    void stopAnimation();
    void updatePlaybackUI();
    void loadAnimationFromFile(const QString& filePath);
    void saveAnimationToFile(const QString& filePath);
    void updateProperties();
    void rebuildTracksFromTable();

    struct AnimationClip {
        QString name;
        double duration = 1.0;
        bool loop = true;
        QMap<QString, QVector<QPair<double, QVariant>>> tracks; // trackName -> (time, value)
    };
    
    struct StateMachine {
        QString name;
        QMap<QString, QString> states; // stateName -> animationName
        QVector<QPair<QString, QString>> transitions; // from -> to
    };

    QTabWidget* m_tabWidget = nullptr;
    
    // Timeline tab
    QWidget* m_timelineTab = nullptr;
    QTreeWidget* m_animationTree = nullptr;
    QTableWidget* m_keyframeTable = nullptr;
    QSlider* m_timelineSlider = nullptr;
    QDoubleSpinBox* m_timeSpin = nullptr;
    QSpinBox* m_frameSpin = nullptr;
    QPushButton* m_playBtn = nullptr;
    QPushButton* m_pauseBtn = nullptr;
    QPushButton* m_stopBtn = nullptr;
    QCheckBox* m_loopCheck = nullptr;
    QLabel* m_timeLabel = nullptr;
    QComboBox* m_interpolationCombo = nullptr;
    QPushButton* m_addTrackBtn = nullptr;
    QPushButton* m_removeTrackBtn = nullptr;
    QDoubleSpinBox* m_durationSpin = nullptr;
    QPushButton* m_exportBtn = nullptr;
    QPushButton* m_importBtn = nullptr;
    
    // State Machine tab
    QWidget* m_stateMachineTab = nullptr;
    QTreeWidget* m_stateTree = nullptr;
    QTableWidget* m_transitionTable = nullptr;
    QPushButton* m_addStateBtn = nullptr;
    QPushButton* m_removeStateBtn = nullptr;
    QPushButton* m_addTransitionBtn = nullptr;
    QComboBox* m_stateAnimCombo = nullptr;
    
    // Blend Tree tab
    QWidget* m_blendTreeTab = nullptr;
    QTreeWidget* m_blendTreeNodes = nullptr;
    QPushButton* m_addBlendNodeBtn = nullptr;
    QPushButton* m_removeBlendNodeBtn = nullptr;
    QComboBox* m_blendTypeCombo = nullptr;
    QDoubleSpinBox* m_blendParamSpin = nullptr;
    
    // Properties tab
    QWidget* m_propertiesTab = nullptr;
    QTreeWidget* m_propertiesTree = nullptr;
    
    // Curves tab
    QWidget* m_curvesTab = nullptr;
    QLabel* m_curvesPlaceholder = nullptr;

    QMap<QString, AnimationClip> m_animations;
    QMap<QString, StateMachine> m_stateMachines;
    QString m_currentAnimation;
    QString m_currentStateMachine;
    bool m_playing = false;
    double m_currentTime = 0.0;
    int m_fps = 30;
    QTimer* m_playbackTimer = nullptr;
};

} // namespace ks
