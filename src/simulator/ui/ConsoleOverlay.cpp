#include "ConsoleOverlay.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sstream>

namespace ks {
namespace sim {
namespace ui {

namespace {

/** The rows of the calibration strip, in draw order. The exposure row stays
 *  last and starts at 0, which is the one value that means "the room keeps
 *  it". HUE turns the colour wheel around neutral grey, SEPIA washes the
 *  period print over the image, and RED / GREEN / BLUE trim one channel each
 *  on top of the white-balance filter (0 kills the channel, 1 leaves it). */
struct ConsoleSliderSpec {
    const char* label;
    float min;
    float max;
    float step;
    int decimals;
};
constexpr int kConsoleSliderCount = 10;
constexpr ConsoleSliderSpec kConsoleSliders[kConsoleSliderCount] = {
    {"TEMPERATURE", 1000.f, 15000.f, 50.f, 0},
    {"BRIGHTNESS", 0.05f, 4.f, 0.01f, 2},
    {"CONTRAST", 0.10f, 3.f, 0.01f, 2},
    // How much colour the image carries: 0 is grey, 1 is as rendered. The floor
    // is a real 0 — a colour row dragged to the end means the monochrome image.
    {"SATURATION", 0.f, 2.f, 0.01f, 2},
    {"HUE", -180.f, 180.f, 1.f, 0},
    {"SEPIA", 0.f, 1.f, 0.01f, 2},
    {"RED", 0.f, 2.f, 0.01f, 2},
    {"GREEN", 0.f, 2.f, 0.01f, 2},
    {"BLUE", 0.f, 2.f, 0.01f, 2},
    {"EXPOSURE", 0.f, 8.f, 0.01f, 2},
};

const char* environmentValue(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && *value != 0 ? value : nullptr;
}

std::string upperCopy(std::string text) {
    for (char& c : text) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return text;
}

} // namespace

ConsoleOverlay::ConsoleOverlay() {
    registerCommand("HELP", [this](const std::vector<std::string>& args) { return cmdHelp(args); });
    registerCommand("CLEAR", [this](const std::vector<std::string>& args) { return cmdClear(args); });
    registerCommand("SCREENSHOT",
                    [this](const std::vector<std::string>& args) { return cmdScreenshot(args); });
    registerCommand("PHOTOMODE",
                    [this](const std::vector<std::string>& args) { return cmdPhotomode(args); });
    registerCommand("PPFILTER",
                    [this](const std::vector<std::string>& args) { return cmdPpfilter(args); });
    registerCommand("EDIT",
                    [this](const std::vector<std::string>& args) { return cmdEdit(args); });
    registerCommand("LIST", [this](const std::vector<std::string>& args) { return cmdList(args); });
    registerCommand("VERSION",
                    [this](const std::vector<std::string>& args) { return cmdVersion(args); });
    registerCommand("DEBUG", [this](const std::vector<std::string>& args) { return cmdDebug(args); });
    registerCommand("COCKPITEDITOR",
                    [this](const std::vector<std::string>& args) { return cmdCockpitEditor(args); });
    registerCommand("THEME", [this](const std::vector<std::string>& args) { return cmdTheme(args); });
    registerCommand("TEMPERATURE",
                    [this](const std::vector<std::string>& args) { return cmdTemperature(args); });
    registerCommand("BRIGHTNESS",
                    [this](const std::vector<std::string>& args) { return cmdBrightness(args); });
    registerCommand("CONTRAST",
                    [this](const std::vector<std::string>& args) { return cmdContrast(args); });
    registerCommand("GRADE", [this](const std::vector<std::string>& args) { return cmdGrade(args); });
    registerCommand("SAVE",
                    [this](const std::vector<std::string>& args) { return cmdSavePreset(args); });
    registerCommand("LOAD",
                    [this](const std::vector<std::string>& args) { return cmdLoadPreset(args); });
    registerCommand("USE",
                    [this](const std::vector<std::string>& args) { return cmdUsePreset(args); });
    registerCommand("RESET",
                    [this](const std::vector<std::string>& args) { return cmdResetGrade(args); });
    registerCommand("MATLIST",
                    [this](const std::vector<std::string>& args) { return cmdMaterialList(args); });
    registerCommand("MAT",
                    [this](const std::vector<std::string>& args) { return cmdMaterial(args); });
    registerCommand("MATREMOVE",
                    [this](const std::vector<std::string>& args) { return cmdMaterialRemove(args); });
    registerCommand("MATRELOAD",
                    [this](const std::vector<std::string>& args) { return cmdMaterialReload(args); });
    registerCommand("MATADD",
                    [this](const std::vector<std::string>& args) { return cmdMaterialAdd(args); });
    // The driver aids, on the same keys a car has for them - while the car is
    // moving, which is the one place a level matters.
    registerCommand("TC", [this](const std::vector<std::string>& args) { return cmdAid(0, args); });
    registerCommand("ABS", [this](const std::vector<std::string>& args) { return cmdAid(1, args); });
    registerCommand("BB", [this](const std::vector<std::string>& args) { return cmdBias(args); });
    registerCommand("EXPOSURE",
                    [this](const std::vector<std::string>& args) { return cmdExposure(args); });
    registerCommand("HUE",
                    [this](const std::vector<std::string>& args) { return cmdHue(args); });
    registerCommand("SEPIA",
                    [this](const std::vector<std::string>& args) { return cmdSepia(args); });
    registerCommand("RED",
                    [this](const std::vector<std::string>& args) { return cmdRed(args); });
    registerCommand("GREEN",
                    [this](const std::vector<std::string>& args) { return cmdGreen(args); });
    registerCommand("BLUE",
                    [this](const std::vector<std::string>& args) { return cmdBlue(args); });
    registerCommand("NEW",
                    [this](const std::vector<std::string>& args) { return cmdNewGrade(args); });
    registerCommand("SATURATION",
                    [this](const std::vector<std::string>& args) { return cmdSaturation(args); });
    addLine("ksim console - HELP lists the commands, INSERT closes it.");
}

void ConsoleOverlay::setVisible(bool v) {
    m_visible = v;
    if (!v) {
        m_typing = false;
        m_input.clear();
        m_historyPos = 0;
    }
}

void ConsoleOverlay::registerCommand(std::string verb, CommandHandler handler) {
    m_commands[upperCopy(std::move(verb))] = std::move(handler);
}

void ConsoleOverlay::addLine(const std::string& line) {
    m_lines.push_back(line);
    while (m_lines.size() > kMaxLines) m_lines.pop_front();
}

void ConsoleOverlay::execute(const std::string& line) {
    if (line.empty()) return;

    m_cmdHistory.push_back(line);
    while (m_cmdHistory.size() > kMaxCmdHistory) m_cmdHistory.pop_front();
    m_historyPos = 0;

    std::istringstream stream(line);
    std::string verb;
    stream >> verb;
    std::vector<std::string> args;
    std::string argument;
    while (stream >> argument) args.push_back(argument);

    addLine("> " + line);
    const auto handler = m_commands.find(upperCopy(verb));
    if (handler == m_commands.end()) {
        addLine("  unknown command '" + verb + "' (type HELP)");
        return;
    }
    handler->second(args);
}

float ConsoleOverlay::sliderValue(int index) const {
    switch (index) {
    case 0: return m_grade.temperatureK;
    case 1: return m_grade.brightness;
    case 2: return m_grade.contrast;
    case 3: return m_grade.saturation;
    case 4: return m_grade.hue;
    case 5: return m_grade.sepia;
    case 6: return m_grade.red;
    case 7: return m_grade.green;
    case 8: return m_grade.blue;
    default: return m_grade.exposure;
    }
}

void ConsoleOverlay::setSliderValue(int index, float value) {
    if (index < 0 || index >= kConsoleSliderCount) return;
    const ConsoleSliderSpec& spec = kConsoleSliders[index];
    value = value < spec.min ? spec.min : (value > spec.max ? spec.max : value);
    if (spec.step > 0.0f && spec.max > spec.min)
        value = spec.min + std::round((value - spec.min) / spec.step) * spec.step;
    switch (index) {
    case 0: m_grade.temperatureK = value; break;
    case 1: m_grade.brightness = value; break;
    case 2: m_grade.contrast = value; break;
    case 3: m_grade.saturation = value; break;
    case 4: m_grade.hue = value; break;
    case 5: m_grade.sepia = value; break;
    case 6: m_grade.red = value; break;
    case 7: m_grade.green = value; break;
    case 8: m_grade.blue = value; break;
    default: m_grade.exposure = value; break;
    }
    applyGrade();
}

void ConsoleOverlay::adoptRoomGrade() {
    if (m_grade.active) return;
    m_grade.temperatureK = m_roomGrade.temperatureK;
    m_grade.brightness = m_roomGrade.brightness;
    m_grade.contrast = m_roomGrade.contrast;
    m_grade.saturation = m_roomGrade.saturation;
    m_grade.exposure = m_roomGrade.exposure;
    m_grade.hue = m_roomGrade.hue;
    m_grade.sepia = m_roomGrade.sepia;
    // No room declares per-channel gains: the rows start from "untouched",
    // which keeps the take-over exactly where the frame already is.
    m_grade.red = 1.f;
    m_grade.green = 1.f;
    m_grade.blue = 1.f;
    applyGrade();
}

void ConsoleOverlay::build(DrawList& dl, int screenW, int screenH, UiInput* input) {
    if (!m_visible) return;

    if (m_compact) {
        // Terminal view: a scrollable log of the last ~20 lines over the prompt.
        // The calibration sliders live in the full (compact=false) panel.
        const float panelX = 24.f;
        const float panelW = static_cast<float>(screenW) - 48.f;
        const float lineH = 15.f;
        const int visibleLines = 20;
        const float historyH = lineH * static_cast<float>(visibleLines);
        const float inputH = 24.f;
        const float pad = 8.f;
        const float titleH = 16.f;
        const float panelH = titleH + historyH + inputH + pad * 3.f;
        const float panelY = static_cast<float>(screenH) - panelH - 40.f;

        dl.addRectFilled({panelX, panelY, panelW, panelH}, Color::rgba(0.03f, 0.04f, 0.06f, 0.94f));
        dl.addRect({panelX, panelY, panelW, panelH}, Color::rgb(0, 200, 255), 1.f);
        dl.addText(panelX + pad, panelY + 4.f, "CONSOLE",
                   Color::rgb(120, 220, 255), 1.0f);
        dl.addText(panelX + pad + 90.f, panelY + 6.f,
                   "ENTER type   UP/DOWN history   ESC or INSERT close",
                   Color::rgb(130, 145, 160), 0.85f);

        const float histX = panelX + pad;
        const float histY = panelY + pad + titleH;
        const float histW = panelW - pad * 2.f;
        drawHistory(dl, histX, histY, histW, historyH);
        drawInputLine(dl, histX, panelY + panelH - inputH - pad, histW);
        ++m_ticks;
        return;
    }

    const float panelX = 24.f;
    const float panelW = static_cast<float>(screenW) - 48.f;
    const float panelY = static_cast<float>(screenH) * 0.34f;
    const float panelH = static_cast<float>(screenH) * 0.62f;

    dl.addRectFilled({panelX, panelY, panelW, panelH}, Color::rgba(0.03f, 0.04f, 0.06f, 0.92f));
    dl.addRect({panelX, panelY, panelW, panelH}, Color::rgb(0, 200, 255), 1.f);

    const float pad = 12.f;
    dl.addText(panelX + pad, panelY + pad, "CONSOLE", Color::rgb(120, 220, 255), 1.1f);
    dl.addText(panelX + pad + 92.f, panelY + pad + 2.f,
               "ENTER type   UP/DOWN history   ESC or INSERT close",
               Color::rgb(130, 145, 160), 0.9f);

    // The numbers a calibration moves stay on screen. They are state, not
    // history: a line that scrolls away is no way to read what the display is
    // doing. While the console has not taken the display over, the row shows
    // the room's own numbers, which is what the frame is actually using.
    {
        const bool owned = m_grade.active;
        const float tempK = owned ? m_grade.temperatureK : m_roomGrade.temperatureK;
        const float bright = owned ? m_grade.brightness : m_roomGrade.brightness;
        const float contrast = owned ? m_grade.contrast : m_roomGrade.contrast;
        const float saturation = owned ? m_grade.saturation : m_roomGrade.saturation;
        const float hue = owned ? m_grade.hue : m_roomGrade.hue;
        const float sepia = owned ? m_grade.sepia : m_roomGrade.sepia;
        // The room brings no channel gains of its own: unowned, the row shows
        // the neutral 1 the take-over would start from.
        const float red = owned ? m_grade.red : 1.f;
        const float green = owned ? m_grade.green : 1.f;
        const float blue = owned ? m_grade.blue : 1.f;
        // The row reports the stop the frame is actually drawn with: the
        // console's own once it typed one, the room's otherwise — a console
        // that never touched exposure still shows the room's number, not a
        // placeholder, because that is the value in use.
        const float exposure = owned && m_grade.exposure > 0.0f ? m_grade.exposure
                                                                : m_roomGrade.exposure;
        char exposureText[16];
        if (exposure > 0.0f)
            std::snprintf(exposureText, sizeof(exposureText), "%.2f",
                          static_cast<double>(exposure));
        else
            std::snprintf(exposureText, sizeof(exposureText), "none");
        char status[384];
        std::snprintf(status, sizeof(status),
                      "TEMP %.0f K   BRIGHTNESS %.2f   CONTRAST %.2f   "
                      "SATURATION %.2f   HUE %.0f deg   SEPIA %.2f   "
                      "R %.2f G %.2f B %.2f   EXPOSURE %s   [%s]",
                      static_cast<double>(tempK), static_cast<double>(bright),
                      static_cast<double>(contrast), static_cast<double>(saturation),
                      static_cast<double>(hue), static_cast<double>(sepia),
                      static_cast<double>(red), static_cast<double>(green),
                      static_cast<double>(blue), exposureText, owned ? "console" : "room");
        dl.addText(panelX + pad, panelY + pad + 17.f, status, Color::rgb(150, 225, 255),
                   0.95f);
    }

    // The calibration strip: one draggable row per number, under the state row.
    const float rowH = 24.f;
    const float stripTop = panelY + pad + 34.f;
    const float labelW = 130.f;
    const float trackX = panelX + pad + labelW;
    const float trackW = 560.f;
    for (int i = 0; i < kConsoleSliderCount; ++i) {
        const ConsoleSliderSpec& spec = kConsoleSliders[i];
        const float rowY = stripTop + static_cast<float>(i) * rowH;
        const Rect row{trackX, rowY, trackW, rowH - 6.f};
        const bool hovered = input != nullptr && input->isHovering(row);
        const bool dragging = m_drag == i;
        const float value = sliderValue(i);
        const float fraction = spec.max > spec.min
                                   ? (value - spec.min) / (spec.max - spec.min)
                                   : 0.f;

        dl.addText(panelX + pad, rowY + 3.f, spec.label,
                   (hovered || dragging) ? Color::rgb(220, 245, 255)
                                         : Color::rgb(150, 175, 195), 0.9f);
        dl.addRectFilled({trackX, rowY + 6.f, trackW, 6.f},
                         Color::rgba(0.10f, 0.13f, 0.16f, 0.95f));
        dl.addRectFilled({trackX, rowY + 6.f, trackW * fraction, 6.f},
                         (hovered || dragging) ? Color::rgb(90, 230, 255)
                                               : Color::rgb(0, 170, 215));
        // The knob: wide enough to grab, drawn last so it sits on the fill.
        dl.addRectFilled({trackX + trackW * fraction - 3.f, rowY + 2.f, 6.f, 14.f},
                         Color::rgb(235, 248, 255));

        char shown[32];
        std::snprintf(shown, sizeof(shown), "%.*f", spec.decimals,
                      static_cast<double>(value));
        std::string text(shown);
        if (i == 0) text += " K";
        if (i == 4) text += " deg";
        if (i == kConsoleSliderCount - 1 && value <= 0.0f) text = "room";
        dl.addText(trackX + trackW + 12.f, rowY + 3.f, text, Color::rgb(190, 220, 240), 0.9f);
    }

    // A held left button drags the row it started on; the row keeps the grab
    // until the button comes up, even past the end of the track.
    if (input != nullptr) {
        const MouseState& mouse = input->state();
        if (mouse.leftDown()) {
            if (m_drag < 0) {
                for (int i = 0; i < kConsoleSliderCount; ++i) {
                    const Rect row{trackX, stripTop + static_cast<float>(i) * rowH, trackW,
                                   rowH - 6.f};
                    if (!row.contains(mouse.x, mouse.y)) continue;
                    m_drag = i;
                    adoptRoomGrade();
                    break;
                }
            }
            if (m_drag >= 0) {
                const ConsoleSliderSpec& spec = kConsoleSliders[m_drag];
                setSliderValue(m_drag, sliderValueAt((mouse.x - trackX) / trackW, spec.min,
                                                     spec.max, spec.step));
            }
        } else {
            m_drag = -1;
        }
    }

    // The preset keys: NEW starts a grade from neutral, SAVE / LOAD / USE work
    // the room's "default" slot (the typed commands keep their named presets).
    // A press that starts on a key and comes up on it clicks; a drag that
    // started on a slider row above never lands here by accident.
    const float btnY = stripTop + static_cast<float>(kConsoleSliderCount) * rowH + 8.f;
    const float btnH = 26.f;
    const float btnW = 104.f;
    const float btnGap = 10.f;
    const char* btnLabels[4] = {"NEW", "SAVE", "LOAD", "USE"};
    for (int b = 0; b < 4; ++b) {
        const Rect btn{panelX + pad + static_cast<float>(b) * (btnW + btnGap), btnY,
                       btnW, btnH};
        const bool hovered = input != nullptr && input->isHovering(btn);
        dl.addRectFilled(btn, hovered ? Color::rgba(0.10f, 0.22f, 0.30f, 1.f)
                                      : Color::rgba(0.06f, 0.10f, 0.15f, 1.f));
        dl.addRect(btn, hovered ? Color::rgb(120, 230, 255) : Color::rgb(60, 150, 200),
                   1.f);
        const float labelW = m_text.measureWidth(btnLabels[b], 0.95f);
        dl.addText(btn.x + (btnW - labelW) * 0.5f, btnY + 6.f, btnLabels[b],
                   hovered ? Color::rgb(235, 250, 255) : Color::rgb(170, 210, 230),
                   0.95f);
        if (input != nullptr && input->wasClicked(btn)) {
            if (b == 0)
                cmdNewGrade({});
            else if (b == 1)
                cmdSavePreset({"default"});
            else if (b == 2)
                cmdLoadPreset({"default"});
            else
                cmdUsePreset({"default"});
        }
    }

    // The material keys: what MATLIST and MATRELOAD do without typing, for the
    // car the room (or the session) carries. The longer labels get wider keys.
    const float matBtnY = btnY + btnH + 8.f;
    const float matBtnW = 120.f;
    const char* matLabels[2] = {"MATLIST", "MATRELOAD"};
    for (int b = 0; b < 2; ++b) {
        const Rect btn{panelX + pad + static_cast<float>(b) * (matBtnW + btnGap),
                       matBtnY, matBtnW, btnH};
        const bool hovered = input != nullptr && input->isHovering(btn);
        dl.addRectFilled(btn, hovered ? Color::rgba(0.10f, 0.22f, 0.30f, 1.f)
                                      : Color::rgba(0.06f, 0.10f, 0.15f, 1.f));
        dl.addRect(btn, hovered ? Color::rgb(120, 230, 255) : Color::rgb(60, 150, 200),
                   1.f);
        const float labelW = m_text.measureWidth(matLabels[b], 0.95f);
        dl.addText(btn.x + (matBtnW - labelW) * 0.5f, matBtnY + 6.f, matLabels[b],
                   hovered ? Color::rgb(235, 250, 255) : Color::rgb(170, 210, 230),
                   0.95f);
        if (input != nullptr && input->wasClicked(btn)) {
            if (b == 0)
                cmdMaterialList({});
            else
                cmdMaterialReload({});
        }
    }

    const float headH = 46.f + static_cast<float>(kConsoleSliderCount) * rowH + 8.f +
                        btnH + 8.f + btnH + 12.f;
    const float inputH = 30.f;
    drawHistory(dl, panelX + pad, panelY + pad + headH, panelW - pad * 2.f,
                panelH - headH - inputH - pad * 2.f);
    drawInputLine(dl, panelX + pad, panelY + panelH - inputH - pad - 4.f, panelW - pad * 2.f);
    ++m_ticks;
}

void ConsoleOverlay::drawHistory(DrawList& dl, float x, float y, float w, float h) {
    const float lineH = 15.f;
    const std::size_t visible = h > lineH ? static_cast<std::size_t>(h / lineH) : 0;
    const std::size_t count = m_lines.size() < visible ? m_lines.size() : visible;
    const std::size_t first = m_lines.size() - count;

    float lineY = y;
    for (std::size_t i = first; i < m_lines.size(); ++i, lineY += lineH) {
        const std::string& line = m_lines[i];
        // The history keeps its own width: a long line is cut, not wrapped,
        // so the panel height stays predictable.
        std::string shown = line;
        if (shown.size() > 4 && m_text.measureWidth(shown, 0.92f) > w - 12.f) {
            while (shown.size() > 4 && m_text.measureWidth(shown + "...", 0.92f) > w - 12.f)
                shown.pop_back();
            shown += "...";
        }
        const Color tint = (!line.empty() && line[0] == '>') ? Color::rgb(150, 210, 255)
                                                            : Color::rgb(215, 225, 235);
        dl.addText(x + 6.f, lineY, shown, tint, 0.92f);
    }
}

void ConsoleOverlay::drawInputLine(DrawList& dl, float x, float y, float w) {
    dl.addRectFilled({x, y, w, 22.f}, Color::rgba(0.06f, 0.09f, 0.14f, 1.f));
    dl.addRect({x, y, w, 22.f}, Color::rgb(60, 150, 200), 1.f);

    std::string shown = std::string(m_typing ? "> " : "1> ") + m_input;
    // The cursor is a solid block, not an underscore: it fills the cell the
    // next character will land in, and it blinks while typing.
    const float textX = x + 6.f;
    const float textY = y + 5.f;
    dl.addText(textX, textY, shown, Color::rgb(240, 245, 250), 0.95f);
    if (m_typing && ((m_ticks / 30) % 2 == 0)) {
        const float textW = m_text.measureWidth(shown, 0.95f);
        const float charW = std::max(4.f, m_text.measureWidth("M", 0.95f));
        const float cursorH = m_text.atlas().baseLineHeight() * 0.95f;
        dl.addRectFilled({textX + textW, textY + 1.f, charW, cursorH},
                         Color::rgba(0.85f, 0.95f, 1.0f, 0.85f));
    }
}

bool ConsoleOverlay::handleKey(int key) {
    if (!m_visible) return false;
    switch (key) {
        case 0x1B: // ESC
            if (m_typing) {
                m_typing = false;
                m_input.clear();
            } else {
                setVisible(false);
            }
            return true;
        case 0x0D: // ENTER
            m_typing = true;
            return true;
        case 0x26: { // UP: older command
            if (m_cmdHistory.empty()) return true;
            if (m_historyPos < m_cmdHistory.size()) ++m_historyPos;
            m_input = m_cmdHistory[m_cmdHistory.size() - m_historyPos];
            m_typing = true;
            return true;
        }
        case 0x28: // DOWN: newer command, then an empty prompt
            if (m_historyPos == 0) return true;
            --m_historyPos;
            m_input = m_historyPos == 0 ? std::string()
                                        : m_cmdHistory[m_cmdHistory.size() - m_historyPos];
            m_typing = true;
            return true;
        default:
            // While the console is up every key belongs to it: nothing
            // underneath (menu, camera, car) reacts to what is being typed.
            return true;
    }
}

bool ConsoleOverlay::handleChar(int c) {
    if (!m_visible) return false;
    if (c == 13) { // ENTER runs the line
        const std::string line = m_input;
        m_input.clear();
        m_typing = false;
        m_historyPos = 0;
        if (!line.empty()) execute(line);
        return true;
    }
    if (c == 8) { // BACKSPACE
        if (!m_input.empty()) m_input.pop_back();
        return true;
    }
    if (c == 27) { // ESC
        if (m_typing) {
            m_typing = false;
            m_input.clear();
        } else {
            setVisible(false);
        }
        return true;
    }
    if (c >= 32 && c < 127) {
        if (m_input.size() < 200) m_input.push_back(static_cast<char>(c));
        m_typing = true;
    }
    return true;
}

// --- commands ---------------------------------------------------------------

void ConsoleOverlay::publishGrade() {
    if (m_gradeHook) m_gradeHook(m_grade);
}

void ConsoleOverlay::applyGrade() {
    m_grade.active = true;
    publishGrade();
}

void ConsoleOverlay::setGrade(const GradeState& grade) {
    m_grade = grade;
    applyGrade();
    reportGrade();
}

void ConsoleOverlay::reportGrade() {
    char buffer[352];
    std::snprintf(buffer, sizeof(buffer),
                  "  grade: temperature %.0f K, brightness %.2f, contrast %.2f, "
                  "saturation %.2f, hue %.0f deg, sepia %.2f, "
                  "rgb %.2f/%.2f/%.2f, exposure %.2f%s",
                  static_cast<double>(m_grade.temperatureK),
                  static_cast<double>(m_grade.brightness),
                  static_cast<double>(m_grade.contrast),
                  static_cast<double>(m_grade.saturation),
                  static_cast<double>(m_grade.hue), static_cast<double>(m_grade.sepia),
                  static_cast<double>(m_grade.red), static_cast<double>(m_grade.green),
                  static_cast<double>(m_grade.blue),
                  static_cast<double>(m_grade.exposure),
                  m_grade.active ? "" : " (the room still owns it)");
    addLine(buffer);
    if (m_grade.exposure <= 0.0f)
        addLine("  the room's own exposure is in use: EXPOSURE <stop> takes it over");
}

bool ConsoleOverlay::setGradeValue(float& slot, const std::vector<std::string>& args,
                                   float min, float max, const char* label) {
    char buffer[224];
    if (args.empty()) {
        std::snprintf(buffer, sizeof(buffer), "  %s %.2f (range %.2f .. %.2f)", label,
                      static_cast<double>(slot), static_cast<double>(min),
                      static_cast<double>(max));
        addLine(buffer);
        return true;
    }
    const char* text = args[0].c_str();
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text) {
        addLine(std::string("  ") + label + ": '" + args[0] + "' is not a number");
        return true;
    }
    // A sign steps the row instead of replacing it, so a value can be walked
    // without retyping it.
    const bool relative = text[0] == '+' || text[0] == '-';
    float next = relative ? slot + static_cast<float>(value) : static_cast<float>(value);
    next = next < min ? min : (next > max ? max : next);
    slot = next;
    applyGrade();
    std::snprintf(buffer, sizeof(buffer), "  %s %.2f", label, static_cast<double>(slot));
    addLine(buffer);
    return true;
}

bool ConsoleOverlay::cmdTemperature(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.temperatureK, args, 1000.f, 15000.f, "temperature K");
}

bool ConsoleOverlay::cmdBrightness(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.brightness, args, 0.05f, 4.f, "brightness");
}

