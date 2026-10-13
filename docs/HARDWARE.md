# Hardware, BOM and Wiring

This document describes the current hardware target for the ESP32-S3 DJ Audio Console.

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
| SH1106 128x64 I2C OLED | 1 | FX / spectrum UI |
| microSD module or socket | 1 | Audio storage |
| UDA1334A / CJMCU I2S DAC | 1 | Audio DAC |
| KY-040 style rotary encoders | 2 | Effect and playback controls |
| Analog joystick module | 1 | Track selection / navigation |
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
| Joystick | X | 1 |
| Joystick | Y | 2 |
| Joystick | SW | 18 |
| Volume fader | analog | 7 |

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

Click enters the highlighted folder or plays the highlighted song; hold toggles shuffle; rotation browses the current artist/album/song level. OLED view is selected in main settings.

### Joystick
- X: GPIO 1
- Y: GPIO 2

The firmware calibrates the joystick center during startup.

### Linear fader
- analog: GPIO 7

The software mapping is intentionally reversed:
- low ADC -> high volume
- high ADC -> low volume

## Power and grounding

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
| Partition Scheme | Huge APP (3 MB No OTA / 1 MB SPIFFS) |
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
- [ ] joystick center is stable
- [ ] fader spans most of the ADC range
- [ ] I2S DAC produces clean audio with FX bypassed
- [ ] all modules share ground
- [ ] no obvious power sag or brownout occurs during playback

## Current hardware scope

The present hardware is intentionally a single-deck embedded audio workstation. Multi-deck audio, recording inputs, MIDI hardware, balanced outputs and custom PCB integration are future expansion areas.


V6 main-screen settings use joystick hold to enter/exit and joystick axes to select/adjust bass/mid/treble EQ and OLED view. Joystick click exits settings or plays/pauses outside settings. TFT BL GPIO 21 is held HIGH; OLED contrast is fixed at 255. Brightness adjustment is removed. See the README for the complete mapping and NVS save behavior.


The TFT update revision requires 153,600 bytes of OPI PSRAM for its screen canvas. It lowers TFT SPI to 16 MHz and sends eight rows per transaction on the shared FSPI bus. Keep CS/DC/RST and power wiring short and secure; periodic redraw cannot correct loss of power, wiring faults or a controller stuck asleep.


R2 reverb reserves about 51.75 KiB additional internal RAM across prototype and stereo clones. OPI PSRAM still holds the TFT canvas. Brightness adjustment is removed; TFT BL is held HIGH and OLED contrast fixed at 255. PCM5102A modules provide line output; TI specifies minimum 1 kOhm load per output. A headphone amplifier is needed for lower-impedance headphones. https://www.ti.com/lit/ds/symlink/pcm5102a.pdf

Current R3 control mapping: E2 rotate browses the current level, click enters a folder or plays the highlighted song, hold toggles shuffle. Joystick left returns to the parent level and right enters/selects. OLED view is selected in main settings.
