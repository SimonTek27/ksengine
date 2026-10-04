#pragma once
#include <string>
namespace ks { namespace physics {
class TrackPhysics {
public:
    bool load(const std::string& /*path*/) { return false; }
    float surfaceGrip(float /*x*/, float /*z*/) const { return 1.0f; }
};
}} // namespace