bool ConsoleOverlay::cmdContrast(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.contrast, args, 0.10f, 3.f, "contrast");
}

bool ConsoleOverlay::cmdSaturation(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.saturation, args, 0.f, 3.f, "saturation");
}

bool ConsoleOverlay::cmdExposure(const std::vector<std::string>& args) {
    if (args.empty()) {
        char buffer[160];
        std::snprintf(buffer, sizeof(buffer), "  exposure %.2f (range 0.05 .. 8.00)%s",
                      static_cast<double>(m_grade.exposure),
                      m_grade.exposure > 0.0f ? "" : " - the room owns it");
        addLine(buffer);
        return true;
    }
    return setGradeValue(m_grade.exposure, args, 0.05f, 8.f, "exposure");
}

bool ConsoleOverlay::cmdHue(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.hue, args, -180.f, 180.f, "hue deg");
}

bool ConsoleOverlay::cmdSepia(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.sepia, args, 0.f, 1.f, "sepia");
}

bool ConsoleOverlay::cmdRed(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.red, args, 0.f, 2.f, "red");
}

bool ConsoleOverlay::cmdGreen(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.green, args, 0.f, 2.f, "green");
}

bool ConsoleOverlay::cmdBlue(const std::vector<std::string>& args) {
    return setGradeValue(m_grade.blue, args, 0.f, 2.f, "blue");
}

