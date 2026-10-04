#pragma once
/**
 * Mouse / pointer input for Qt-free UI overlays.
 * Platform feeds events; widgets query hover/click via hit-test rects.
 */
#include "NativeUiTypes.h"
#include <cstdint>
#include <vector>
#include <functional>
#include <algorithm>

namespace ks {
namespace sim {
namespace ui {

enum class MouseButton : uint8_t {
    Left = 0,
    Right = 1,
    Middle = 2,
    Count = 3
};

enum class MouseEventType : uint8_t {
    Move,
    Down,
    Up,
    Wheel,
    Leave
};

struct MouseEvent {
    MouseEventType type = MouseEventType::Move;
    float x = 0, y = 0;
    MouseButton button = MouseButton::Left;
    float wheelDelta = 0; // +1 notch up typically
    bool shift = false;
    bool ctrl = false;
    bool alt = false;
};

struct MouseState {
    float x = 0, y = 0;
    bool inside = true;
    bool down[3] = {false, false, false};
    bool clicked[3] = {false, false, false}; // pressed this frame
    bool released[3] = {false, false, false};
    float wheel = 0;

    bool leftDown() const { return down[0]; }
    bool rightDown() const { return down[1]; }
    bool leftClicked() const { return clicked[0]; }
    bool leftReleased() const { return released[0]; }
};

/** Interactive region registered for hit-testing. */
struct UiHitTarget {
    int id = 0;
    Rect rect;
    int z = 0; // higher = on top
    std::function<void()> onClick;
    std::function<void(bool)> onHover; // true enter / false leave
};

class UiInput {
public:
    void beginFrame() {
        // Hit targets are registered by build() during this frame; the
        // one-frame input flags are NOT cleared here - the message pump
        // runs before renderFrame, so clearing them here would wipe every
        // click/scroll before any overlay could read it. They are retired
        // in endFrame() instead.
        m_targets.clear();
    }

    void endFrame() {
        // Resolve hover change
        int hit = hitTest(m_state.x, m_state.y);
        if (hit != m_hoveredId) {
            if (m_hoveredId >= 0) {
                if (auto* t = find(m_hoveredId))
                    if (t->onHover) t->onHover(false);
            }
            m_hoveredId = hit;
            if (m_hoveredId >= 0) {
                if (auto* t = find(m_hoveredId))
                    if (t->onHover) t->onHover(true);
            }
        }
        // Click on release over same target that received down
        if (m_state.released[0] && m_pressedId >= 0 && m_pressedId == hit) {
            if (auto* t = find(m_pressedId))
                if (t->onClick) t->onClick();
        }
        if (!m_state.down[0])
            m_pressedId = -1;
        for (int i = 0; i < 3; ++i) {
            m_state.clicked[i] = false;
            m_state.released[i] = false;
        }
        m_state.wheel = 0;
    }

    void inject(const MouseEvent& e) {
        switch (e.type) {
        case MouseEventType::Move:
            m_state.x = e.x;
            m_state.y = e.y;
            m_state.inside = true;
            break;
        case MouseEventType::Down: {
            const int b = static_cast<int>(e.button);
            if (b >= 0 && b < 3) {
                m_state.down[b] = true;
                m_state.clicked[b] = true;
            }
            m_state.x = e.x;
            m_state.y = e.y;
            if (e.button == MouseButton::Left)
                m_pressedId = hitTest(e.x, e.y);
            break;
        }
        case MouseEventType::Up: {
            const int b = static_cast<int>(e.button);
            if (b >= 0 && b < 3) {
                m_state.down[b] = false;
                m_state.released[b] = true;
            }
            m_state.x = e.x;
            m_state.y = e.y;
            break;
        }
        case MouseEventType::Wheel:
            m_state.wheel += e.wheelDelta;
            m_state.x = e.x;
            m_state.y = e.y;
            break;
        case MouseEventType::Leave:
            m_state.inside = false;
            break;
        }
        if (onEvent) onEvent(e);
    }

    /** Platform helpers */
    void setPosition(float x, float y) {
        MouseEvent e;
        e.type = MouseEventType::Move;
        e.x = x; e.y = y;
        inject(e);
    }
    void setButton(MouseButton b, bool pressed, float x, float y) {
        MouseEvent e;
        e.type = pressed ? MouseEventType::Down : MouseEventType::Up;
        e.button = b;
        e.x = x; e.y = y;
        inject(e);
    }
    void addWheel(float delta, float x, float y) {
        MouseEvent e;
        e.type = MouseEventType::Wheel;
        e.wheelDelta = delta;
        e.x = x; e.y = y;
        inject(e);
    }

    int registerTarget(const Rect& r, int z = 0) {
        UiHitTarget t;
        t.id = ++m_nextId;
        t.rect = r;
        t.z = z;
        m_targets.push_back(std::move(t));
        return m_targets.back().id;
    }

    void setOnClick(int id, std::function<void()> fn) {
        if (auto* t = find(id)) t->onClick = std::move(fn);
    }
    void setOnHover(int id, std::function<void(bool)> fn) {
        if (auto* t = find(id)) t->onHover = std::move(fn);
    }

    const MouseState& state() const { return m_state; }
    int hoveredId() const { return m_hoveredId; }
    int pressedId() const { return m_pressedId; }

    bool isHovering(const Rect& r) const {
        return m_state.inside && r.contains(m_state.x, m_state.y);
    }
    bool wasClicked(const Rect& r) const {
        return m_state.released[0] && r.contains(m_state.x, m_state.y)
            && r.contains(m_downX, m_downY);
    }

    /** Call on left down to remember press position for drag/click. */
    void notePressPosition() {
        m_downX = m_state.x;
        m_downY = m_state.y;
    }

    std::function<void(const MouseEvent&)> onEvent;

private:
    int hitTest(float x, float y) const {
        int best = -1;
        int bestZ = -1;
        for (const auto& t : m_targets) {
            if (t.rect.contains(x, y) && t.z >= bestZ) {
                bestZ = t.z;
                best = t.id;
            }
        }
        return best;
    }

    UiHitTarget* find(int id) {
        for (auto& t : m_targets)
            if (t.id == id) return &t;
        return nullptr;
    }

    MouseState m_state;
    std::vector<UiHitTarget> m_targets;
    int m_nextId = 0;
    int m_hoveredId = -1;
    int m_pressedId = -1;
    float m_downX = 0, m_downY = 0;
};

} // namespace ui
} // namespace sim
} // namespace ks
