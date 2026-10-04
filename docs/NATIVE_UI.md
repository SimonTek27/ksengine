# Native UI overlays (Qt-free)

## Font atlas
`ui/FontAtlas.h` bakes an **8×8 monospace** set for ASCII 32–126 into a **128×48 R8** texture.

```cpp
UiRenderer ui;
// GPU once:
uploadR8(ui.atlasPixels().data(), ui.atlasWidth(), ui.atlasHeight());
// each frame: ui.endFrame() → vertices with correct glyph UVs
```

- Solid rects/lines sample the **white texel** in the atlas.
- Text samples per-glyph UV; `fontScale` scales quads.
- `FontAtlas::loadR8(...)` can replace the built-in bake with an external sheet (same layout).

### Real TrueType fonts
`KS_FONT_TTF=/path/to/font.ttf` (plus optional `KS_FONT_PX`, default 16) swaps the baked set
for a rasterized TrueType font when `NativeUiHub` is constructed. A failure prints
`[ui] KS_FONT_TTF ignored: <reason>` and keeps the baked 8×8 set.

```cpp
FontAtlas font;
std::string err;
ks::sim::ui::loadTtfFont(font, "content/fonts/Formula-Medium.ttf", 20.f, &err);
font.width(); font.height(); font.baseLineHeight();  // now follow the TTF
```

Engine side (std-only, no FreeType):
- `engine/FileFormat/TtfReader.{h,cpp}` — sfnt directory, `head`/`maxp`/`hhea`/`hmtx`,
  cmap formats 4/12/6/0, `loca`+`glyf` with simple **and** composite glyphs (F2Dot14 scale,
  2×2 matrix, recursion guard). CFF/OTTO and `ttcf` are rejected with a clear message.
- `engine/FileFormat/TtfRasterizer.{h,cpp}` — quadratic flattening, non-zero winding fill
  over 8 sub-scanlines per pixel row (exact horizontal coverage, vertical AA), then
  `buildTtfAtlas` shelf-packs ASCII 32–126 into an R8 sheet with a reserved white texel.

The atlas keeps the same `GlyphInfo` contract as the baked set, so `BitmapText`, `UiRenderer`
and `UiGpuPass` need no changes; `baseLineHeight()` drives line spacing.

## Layout
```
ui/NativeUiTypes.h   DrawList
ui/FontAtlas.h       glyphs + R8 pixels
ui/UiRenderer.h      batch + UV
ui/DeviceSettingsOverlay.h
ui/MultiplayerOverlay.h   F2 browser, status line
ui/ChatOverlay.h           message log + composer
ui/NativeUiHub.h
```

## Input routing
`SimulatorApp::handleKeyDown` gives the key to the single menu instance first,
then `SetupGarage`, then `SimulationLoop::handleUiKey` (the hub), and only the
fallout reaches the driving bindings. `handleUiChar` carries WM_CHAR into the
chat composer, `handleUiMouse*` carries the mouse into `UiInput`; input flags
are retired in `UiInput::endFrame()` so a click injected during the message
pump survives until the overlay reads it.

The menu pauses physics (`blocksDrivingInput`) but still renders, so the
cinematic menu is drawn over a frozen world.

## Hotkeys
Esc menu · F1 devices · F2 multiplayer · Y race HUD mode · T telemetry ·
Enter chat composer (while online) · arrows/enter navigate
