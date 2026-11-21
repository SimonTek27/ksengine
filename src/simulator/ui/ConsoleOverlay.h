#pragma once
/**
 * Developer console (roadmap 4.x): a rolling log plus a one-line command
 * prompt, opened and closed with INSERT.
 *
 * Text arrives through WM_CHAR, so the printable ASCII range is available
 * including punctuation; ENTER runs the line, ESC closes the console, the
 * arrow keys walk the command history. Commands are registered by verb (upper
 * case) and each handler appends its own output with addLine().
 *
 * Nothing here reaches into the renderer or the simulation: an overlay owns
 * what it draws and what it prints, like the chat does.
 */
#include "NativeUiTypes.h"
#include "TextRenderer.h"
#include "UiInput.h"

#include <cmath>
#include <cstddef>
#include <deque>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace ks {
namespace sim {
namespace ui {

class ConsoleOverlay {
public:
    ConsoleOverlay();

    void setVisible(bool v);
    bool isVisible() const { return m_visible; }
    bool isTyping() const { return m_typing; }
    void startTyping() { m_typing = true; }
    void stopTyping() { m_typing = false; }
    void toggle() { setVisible(!m_visible); }

    /// Command execution: return true when the command was handled.
    using CommandHandler = std::function<bool(const std::vector<std::string>& args)>;
    void registerCommand(std::string verb, CommandHandler handler);
    void execute(const std::string& line);

    void addLine(const std::string& line);
    const std::string& input() const { return m_input; }

    void build(DrawList& dl, int screenW, int screenH, UiInput* input);
    /** Compact mode: only the prompt line is drawn. */
    void setCompact(bool v) { m_compact = v; }
    bool compact() const { return m_compact; }

    /** The display grading the console tunes. The overlay owns the numbers
     *  only: the hook registered by the loop applies them to the renderer, so
     *  this file keeps its rule of never reaching into the frame. */
    struct GradeState {
        float temperatureK = 6500.f; ///< white point the image is balanced to
        float brightness = 1.f;      ///< linear gain before the tone curve
        float contrast = 1.f;        ///< around mid-grey, after the curve
        /** How much colour the image carries: 0 = grey, 1 = as rendered. The
         *  room's [COLOR] SATURATION, applied display-referred. */
        float saturation = 1.f;
        /** The room's stop. 0 = the room (and its [AUTO_EXPOSURE] loop) keeps
         *  owning it, which is what a console that never typed EXPOSURE does. */
        float exposure = 0.f;
        /// Rotation of the colour wheel, degrees (-180..180, 0 = as rendered).
        float hue = 0.f;
        /// Sepia wash over the image, 0 = none, 1 = full period print.
        float sepia = 0.f;
        /// Per-channel gains riding on the white-balance filter, 1 = untouched.
        /// 0 kills the channel outright, which is the point of the rows.
        float red = 1.f;
        float green = 1.f;
        float blue = 1.f;
        bool active = false; ///< a command touched it: the room stops owning the grade
    };

    void setGradeHook(std::function<void(const GradeState&)> hook) {
        m_gradeHook = std::move(hook);
    }

    /** The display the room itself asks for, pushed by the loop: the panel's
     *  state row shows reality while the console has not taken the display
     *  over, instead of the neutral defaults the console would apply. */
    struct RoomGrade {
        float temperatureK = 6500.f;
        float brightness = 1.f;
        float contrast = 1.f;
        float saturation = 1.f;
        float exposure = 0.f; ///< 0 = the room brings no stop of its own
        float hue = 0.f;
        float sepia = 0.f;
    };
    void setRoomGrade(const RoomGrade& grade) { m_roomGrade = grade; }

    /** Where a horizontal drag at `fraction` (0..1 along the track, already
     *  clamped) lands inside a row, snapped to that row's own step. Pure, so a
     *  test can check the ends and the snapping without a window. */
    static float sliderValueAt(float fraction, float min, float max, float step) {
        if (fraction < 0.0f) fraction = 0.0f;
        if (fraction > 1.0f) fraction = 1.0f;
        float value = min + (max - min) * fraction;
        if (step > 0.0f && max > min)
            value = min + std::round((value - min) / step) * step;
        return value < min ? min : (value > max ? max : value);
    }

