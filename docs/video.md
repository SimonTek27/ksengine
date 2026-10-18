# Video

Video support in ksEditor is focused on asset preview and replay export rather than video editing.

## Features

### Thumbnail Generation
- **`AssetSearchEngine::generateVideoThumbnail()`** — Extracts frames from video files using ffmpeg for asset browser previews
- **`ACThumbnailGenerator::generateFromVideo()`** — Simulator-specific video thumbnail generation for the asset library

### Replay-to-Video Export
- **`exportToVideo()`** — Converts in-app replay visualizations to video output for sharing lap analyses and telemetry data

### Display Configuration
- Video mode settings (resolution, fullscreen) configurable through the Kinetika integration layer

## Implementation Details

| Feature | Location | Method |
|---------|----------|--------|
| Video thumbnails | `src/core/assets/AssetSearchEngine.cpp` | ffmpeg frame extraction |
| Video thumbnails (plugin) | `src/plugins/simulators/kunos/assettocorsa/AssetsLibraryModule.cpp` | ffmpeg integration |
| Video export | `src/plugins/simulators/kunos/assettocorsa/assettocorsa.h` | Replay frame capture |
| Video mode config | `src/plugins/simulators/kunos/assettocorsa/assettocorsa.h` | Resolution/fullscreen settings |

## Limitations

- No dedicated video editor module
- No timeline-based video editing
- No video effects or transitions
- Video support is limited to the asset pipeline and replay export use cases
