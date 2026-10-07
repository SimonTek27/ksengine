#pragma once
/**
 * Personal best store — file-backed per track/car (Sprint 4 / P1.5).
 */
#include <string>
#include <fstream>
#include <cstdio>
#include <map>
#include <vector>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <cctype>

namespace ks {
namespace sim {

struct PersonalBestRecord {
    std::string trackId;
    std::string carId;
    std::string driver;
    float bestLap = 1e9f;
    float sectors[3] = {1e9f, 1e9f, 1e9f};
    int lapsCompleted = 0;
};

class PersonalBestStore {
public:
    void setDirectory(const std::string& dir) {
        if (!isSafeDir(dir)) {
            std::fprintf(stderr, "PersonalBestStore: unsafe dir rejected\n");
            return;
        }
        m_dir = dir;
        namespace fs = std::filesystem;
        std::error_code ec;
        fs::create_directories(m_dir, ec);
        loadAll();
    }

    const std::string& directory() const { return m_dir; }

    static std::string key(const std::string& track, const std::string& car) {
        return sanitizeId(track) + "__" + sanitizeId(car);
    }

    PersonalBestRecord get(const std::string& track, const std::string& car) const {
        auto k = key(track, car);
        auto it = m_cache.find(k);
        if (it != m_cache.end()) return it->second;
        PersonalBestRecord r; r.trackId = track; r.carId = car; return r;
    }

    bool submitLap(const std::string& track, const std::string& car,
                   const std::string& driver, float lapTime, const float* sectors) {
        if (!(lapTime > 1.f) || lapTime > 600.f || !std::isfinite(lapTime)) return false;
        if (track.empty() || car.empty()) return false;
        auto& r = m_cache[key(track, car)];
        r.trackId = sanitizeId(track);
        r.carId = sanitizeId(car);
        if (!driver.empty()) r.driver = driver;
        r.lapsCompleted++;
        bool improved = false;
        if (lapTime < r.bestLap) { r.bestLap = lapTime; improved = true; }
        if (sectors) {
            for (int i = 0; i < 3; ++i)
                if (sectors[i] > 0.f && sectors[i] < r.sectors[i] && std::isfinite(sectors[i])) {
                    r.sectors[i] = sectors[i];
                    improved = true;
                }
        }
        if (improved) saveOne(r);
        return improved;
    }

    bool loadOne(const std::string& track, const std::string& car) {
        std::ifstream in(filePath(track, car));
        if (!in) return false;
        PersonalBestRecord r; r.trackId = track; r.carId = car;
        std::string line;
        while (std::getline(in, line)) {
            auto eq = line.find('=');
            if (eq == std::string::npos) continue;
            std::string k = line.substr(0, eq), val = line.substr(eq + 1);
            if (k == "BestLap") r.bestLap = std::strtof(val.c_str(), nullptr);
            else if (k == "S0") r.sectors[0] = std::strtof(val.c_str(), nullptr);
            else if (k == "S1") r.sectors[1] = std::strtof(val.c_str(), nullptr);
            else if (k == "S2") r.sectors[2] = std::strtof(val.c_str(), nullptr);
            else if (k == "Laps") r.lapsCompleted = std::atoi(val.c_str());
            else if (k == "Driver") r.driver = val;
        }
        m_cache[key(track, car)] = r;
        return true;
    }

    void loadAll() {
        if (m_dir.empty()) return;
        namespace fs = std::filesystem;
        std::error_code ec;
        if (!fs::exists(m_dir, ec)) return;
        for (auto& ent : fs::directory_iterator(m_dir, ec)) {
            if (!ent.is_regular_file()) continue;
            auto name = ent.path().filename().string();
            if (name.size() < 5 || name.substr(name.size() - 4) != ".pb") continue;
            // track__car.pb
            auto stem = name.substr(0, name.size() - 3);
            auto sep = stem.find("__");
            if (sep == std::string::npos) continue;
            loadOne(stem.substr(0, sep), stem.substr(sep + 2));
        }
        std::fprintf(stderr, "PersonalBestStore: loaded %zu records from %s\n",
                     m_cache.size(), m_dir.c_str());
    }

    /** Top N for a track (any car), sorted by best lap. */
    std::vector<PersonalBestRecord> leaderboard(const std::string& track, int maxN = 20) const {
        std::vector<PersonalBestRecord> out;
        const std::string t = sanitizeId(track);
        for (const auto& kv : m_cache) {
            if (kv.second.trackId == t && kv.second.bestLap < 1e8f)
                out.push_back(kv.second);
        }
        std::sort(out.begin(), out.end(), [](const PersonalBestRecord& a, const PersonalBestRecord& b) {
            return a.bestLap < b.bestLap;
        });
        if ((int)out.size() > maxN) out.resize(static_cast<size_t>(maxN));
        return out;
    }

    size_t size() const { return m_cache.size(); }

private:
    static std::string sanitizeId(const std::string& s) {
        std::string o;
        o.reserve(s.size());
        for (unsigned char c : s) {
            if (std::isalnum(c) || c == '_' || c == '-' || c == '.') o.push_back((char)c);
            else if (c == ' ' || c == '/') o.push_back('_');
        }
        if (o.size() > 64) o.resize(64);
        return o.empty() ? "unknown" : o;
    }

    static bool isSafeDir(const std::string& dir) {
        if (dir.empty() || dir.size() > 512) return false;
        if (dir.find("..") != std::string::npos) return false;
        if (dir.find('\0') != std::string::npos) return false;
        return true;
    }

    std::string filePath(const std::string& track, const std::string& car) const {
        return m_dir + "/" + key(track, car) + ".pb";
    }

    void saveOne(const PersonalBestRecord& r) {
        if (m_dir.empty()) return;
        std::ofstream out(filePath(r.trackId, r.carId));
        if (!out) return;
        out << "BestLap=" << r.bestLap << "\n";
        out << "S0=" << r.sectors[0] << "\n";
        out << "S1=" << r.sectors[1] << "\n";
        out << "S2=" << r.sectors[2] << "\n";
        out << "Laps=" << r.lapsCompleted << "\n";
        out << "Driver=" << r.driver << "\n";
    }

    std::string m_dir;
    std::map<std::string, PersonalBestRecord> m_cache;
};

} // namespace sim
} // namespace ks
