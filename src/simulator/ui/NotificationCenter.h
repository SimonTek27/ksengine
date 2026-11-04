#pragma once
/**
 * What the bar reports.
 *
 * The simulator has a handful of moments worth telling the player about while
 * the menu is up — an output device that failed to open, a server that started
 * or refused to, a showroom with no car in it. They used to exist only as a
 * line in the log, which nobody reads from the driver's seat.
 *
 * This is the list behind the bell: the badge counts what arrived while the
 * player was not looking, and opening the panel is what clears it. It holds
 * text only, so it cannot grow into a second event system; anything with state
 * of its own belongs to the subsystem that owns that state.
 */
#include <cstddef>
#include <deque>
#include <string>
#include <utility>

namespace ks {
namespace sim {
namespace ui {

enum class NoticeKind {
    Info,
    Warning,
};

struct Notice {
    std::string text;
    NoticeKind kind = NoticeKind::Info;
};

class NotificationCenter {
public:
    /** How many notices the panel can hold. An older one falls off the front:
     *  the list is a readout of what just happened, not a log. */
    static constexpr std::size_t kCapacity = 12;

    void push(std::string text, NoticeKind kind = NoticeKind::Info) {
        if (text.empty()) return;
        m_items.push_back({std::move(text), kind});
        if (m_items.size() > kCapacity) m_items.pop_front();
        ++m_unread;
    }

    const std::deque<Notice>& items() const { return m_items; }
    std::size_t unread() const { return m_unread; }
    bool open() const { return m_open; }
    bool empty() const { return m_items.empty(); }

    /** Opening the list is reading it. */
    void setOpen(bool open) {
        m_open = open;
        if (m_open) m_unread = 0;
    }

    void toggle() { setOpen(!m_open); }

    void clear() {
        m_items.clear();
        m_unread = 0;
        m_open = false;
    }

private:
    std::deque<Notice> m_items;
    std::size_t m_unread = 0;
    bool m_open = false;
};

} // namespace ui
} // namespace sim
} // namespace ks