bool ConsoleOverlay::cmdNewGrade(const std::vector<std::string>& /*args*/) {
    m_grade = GradeState{};
    m_grade.active = true;
    publishGrade();
    addLine("  new grade from neutral (the console owns it)");
    reportGrade();
    return true;
}

bool ConsoleOverlay::cmdGrade(const std::vector<std::string>& args) {
    if (!args.empty() && upperCopy(args[0]) == "SAVE") return cmdSavePreset({});
    if (!args.empty() && upperCopy(args[0]) == "RESET") return cmdResetGrade({});
    reportGrade();
    return true;
}

bool ConsoleOverlay::cmdHelp(const std::vector<std::string>& /*args*/) {
    addLine("  HELP        - this list");
    addLine("  CLEAR       - empty the log");
    addLine("  LIST        - presets shipped in system/ppfilters");
    addLine("  PPFILTER    - current post-processing preset");
    addLine("  EDIT PPFILTER - the grade rows: temperature, brightness, contrast,");
    addLine("                  saturation, exposure (drag them, or type a value)");
    addLine("  SCREENSHOT  - how to capture a frame");
    addLine("  PHOTOMODE   - photo mode key");
    addLine("  COCKPITEDITOR - toggle the cockpit editor flag");
    addLine("  THEME       - interface theme in use");
    addLine("  DEBUG       - list or set the KS_DEBUG_* flags");
    addLine("  VERSION     - build info");
    addLine("  TEMPERATURE - white point in K; TEMPERATURE 4500, or +250 to step");
    addLine("  BRIGHTNESS  - linear gain before the tone curve (0.05 .. 4)");
    addLine("  CONTRAST    - around mid-grey, after the curve (0.10 .. 3)");
    addLine("  EXPOSURE    - the room's stop; saved as tonemapping.exposure");
    addLine("  SATURATION  - how much colour: 0 grey, 1 as rendered, 2 twice");
    addLine("  HUE         - colour wheel in degrees (-180 .. 180, 0 as rendered)");
    addLine("  SEPIA       - period wash over the image (0 none, 1 full)");
    addLine("  RED/GREEN/BLUE - one channel's gain (0 kills it, 1 as rendered, 2 twice)");
    addLine("  NEW         - a grade from neutral, owned by the console");
    addLine("  SAVE [name] - write the grade into the room's grade.json");
    addLine("  LOAD <name> - put a saved calibration back on the rows");
    addLine("  USE <name>  - LOAD it and make it the room's own at start-up");
    addLine("  RESET       - hand the grade back to the room");
    addLine("  GRADE       - report it; GRADE SAVE / GRADE RESET still work");
    addLine("  MATLIST     - the car's materials as model.json declares them");
    addLine("  MAT <m>     - one material: shader, preset, properties");
    addLine("  MAT <m> <key> <value> - edit and save (shader/preset or a property)");
    addLine("  MATADD <m> [shader <s>] [preset <p>] - add the entry");
    addLine("  MATREMOVE <m> - drop the entry; MATRELOAD re-reads the car");
    addLine("  TC <n> / ABS <n> - driver aid levels (0 = off), applied while driving");
    addLine("  BB <percent> - brake bias, front share (30..80), applied while driving");
    addLine("  EDIT PHYSICS - the data screen: the car's data files as rows,");
    addLine("                  wings and suspension included, edited live");
    addLine("  the keys under the rows: NEW/SAVE/LOAD/USE for the grade,");
    addLine("  MATLIST/MATRELOAD for the materials");
    return true;
}

