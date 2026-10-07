// Drop-in replacement body for MultiCarManager::update — traffic-aware AI
void MultiCarManager::update(float dt)
{
    std::vector<AiTrafficCar> traffic;
    traffic.reserve(m_cars.size());
    for (const auto& car : m_cars) {
        if (!car || !car->isActive || !car->vehicle) continue;
        auto st = car->vehicle->getState();
        AiTrafficCar t;
        t.id = car->id;
        t.x = st.position.x;
        t.z = st.position.z;
        t.heading = st.heading;
        t.speed = st.speed;
        traffic.push_back(t);
    }
    for (auto& car : m_cars) {
        if (!car || !car->isActive || !car->vehicle) continue;
        if (!car->isPlayer && car->ai && car->ai->isReady()) {
            auto state = car->vehicle->getState();
            car->ai->update(
                vec3(state.position.x, state.position.y, state.position.z),
                state.heading, state.speed, state.gear, dt, traffic, car->id);
            car->vehicle->setThrottle(car->ai->throttle());
            car->vehicle->setBrake(car->ai->brake());
            car->vehicle->setSteering(car->ai->steering());
        }
        car->vehicle->updatePhysics(dt);
        auto state = car->vehicle->getState();
        car->transform = mat4();
        car->transform(0, 3) = state.position.x;
        car->transform(1, 3) = state.position.y;
        car->transform(2, 3) = state.position.z;
    }
    if (m_collisionEnabled) checkCollisions();
}
