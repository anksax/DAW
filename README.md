# ESP32-S3 DJ Audio Console / DAW

<p align="center">
  <img src="assets/banner.svg" alt="ESP32-S3 DJ Audio Console banner" width="100%">
</p>

<p align="center">
  <a href="https://github.com/anksax/DAW/actions/workflows/ci.yml"><img alt="Firmware CI" src="https://github.com/anksax/DAW/actions/workflows/ci.yml/badge.svg"></a>
  <a href="LICENSE"><img alt="License: MIT" src="https://img.shields.io/badge/License-MIT-yellow.svg"></a>
  <img alt="ESP32-S3" src="https://img.shields.io/badge/MCU-ESP32--S3-000000?logo=espressif&logoColor=white">
  <img alt="FLAC" src="https://img.shields.io/badge/Audio-FLAC-7C3AED">
  <img alt="AudioTools" src="https://img.shields.io/badge/AudioTools-realtime%20DSP-0EA5E9">
  <img alt="Status" src="https://img.shields.io/badge/status-active%20development-F59E0B">
</p>

<p align="center">
  <b>Standalone lossless playback • hardware controls • real-time DSP • dual-display UI • real PCM visualization</b>
</p>


A compact, standalone digital audio workstation / DJ-style audio console built around an **ESP32-S3-WROOM-1 N16R8**.

The project combines local lossless playback, physical controls, two displays, a real-time PCM DSP chain, a real audio-reactive spectrum visualizer, and an external I2S DAC in one self-contained embedded system.

> **Project state:** active development. The current priority is making the audio path, DSP, visualizer and UI completely stable before expanding into MIDI, recording and broader workstation features.


## Why this project

Most ESP32 audio projects stop at basic playback. This project explores how far a single embedded system can be pushed toward a **real tactile music workstation**: lossless local audio, dedicated controls, responsive UI, real-time effects, and visual feedback without depending on a phone or PC.

## The three pillars

| What | Why | How |
|---|---|---|
| A standalone ESP32-S3 DJ/audio workstation | To combine lossless playback, physical interaction and DSP in one compact embedded platform | FLAC from microSD → Foxen decoder → PCM DSP → I2S DAC, with controls and dual displays |

## Quick start

