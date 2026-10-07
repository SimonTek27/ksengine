#ifndef KSENGINE_QT_FREE
#pragma once

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QTabWidget>
#include <QGroupBox>
#include <QLabel>
#include <QSlider>
#include <QComboBox>
#include <QCheckBox>
#include <QPushButton>
#include <QSpinBox>
#include <QDoubleSpinBox>
#include <QTableWidget>
#include <QProgressBar>
#include <QTimer>
#include <QSettings>
#include <memory>

#include "engine/devices/DeviceManager.h"

namespace ks::ui {

// ============================================================================
// MonitorSettingsPanel — triple monitor configuration
// ============================================================================
class MonitorSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit MonitorSettingsPanel(ks::device::DeviceManager* dm, QWidget* parent = nullptr);

    void refresh();
    void apply();

signals:
    void settingsChanged();

private:
    void buildUI();
    void updateMonitorList();
    void updatePreview();

    ks::device::DeviceManager* m_dm;

    QComboBox* m_centerCombo;
    QDoubleSpinBox* m_bezelSpin;
    QDoubleSpinBox* m_fovSpin;
    QDoubleSpinBox* m_eyeDistSpin;
    QCheckBox* m_vsyncCheck;
    QSpinBox* m_fpsSpin;
    QComboBox* m_arrangementCombo;
    QWidget* m_previewWidget;
};

// ============================================================================
// InputSettingsPanel — racing wheel/pedals/FFB configuration
// ============================================================================
class InputSettingsPanel : public QWidget {
    Q_OBJECT
public:
    explicit InputSettingsPanel(ks::device::DeviceManager* dm, QWidget* parent = nullptr);

    void refresh();
    void apply();

signals:
    void settingsChanged();

private:
    void buildUI();
    void updateDeviceList();
    void updateAxisBars();
    void onDeviceSelected(int index);

    ks::device::DeviceManager* m_dm;

    QComboBox* m_deviceCombo;
    QDoubleSpinBox* m_steerRangeSpin;
    QCheckBox* m_ffbEnabledCheck;
    QDoubleSpinBox* m_ffbStrengthSpin;
    QTableWidget* m_axisTable;
    QTableWidget* m_buttonTable;
    QProgressBar* m_axisBars[8];
    QTimer* m_pollTimer;
};

// ============================================================================
// DeviceSettingsWidget — tabbed settings for all devices
// ============================================================================
class DeviceSettingsWidget : public QWidget {
    Q_OBJECT
public:
    explicit DeviceSettingsWidget(ks::device::DeviceManager* dm, QWidget* parent = nullptr);

    void refresh();
    void save();
    void load();

signals:
    void closed();

private:
    void buildUI();

    ks::device::DeviceManager* m_dm;
    QTabWidget* m_tabs;
    MonitorSettingsPanel* m_monitorPanel;
    InputSettingsPanel* m_inputPanel;
};

} // namespace ks::ui
#endif // !KSENGINE_QT_FREE