bool ConsoleOverlay::cmdClear(const std::vector<std::string>& /*args*/) {
    m_lines.clear();
    addLine("log cleared");
    return true;
}

bool ConsoleOverlay::cmdScreenshot(const std::vector<std::string>& /*args*/) {
    addLine("start with KS_SCREENSHOT=<path>; the frame is written after the first render");
    return true;
}

bool ConsoleOverlay::cmdPhotomode(const std::vector<std::string>& /*args*/) {
    addLine("photo mode: KS_PHOTOMODE=<key>, or F12 while driving");
    return true;
}

bool ConsoleOverlay::cmdPpfilter(const std::vector<std::string>& args) {
    if (!args.empty() && upperCopy(args[0]) == "EDIT") return cmdEdit({"PPFILTER"});
    if (const char* preset = environmentValue("KS_PPFILTER"))
        addLine(std::string("current preset: ") + preset);
    else
        addLine("no KS_PPFILTER set: the filter is chosen by the session");
    return true;
}

bool ConsoleOverlay::cmdEdit(const std::vector<std::string>& args) {
    const std::string what = args.empty() ? std::string("PPFILTER") : upperCopy(args[0]);
    if (what == "PHYSICS") {
        // The data screen, not the grade rows: the hook owns the car, so it
        // opens the panel on the showroom car and reports what happened.
        if (!m_physicsEdit) {
            addLine("  EDIT PHYSICS is not wired in this build");
            return true;
        }
        addLine(m_physicsEdit());
        return true;
    }
    if (what != "PPFILTER" && what != "GRADE") {
        addLine("  EDIT PPFILTER - take the grade over and show its rows");
        addLine("  EDIT PHYSICS - the data screen for the showroom car");
        return true;
    }
    // The rows above are the editor and they are drawn with the console, so
    // showing them means taking the grade over first: the rows then move
    // from the numbers the frame is using now, not from the console's
    // untouched defaults.
    adoptRoomGrade();
    reportGrade();
    addLine("  drag the rows above (temperature, brightness, contrast, saturation,");
    addLine("  hue, sepia, red, green, blue, exposure), type a value, or press");
    addLine("  NEW / SAVE / LOAD / USE");
    return true;
}

