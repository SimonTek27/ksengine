#include "PhysicsMessage.h"

namespace ks {
namespace physics {

PhysicsMessageBus& PhysicsMessageBus::instance() {
    static PhysicsMessageBus bus;
    return bus;
}

void PhysicsMessageBus::subscribe(const std::string& messageType, MessageHandler handler) {
    m_handlersByName[messageType].push_back(std::move(handler));
}

void PhysicsMessageBus::subscribe(MessageType type, MessageHandler handler) {
    m_handlersByType[static_cast<int>(type)].push_back(std::move(handler));
}

void PhysicsMessageBus::publish(const PhysicsMessage& message) {
    const std::string typeName = message.messageType();
    auto it = m_handlersByName.find(typeName);
    if (it != m_handlersByName.end()) {
        for (auto& handler : it->second)
            handler(message);
    }
    auto it2 = m_handlersByType.find(static_cast<int>(message.type));
    if (it2 != m_handlersByType.end()) {
        for (auto& handler : it2->second)
            handler(message);
    }
}

void PhysicsMessageBus::clear() {
    m_handlersByName.clear();
    m_handlersByType.clear();
}

} // namespace physics
} // namespace ks
