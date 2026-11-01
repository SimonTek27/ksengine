#pragma once
/**
 * Garage model: one garage per team, two cars per garage.
 *
 * Assetto Corsa conflates three different things in a single "pit box" cube:
 * where a car stops for service, where it waits before a session, and who it
 * belongs to. ksim splits them:
 *
 *   - the PAD   : the shared stop area in front of the garage. One per garage,
 *                 shared by every car of the team (the AC_PIT_n cube).
 *   - the SPOTS : where each car of the team sits when the session starts in
 *                 the garage (practice / qualifying). Two per garage, derived
 *                 from the pad unless the track authors them explicitly.
 *
 * Capacity is therefore garages x spots_per_garage, and "one box per car"
 * (GarageStart=3 for the first car, GarageStart=4 for the second) becomes
 * "garage 3, slot 0" and "garage 3, slot 1".
 *
 * Header-only and Qt-free, same as GarageSpawn.h / TeamInfo.h: the simulator,
 * the editor and the tools all build the garage row from the same code.
 */
#include "GarageSpawn.h" // WorldPose, GarageLayout (legacy per-spot view)

#include <cmath>
#include <cstddef>
#include <cstdio>
#include <vector>

namespace ks {
namespace sim {

/// Where one car of the team waits: a cube inside the garage, on the pad's
/// axis. slot 0 and 1 are the two cars of the garage; a single-car team uses
/// slot 0 only.
struct GarageSpot {
    int slot = 0;
    WorldPose pose;
    bool occupied = false;
    int raceNumber = -1;

    void clear() {
        occupied = false;
        raceNumber = -1;
    }
};

/// One garage: the shared pad plus the spots of its cars.
struct Garage {
    int index = 0;
    WorldPose pad;             ///< shared stop area (service happens here)
    float padRadiusM = 8.f;    ///< "inside the pad" footprint, same for both cars
    float heading = 0.f;       ///< driving direction on the pad
    int teamCars = 2;          ///< spots in this garage
    std::vector<GarageSpot> spots;

    const GarageSpot* spot(int slot) const {
        for (const auto& s : spots)
            if (s.slot == slot) return &s;
        return nullptr;
    }

    GarageSpot* spot(int slot) {
        for (auto& s : spots)
            if (s.slot == slot) return &s;
        return nullptr;
    }

    /// Race numbers of the cars currently parked here.
    std::vector<int> raceNumbers() const {
        std::vector<int> numbers;
        for (const auto& s : spots)
            if (s.occupied) numbers.push_back(s.raceNumber);
        return numbers;
    }
};

class PitGarageLayout {
public:
    /**
     * Builds the garage row from the pads a track provides (one AC_PIT_n cube
     * each). The spots sit inside the garage, i.e. BEHIND the pad — the car
     * reverses onto the pad only in AC's model; here the car starts from its
     * spot and drives forward onto the shared pad, then out.
     *
     * @param pads            pad poses, one per garage, in pit-lane order
     * @param spotsPerGarage  two cars per team by default (1 restores AC parity)
     * @param spotSetbackM    how far inside the garage the spots sit
     * @param spotSpreadM     lateral distance between the two spots
     */
    static PitGarageLayout fromPads(const std::vector<WorldPose>& pads,
                                    int spotsPerGarage = 2, float spotSetbackM = 4.5f,
                                    float spotSpreadM = 2.6f) {
        PitGarageLayout layout;
        const int spots = spotsPerGarage > 0 ? spotsPerGarage : 1;
        for (std::size_t i = 0; i < pads.size(); ++i) {
            Garage garage;
            garage.index = static_cast<int>(i);
            garage.pad = pads[i];
            garage.heading = pads[i].heading;
            garage.teamCars = spots;
            const float h = pads[i].heading;
            const float forwardX = std::sin(h);
            const float forwardZ = std::cos(h);
            const float rightX = std::cos(h);
            const float rightZ = -std::sin(h);
            for (int slot = 0; slot < spots; ++slot) {
                GarageSpot spot;
                spot.slot = slot;
                const float lateral = spots == 1
                                          ? 0.f
                                          : (static_cast<float>(slot) -
                                             0.5f * static_cast<float>(spots - 1)) * spotSpreadM;
                spot.pose.x = garage.pad.x - forwardX * spotSetbackM + rightX * lateral;
                spot.pose.y = garage.pad.y;
                spot.pose.z = garage.pad.z - forwardZ * spotSetbackM + rightZ * lateral;
                spot.pose.heading = h;
                garage.spots.push_back(spot);
            }
            layout.m_garages.push_back(garage);
        }
        return layout;
    }