    /** The row being dragged (0..3), or -1. Exposed for the tests. */
    int draggedRow() const { return m_drag; }
    /** What the log holds, oldest first. Exposed for the tests: the commands
     *  answer with the line they print, and that line is part of the contract. */
    const std::deque<std::string>& log() const { return m_lines; }
    /** Writes the tuned values where the room keeps them. The overlay owns no
     *  path: the hook reports back the line it wants printed, which is the file
     *  it wrote or the reason it could not. */
    /** One saved calibration, as the panel lists it. */
    struct GradePreset {
        std::string name;
        GradeState grade;
        bool active = false; ///< the one the room loads with
    };
    /** SAVE <name>: writes `grade` as `name`; `makeActive` records it as the
     *  preset the room starts with. Returns the line to print. */
    using GradeSaveHook = std::function<std::string(const GradeState&, const std::string& name,
                                                    bool makeActive)>;
    void setGradeSaveHook(GradeSaveHook hook) { m_gradeSave = std::move(hook); }
    /** LOAD / USE <name>: hands back a saved calibration. False when the room
     *  has no preset by that name. */
    using GradeLoadHook = std::function<bool(const std::string& name, GradeState& out)>;
    void setGradeLoadHook(GradeLoadHook hook) { m_gradeLoad = std::move(hook); }
    /** LIST: every calibration the open room carries. */
    using GradeListHook = std::function<std::vector<GradePreset>()>;
    void setGradeListHook(GradeListHook hook) { m_gradeList = std::move(hook); }

    /** One entry of the car's model.json material block, as MATLIST shows it. */
    struct MaterialLine {
        std::string name;
        std::string shader;
        std::string preset;
        std::vector<std::pair<std::string, std::string>> properties;
    };
    using MaterialListHook = std::function<std::vector<MaterialLine>()>;
    /** MAT <material> <key> <value>: edits and saves one entry; "shader" and
     *  "preset" set those fields. Returns the line to print. */
    using MaterialEditHook = std::function<std::string(const std::string& material,
                                                      const std::string& key,
                                                      const std::string& value)>;
    using MaterialRemoveHook = std::function<std::string(const std::string& material)>;
    using MaterialReloadHook = std::function<std::string()>;
    void setMaterialHooks(MaterialListHook list, MaterialEditHook edit,
                          MaterialRemoveHook remove, MaterialReloadHook reload) {
        m_materialList = std::move(list);
        m_materialEdit = std::move(edit);
        m_materialRemove = std::move(remove);
        m_materialReload = std::move(reload);
    }
    /** MATADD <material> [shader <name>] [preset <name>]: adds the entry (or
     *  tops up the shader/preset of the one there) and reloads the car once.
     *  Returns the line to print. */
    using MaterialAddHook = std::function<std::string(const std::string& material,
                                                     const std::string& shader,
                                                     const std::string& preset)>;
    void setMaterialAddHook(MaterialAddHook hook) { m_materialAdd = std::move(hook); }
    /** TC <level> / ABS <level>: the driver aids, changed while driving the way
     *  a car's own buttons would. `which` is 0 for traction control and 1 for
     *  ABS; a level of -1 asks for the current one. The hook owns the car, so it
     *  returns the line to print - it is the only side that knows whether the
     *  car carries that aid at all. */
    using AidHook = std::function<std::string(int which, int level)>;
    void setAidHook(AidHook hook) { m_aid = std::move(hook); }
    /** BB <percent> / BB: the brake bias, the third cockpit knob (front share,
     *  30..80 %). -1 asks for the value in use; the hook owns the car, so it
     *  clamps, applies it through the light path and returns the line to print
     *  (including whether the car's brakes can be adjusted at all). */
    using BiasHook = std::function<std::string(int percent)>;
    void setBiasHook(BiasHook hook) { m_bias = std::move(hook); }
    /** EDIT PHYSICS: opens the physics data screen for the showroom car and
     *  answers with the line to print. The hook owns the car, so it decides
     *  what "the car" is when no drive session runs and reports why the
     *  screen could not open. */
    using PhysicsEditHook = std::function<std::string()>;
    void setPhysicsEditHook(PhysicsEditHook hook) { m_physicsEdit = std::move(hook); }
    const GradeState& grade() const { return m_grade; }
    /// Applies a whole state at once (a start-up seed), reporting it like a row.
    void setGrade(const GradeState& grade);

    /// Non-character keys (virtual-key codes). True when handled.
    bool handleKey(int key);
    /// WM_CHAR payload. False when the console is closed, so the key falls
    /// through to whoever else may want it.
    bool handleChar(int c);

private:
    static constexpr std::size_t kMaxLines = 80;
    static constexpr std::size_t kMaxCmdHistory = 32;

