#pragma once
#include <QObject>
#include <QVector3D>
#include <QString>
#include <QVector>
#include <QPointF>
#include <QElapsedTimer>

#include "engine/physics/PhysicsCoreTypes.h"
#include "simulator/phys_TireModel.h"
#include "simulator/phys_LapTimer.h"
#include "sdk/kseditor/plugins/simulators/kunos/assettocorsa/physics/phys_Simulator.h"

namespace ks {

using SimulationState = ks::physics::SimulationState;
using TireSlipCurve = ks::physics::TireSlipCurve;
using LapTimeEstimate = ks::physics::LapTimeEstimate;
using WheelState = ks::physics::WheelState;
using ValidationMetrics = ks::physics::ValidationMetrics;
using WeatherState = ks::physics::WeatherState;
using DamageState = ks::physics::DamageState;
using DriveLayout = ks::physics::DriveLayout;

// Re-export the phys_Simulator class from the simulator module
// The implementation is now split across:
// - simulator/phys_TireModel.h/.cpp
// - simulator/phys_LapTimer.h/.cpp  
// - simulator/phys_Simulator.h/.cpp

} // namespace ks