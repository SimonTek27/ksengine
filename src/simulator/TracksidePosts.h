#pragma once
/**
 * Trackside posts: the safety-car bay at the pit exit and the marshals.
 *
 * Same idea as PitGarage.h — a post is a PAD (a footprint someone parks in) plus
 * the people who work it. The difference is what happens at runtime: a garage is
 * passive, a marshal post is dynamic. It watches a radius around itself and
 * reacts to what is in it, instead of waiting for the session to raise a flag.
 *
 * Four posts per corner, as the track asset describes them: two on the right of
 * the corner and two on the left (one at the entry, one at the exit). An
 * incident makes the nearest post attend it and every post of that corner — plus
 * the corner leading into it — wave yellow for a while.
 *
 * Header-only and Qt-free like the rest of the sim headers.
 */
#include "GarageSpawn.h" // WorldPose
#include "RaceFlag.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

namespace ks {
namespace sim {

/// The one marshal mesh every track instances. Vallelunga ships
/// marshall_A_HI_010 (660 vertices, 908 triangles, 1.78 m tall) next to a
/// low-detail twin; posts always use the high-detail one. The mesh is baked
/// ONCE into the shared prop folder, recentred on its own footprint so it can
/// stand at any coordinate:
///
///   kn5baker --only marshall_A_HI_010 --recentre <track.kn5> content/objects3D/marshal
///
/// A track then carries only the coordinates of its posts, and its own bake
/// leaves the marshal out with `--skip marshall_A_`: one copy of the geometry,
/// referenced by position from every track.
inline constexpr const char* kMarshalMeshName = "marshall_A_HI_010";
/// Shared asset folder, relative to the content root: manifest.txt + .nmsh +
/// textures/, the shape NativeRenderer::loadMeshFromFile() reads. Loaded once
/// and instanced at each post, never as static scene at a baked-in position.
inline constexpr const char* kMarshalAssetDir = "objects3D/marshal";

/// A footprint a vehicle parks in: the safety-car bay at the pit exit, and any
/// recovery vehicle spot the track authors later.
struct TracksidePad {
    int index = 0;
    WorldPose pose;
    float radiusM = 8.f;
    bool occupied = false;
    int occupantRaceNumber = -1;

    bool contains(float x, float z, float extraM = 0.f) const {
        const float dx = x - pose.x;
        const float dz = z - pose.z;
        const float radius = radiusM + extraM;
        return (dx * dx + dz * dz) <= radius * radius;
    }
};

enum class MarshalActivity : uint8_t {
    Idle = 0,      // nothing to do
    Alerted,       // incident nearby, preparing the flag
    Flagging,      // yellow out
    Attending      // the closest post, working the incident
};

struct MarshalPost {
    int corner = 0;       // corner this post belongs to (1-based, lap order)
    std::string side;     // "left" | "right"
    float station = 0.f;  // metres from the start line along the reference line
    // Mesh instanced for this post. Defaults to the high-detail marshal, so a
    // manifest that predates the field (or a track with no marshal geometry)
    // still renders one post per worker.
    std::string mesh = kMarshalMeshName;
    WorldPose pose;
    float radiusM = 40.f; // how far it watches
    MarshalActivity activity = MarshalActivity::Idle;
    float timer = 0.f;    // seconds of flag left

    bool flagging() const {
        return activity == MarshalActivity::Flagging || activity == MarshalActivity::Attending;
    }
};

/// What a post reacts to: a car that has stopped, gone off, or is crawling.
struct MarshalIncident {
    float x = 0.f;
    float z = 0.f;
    float speedMs = 0.f;
    float stoppedSec = 0.f; // seconds stationary, 0 when moving
    bool offTrack = false;

    /// A car parked on the racing line for two seconds is worth a yellow even
    /// if it never left the asphalt.
    bool actionable() const { return offTrack || stoppedSec >= 2.f || speedMs < 2.5f; }
};

class TracksidePosts {
public:
    void addPost(const MarshalPost& post) { m_posts.push_back(post); }

    void setSafetyCarPad(const TracksidePad& pad) {
        m_safetyCar = pad;
        m_hasSafetyCar = true;
    }

    const TracksidePad& safetyCarPad() const { return m_safetyCar; }
    bool hasSafetyCarPad() const { return m_hasSafetyCar; }
    bool safetyCarParked(float x, float z) const {
        return m_hasSafetyCar && m_safetyCar.contains(x, z);
    }

    std::size_t postCount() const { return m_posts.size(); }
    const std::vector<MarshalPost>& posts() const { return m_posts; }

