# Pit lane collisions

## Model

- **Car–car**: 2D OBB (length × width) on XZ, SAT + soft separation + impulse
- **Car–wall**: corridor ±`pitHalfWidthM` from the pit axis
- Low typical speeds → `restitution` 0.15, limited `maxImpulse`

## Frame pipeline

```text
queue.updateCar / update
    ▼
syncPitCollisionFromQueue(col, queue, pitHeading)
    ▼
col.step(dt)          // contacts + resolve pose/vel
    ▼
applyPitCollisionResults → vehicle poses
    ▼
damageImpulseFor(id)  → DamageSystem if relSpeed > threshold
```

## Code

```cpp
PitLaneCollision col;
col.setAxis(queue.axis());
PitLaneCollisionConfig cfg;
cfg.pitHalfWidthM = 3.5f;
cfg.damageSpeedThreshold = 2.5f;
col.setConfig(cfg);

col.onContact = [&](const PitCollisionContact& c) {
    if (c.relSpeed > cfg.damageSpeedThreshold)
        damage.applyImpulse(c.impulse, /*zone*/…);
};

// each car
col.upsert({ id, x, z, heading, vx, vz, 4.6f, 2.0f, mass });
col.step(dt);

// write back
for (auto& b : col.bodies())
    vehicle.setPosVel(b.carId, b.x, b.z, b.vx, b.vz);
```

## Useful parameters

| Field | Default | Role |
|-------|---------|------|
| `pitHalfWidthM` | 3.5 | corridor half-width |
| `minSpacing` (queue) | 8 | prevents overlap before contact |
| `restitution` | 0.15 | soft bounce |
| `damageSpeedThreshold` | 2.5 m/s | below = push only |

File: `src/simulator/PitLaneCollision.h`
