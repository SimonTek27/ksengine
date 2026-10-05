#pragma once
/**
 * Car setup save/load (ksim .setup text format).
 */
#include "SetupGarage.h"
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

namespace ks {
namespace sim {

inline bool saveSetupToFile(const SetupData& s, const std::string& path) {
    std::ofstream out(path);
    if (!out) return false;
    out << "; ksim car setup\n";
    out << "[TYRES]\n";
    out << "PressureFL=" << s.tirePressureFL << "\n";
    out << "PressureFR=" << s.tirePressureFR << "\n";
    out << "PressureRL=" << s.tirePressureRL << "\n";
    out << "PressureRR=" << s.tirePressureRR << "\n";
    out << "[BRAKES]\n";
    out << "Bias=" << s.brakeBias << "\n";
    out << "[SUSPENSION]\n";
    out << "RideHeightFront=" << s.rideHeightFront << "\n";
    out << "RideHeightRear=" << s.rideHeightRear << "\n";
    out << "SpringFront=" << s.springRateFront << "\n";
    out << "SpringRear=" << s.springRateRear << "\n";
    out << "[AERO]\n";
    out << "FrontWing=" << s.frontWingAngle << "\n";
    out << "RearWing=" << s.rearWingAngle << "\n";
    out << "[DRIVETRAIN]\n";
    out << "DiffPreload=" << s.diffPreload << "\n";
    out << "[FUEL]\n";
    out << "Fuel=" << s.fuel << "\n";
    out << "Ballast=" << s.ballast << "\n";
    out << "[AIDS]\n";
    out << "TC=" << s.tcLevel << "\n";
    out << "ABS=" << s.absLevel << "\n";
    return true;
}

inline bool loadSetupFromFile(SetupData& s, const std::string& path) {
    std::ifstream in(path);
    if (!in) return false;
    auto trim = [](std::string x) {
        while (!x.empty() && (unsigned char)x.front() <= ' ') x.erase(x.begin());
        while (!x.empty() && (unsigned char)x.back() <= ' ') x.pop_back();
        return x;
    };
    std::string line;
    while (std::getline(in, line)) {
        auto sc = line.find(';');
        if (sc != std::string::npos) line = line.substr(0, sc);
        line = trim(line);
        if (line.empty() || line.front() == '[') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        std::string key = trim(line.substr(0, eq));
        std::string val = trim(line.substr(eq + 1));
        float f = std::strtof(val.c_str(), nullptr);
        int i = std::atoi(val.c_str());
        if (key == "PressureFL") s.tirePressureFL = f;
        else if (key == "PressureFR") s.tirePressureFR = f;
        else if (key == "PressureRL") s.tirePressureRL = f;
        else if (key == "PressureRR") s.tirePressureRR = f;
        else if (key == "Bias") s.brakeBias = f;
        else if (key == "RideHeightFront") s.rideHeightFront = f;
        else if (key == "RideHeightRear") s.rideHeightRear = f;
        else if (key == "SpringFront") s.springRateFront = f;
        else if (key == "SpringRear") s.springRateRear = f;
        else if (key == "FrontWing") s.frontWingAngle = f;
        else if (key == "RearWing") s.rearWingAngle = f;
        else if (key == "DiffPreload") s.diffPreload = f;
        else if (key == "Fuel") s.fuel = f;
        else if (key == "Ballast") s.ballast = f;
        else if (key == "TC") s.tcLevel = i;
        else if (key == "ABS") s.absLevel = i;
    }
    return true;
}

} // namespace sim
} // namespace ks
