# THE DAW — ESP32-S3 Audio Console

<p align="center"><img src="assets/banner.svg" alt="THE DAW — ESP32-S3 audio console" width="100%"></p>

[![Firmware CI](https://github.com/anksax/DAW/actions/workflows/ci.yml/badge.svg)](https://github.com/anksax/DAW/actions/workflows/ci.yml) [![MIT License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)

A standalone FLAC player and DJ-style audio console for the **ESP32-S3 N16R8**, with physical controls, an ST7789 main display, an SH1106 OLED, real PCM effects, EQ, artist metadata and a playback timeline.

**Active development:** USB/Serial Monitor disconnection still causes distorted output on the user's hardware. Disabling diagnostic writes has not resolved the reported behavior. Do not treat V6 as a stable audio release.

## Quick start

1. Install Arduino-ESP32 **3.3.12**, AudioTools, codec-libfoxenflac, Adafruit GFX, Adafruit ST7735 and ST7789 Library, and U8g2.
2. Open **[DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino](DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino)**. Keep **all files** in that sketch folder together, including lyric/browser/cover headers and the bundled JPEG C decoder. The root V4 sketch is retained as a legacy reference.
3. Use ESP32S3 Dev Module, 16 MB flash, OPI PSRAM and the settings in [Board setup](docs/BOARD_SETUP.md). Wiring is in [Hardware](docs/HARDWARE.md).
4. Put mono/stereo `.flac` files on microSD; nested folders are scanned. Source 24-bit FLAC is decoded into **16-bit PCM** for DSP/I2S. This is not a 24-bit output pipeline.
5. Compile and upload. Use Encoder 2 rotation/click to enter artist and album folders, then select a song; playback does not start automatically at boot. At EOF, playback advances automatically in sequence or shuffle mode.
6. Hold Encoder 2 to open Settings and confirm the build label **V6 R4 20261019 LOG:OFF**. This identifies the new firmware; a failed build/upload can leave the previous build installed.
7. Serial diagnostics are **off by default**. For a capture, set `DAW_SERIAL_DIAGNOSTICS` to `1` in `SerialDiagnostics.h` and rebuild; use 115200 baud. Return it to `0` for normal use.

For Windows, extract the complete project first. A simple path such as `C:\Arduino\DJ_Audio_Console_V6` avoids command-parser issues. See [Troubleshooting](docs/TROUBLESHOOTING.md).

## Controls

| Control | Player / FX action |
|---|---|
| Encoder 1 rotate | Selected FX strength, 0–25; reversed rotation retained |
| Encoder 1 click | Next effect and return to FX page |
| Encoder 1 hold | Selected effect ON/OFF |
| Encoder 2 rotate | Browse current artist/album/song level |
| Encoder 2 click | Enter highlighted folder / play song; selecting the playing song pauses/resumes |
| Encoder 2 double-click | Return one library level |
| Encoder 2 hold | Open/close Settings |
| Encoder 2 rotate in Settings | Select a row; adjust its value while editing |
| Encoder 2 click in Settings | Start/finish editing |
| Encoder 2 double-click in Settings | Finish editing; if not editing, close Settings |
| Fader | Master volume, 0–25 |

Settings contains **BASS, MID, TREBLE, OLED VIEW and PLAY ORDER**. Click to edit OLED VIEW, then rotate between FX, SPECTRUM and LYRICS. Edit PLAY ORDER and rotate clockwise for SHUFFLE or counterclockwise for SEQUENTIAL. A hold takes about 650 ms. E2 single-click waits for the 300 ms double-click window so a back gesture does not first enter/play a selection.

FX, spectrum and lyrics appear on the OLED. Title, artist, album, timeline, library, volume and Settings appear on the TFT. Brightness adjustment is removed; TFT BL is fixed HIGH and OLED contrast is full. GPIOs 1, 2 and 18 are no longer assigned to joystick inputs.

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
| Reverb | Four damped comb delays and two all-pass diffusers; saved 0–25 strength |

Enable up to **three PCM effects**, plus SLOW. Only per-effect enable toggles exist; there is no separate global enable control. Each effect keeps its own 0–25 strength when browsing effects. Internal character presets replace separate parameter/mix controls. Bass/mid/treble EQ is ±12 dB.

FX strength, enabled stages and EQ are stored in NVS after changes settle and playback is paused/stopped. Pause and wait at least two seconds before removing power if you want edits saved. Legacy settings are migrated. Volume, shuffle, OLED view and track selection are not persisted; reboot starts in sequential order on the FX view.

SLOW changes wait for **250 ms without another adjustment**, then update the output clock only if the requested rate differs. This reduces repeated reconfiguration; a final clock transition can still be audible. SLOW is not pitch-preserving time stretching. Vocoder uses a four-harmonic carrier, lower gain, a soft clipping knee and approximately 20 ms strength/enable ramps. Hardware sound quality remains under test.

## Metadata, timeline and visualization

- Bounded native FLAC Vorbis-comment parsing for TITLE, ARTIST, ALBUM and ALBUMARTIST, with a small cache for visible/playing tracks.
- Missing artist tags fall back to ALBUMARTIST or the album-artist folder where available, otherwise unknown. [MusicBrainz Picard tagging](docs/MUSICBRAINZ_TAGGING.md) happens on a PC; the ESP32 does not query MusicBrainz or extract vocal stems.
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
- Hardware confirmation is needed for encoder single/double/hold gestures during playback, fixed TFT backlight, rapid SLOW/vocoder changes, long playback, sample-rate changes and USB disconnects.
- TFT and SD share SPI. UI drawing uses a 153,600-byte PSRAM canvas; only one 8-row stripe is transferred per loop, at 16 MHz. The standard Arduino SPIClass transaction mutex serializes TFT and SD traffic; transfers do not acquire the broader decoder/I2S audio mutex. A five-second repaint restores retained pixels without clearing/resetting the panel. This is recovery for lost contents, not proof of a physical SPI/reset/power fix.
- New tag reads are deferred while playing. Previously cached tags remain visible; uncached artists show a pause-to-read hint until playback pauses/stops.
- No dual-deck mixing, recording, network metadata lookup, MP3, MIDI or pitch-preserving time stretching is implemented. Two-song mixing requires separate decoder/buffer paths and a mixer; it is not enabled by stacking effects.

See [V6 changes](docs/CHANGELOG_V6.md), [development/testing](docs/DEVELOPMENT.md), [contributing](CONTRIBUTING.md) and [license](LICENSE).


### Current encoder-only prototype — 19 October 2026

The owner-supplied `DAW_Encoder2(2).ino` is integrated into the existing V6 sketch folder so the required headers, scripts and CI remain together. The current build uses two encoders and a volume fader; the joystick is removed.

TFT composition uses a 240×320 RGB565 PSRAM canvas and short SPI transfers; SD and TFT transactions share the SPIClass bus lock. Tags not already cached are read only while paused/stopped, while album names fall back to folders immediately. Keep OPI PSRAM enabled.

Reverb reserves about 17.25 KiB of internal delay memory per DSP instance (about 51.75 KiB across the prototype and stereo clones). Delay buffers are reserved before playback; a failed allocation retains dry audio. Positive EQ boosts apply compensating gain to reduce digital clipping; analog output quality still needs a listening test with the actual CJM1334/output load.

The supported folder structure is **album artist/album/song**. Optional same-basename `.lrc` files provide synchronized OLED lyrics. Folder `cover.jpg`/`folder.jpg` (also `.jpeg`) or embedded FLAC JPEG can add a thumbnail. Missing or unsupported artwork leaves no placeholder. Files load at stopped track transitions. See [Lyrics and albums](docs/LYRICS_AND_ALBUMS.md) for format/memory limits and [third-party attribution](docs/THIRD_PARTY.md) for the bundled JPEG decoder.

Earlier control mappings are recorded as history in [V6 changes](docs/CHANGELOG_V6.md); the table above describes the current build.
