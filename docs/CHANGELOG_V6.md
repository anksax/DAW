# V6 changes from the repository's V4 baseline

## Playback and DSP

- Explicit PCM16 decoder/DSP/output routing for 16/24-bit FLAC sources; preserve decoded native rates and configure the physical clock separately for SLOW.
- Apply master gain after nonlinear processing, clamp PCM, keep sample alignment and retry bounded partial output writes.
- Own sequential/shuffled EOF advancement, avoid immediate shuffle repeats, preserve pause/resume position and guard track changes under the audio mutex.
- Add chorus, bitcrusher, SLOW, phaser, flanger, vocoder and ring modulation to the original effects.
- Stack up to three PCM stages plus SLOW; preserve per-effect strength and enable state. Remove redundant global enable and separate UI parameter controls.
- Cache active stages/coefficient updates, ramp wet changes, bound chorus delay at high rates and reduce unconditional audio-task delays.
- Coalesce SLOW edits for 250 ms and avoid clock changes for unrelated FX toggles. An individual transition can remain audible.
- Soften vocoder carrier/clip behavior and extend its strength ramp; validate native bounds and release to silence.

## Controls, UI and storage

- Reverse encoder 1 rotation; 0–25 strength and master volume ranges.
- E1 click selects the next effect, E1 hold toggles that effect, E2 click selects shuffle, E2 hold switches FX/spectrum. E2 rotation is unassigned.
- Joystick hold opens main-screen brightness/EQ settings; axes select/adjust and click exits without pausing. Outside settings click plays/pauses, axes browse/play/previous.
- Lower joystick threshold, reject boot end-stop calibration and use playing-track reference for previous. Navigation remains subject to board confirmation.
- Keep FX/spectrum on OLED; update TFT player/library/volume/settings by dirty regions. Synchronize shared SPI display work with audio.
- Add nonlinear brightness PWM and OLED contrast, plus opt-in backlight/joystick/performance diagnostics. Physical BL wiring is required.
- Add bounded FLAC title/artist/album-artist tags, visible/playing cache, UTF-8 truncation handling and a source-frame timeline.
- Persist strengths, stage mask, EQ and brightness with idle saves and migration. Retain album-artist fallback and PC MusicBrainz Picard tagging instructions.

## Build and diagnostics

- Show V6 20261010 LOG:OFF/ON in settings to verify the uploaded build. Sketch/headers compile into one image; a failed upload may leave old firmware running.

- Include FlacTags.h and SerialDiagnostics.h beside the sketch.
- Replace mixed unsigned-type max() in the wet ramp with explicit uint32_t arithmetic, compatible with ESP32's unsigned-long uint32_t.
- Keep AudioTools headers ahead of the first function and explicitly declare the FLAC reader to prevent generated prototypes preceding AudioInfo.
- Add saved N16R8 native USB/UART profiles, CLI defaults and Windows build/upload helpers. These do not repair IDE board/port persistence.
- Disable serial diagnostics by default and route firmware/AudioTools errors through a best-effort stream. Opt-in logs drop disconnected/full writes, do not flush/retry and use a positive bounded USB timeout.
- **Unresolved:** the user reports distorted audio after closing Arduino even after the logging change. This mitigation is not a verified fix. No reboot, USB reset, power or underrun cause is established for that event.

## Validation and evidence

Native address/undefined-behavior checks pass, including all PCM effects, rapid vocoder strength/full-scale stress at 8/24/48/192 kHz, silence release, control mapping, real joystick/previous functions, SLOW delay, NVS migration, timeline, FFT, metadata fuzz cases and logging on/off drops/no retries.

Supplied earlier runtime logs show 48 kHz / stereo / 24-bit FLAC decoded to PCM16, sequential EOF track 3 → 4, copy maxima around 208 ms during speed changes, and display holds around 90 ms. Zero short-write counts do not prove absence of DMA underruns. Those logs precede the latest edits and do not validate them.

Full local ESP32 compilation, PowerShell execution, physical uploads and listening/USB-disconnect tests were unavailable. Arduino CI is configured to compile this revision; its actual result must be checked independently of the host checks. Do not mark this release stable until hardware acceptance passes.