    /// Posts of one corner, in the order they were added (entry-left,
    /// entry-right, exit-left, exit-right as the asset emits them).
    std::vector<const MarshalPost*> postsOfCorner(int corner) const {
        std::vector<const MarshalPost*> out;
        for (const auto& p : m_posts)
            if (p.corner == corner) out.push_back(&p);
        return out;
    }

    int flaggingCount() const {
        int n = 0;
        for (const auto& p : m_posts)
            if (p.flagging()) ++n;
        return n;
    }

    /// Local flag for a corner: yellow as soon as one of its posts is out.
    RaceFlag flagForCorner(int corner) const {
        for (const auto& p : m_posts)
            if (p.corner == corner && p.flagging()) return RaceFlag::Yellow;
        return RaceFlag::None;
    }

    /// Highest flag any post is showing: what the HUD and the session light up.
    RaceFlag trackFlag() const {
        return flaggingCount() > 0 ? RaceFlag::Yellow : RaceFlag::None;
    }

    /**
     * One simulation step.
     *
     * @param dt        seconds
     * @param incidents cars in trouble (the caller builds them from vehicle
     *                  state; no vehicle type is dragged in here)
     */
    void update(float dt, const std::vector<MarshalIncident>& incidents) {
        for (auto& post : m_posts) {
            post.timer = std::max(0.f, post.timer - dt);
            if (post.timer <= 0.f) post.activity = MarshalActivity::Idle;
        }

        for (const MarshalIncident& incident : incidents) {
            if (!incident.actionable()) continue;
            int nearest = -1;
            float bestDistance = 0.f;
            for (std::size_t i = 0; i < m_posts.size(); ++i) {
                const float dx = incident.x - m_posts[i].pose.x;
                const float dz = incident.z - m_posts[i].pose.z;
                const float distance = std::sqrt(dx * dx + dz * dz);
                if (distance > m_posts[i].radiusM) continue;
                if (nearest < 0 || distance < bestDistance) {
                    nearest = static_cast<int>(i);
                    bestDistance = distance;
                }
            }
            if (nearest < 0) continue; // nobody saw it

            MarshalPost& worker = m_posts[static_cast<std::size_t>(nearest)];
            worker.activity = MarshalActivity::Attending;
            worker.timer = kFlagHoldSec;

            // Same corner and the one leading into it: that is what a driver
            // approaching the incident needs to see. The post that attends keeps
            // its own state — it is working the incident, not waving at it.
            for (std::size_t i = 0; i < m_posts.size(); ++i) {
                if (static_cast<int>(i) == nearest) continue;
                MarshalPost& post = m_posts[i];
                if (post.corner != worker.corner && post.corner != worker.corner - 1) continue;
                post.activity = MarshalActivity::Flagging;
                post.timer = std::max(post.timer, kFlagHoldSec);
            }
        }
    }

    static constexpr float kFlagHoldSec = 10.f;

    /**
     * Loads the posts and the safety-car bay from a track manifest — the
     * "marshal_posts" / "safety_car" blocks tools/trackmodules writes next to
     * data/data.json.
     *
     * Deliberately a string scan like TrackDataJsonLoader: no JSON library, no
     * nesting beyond the two blocks this needs, so the file stays header-only
     * and is compiled by whoever includes it.
     *
     * @return true when at least one post or the bay was read.
     */
    bool loadFromJson(const std::string& text) {
        if (text.find('{') == std::string::npos) return false;
        bool any = false;

        const std::size_t list = findKey(text, "marshal_posts");
        if (list != std::string::npos) {
            const std::size_t open = text.find('[', list);
            const std::size_t close =
                open == std::string::npos ? std::string::npos : matchingClose(text, open);
            if (open != std::string::npos && close != std::string::npos) {
                std::size_t cursor = open;
                while (cursor < close) {
                    const std::size_t begin = text.find('{', cursor);
                    if (begin == std::string::npos || begin > close) break;
                    const std::size_t end = text.find('}', begin);
                    if (end == std::string::npos) break;
                    const std::string object = text.substr(begin, end - begin + 1);
                    MarshalPost post;
                    post.corner = static_cast<int>(scalarOf(object, "corner"));
                    post.side = textOf(object, "side");
                    post.station = static_cast<float>(scalarOf(object, "station"));
                    post.radiusM = kDefaultWatchRadiusM;
                    const std::string mesh = textOf(object, "mesh");
                    if (!mesh.empty()) post.mesh = mesh;
                    readPosition(object, post.pose);
                    if (post.corner > 0) {
                        m_posts.push_back(post);
                        any = true;
                    }
                    cursor = end + 1;
                }
            }
        }

        const std::size_t bay = findKey(text, "safety_car");
        if (bay != std::string::npos) {
            const std::string window = text.substr(bay, 512);
            const std::size_t brace = window.find('{');
            const std::size_t null = window.find("null");
            if (brace != std::string::npos && (null == std::string::npos || null > brace)) {
                const std::size_t end = window.find('}', brace);
                const std::string object =
                    window.substr(brace, end == std::string::npos ? std::string::npos : end - brace);
                TracksidePad pad;
                pad.index = 0;
                readPosition(object, pad.pose);
                pad.radiusM = static_cast<float>(scalarOf(object, "radius_m"));
                if (pad.radiusM <= 0.f) pad.radiusM = 8.f;
                setSafetyCarPad(pad);
                any = true;
            }
        }
        return any;
    }

