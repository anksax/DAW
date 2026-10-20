# Hardware, BOM and Wiring

This document describes the current **THE DAW** prototype, updated 19 October 2026. The active sketch uses two encoders and a fader; the joystick is removed.

## Core controller

**ESP32-S3-WROOM-1 DevKit N16R8**
- 16 MB flash
- 8 MB PSRAM
- dual-core ESP32-S3
- project target: Arduino-ESP32 3.3.12
- board profile: ESP32S3 Dev Module

The current firmware uses:
- Core 0 for the dedicated audio task
- Core 1 for Arduino/UI work
- OPI PSRAM enabled
- 240 MHz CPU clock

## Bill of materials

| Part | Quantity | Purpose |
|---|---:|---|
| ESP32-S3-WROOM-1 N16R8 DevKit | 1 | Main MCU |
| ST7789 240x320 TFT | 1 | Main UI |
| SH1106 128x64 I2C OLED (current driver) | 1 | FX / spectrum / lyrics |
| ESP board's onboard microSD reader | 1 | Audio storage; TFT SD slot unused |
| CJM1334 / UDA1334A I2S DAC | 1 | Audio DAC |
| KY-040 style rotary encoders | 2 | Effect and playback controls |
| 10 kΩ linear slide potentiometer | 1 | Master volume |
| 3.5 mm audio jack | 1 | Analog audio output |
| Optional headphone amplifier | 1 | Higher headphone drive |
| Decoupling capacitors / wiring / breadboard or PCB | as required | Integration |

## Pin assignment

| Peripheral | Signal | GPIO |
|---|---|---:|
| microSD | CS | 10 |
| microSD + TFT | MOSI | 11 |
| microSD + TFT | SCK | 12 |
| microSD | MISO | 13 |
| TFT | CS | 3 |
| TFT | DC | 14 |
| TFT | RST | 15 |
| TFT | BL | 21 |
| OLED | SDA | 8 |
| OLED | SCL | 9 |
| I2S DAC | BCLK | 4 |
| I2S DAC | LRCK / WS | 5 |
| I2S DAC | DATA | 6 |
| Encoder 1 | CLK | 41 |
| Encoder 1 | DT | 42 |
| Encoder 1 | SW | 38 |
| Encoder 2 | CLK | 39 |
| Encoder 2 | DT | 40 |
| Encoder 2 | SW | 47 |
| Volume fader | analog | 7 |

GPIOs **1, 2 and 18** are freed from joystick use. These are firmware GPIO assignments, not verified breadboard-hole coordinates.

## Onboard SD reader

The owner uses the ESP board's onboard reader. No separate SD module is needed, and the TFT's SD socket/pads are unused. The sketch configures SPI SD on CS=10, MOSI=11, SCK=12, MISO=13. Confirm those signals against the exact board schematic/pinout before reproducing this on another board; the reader's fixed wiring has not been independently established. Do not assume every N16R8 board includes the same reader.

## Shared SPI bus

The TFT and SD card intentionally share the same FSPI clock and data lines.

Shared:
- MOSI: GPIO 11
- SCK: GPIO 12

SD-only:
- MISO: GPIO 13
- CS: GPIO 10

TFT-only:
- CS: GPIO 3
- DC: GPIO 14
- RST: GPIO 15

Because the TFT and SD share the bus, display writes should be kept controlled during playback. Excessive screen refreshes can compete with SD reads and contribute to audio dropouts.

## I2S DAC

Current digital audio wiring:

| DAC signal | ESP32-S3 |
|---|---:|
| BCLK | GPIO 4 |
| LRCK / WS | GPIO 5 |
| DIN | GPIO 6 |
| GND | GND |
| VCC | according to module requirement |

The current firmware configures I2S around the decoded FLAC format instead of fixing output to one sample rate.

## OLED

SH1106:
- SDA: GPIO 8
- SCL: GPIO 9
- I2C speed used by firmware: 400 kHz

## Controls

### Encoder 1
- CLK: GPIO 41
- DT: GPIO 42
- switch: GPIO 38

