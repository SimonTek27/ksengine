#pragma once

#include "MathTypes.h"
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include "engine/physics/VehiclePhysics.h"
#include "AIController.h"

namespace ks::sim {

struct CarEntry {
    int id = 0;
    int clientIndex = -1;
    std::string carName;
    std::string driverName;
    std::unique_ptr<ks::physics::VehicleSimulator> vehicle;
    std::unique_ptr<AIController> ai;
    bool isPlayer = false;
    bool isActive = true;

    mat4 transform;
    vec3 color{1, 1, 1};
};

class MultiCarManager {
public:
    MultiCarManager();
    ~MultiCarManager();

    int addCar(const std::string& carName, const std::string& driverName,
               const vec3& startPosition, bool isPlayer = false);
    void removeCar(int carId);
    void clearAllCars();

    void update(float dt);

    int carCount() const { return static_cast<int>(m_cars.size()); }
    CarEntry* getCar(int id);
    CarEntry* getCarByClientIndex(int clientIndex);
    CarEntry* getCarById(uint32_t carId);
    const std::vector<std::unique_ptr<CarEntry>>& cars() const { return m_cars; }

    CarEntry* playerCar();
    int playerCarId() const { return m_playerCarId; }

    void loadAiSpline(const std::string& trackDirectory);

    /** Spawn `count` AI cars on a staggered two-wide grid on the loaded
     *  spline (roadmap 3.5): rows sit `spacing` meters apart, closed
     *  splines stagger behind the start line, open ones forward from it.
     *  Cars start with their simulation running. Empty without a spline. */
    std::vector<int> spawnGrid(int count, const std::string& carName,
                               const std::string& driverPrefix, float spacing = 6.0f);

    /** One car to place on the grid (roadmap 2.8): identity + 0-based slot.
     *  The player's slot is simply absent from the vector, so its geometry
     *  stays reserved on the grid. */
    struct GridCar {
        std::string driverName;
        std::string carName;
        int gridSlot = 0;
    };

    /** Grid geometry for a 0-based slot — rows of `spacing` meters, two-wide
     *  (same staggered layout as spawnGrid). False without a loaded spline;
     *  used for the player's garage/grid pose too (roadmap 2.8). */
    bool gridPose(int slot, float spacing, vec3& outPos, float& outHeading) const;

    /** Spawn `field`, each car at its GridCar::gridSlot. Returns the spawned
     *  ids in field order (zip with the input). Empty without a spline. */
    std::vector<int> spawnGrid(const std::vector<GridCar>& field, float spacing = 6.0f);

    void setPlayerCarId(int id) { m_playerCarId = id; }
    void setCollisionEnabled(bool e) { m_collisionEnabled = e; }

    /** Propagate the session flag's AI limiter to every AI car (and cars
     *  added later inherit it). No-op when no AI cars exist yet. */
    void setAISpeedFactor(float f);

    /** Hand the car to an external driver (roadmap 3.1): drops the spline
     *  AI controller so update() stops feeding it controls, leaving the
     *  vehicle to whatever writes setThrottle/setBrake/setSteering -
     *  networked input on a host, a synced host state on a client.
     *  Returns false when there is no such car. */
    bool setCarExternallyDriven(int carId);

    /** Tag `carId` as owned by server slot `clientIndex` (implies
     *  setCarExternallyDriven) so the entry can be found back through
     *  getCarByClientIndex(). Returns false when there is no such car. */
    bool setCarClientIndex(int carId, int clientIndex);

    std::function<void(int, const std::string&)> onCarAdded;
    std::function<void(int)> onCarRemoved;
    std::function<void(int, int)> onCollisionOccurred;

private:
    void updateCarTransforms();
    void checkCollisions();
    void resolveCollision(CarEntry& a, CarEntry& b);

    std::vector<std::unique_ptr<CarEntry>> m_cars;
    int m_nextCarId = 0;
    int m_playerCarId = -1;
    bool m_collisionEnabled = true;
    std::string m_aiSplinePath;
    ks::ai::AiSpline m_gridSpline;
    float m_aiSpeedFactor = 1.0f;
};

} // namespace ks::sim