    bool cmdTemperature(const std::vector<std::string>& args);
    bool cmdBrightness(const std::vector<std::string>& args);
    bool cmdContrast(const std::vector<std::string>& args);
    bool cmdSaturation(const std::vector<std::string>& args);
    bool cmdExposure(const std::vector<std::string>& args);
    bool cmdGrade(const std::vector<std::string>& args);
    /// One grading row: no argument prints the value, an argument sets it
    /// (clamped) or steps it when it carries a sign.
    bool setGradeValue(float& slot, const std::vector<std::string>& args, float min,
                       float max, const char* label);
    void applyGrade();
    /// Hands the current state to the hook without claiming it as active
    /// (GRADE RESET gives the display back to the room).
    void publishGrade();
    void reportGrade();

    bool cmdHelp(const std::vector<std::string>& args);
    bool cmdSavePreset(const std::vector<std::string>& args);
    bool cmdLoadPreset(const std::vector<std::string>& args);
    bool cmdUsePreset(const std::vector<std::string>& args);
    bool cmdResetGrade(const std::vector<std::string>& args);
    bool cmdListPresets(const std::vector<std::string>& args);
    bool cmdMaterialList(const std::vector<std::string>& args);
    bool cmdMaterial(const std::vector<std::string>& args);
    bool cmdMaterialRemove(const std::vector<std::string>& args);
    bool cmdMaterialReload(const std::vector<std::string>& args);
    bool cmdMaterialAdd(const std::vector<std::string>& args);
    /// TC / ABS: no argument reports the level, one sets it (which = 0 TC, 1 ABS).
    bool cmdAid(int which, const std::vector<std::string>& args);
    /// BB: no argument reports the bias, one sets it (front percentage).
    bool cmdBias(const std::vector<std::string>& args);
    bool cmdClear(const std::vector<std::string>& args);
    bool cmdScreenshot(const std::vector<std::string>& args);
    bool cmdPhotomode(const std::vector<std::string>& args);
    bool cmdPpfilter(const std::vector<std::string>& args);
    bool cmdHue(const std::vector<std::string>& args);
    bool cmdSepia(const std::vector<std::string>& args);
    bool cmdRed(const std::vector<std::string>& args);
    bool cmdGreen(const std::vector<std::string>& args);
    bool cmdBlue(const std::vector<std::string>& args);
    /// NEW: a grade from the neutral defaults, owned by the console (unlike
    /// RESET, which hands the display back to the room).
    bool cmdNewGrade(const std::vector<std::string>& args);
    /// EDIT PPFILTER: the five draggable rows above are the editor; the
    /// command takes the grade over from the room and reports it, so the
    /// rows move from what the frame is using now.
    bool cmdEdit(const std::vector<std::string>& args);
    bool cmdList(const std::vector<std::string>& args);
    bool cmdVersion(const std::vector<std::string>& args);
    bool cmdDebug(const std::vector<std::string>& args);
    bool cmdCockpitEditor(const std::vector<std::string>& args);
    bool cmdTheme(const std::vector<std::string>& args);

    // --- the calibration strip ------------------------------------------
    float sliderValue(int index) const;
    void setSliderValue(int index, float value);
    /// Copies what the frame is already using into the console's own state, so
    /// the first drag starts where the image is instead of at the neutral
    /// defaults.
    void adoptRoomGrade();

    void drawHistory(DrawList& dl, float x, float y, float w, float h);
    void drawInputLine(DrawList& dl, float x, float y, float w);

    std::deque<std::string> m_lines;
    std::deque<std::string> m_cmdHistory;
    std::size_t m_historyPos = 0; // command history: 0 = newest
    std::unordered_map<std::string, CommandHandler> m_commands;
    std::string m_input;
    GradeState m_grade;
    RoomGrade m_roomGrade;
    // Row under the cursor while the left button is held: -1 when idle. A drag
    // owns the row until the button comes up, so sliding past the track's end
    // keeps editing rather than dropping the grab.
    int m_drag = -1;
    std::function<void(const GradeState&)> m_gradeHook;
    GradeSaveHook m_gradeSave;
    GradeLoadHook m_gradeLoad;
    GradeListHook m_gradeList;
    MaterialListHook m_materialList;
    MaterialEditHook m_materialEdit;
    MaterialRemoveHook m_materialRemove;
    MaterialReloadHook m_materialReload;
    MaterialAddHook m_materialAdd;
    AidHook m_aid;
    BiasHook m_bias;
    PhysicsEditHook m_physicsEdit;

    bool m_visible = false;
    bool m_compact = true;
    bool m_typing = false;
    unsigned m_ticks = 0; // caret phase, no frame timer needed
    TextRenderer m_text;
};

} // namespace ui
} // namespace sim
} // namespace ks