    /// Convenience: reads the manifest from disk. False when unreadable/empty.
    bool loadFromFile(const std::string& path) {
        std::ifstream in(path);
        if (!in) return false;
        const std::string text((std::istreambuf_iterator<char>(in)),
                               std::istreambuf_iterator<char>());
        return loadFromJson(text);
    }

    static constexpr float kDefaultWatchRadiusM = 40.f;

private:
    /// Post/bay position: flat x/y/z, which is what data.json carries today,
    /// with the [x, y, z] "center" triplet as the fallback so a manifest in
    /// either shape loads to real coordinates instead of the origin.
    static void readPosition(const std::string& object, WorldPose& pose) {
        if (findKey(object, "x") != std::string::npos) {
            pose.x = static_cast<float>(scalarOf(object, "x"));
            pose.y = static_cast<float>(scalarOf(object, "y"));
            pose.z = static_cast<float>(scalarOf(object, "z"));
            return;
        }
        double center[3] = {0.0, 0.0, 0.0};
        if (!tripletOf(object, "center", center)) return;
        pose.x = static_cast<float>(center[0]);
        pose.y = static_cast<float>(center[1]);
        pose.z = static_cast<float>(center[2]);
    }

    /// First three numbers of a [a, b, c] array stored under `key`.
    static bool tripletOf(const std::string& text, const char* key, double out[3]) {
        const std::size_t keyPos = findKey(text, key);
        if (keyPos == std::string::npos) return false;
        const std::size_t open = text.find('[', keyPos);
        if (open == std::string::npos) return false;
        const std::size_t close = text.find(']', open);
        if (close == std::string::npos) return false;
        const std::string body = text.substr(open + 1, close - open - 1);
        const char* cursor = body.c_str();
        for (int i = 0; i < 3; ++i) {
            // strtod stops on the separator, not past it: skip the comma (and
            // any space) between the three numbers or the second read fails.
            while (*cursor && (*cursor == ',' || std::isspace(static_cast<unsigned char>(*cursor))))
                ++cursor;
            char* end = nullptr;
            const double value = std::strtod(cursor, &end);
            if (end == cursor) return false;
            out[i] = value;
            cursor = end;
        }
        return true;
    }

    /// Index of the bracket that closes the one at `open` (depth-counted: the
    /// per-post [x, y, z] triplets would otherwise end the list immediately).
    static std::size_t matchingClose(const std::string& text, std::size_t open) {
        int depth = 0;
        for (std::size_t i = open; i < text.size(); ++i) {
            const char c = text[i];
            if (c == '[' || c == '{') ++depth;
            else if (c == ']' || c == '}') {
                --depth;
                if (depth == 0) return i;
            }
        }
        return std::string::npos;
    }

    static std::size_t findKey(const std::string& text, const char* key) {
        return text.find(std::string("\"") + key + "\"");
    }

    static double scalarOf(const std::string& text, const char* key) {
        const std::size_t keyPos = findKey(text, key);
        if (keyPos == std::string::npos) return 0.0;
        const char* begin = text.c_str() + keyPos;
        while (*begin && !(std::isdigit(static_cast<unsigned char>(*begin)) || *begin == '-' ||
                           *begin == '+'))
            ++begin;
        char* end = nullptr;
        const double value = std::strtod(begin, &end);
        return end == begin ? 0.0 : value;
    }

    static std::string textOf(const std::string& text, const char* key) {
        const std::size_t keyPos = findKey(text, key);
        if (keyPos == std::string::npos) return std::string();
        const std::size_t quote = text.find('"', text.find(':', keyPos));
        if (quote == std::string::npos) return std::string();
        const std::size_t end = text.find('"', quote + 1);
        if (end == std::string::npos) return std::string();
        return text.substr(quote + 1, end - quote - 1);
    }

    std::vector<MarshalPost> m_posts;
    TracksidePad m_safetyCar;
    bool m_hasSafetyCar = false;
};

} // namespace sim
} // namespace ks
