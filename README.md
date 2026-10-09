# ESP32-S3 DJ Audio Console / DAW

<p align="center"><img src="assets/banner.svg" alt="ESP32-S3 DJ Audio Console" width="100%"></p>

[![Firmware CI](https://github.com/anksax/DAW/actions/workflows/ci.yml/badge.svg)](https://github.com/anksax/DAW/actions/workflows/ci.yml) [![MIT License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A standalone FLAC player and DJ-style audio console for the **ESP32-S3 N16R8**, with physical controls, an ST7789 main display, an SH1106 OLED, real PCM effects, EQ, artist metadata and a playback timeline.

**Active development:** USB/Serial Monitor disconnection still causes distorted output on the user's hardware. Disabling diagnostic writes has not resolved the reported behavior. Do not treat V6 as a stable audio release.

## Quick start

1. Install Arduino-ESP32 **3.3.12**, AudioTools, codec-libfoxenflac, Adafruit GFX, Adafruit ST7735 and ST7789 Library, and U8g2.
2. Open **[DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino](DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino)**. Keep `FlacTags.h` and `SerialDiagnostics.h` in the same folder. The root V4 sketch is retained as a legacy reference.
3. Use ESP32S3 Dev Module, 16 MB flash, OPI PSRAM and the settings in [Board setup](docs/BOARD_SETUP.md). Wiring is in [Hardware](docs/HARDWARE.md).
4. Put mono/stereo `.flac` files on microSD; nested folders are scanned. Source 24-bit FLAC is decoded into **16-bit PCM** for DSP/I2S. This is not a 24-bit output pipeline.
5. Compile and upload. Joystick click starts the selected track; playback does not start automatically at boot. At EOF, playback advances automatically in sequence or shuffle mode.
6. Hold the joystick to open settings and confirm the build label **V6 20261010 LOG:OFF**. This identifies the new firmware; a failed build/upload can leave the previous build installed.
7. Serial diagnostics are **off by default**. For a capture, set `DAW_SERIAL_DIAGNOSTICS` to `1` in `SerialDiagnostics.h` and rebuild; use 115200 baud. Return it to `0` for normal use.

For Windows, extract the complete project first. A simple path such as `C:\Arduino\DJ_Audio_Console_V6` avoids command-parser issues. See [Troubleshooting](docs/TROUBLESHOOTING.md).

## Controls

| Control | Player / FX action |
|---|---|
| Encoder 1 rotate | Selected FX strength, 0–25; reversed rotation retained |
| Encoder 1 click | Next effect and return to FX page |
| Encoder 1 hold | Selected effect ON/OFF |
| Encoder 2 click | Shuffle / sequential |
| Encoder 2 hold | FX / spectrum page on OLED |
| Encoder 2 rotate | Unassigned |
| Joystick up/down | Browse tracks; return to neutral between steps |
| Joystick right | Play selected track |
| Joystick left | Previous playing track |
| Joystick click | Play/pause/resume; exit settings when settings are open |
| Joystick hold | Open/close main-screen brightness/EQ settings |
| Joystick up/down in settings | Select brightness, bass, mid or treble |
| Joystick left/right in settings | Decrease/increase selected setting |
| Fader | Master volume, 0–25 |

Encoders do not edit main-screen settings. FX and spectrum appear only on the OLED. Title, artist, timeline, library, volume and settings appear on the TFT. Brightness controls TFT PWM and OLED contrast; TFT BL must be connected to GPIO 21.

## Audio path and effects

FLAC from SD → Foxen PCM16 decoder → PCM visualization tap → per-channel DSP/EQ → master gain → I2S DAC.

| Effect | Processing |
|---|---|
| Filter | Low/high-pass filtering |
| Echo | Feedback delay |
| Fuzz / distortion | Driven nonlinear processing / clipping |
| Tremolo | Amplitude modulation |
| Compressor | Dynamic-range processing |
| Chorus | Modulated fractional delay |
| Bitcrusher | Reduced resolution / sample holding |
| SLOW | I2S clock rate, 100–50% speed with pitch reduction |
| Phaser | Modulated all-pass stages |
| Flanger | Short modulated delay with feedback |
| Vocoder | Six-band track-envelope modulation of a local synth carrier |
| Ring modulation | Multiplication by an oscillator |

Enable up to **three PCM effects**, plus SLOW. Only per-effect enable toggles exist; there is no separate global enable control. Each effect keeps its own 0–25 strength when browsing effects. Internal character presets replace separate parameter/mix controls. Bass/mid/treble EQ is ±12 dB.

FX strength, enabled stages, EQ and brightness are stored in NVS after changes settle and playback is paused/stopped. Pause and wait at least two seconds before removing power if you want edits saved. Legacy settings are migrated. Volume, shuffle and track selection are not persisted.

SLOW changes wait for **250 ms without another adjustment**, then update the output clock only if the requested rate differs. This reduces repeated reconfiguration; a final clock transition can still be audible. SLOW is not pitch-preserving time stretching. Vocoder uses a four-harmonic carrier, lower gain, a soft clipping knee and approximately 20 ms strength/enable ramps. Hardware sound quality remains under test.

## Metadata, timeline and visualization

- Bounded native FLAC Vorbis-comment parsing for TITLE, ARTIST and ALBUMARTIST, with a small cache for visible/playing tracks.
- Missing artist tags display as unknown. [MusicBrainz Picard tagging](docs/MUSICBRAINZ_TAGGING.md) happens on a PC; the ESP32 does not query MusicBrainz or extract vocal stems.
- Timeline comes from decoded source-frame progress and FLAC total samples. It tracks source position under SLOW. Seeking/scrubbing is not implemented.
- Eight FFT bands use real decoded PCM, with smoothing and peaks. Master gain is applied after DSP.

## Build and validation

The saved [native USB](board-fqbn.txt) and [UART](board-fqbn-uart.txt) profiles select N16R8 board options. CLI defaults are in `sketch.yaml`; they do not force Arduino IDE to remember the board/port.

```powershell
.\build_upload.ps1 -Action list
.\build_upload.ps1 -Action build
.\build_upload.ps1 -Action upload -Port COM7
```

Use your actual port, and `-Uart` for the USB-to-UART profile. Scripts require Arduino CLI and installed dependencies; they compile before uploading and never guess a port. PowerShell execution and physical upload have not been verified here.

```bash
bash validation/run_checks.sh
```

Host checks regenerate DSP/control tests from the sketch and run address/undefined-behavior sanitizers. They cover PCM bounds, bypass, all effects, vocoder stress/silence, rapid strength changes, controls, settings migration, SLOW coalescing, timeline, FFT, partial writes, metadata fuzzing and the optional diagnostic stream. **Host API models are not ESP32 compilation, timing measurements or listening tests.** CI builds V6 with the saved board profile and pinned core/AudioTools/Foxen versions.

## Known limits and next checks

- Closing Arduino IDE/Serial Monitor still produces distorted output on the user's board. The root cause is unconfirmed. Test standalone USB power and distinguish a reboot from an audio stall; see [Troubleshooting](docs/TROUBLESHOOTING.md).
- Hardware confirmation is needed for joystick navigation, TFT brightness, rapid SLOW/vocoder changes, long playback, sample-rate changes and USB disconnects.
- TFT and SD share SPI. UI/metadata work is synchronized with the audio task and dirty regions are redrawn, but measured display holds can still be substantial.
- No dual-deck mixing, recording, network metadata lookup, MP3, MIDI or pitch-preserving time stretching is implemented. Two-song mixing requires separate decoder/buffer paths and a mixer; it is not enabled by stacking effects.

See [V6 changes](docs/CHANGELOG_V6.md), [development/testing](docs/DEVELOPMENT.md), [contributing](CONTRIBUTING.md) and [license](LICENSE).
