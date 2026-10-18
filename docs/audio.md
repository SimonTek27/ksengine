# Audio

ksEditor's audio subsystem consists of two complementary components:

## Audio Editor (ksAudioEditor)

The **user-facing DAW environment** in `src/modules/soundEditor/` (28 files) providing:
- Multi-track timeline with region editing
- Real-time effects rack (EQ, compression, reverb, delay, distortion)
- Spectral analysis (sonogram, frequency response)
- **AI-assisted engine sound synthesis** (granular, sample-based, RPM-driven layering)
- Sound bank management (Wwise-style containers, randomizers, switches, RTPCs)
- Loudness metering (EBU R128)
- Export to game audio middleware formats
- Event bridge for Kinetika integration

## Audio Studio (ksAudioStudio)

The **internal audio engine** in `src/core/Audio/` (62 files) powering all audio functionality:
- **KSAudio / KSAudioCore** — Core audio engine with real-time mixing
- **KSAudioMixer** — Multi-bus mixing with DSP chain
- **KSAudioGenerator** — Procedural audio generation (additive, subtractive, FM, granular, wavetable)
- **KSAudioVST3Host / KSAudioVSTHost / LADSPAHost** — VST2/3 plugin hosting with parameter automation
- **KSBankParser / KSBankWriter** — FMOD bank read/write/encrypt (version-aware)
- **BankVersionManager** — Auto-detects bank format version and game target
- **BankParserFactory / BankWriterFactory** — Factory pattern for version-specific parsers/writers
- **AudioFormatConverter** — Cross-format conversion
- **AudioRecording** — Multi-channel recording with punch-in/out, loop recording, take management
- **AudioTimeStretch / FFTProcessor** — DSP processing
- **WaveformEngine / WaveProcessor** — Waveform analysis
- **KSCarAcoustics / KsCarAudioEngine** — Vehicle acoustic simulation
- **KSRPMProfile / KSRPMRecorder / EngineSimHook** — RPM-based engine sound synthesis
- **KSAudioBankGenerator** — Bank generation for runtime deployment
- **Node-based audio graph editor** — Visual signal flow design with real-time modulation
- **Analysis tools** — Spectrum (FFT, 1/3 octave), oscilloscope, phase correlation, loudness (EBU R128, ATSC A/85), true peak detection
- **Batch processing** — Effect chains, format conversion, loudness normalization, dithering

## Feature Parity

| Feature | FMOD | Wwise | GoldWave | ksEditor |
|---------|------|-------|----------|----------|
| Event System | Yes | Yes | — | Yes |
| Multi-bus Mixer | Yes | Yes | Yes | Yes |
| Effects/DSP | Yes | Yes | Yes | Yes |
| Spatial Audio | Yes | Yes | — | Yes |
| Recording | — | — | Yes | Yes |
| Bank Export | Yes | Yes | — | Yes |
| VST Hosting | — | — | Yes | Yes |
| AI Engine Sounds | — | — | — | Yes (unique) |
| Loudness Metering | — | — | Yes | EBU R128 |

## File Format: `.ksaudio`

**Native JSON-based project format** with **100% bidirectional compatibility** with **FMOD Studio `.fspro` project files version 1.08.12**.

- **Import:** Reads `.fspro` XML (events, parameters, buses, VCAs, banks, snapshots, mixer routing, DSP chains)
- **Export:** Writes `.fspro` XML fully loadable in FMOD Studio 1.08.12+
- **Internal:** `.ksaudio` JSON stores full signal graph, automation, plugin state, and asset references
- **Round-trip:** Lossless conversion between `.ksaudio` ↔ `.fspro` (1.08.12 schema)

**Code:** `src/core/Audio/FSPROImporter.cpp/.h` — parses FMOD Studio 1.08.12 `.fspro` entities into `.ksaudio` JSON

## Audio Import/Export

- **Import:** WAV, MP3, OGG, FLAC, FSB, **FMOD Studio `.fspro` (1.08.12)**
- **Export:** WAV, OGG, MP3, FLAC, **FMOD Studio `.fspro` (1.08.12)**
- **Bank Formats:** FMOD `.bank` (read/write/encrypt, 1.08.12 runtime format), Wwise-compatible soundbanks
- **Proprietary:** `.ksaudio` project files