1. Use an **ESP32S3 Dev Module** with **16 MB flash** and **OPI PSRAM**.
2. Install the libraries listed in [Software stack](#software-stack).
3. Apply the documented [Arduino board settings](#recommended-arduino-board-settings).
4. Put 16-bit mono/stereo `.flac` files on the microSD card.
5. Compile and upload `DJ_Audio_Console_V4.ino`.
6. Open Serial Monitor at **115200 baud** and verify SD, FLAC, I2S and DSP startup logs.


---

## What it does today

The current firmware in **DJ_Audio_Console_V4.ino** implements:

- recursive **FLAC library scanning** from microSD
- direct playback by file path instead of relying on AudioSourceSD numerical indexing
- native sample-rate handling so 44.1 kHz / 48 kHz files are not intentionally forced to one fixed rate
- external **I2S DAC** output
- dedicated FreeRTOS audio task
- master volume from a physical linear fader
- two rotary encoders with click and long-press actions
- analog joystick track navigation
- ST7789 TFT interface
- SH1106 OLED FX / visualizer interface
- real PCM DSP effects
- real decoded-audio spectrum analysis
- safe boot when no playable FLAC files are present
- PSRAM awareness and runtime diagnostics over Serial

### Current DSP effects

| Effect | Implementation |
|---|---|
| Filter | Low-pass / high-pass |
| Echo | Delay line with feedback |
| Fuzz | Non-linear driven clipping |
| Distortion | Hard clipping |
| Tremolo | Amplitude modulation |
| Compressor | Dynamic-range compression |

The effect parameter and wet/dry amount are adjustable live from the encoders.

---

## System architecture

~~~text
                          ┌────────────────────────────┐
                          │        microSD Card        │
                          │       FLAC Library         │
                          └─────────────┬──────────────┘
                                        │
                                  SPI / FSPI
                                        │
                                        ▼
┌──────────────┐              ┌────────────────────────────┐
│  Joystick    │─────────────▶│                            │
│  Fader       │─────────────▶│      ESP32-S3 N16R8        │
│  Encoder 1   │─────────────▶│                            │
│  Encoder 2   │─────────────▶│  FLAC decode + DSP + UI    │
└──────────────┘              └───────┬─────────┬──────────┘
                                      │         │
                              I2C     │         │ SPI
                                      ▼         ▼
                                ┌─────────┐ ┌─────────┐
                                │ SH1106  │ │ ST7789  │
                                │  OLED   │ │   TFT   │
                                └─────────┘ └─────────┘

                                      │
                                      │ decoded PCM
                                      ▼
                              ┌─────────────────┐
                              │ AudioEffectStream│
                              │  realtime DSP    │
                              └────────┬─────────┘
                                       │
                                      I2S
                                       │
                                       ▼
                              ┌─────────────────┐
                              │ UDA1334A-style  │
                              │    I2S DAC      │
                              └────────┬────────┘
                                       │
                                  analog audio
                                       │
                                       ▼
                             Headphones / amplifier
~~~

The audio task is separated from the main UI loop so display rendering and input handling do not have to share the same real-time execution path as FLAC decoding and I2S output.

---

## Hardware

### Main parts

| Part | Role |
|---|---|
| **ESP32-S3-WROOM-1 DevKit N16R8** | Main controller, 16 MB flash + 8 MB PSRAM |
| **ST7789 TFT** | Main 240×320 display |
| **SH1106 OLED** | 128×64 secondary FX / spectrum display |
| **microSD module / socket** | Local music library |
| **UDA1334A/CJMCU-style I2S DAC** | Digital-to-analog audio output |
| **2× rotary encoders with push switches** | FX parameter, mix and mode controls |
| **2-axis analog joystick** | Track navigation |
| **10 kΩ linear slide potentiometer / fader** | Master volume |
| **3.5 mm audio output stage** | Headphones / amplifier connection |

A separate headphone amplifier such as a TDA1308-class module can be used after the DAC if additional headphone drive is required.

For detailed wiring and hardware notes, see **docs/HARDWARE.md**.

---

## Pin map

| Function | ESP32-S3 GPIO |
|---|---:|
| microSD CS | 10 |
| SPI MOSI | 11 |
| SPI SCK | 12 |
| SPI MISO | 13 |
| TFT CS | 3 |
| TFT DC | 14 |
| TFT RST | 15 |
| TFT Backlight | 21 |
| OLED SDA | 8 |
| OLED SCL | 9 |
| I2S BCLK | 4 |
| I2S LRCK / WS | 5 |
| I2S DATA | 6 |
| Encoder 1 CLK | 41 |
| Encoder 1 DT | 42 |
| Encoder 1 SW | 38 |
| Encoder 2 CLK | 39 |
| Encoder 2 DT | 40 |
| Encoder 2 SW | 47 |
| Joystick X | 1 |
| Joystick Y | 2 |
| Volume fader | 7 |

The TFT and microSD share the same FSPI clock/data bus but use separate chip-select lines.

---

## Controls

| Control | Action |
|---|---|
| Encoder 1 rotate | Change current FX parameter |
| Encoder 1 click | Cycle to next FX |
| Encoder 1 long press | Toggle normal UI / real spectrum visualizer |
| Encoder 2 rotate | Change wet/dry FX mix |
| Encoder 2 click | Play / pause |
| Encoder 2 long press | Toggle FX bypass |
| Joystick up/down | Select track |
| Joystick right | Play selected track |
| Joystick left | Previous track |
| Linear fader | Master volume |

The fader direction is intentionally reversed in software:

~~~text
Low ADC value  -> high volume
High ADC value -> low volume
~~~

---

## Audio pipeline

~~~text
microSD
  │
  ▼
recursive FLAC scanner
  │
  ▼
AudioSourceSD
  │
  ▼
FLACDecoderFoxen
  │
  ▼
decoded PCM
  │
  ├──────────────▶ real PCM spectrum capture
  │
  ▼
AudioEffectStream
  │
  ▼
I2SStream
  │
  ▼
external DAC
~~~

### Why native sample-rate handling matters

A 48 kHz FLAC played through a fixed 44.1 kHz output clock will sound slower and lower in pitch. The firmware reads FLAC STREAMINFO and configures the output around the actual track format instead of blindly assuming one sample rate.

### Current audio-format target

The current build is aimed at:

- FLAC
- 16-bit PCM output
- mono or stereo
- native track sample-rate handling

24-bit FLAC and additional codecs are outside the current stable target and are part of future development.

---

## Real PCM visualizer

The visualizer is based on **decoded PCM**, not random animation.

The current design:

1. captures samples from the live decoded audio path
2. prepares a spectrum frame
3. performs FFT analysis outside the most timing-sensitive audio work
4. produces 8 display bands
5. renders bars and peak indicators on the OLED and TFT

The TFT visualizer is deliberately frame-limited to reduce display overhead while audio is playing.

---

## Software stack

### Required Arduino libraries

- **arduino-audio-tools**
- **codec-libfoxenflac**
- **Adafruit GFX Library**
- **Adafruit ST7735 and ST7789 Library**
- **U8g2**

The firmware also uses ESP32 Arduino core components including:

- FS / SD
- SPI
- Wire
- FreeRTOS
- ESP heap capabilities

### Current development environment

- Arduino IDE 2.x
- Espressif ESP32 Arduino core **3.3.12**
- Board: **ESP32S3 Dev Module**

---

## Recommended Arduino board settings

These are the current project settings:

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| USB CDC On Boot | Enabled |
| CPU Frequency | 240 MHz (WiFi) |
| Core Debug Level | None |
| USB DFU On Boot | Disabled |
| Erase All Flash Before Sketch Upload | Disabled |
| Events Run On | Core 1 |
| Flash Mode | QIO 80 MHz |
| Flash Size | 16 MB (128 Mb) |
| JTAG Adapter | Disabled |
| Arduino Runs On | Core 1 |
| USB Firmware MSC On Boot | Disabled |
| Partition Scheme | Huge APP (3 MB No OTA / 1 MB SPIFFS) |
| PSRAM | OPI PSRAM |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |
| Zigbee Mode | Disabled |

Serial Monitor:

~~~text
115200 baud
~~~

Upload speed and Serial baud are independent settings.

---

## Build and upload

1. Install the Arduino ESP32 board package.
2. Select **ESP32S3 Dev Module**.
3. Apply the board settings above.
4. Install the required libraries.
5. Format the microSD card with a filesystem supported by the ESP32 SD library.
6. Copy one or more **.flac** files to the card. Subfolders are supported.
7. Open **DJ_Audio_Console_V4.ino**.
8. Compile and upload.
9. Open Serial Monitor at **115200 baud**.

Typical startup output includes:

~~~text
[MEM] PSRAM: YES
[SD] OK @ 20 MHz
[SD] FLAC tracks: ...
[FLAC] FIRST FORMAT ... Hz / ... ch / ... bit
[I2S] OK @ ...
[FX] AudioEffectStream OK
[PLAYER] DIRECT FLAC READY: ...
[AUDIO] Dedicated playback task running on core 0
[SYSTEM] READY
~~~

If the SD card contains no playable FLAC files, the UI still boots and the audio engine is bypassed rather than intentionally starting on an empty source.

---

## Development status

| Area | Status |
|---|---|
| ESP32-S3 hardware bring-up | ✅ Working |
| PSRAM detection | ✅ Working |
| SD card access | ✅ Working |
| Recursive FLAC discovery | ✅ Working |
| Direct FLAC path playback | ✅ Working |
| I2S DAC output | ✅ Working |
| Physical master fader | ✅ Working |
| Dual encoder input | ✅ Working |
| Joystick navigation | ✅ Working |
| TFT UI | ✅ Working |
| OLED UI | ✅ Working |
| DSP signal path | 🟡 Active tuning |
| Native sample-rate switching | 🟡 Active tuning |
| Audio stability under heavy FX | 🟡 Active tuning |
| Real spectrum visualizer | 🟡 Active tuning |
| Metadata / album / artist parsing | ⬜ Planned |
| Seeking / scrub timeline | ⬜ Planned |
| Playlist modes / queue system | ⬜ Planned |
| WAV / AAC / MP3 support | ⬜ Future |
| USB-MIDI / MIDI control | ⬜ Future |
| Recording / sampling | ⬜ Future |
| Advanced mixer / multi-deck features | ⬜ Future |

See **docs/DEVELOPMENT.md** for the detailed roadmap and engineering scope.

---

## Current engineering priorities

The project is intentionally being developed in layers.

### Phase 1 — stability

- eliminate playback underruns and stutter
- verify sample-rate changes across different FLAC files
- keep DSP frame alignment correct
- keep audio decoding isolated from display overhead
- validate SD + TFT shared-SPI behavior
- make startup and no-file conditions fail safely

### Phase 2 — DSP and visualizer

- tune every FX parameter range
- reduce DSP CPU load further
- improve wet/dry behavior
- improve FFT normalization and visual scaling
- verify the visualizer against real music at multiple sample rates
- add clipping/headroom management

### Phase 3 — player UX

- duration and timeline
- seek / scrub
- artist / album / title metadata
- folder browsing
- queue and playlist modes
- shuffle / repeat
- persistent settings

### Phase 4 — DAW expansion

- USB-MIDI / MIDI control
- assignable controls
- sampling / recording experiments
- multiple audio layers
- performance-oriented effect routing
- more advanced mixer workflow

---

## Design goals

The project is not meant to be just another ESP32 music player.

The long-term goal is a **small physical music workstation** with:

- local high-quality audio playback
- tactile controls
- responsive embedded UI
- real-time DSP
- visual feedback
- modular audio routing
- MIDI integration
- room for experimental DJ / performance features

The focus is on building the whole signal path and interaction model directly on embedded hardware rather than relying on a phone or PC for the primary interface.

---

## Known limitations

Current development firmware has several intentional constraints:

- FLAC is the only supported file type in the current build.
- The stable target is 16-bit mono/stereo FLAC.
- Library size is currently capped at 512 tracks.
- Metadata tags are not yet parsed.
- Track duration and seeking are not yet part of the current AudioTools build.
- DSP and spectrum code are still being performance-tuned.
- TFT and SD share the SPI bus, so display work must remain controlled during playback.
- The hardware and firmware are still under active iteration.

These are development constraints, not final design limits.

---

## Documentation

- **[Hardware / BOM / wiring](docs/HARDWARE.md)**
- **[Development roadmap and scope](docs/DEVELOPMENT.md)**
- **[Troubleshooting](docs/TROUBLESHOOTING.md)**

---

## Repository layout

~~~text
DAW/
├── DJ_Audio_Console_V4.ino
├── README.md
└── docs/
    ├── HARDWARE.md
    ├── DEVELOPMENT.md
    └── TROUBLESHOOTING.md
~~~

---


## Contributing & community

Contributions are welcome, especially around audio stability, DSP efficiency, visualizer accuracy, metadata, testing and hardware integration.

- [Contributing guide](CONTRIBUTING.md)
- [Security policy](SECURITY.md)
- [Hardware documentation](docs/HARDWARE.md)
- [Development roadmap](docs/DEVELOPMENT.md)
- [Troubleshooting guide](docs/TROUBLESHOOTING.md)
- [MIT License](LICENSE)

Pull requests use a hardware/audio validation checklist, and issues include dedicated bug and feature templates.

## Author

Built and developed by **Ankur Saxena**.

GitHub: [@anksax](https://github.com/anksax)

---

## Status note

This repository represents a live hardware project. Features marked as working are already present in the current firmware, while items under the roadmap are development targets rather than claims about the current build.
