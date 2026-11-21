#pragma once
/**
 * The physics data screen, laid out the way the setup screen is laid out.
 *
 * From the showroom console `edit physics` opens this instead of a setup
 * sheet: the same tab strip, the same row grid, the same keys (UP/DOWN to
 * move, LEFT/RIGHT to adjust, Q/E for the tabs, ENTER to write the car
 * files, R to go back to the numbers the car opened with, ESC to close).
 * What differs is the document: not the car's setup.ini but its data files
 * (engine, aero, suspensions, drivetrain, brakes, tyres) plus the geometry
 * the setup screen does not own (roll centers, sprung-mass placement,
 * compliances) and the wings with their positions, not just their angles.
 *
 * Every row edits the live VehicleSimulator on the spot (the showroom car
 * gets its physics loaded on demand when the screen opens), and ENTER
 * writes the same numbers back to the data/*.ini files with a
 * read-modify-write pass, so comments and unknown keys survive.
 *
 * Qt-free; draws via ui::DrawList from NativeUiHub, like SetupGarage.
 */
#include <functional>
#include <map>
#include <string>
#include <vector>

namespace ks {
namespace engine {
namespace config {
class Locale;
}
}
}

namespace ks {
namespace physics {
class VehicleSimulator;
}
}

namespace ks {
namespace sim {

namespace ui {
class DrawList;
class TextRenderer;
class UiInput;
}

class PhysicsGarage {
public:
    PhysicsGarage() = default;
    ~PhysicsGarage() = default;

    /** One adjustable number: a live getter/setter pair plus its range. */
    struct Row {
        std::string id;    ///< stable key (setRowValue, tests)
        std::string label; ///< printed on the row
        std::string unit;  ///< printed after the value, may be empty
        int decimals = 0;
        double min = 0.0;
        double max = 1.0;
        double step = 0.1;
        std::function<double()> get;
        std::function<void(double)> set; ///< applies live, marks dirty
        std::string help;
        double base = 0.0; ///< the value the car opened with (delta arrows, R)
        bool isAction = false; ///< an action row (ADD/REMOVE WING/GEAR)
        std::function<std::string()> fire; ///< runs on ENTER/RIGHT/LEFT/click
    };
    struct Tab {
        std::string name;
        std::vector<Row> rows;
    };

    /**
     * Binds the screen to a live vehicle and rebuilds the rows from what it
     * runs on now. A null vehicle leaves the document the car shipped with
     * (zeros), so the screen still opens; every setter guards it.
     */
    void attach(physics::VehicleSimulator* vehicle, const std::string& carDir,
                const std::string& carName);

    void update(float dt);
    void render(int width, int height); // timers only; draw via build()

    /** Emit the screen into DrawList. */
    void build(ui::DrawList& dl, ui::TextRenderer& text, int width, int height,
               ui::UiInput* input = nullptr);

    bool handleKeyPress(int key);
    bool handleKeyRelease(int key);

    void setVisible(bool v);
    bool isVisible() const { return m_visible; }
    void toggleVisible() { setVisible(!m_visible); }
    float fadeAlpha() const { return m_fadeIn; }

    void setLocale(const engine::config::Locale* locale) { m_locale = locale; }

    const std::string& tab() const { return m_tab; }
    void selectTabByName(const std::string& name);
    int selectedRow() const { return m_selectedRow; }
    const std::vector<Tab>& tabs() const { return m_tabs; }
    /** The rows of the open tab, in the order the screen draws them. */
    std::vector<const Row*> rows() const;

    void setCarName(const std::string& n) { m_carName = n; }
    const std::string& status() const { return m_status; }
    bool dirty() const { return m_dirty; }

    /** Puts one row at a value (a capture, or a script driving the screen):
     *  clamped to the row's range, then handed to the car. */
    bool setRowValue(const std::string& id, double value);

    /**
     * Writes the live numbers back to the car's data/*.ini files
     * (read-modify-write: comments and unknown keys survive) and reports the
     * line the console prints: what was written, or the reason it could not.
     */
    std::string saveToCarFiles();

private:
    std::string tr(const char* key, const char* fallback) const;
    void clampSelection();
    void nextTab(int direction);
    void selectTab(const std::string& tab);
    void adjustSelected(int direction);
    void resetToBase();
    /// Rebuilds the tabs from the live vehicle (a wing or gear was added or
    /// removed), keeping the page, the cursor and the attach-time bases the
    /// delta arrows measure against.
    void buildTabsKeepSelection();
    /// The tab builders, shared by attach() and buildTabsKeepSelection().
    void rebuildTabs();
    /// Carries the attach-time numbers into fresh rows; new rows (an added
    /// wing or gear) open with base == current, so no arrow shows.
    void syncBases();

    const engine::config::Locale* m_locale = nullptr;
    physics::VehicleSimulator* m_vehicle = nullptr;
    std::string m_carDir;
    std::string m_carName;
    std::vector<Tab> m_tabs;
    std::string m_tab = "ENGINE";
    int m_selectedRow = 0;
    bool m_visible = false;
    float m_fadeIn = 0.f;
    float m_slide = 0.f;
    bool m_dirty = false;
    std::string m_status;
    /// Attach-time value per row id: what R restores and the arrows measure.
    std::map<std::string, double> m_savedBases;
};

} // namespace sim
} // namespace ks
