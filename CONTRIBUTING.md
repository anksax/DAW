# Contributing to ESP32-S3 DJ Audio Console

Thanks for helping improve the project. This is real-time embedded audio firmware, so stability and reproducibility matter more than feature count.

## Before you start

Please read:

- `README.md`
- `docs/HARDWARE.md`
- `docs/DEVELOPMENT.md`
- `docs/TROUBLESHOOTING.md`

The current supported target is an **ESP32-S3-WROOM-1 N16R8** using the **ESP32S3 Dev Module** board profile.

## Development environment

Recommended baseline:

- Arduino IDE 2.x or Arduino CLI
- Espressif Arduino-ESP32 3.3.12
- OPI PSRAM enabled
- 16 MB flash
- 240 MHz CPU
- Serial Monitor at 115200 baud

Required libraries:

- arduino-audio-tools
- codec-libfoxenflac
- Adafruit GFX Library
- Adafruit ST7735 and ST7789 Library
- U8g2

## Building locally

1. Clone the repository.
2. Install the ESP32 Arduino core and libraries above.
3. Select **ESP32S3 Dev Module**.
4. Apply the board configuration documented in `README.md`.
5. Compile `DJ_Audio_Console_V4.ino`.
6. Test on hardware with a known-good 16-bit mono/stereo FLAC.

## What to test

For audio or DSP changes, test at minimum:

- boot with an empty SD card
- one FLAC in SD root
- nested FLAC folders
- 44.1 kHz FLAC
- 48 kHz FLAC
- FX bypass
- each FX individually
- visualizer disabled
- visualizer enabled
- rapid encoder input
- rapid track changes

Longer playback testing is strongly encouraged.

## Pull requests

Keep pull requests focused. A good PR should include:

- what changed
- why the change is needed
- hardware used
- ESP32 core and library versions
- FLAC format used for testing
- relevant Serial Monitor logs
- whether DSP and visualizer were enabled
- before/after behavior

Do not mix broad refactors with unrelated features.

## Coding guidelines

- Avoid heap allocation inside the per-sample DSP path.
- Avoid expensive floating-point work in the hottest audio callback where possible.
- Preserve PCM frame alignment.
- Keep display work out of the timing-critical audio path.
- Prefer explicit bounds checks over assumptions.
- Fail safely when an SD card, track or codec input is invalid.
- Avoid full-screen TFT redraws during playback unless absolutely necessary.
- Do not silently add support for an audio format without testing it.

## Commit style

Use concise, descriptive commits, for example:

```text
fix: preserve FLAC sample rate when switching tracks
perf: reduce TFT refresh load during playback
feat: add compressor bypass indicator
docs: document N16R8 board settings
```

## Reporting a bug

Use the bug-report issue template and include complete logs. Compiler errors should be pasted in full rather than only the last line.

## Feature requests

Feature ideas are welcome, but audio stability has priority over adding more DSP, UI animation or codecs.

## License

By contributing, you agree that your contributions will be licensed under the repository's MIT License.