Click selects FX, rotation changes strength (0–25), hold toggles the selected effect.

### Encoder 2
- CLK: GPIO 39
- DT: GPIO 40
- switch: GPIO 47

Rotate to browse the current artist/album/song level. Click enters a folder or plays a song; selecting the playing song pauses/resumes it. Double-click returns one level. Hold opens/closes Settings. In Settings, rotate to select a row, click to edit, rotate to adjust and click to finish. Shuffle is under PLAY ORDER; OLED VIEW selects FX/spectrum/lyrics.

### Linear fader
- analog: GPIO 7

The software mapping is intentionally reversed:
- low ADC -> high volume
- high ADC -> low volume

## Physical arrangement

The prototype uses two breadboards side by side, with the ESP bridging their inner terminal strips so every header pin remains accessible. In the agreed landscape orientation USB is to the right and the antenna to the left; occupied positions were reported as 22–43. The TFT, OLED and CJM1334 must also fit on those two boards; only encoders/fader remain outside. Exact hole-to-GPIO positions depend on the printed labels and are not established by the earlier provisional diagrams.

## Power and grounding

The owner supplies the TFT VCC from **5 V**. GPIO signals remain **3.3 V logic**. Keep the 5 V and 3.3 V rails separate, join grounds, and verify each breakout's supply markings. CJM1334 supply/regulator pins must follow the actual module; do not reuse PCM5102A wiring or assume an SCLK-to-ground connection.

For reliable audio:
- use a common ground between ESP32, DAC, displays and controls
- keep DAC analog output wiring short
- avoid routing noisy SPI / display wiring directly beside analog output wiring
- decouple the DAC supply locally
- use a clean supply for any external headphone amplifier
- avoid powering demanding analog stages directly from weak USB rails when debugging noise

## Recommended board settings

| Setting | Value |
|---|---|
| Board | ESP32S3 Dev Module |
| CPU Frequency | 240 MHz (WiFi) |
| Flash Mode | QIO 80 MHz |
| Flash Size | 16 MB |
| Partition Scheme | 16M Flash (3MB APP/9.9MB FATFS), as saved in board-fqbn.txt |
| PSRAM | OPI PSRAM |
| USB CDC On Boot | Enabled |
| Upload Mode | UART0 / Hardware CDC |
| Upload Speed | 921600 |
| USB Mode | Hardware CDC and JTAG |

Serial Monitor:
- 115200 baud

## Hardware validation checklist

Before debugging software, verify:

- [ ] ESP32 boots consistently
- [ ] PSRAM reports approximately 8 MB
- [ ] SD card mounts successfully
- [ ] recursive FLAC scanner finds files
- [ ] TFT renders correctly
- [ ] OLED renders correctly
- [ ] both encoders register rotation and click
- [ ] E2 double-click goes back without playing/entering; hold opens Settings
- [ ] fader spans most of the ADC range
- [ ] I2S DAC produces clean audio with FX bypassed
- [ ] all modules share ground
- [ ] no obvious power sag or brownout occurs during playback

## Current hardware scope

The present hardware is intentionally a single-deck embedded audio workstation. Multi-deck audio, recording inputs, MIDI hardware, balanced outputs and custom PCB integration are future expansion areas.


## Memory and display notes

The TFT canvas needs 153,600 bytes of OPI PSRAM. TFT SPI runs at 16 MHz with eight rows per transaction. Keep CS/DC/RST and power connections secure; periodic repaint cannot correct a power/wiring fault. Reverb reserves about 51.75 KiB additional internal RAM across prototype/stereo clones. Check maximum-stack playback on the actual board.

The sketch instantiates `U8G2_SH1106_128X64_NONAME_F_HW_I2C`; earlier SSD1306 descriptions should not be used to select a driver without identifying the actual OLED. TFT BL GPIO 21 is held HIGH and OLED contrast is 255. Brightness adjustment is removed. FX/EQ save behavior and the full controls are in the [README](../README.md).
