#include "MultiCarManager.h"
#include <algorithm>
#include <cmath>

namespace ks::sim {

MultiCarManager::MultiCarManager() = default;
MultiCarManager::~MultiCarManager() = default;

int MultiCarManager::addCar(const std::string& carName, const std::string& driverName,
                             const vec3& startPosition, bool isPlayer)
{
    auto entry = std::make_unique<CarEntry>();
    entry->id = m_nextCarId++;
    entry->carName = carName;
    entry->driverName = driverName;
    entry->isPlayer = isPlayer;
    entry->vehicle = std::make_unique<ks::physics::VehicleSimulator>();
    entry->vehicle->setMass(1200);
    entry->vehicle->setEnginePower(260);
    entry->vehicle->setMaxRpm(8500);
    entry->vehicle->setDragCoeff(0.35);
    entry->vehicle->setFrontalArea(2.2);
    entry->vehicle->setWheelBase(2.6);
    entry->vehicle->setTrackWidth(1.6);

    if (!isPlayer) {
        entry->ai = std::make_unique<AIController>();
        entry->ai->setSpeedFactor(m_aiSpeedFactor);
        if (!m_aiSplinePath.empty()) {
            entry->ai->loadSpline(m_aiSplinePath);
        }
    }

    // Grid/network spawns must land at their start position and the vehicle
    // must actually simulate: updatePhysics() is a no-op until the run flag
    // is set, so without startSimulation() every non-player car stayed frozen
    // at the origin (roadmap 3.5).
    entry->vehicle->startSimulation();
    auto& st = entry->vehicle->state();
    st.position = {startPosition.x, startPosition.y, startPosition.z};

    entry->transform = mat4();
    entry->transform(0,3) = startPosition.x;
    entry->transform(1,3) = startPosition.y;
    entry->transform(2,3) = startPosition.z;

    int id = entry->id;
    if (isPlayer) {
        m_playerCarId = id;
    }

    m_cars.push_back(std::move(entry));
    if (onCarAdded) onCarAdded(id, carName);
    return id;
}

void MultiCarManager::removeCar(int carId)
{
    for (auto it = m_cars.begin(); it != m_cars.end(); ++it) {
        if ((*it)->id == carId) {
            m_cars.erase(it);
            if (onCarRemoved) onCarRemoved(carId);
            return;
        }
    }
}

void MultiCarManager::setAISpeedFactor(float f)
{
    m_aiSpeedFactor = f;
    for (auto& car : m_cars) {
        if (car && car->ai) car->ai->setSpeedFactor(f);
    }
}

bool MultiCarManager::setCarExternallyDriven(int carId)
{
    CarEntry* car = getCar(carId);
    if (!car) return false;
    // A remote driver, not the spline, decides what this car does; without
    // dropping it MultiCarManager::update() would keep overwriting the
    // networked controls with the AI's own every step.
    car->ai.reset();
    return true;
}

bool MultiCarManager::setCarClientIndex(int carId, int clientIndex)
{
    if (!setCarExternallyDriven(carId)) return false;
    getCar(carId)->clientIndex = clientIndex;
    return true;
}

void MultiCarManager::clearAllCars()
{
    m_cars.clear();
    m_playerCarId = -1;
}

void MultiCarManager::update(float dt)
{
    for (auto& car : m_cars) {
        if (!car->isActive) continue;

        if (!car->isPlayer && car->ai && car->ai->isReady()) {
            auto state = car->vehicle->getState();
            // Real heading: steering error is target-minus-car heading, so
            // feeding a constant 0 left every AI on a fixed lock (3.5).
            car->ai->update(vec3(state.position.x, state.position.y, state.position.z),
                            state.heading, state.speed, state.gear, dt);
            car->vehicle->setThrottle(car->ai->throttle());
            car->vehicle->setBrake(car->ai->brake());
            car->vehicle->setSteering(car->ai->steering());
        }

        car->vehicle->updatePhysics(dt);

        auto state = car->vehicle->getState();
        // Build transform matrix from simulation state
        car->transform = mat4();
        // Translate
        car->transform(0,3) = state.position.x;
        car->transform(1,3) = state.position.y;
        car->transform(2,3) = state.position.z;
    }

    if (m_collisionEnabled) {
        checkCollisions();
    }
}

CarEntry* MultiCarManager::getCar(int id)
{
    for (auto& car : m_cars) {
        if (car->id == id) return car.get();
    }
    return nullptr;
}

CarEntry* MultiCarManager::getCarByClientIndex(int clientIndex)
{
    for (auto& car : m_cars) {
        if (car->clientIndex == clientIndex) return car.get();
    }
    return nullptr;
}

CarEntry* MultiCarManager::getCarById(uint32_t carId)
{
    for (auto& car : m_cars) {
        if (static_cast<uint32_t>(car->id) == carId) return car.get();
    }
    return nullptr;
}

CarEntry* MultiCarManager::playerCar()
{
    return getCar(m_playerCarId);
}

void MultiCarManager::loadAiSpline(const std::string& trackDirectory)
{
    m_aiSplinePath = trackDirectory;
    // Keep our own copy of the line for grid placement (spawnGrid); each
    // car's AIController loads the same file independently.
    m_gridSpline = ks::ai::AiFileReader::readSpline(trackDirectory + "/ai/fast_lane.ai");
    if (!m_gridSpline.isValid())
        m_gridSpline = ks::ai::AiFileReader::readSpline(trackDirectory + "/ai/fast_lane.ai.txt");
    for (auto& car : m_cars) {
        if (!car->isPlayer && car->ai) {
            car->ai->loadSpline(trackDirectory);
        }
    }
}

std::vector<int> MultiCarManager::spawnGrid(int count, const std::string& carName,
                                             const std::string& driverPrefix, float spacing)
{
    std::vector<int> ids;
    if (count <= 0 || !m_gridSpline.isValid() || m_gridSpline.totalDistance <= 0.0f)
        return ids;

    const auto& pts = m_gridSpline.points;
    const float total = m_gridSpline.totalDistance;
    for (int i = 0; i < count; ++i) {
        // Two-wide stagger: row i/2 sits row*spacing meters from the start
        // line (behind it on a closed loop, ahead of it on an open one).
        const float back = static_cast<float>(i / 2) * spacing;
        float d;
        if (m_gridSpline.closed) {
            d = std::fmod(total - back, total);
            if (d < 0.0f) d += total;
        } else {
            d = std::min(back, total * 0.999f);
        }

        // Segment containing d: first point at/after d is the segment end.
        auto it = std::lower_bound(pts.begin(), pts.end(), d,
                                   [](const ks::ai::AiSplinePoint& p, float v) {
                                       return p.distance < v;
                                   });
        size_t hi = static_cast<size_t>(it - pts.begin());
        if (hi >= pts.size()) hi = pts.size() - 1;
        if (hi == 0) hi = 1; // d sits at/before the first point: segment 0->1
        const size_t lo = hi - 1;
        const float segLen = pts[hi].distance - pts[lo].distance;
        const float t = segLen > 1e-4f
                          ? std::clamp((d - pts[lo].distance) / segLen, 0.0f, 1.0f)
                          : 0.0f;
        const auto& a = pts[lo].position;
        const auto& b = pts[hi].position;
        vec3 pos{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};

        // Heading = direction of travel (atan2 convention matches
        // AIController::calculateSteering: forward = (sin h, 0, cos h)).
        const float dx = b.x - a.x, dz = b.z - a.z;
        const float heading = (std::fabs(dx) + std::fabs(dz) > 1e-5f)
                                ? std::atan2(dx, dz) : 0.0f;
        // Lateral offset: right vector for heading h is (-cos h, 0, sin h).
        const float lateral = (i % 2 == 0) ? 1.5f : -1.5f;
        pos.x += -std::cos(heading) * lateral;
        pos.z += std::sin(heading) * lateral;

        const int id = addCar(carName, driverPrefix + std::to_string(i + 1), pos, false);
        if (CarEntry* e = getCar(id)) {
            auto& st = e->vehicle->state();
            st.position = {pos.x, pos.y, pos.z};
            st.heading = heading;
        }
        ids.push_back(id);
    }
    return ids;
}

void MultiCarManager::checkCollisions()
{
    for (size_t i = 0; i < m_cars.size(); ++i) {
        for (size_t j = i + 1; j < m_cars.size(); ++j) {
            if (!m_cars[i]->isActive || !m_cars[j]->isActive) continue;

            auto stateA = m_cars[i]->vehicle->getState();
            auto stateB = m_cars[j]->vehicle->getState();

            float dx = stateA.position.x - stateB.position.x;
            float dy = stateA.position.y - stateB.position.y;
            float dz = stateA.position.z - stateB.position.z;
            float dist = std::sqrt(dx * dx + dy * dy + dz * dz);

            if (dist < 2.0f && dist > 0.01f) {
                if (onCollisionOccurred) onCollisionOccurred(m_cars[i]->id, m_cars[j]->id);
            }
        }
    }
}

} // namespace ks::sim