bool ConsoleOverlay::cmdMaterialList(const std::vector<std::string>& /*args*/) {
    const std::vector<MaterialLine> lines =
        m_materialList ? m_materialList() : std::vector<MaterialLine>();
    if (lines.empty()) {
        addLine("the car's model.json declares no material entry");
        return true;
    }
    addLine("materials in the car's model.json (MAT <name> for the values):");
    for (const MaterialLine& line : lines) {
        std::string text = "  " + line.name;
        if (!line.shader.empty()) text += "   shader " + line.shader;
        if (!line.preset.empty()) text += "   preset " + line.preset;
        text += "   " + std::to_string(line.properties.size()) + " propert(y/ies)";
        addLine(text);
    }
    return true;
}

bool ConsoleOverlay::cmdAid(int which, const std::vector<std::string>& args) {
    const char* name = which == 0 ? "TC" : "ABS";
    if (!m_aid) {
        addLine(std::string("  ") + name + ": driver aids are not wired in this build");
        return true;
    }
    if (args.empty()) {
        addLine(m_aid(which, -1));
        return true;
    }
    // The number is parsed here, the meaning is the car's: the hook clamps and
    // answers whether the car has the aid in the first place.
    const int level = std::atoi(args[0].c_str());
    addLine(m_aid(which, level));
    return true;
}