    bool empty() const { return m_garages.empty(); }
    std::size_t size() const { return m_garages.size(); }
    const std::vector<Garage>& garages() const { return m_garages; }

    const Garage* find(int garageIndex) const {
        for (const auto& g : m_garages)
            if (g.index == garageIndex) return &g;
        return nullptr;
    }

    Garage* find(int garageIndex) {
        for (auto& g : m_garages)
            if (g.index == garageIndex) return &g;
        return nullptr;
    }

    const GarageSpot* spot(int garageIndex, int slot) const {
        const Garage* garage = find(garageIndex);
        return garage ? garage->spot(slot) : nullptr;
    }

    /// The shared pad, or nullptr when the garage does not exist.
    const WorldPose* padPose(int garageIndex) const {
        const Garage* garage = find(garageIndex);
        return garage ? &garage->pad : nullptr;
    }

    float padRadiusM(int garageIndex) const {
        const Garage* garage = find(garageIndex);
        return garage ? garage->padRadiusM : 8.f;
    }

    /// True when (x, z) is inside the shared pad of that garage. Both cars of
    /// the team must test the SAME pad — that is the whole point of the model.
    bool isInsidePad(int garageIndex, float x, float z, float extraM = 0.f) const {
        const Garage* garage = find(garageIndex);
        if (!garage) return false;
        const float dx = x - garage->pad.x;
        const float dz = z - garage->pad.z;
        const float radius = garage->padRadiusM + extraM;
        return (dx * dx + dz * dz) <= radius * radius;
    }

    /// Which garage and slot a race number occupies. slot is -1 when absent.
    bool locateCar(int raceNumber, int& garageIndex, int& slot) const {
        for (const auto& g : m_garages)
            for (const auto& s : g.spots)
                if (s.occupied && s.raceNumber == raceNumber) {
                    garageIndex = g.index;
                    slot = s.slot;
                    return true;
                }
        return false;
    }

    /// Parks a car in a garage slot. The pad is untouched: it is shared.
    bool assignCar(int garageIndex, int slot, int raceNumber) {
        Garage* garage = find(garageIndex);
        if (!garage) return false;
        GarageSpot* target = garage->spot(slot);
        if (!target) return false;
        target->occupied = true;
        target->raceNumber = raceNumber;
        return true;
    }

    /// Total cars that fit: garages x spots.
    int carCapacity() const {
        int capacity = 0;
        for (const auto& g : m_garages) capacity += static_cast<int>(g.spots.size());
        return capacity;
    }

    /**
     * Legacy per-spot view for the existing spawn / garage-exit code, which
     * still addresses one box at a time. Box index = garage * teamCars + slot,
     * so the mapping stays stable while the model lives above it.
     */
    GarageLayout toGarageLayout() const {
        GarageLayout layout;
        if (!m_garages.empty()) layout.pitLaneHeading = m_garages.front().heading;
        for (const auto& g : m_garages) {
            // pad + spots: the pad needs its own index, otherwise spot 0 would
            // answer to the same address as the shared pad.
            const int stride = static_cast<int>(g.spots.size()) + 1;
            // The pad is exposed as the first box of the garage so the pit
            // service and the pit queue keep a single address for the team.
            GarageBox pad;
            pad.index = g.index * stride;
            pad.pose = g.pad;
            pad.occupied = true;
            if (!g.spots.empty() && g.spots.front().occupied)
                pad.teamCarRaceNumber = g.spots.front().raceNumber;
            layout.boxes.push_back(pad);
            for (const auto& s : g.spots) {
                GarageBox box;
                box.index = g.index * stride + 1 + s.slot;
                box.pose = s.pose;
                box.occupied = s.occupied;
                box.teamCarRaceNumber = s.raceNumber;
                layout.boxes.push_back(box);
            }
        }
        return layout;
    }

private:
    std::vector<Garage> m_garages;
};

} // namespace sim
} // namespace ks
