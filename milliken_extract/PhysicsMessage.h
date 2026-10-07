#pragma once
/**
 * Physics message bus — Qt-free (std::string / PhysVec3).
 */
#include "PhysicsCoreTypes.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <cstdint>

namespace ks {
namespace physics {

enum class MessageType {
    Generic = 0,
    Collision,
    LapCompleted,
    SectorCompleted,
    WeatherChanged,
    Damage,
    Count
};

struct PhysicsMessage {
    MessageType type = MessageType::Generic;
    virtual ~PhysicsMessage() = default;
    virtual std::string messageType() const { return "PhysicsMessage"; }
};

struct CollisionMessage : public PhysicsMessage {
    double impactForce = 0;
    PhysVec3 collisionPoint{};
    int otherObjectIndex = -1;
    CollisionMessage() { type = MessageType::Collision; }
    std::string messageType() const override { return "CollisionMessage"; }
};

struct LapCompletedMessage : public PhysicsMessage {
    double lapTime = 0;
    int lapNumber = 0;
    LapCompletedMessage() { type = MessageType::LapCompleted; }
    std::string messageType() const override { return "LapCompletedMessage"; }
};

using MessageHandler = std::function<void(const PhysicsMessage&)>;

class PhysicsMessageBus {
public:
    static PhysicsMessageBus& instance();

    void subscribe(const std::string& messageType, MessageHandler handler);
    void subscribe(MessageType type, MessageHandler handler);
    void publish(const PhysicsMessage& message);
    void clear();

private:
    PhysicsMessageBus() = default;
    std::unordered_map<std::string, std::vector<MessageHandler>> m_handlersByName;
    std::unordered_map<int, std::vector<MessageHandler>> m_handlersByType;
};

} // namespace physics
} // namespace ks