bool ConsoleOverlay::cmdBias(const std::vector<std::string>& args) {
    if (!m_bias) {
        addLine("  BB: brake bias is not wired in this build");
        return true;
    }
    if (args.empty()) {
        addLine(m_bias(-1));
        return true;
    }
    // A front percentage: the car clamps it and answers with what it is really
    // running (its setup's value after a garage visit, its own data otherwise).
    addLine(m_bias(std::atoi(args[0].c_str())));
    return true;
}

bool ConsoleOverlay::cmdMaterial(const std::vector<std::string>& args) {
    if (args.empty()) {
        addLine("  MAT <material>                  - show one entry");
        addLine("  MAT <material> <key> <value>    - edit and save it");
        addLine("  MAT <material> shader <name>    - set the shader");
        return true;
    }
    const std::string& name = args[0];
    if (args.size() == 1) {
        for (const MaterialLine& line : m_materialList ? m_materialList() : std::vector<MaterialLine>()) {
            if (upperCopy(line.name) != upperCopy(name)) continue;
            std::string text = "  " + line.name;
            if (!line.shader.empty()) text += "   shader " + line.shader;
            if (!line.preset.empty()) text += "   preset " + line.preset;
            addLine(text);
            for (const auto& property : line.properties)
                addLine("    " + property.first + " = " + property.second);
            return true;
        }
        addLine("  no material called '" + name + "' in this car's model.json");
        return true;
    }
    if (args.size() < 3) {
        addLine("  MAT <material> <key> <value>");
        return true;
    }
    if (!m_materialEdit) {
        addLine("  material editing is not wired in this build");
        return true;
    }
    addLine(m_materialEdit(name, args[1], args[2]));
    return true;
}

bool ConsoleOverlay::cmdMaterialRemove(const std::vector<std::string>& args) {
    if (args.empty()) {
        addLine("  MATREMOVE <material>: drop its entry from the car's model.json");
        return true;
    }
    if (!m_materialRemove) {
        addLine("  material editing is not wired in this build");
        return true;
    }
    addLine(m_materialRemove(args[0]));
    return true;
}

bool ConsoleOverlay::cmdMaterialReload(const std::vector<std::string>& /*args*/) {
    if (!m_materialReload) {
        addLine("  material editing is not wired in this build");
        return true;
    }
    addLine(m_materialReload());
    return true;
}

bool ConsoleOverlay::cmdMaterialAdd(const std::vector<std::string>& args) {
    if (args.empty()) {
        addLine("  MATADD <material> [shader <name>] [preset <name>]");
        return true;
    }
    std::string shader;
    std::string preset;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string key = upperCopy(args[i]);
        if ((key == "SHADER" || key == "PRESET") && i + 1 < args.size()) {
            if (key == "SHADER")
                shader = args[++i];
            else
                preset = args[++i];
        } else {
            addLine("  MATADD <material> [shader <name>] [preset <name>]");
            return true;
        }
    }
    if (!m_materialAdd) {
        addLine("  material editing is not wired in this build");
        return true;
    }
    addLine(m_materialAdd(args[0], shader, preset));
    return true;
}

bool ConsoleOverlay::cmdList(const std::vector<std::string>& args) {
    return cmdListPresets(args);
}

bool ConsoleOverlay::cmdSavePreset(const std::vector<std::string>& args) {
    if (!m_gradeSave) {
        addLine("  saving is not wired in this build");
        return true;
    }
    const std::string name = args.empty() ? std::string("default") : args[0];
    addLine(m_gradeSave(m_grade, name, true));
    return true;
}

bool ConsoleOverlay::cmdLoadPreset(const std::vector<std::string>& args) {
    if (args.empty()) {
        addLine("  LOAD <name>: LIST shows the calibrations this room carries");
        return true;
    }
    GradeState loaded;
    if (!m_gradeLoad || !m_gradeLoad(args[0], loaded)) {
        addLine("  no calibration called '" + args[0] + "' in this room");
        return true;
    }
    m_grade = loaded;
    m_grade.active = true;
    publishGrade();
    reportGrade();
    return true;
}

bool ConsoleOverlay::cmdUsePreset(const std::vector<std::string>& args) {
    // USE is LOAD plus remembering the choice: the room starts with it.
    if (args.empty()) {
        addLine("  USE <name>: LOAD a calibration and make it the room's own");
        return true;
    }
    GradeState loaded;
    if (!m_gradeLoad || !m_gradeLoad(args[0], loaded)) {
        addLine("  no calibration called '" + args[0] + "' in this room");
        return true;
    }
    m_grade = loaded;
    m_grade.active = true;
    publishGrade();
    reportGrade();
    if (m_gradeSave) addLine(m_gradeSave(m_grade, args[0], true));
    return true;
}

bool ConsoleOverlay::cmdResetGrade(const std::vector<std::string>& /*args*/) {
    m_grade = GradeState{};
    // Back to the room's own colour and stop, not to a console-held 1.0.
    publishGrade();
    addLine("  grade reset: 6500 K, brightness 1.00, contrast 1.00; the room owns it again");
    return true;
}

bool ConsoleOverlay::cmdListPresets(const std::vector<std::string>& /*args*/) {
    addLine("presets in system/ppfilters: normal, bw, inverted, sepia, tv80, photomode");
    addLine("pick one with KS_PPFILTER=<id> at start-up");
    const std::vector<GradePreset> saved = m_gradeList ? m_gradeList() : std::vector<GradePreset>();
    if (saved.empty()) {
        addLine("this room has no saved calibration: SAVE <name> writes the current grade");
        return true;
    }
    addLine("this room's calibrations (LIST, LOAD <name>, USE <name>):");
    for (const GradePreset& preset : saved) {
        char line[200];
        std::snprintf(line, sizeof(line), "%s%s   %.0f K   b %.2f   c %.2f   s %.2f   e %.2f",
                      preset.active ? "* " : "  ", preset.name.c_str(),
                      static_cast<double>(preset.grade.temperatureK),
                      static_cast<double>(preset.grade.brightness),
                      static_cast<double>(preset.grade.contrast),
                      static_cast<double>(preset.grade.saturation),
                      static_cast<double>(preset.grade.exposure));
        addLine(line);
    }
    return true;
}

bool ConsoleOverlay::cmdVersion(const std::vector<std::string>& /*args*/) {
    addLine("ksim - developer console");
    addLine(std::string("build ") + __DATE__ + " " + __TIME__);
    return true;
}

bool ConsoleOverlay::cmdDebug(const std::vector<std::string>& args) {
    static const char* kFlags[] = {"TYRES",     "ENGINE",      "TURBO",    "HYBRID",
                                   "ELECTRIC",  "ELECTRONICS", "SUSPENSIONS", "TRANSMISSION",
                                   "DRS",       "ASPHALT",     "ESCAPE",   "GRASS",
                                   "GRAVEL",    "PITLANE",     "GRID",     "SECTOR1",
                                   "SECTOR2",   "SECTOR3",     "CARS",     "TRACKS",
                                   "CHARACTERS", "SHOWROOM"};
    if (args.empty()) {
        addLine("debug flags (set with: DEBUG <name>, or KS_DEBUG_<NAME>=1):");
        for (const char* flag : kFlags) {
            const std::string env = std::string("KS_DEBUG_") + flag;
            if (environmentValue(env.c_str()) != nullptr) addLine(std::string("  ") + flag + ": on");
        }
        addLine("nothing listed means every flag is off");
        return true;
    }

    const std::string flag = upperCopy(args[0]);
    bool known = false;
    for (const char* candidate : kFlags) {
        if (flag == candidate) {
            known = true;
            break;
        }
    }
    if (!known) {
        addLine("unknown debug target '" + args[0] + "'");
        return true;
    }
    const std::string env = "KS_DEBUG_" + flag;
#ifdef _WIN32
    _putenv_s(env.c_str(), "1");
#else
    setenv(env.c_str(), "1", 1);
#endif
    addLine(env + "=1 (read by the next start)");
    return true;
}

bool ConsoleOverlay::cmdCockpitEditor(const std::vector<std::string>& /*args*/) {
    const bool on = environmentValue("KS_COCKPIT_EDITOR") == nullptr;
#ifdef _WIN32
    _putenv_s("KS_COCKPIT_EDITOR", on ? "1" : "0");
#else
    setenv("KS_COCKPIT_EDITOR", on ? "1" : "0", 1);
#endif
    addLine(on ? "cockpit editor: on" : "cockpit editor: off");
    return true;
}

bool ConsoleOverlay::cmdTheme(const std::vector<std::string>& /*args*/) {
    if (const char* theme = environmentValue("KS_UI_THEME"))
        addLine(std::string("interface theme: ") + theme);
    else
        addLine("interface theme: default (KS_UI_THEME picks another)");
    return true;
}

} // namespace ui
} // namespace sim
} // namespace ks
