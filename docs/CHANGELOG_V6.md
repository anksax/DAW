# V6 changes from the repository's V4 baseline

## R4 — encoder-only prototype (19 October 2026)

This section supersedes historical control mappings below.

- Integrate the owner's latest `DAW_Encoder2(2).ino` into the existing V6 folder. Add the visible **V6 R4 20261019 LOG:OFF/ON** identifier.
- Remove joystick pins, calibration, reads and button handling; GPIOs 1/2/18 are no longer used for those inputs.
- E2 rotates to browse, clicks to enter/play (current song pauses/resumes), double-clicks to return one folder level and holds to open/close Settings.
- Settings has bass/mid/treble, OLED VIEW and PLAY ORDER. Rotate to select a row, click to edit, rotate to adjust, then click to finish. Double-click leaves editing or closes Settings when not editing.
- Keep E1 FX strength/select/toggle and fader volume. Retain reverb, three PCM stages plus SLOW, EQ headroom, album hierarchy/art, lyrics, timeline and buffered TFT updates.
- Correct hardware documentation to CJM1334, the ESP onboard SD reader and unused TFT SD slot. SD GPIOs remain a firmware assumption pending exact-board verification.
- Update actual-source gesture/Settings checks. CI compiles the same V6 entry point.
- Preserve NVS behavior: FX strengths/enables and EQ save while paused/stopped; shuffle/OLED page/volume/selection remain session-only. USB-disconnect distortion and physical display/audio acceptance remain open.


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


## 13 October: playback-time UI responsiveness and TFT refresh

- Separate TFT composition into a 153,600-byte PSRAM canvas; no audio mutex is needed for RAM drawing. Allocate once, clip pixel/line writes and report failed OPI PSRAM allocation on OLED.
- Transfer one dirty eight-row stripe at 16 MHz per loop using the existing SPIClass transaction mutex shared by Arduino SD and Adafruit hardware SPI. Remove the decoder/I2S audio-mutex gate from display transfers, so blocked I2S writes no longer block the screen. Standard HAL SPI locks are required; the header rejects builds that disable them. Round-robin selection prevents a frequently updated stripe from starving others.
- Repaint retained content every five seconds without clearing or resetting the physical display. This may restore lost pixels; physical SPI/controller/power faults are not established or claimed fixed.
- Poll controls first. Defer new SD metadata reads during playback; keep existing cached artists and show an explicit pause-to-read hint for uncached entries.
- Add held vertical joystick repeat after 450 ms at 140 ms intervals; preserve one-shot horizontal actions and settings-exit neutral protection.
- Assign encoder 2 rotation to wrapped library browsing; keep click shuffle and hold FX/spectrum.
- Show V6 20261013 LOG:OFF/ON in settings. Extend actual-source host tests for RAM drawing during audio lock contention, SPI transfer independence/refresh, repeat behavior and E2 browsing; add actual-header canvas allocation/clipping/fairness stress tests.
- Hardware acceptance remains pending; especially verify audio continuity under short SPI transfers, display recovery, joystick response and maximum-stack playback. PC serial-disconnect distortion remains open.


## R2: reverb, album and final E2 mapping

- Append REVERB as persisted FX ID 13; add four damped combs/two all-pass diffusers, fixed startup internal-RAM allocation, bounded lengths/feedback, output headroom and dry fallback on allocation failure. It keeps independent saved strength and the existing per-stage enable/three-PCM-stage cap. Accept old 26-byte strength/parameter records alongside 16/18/new 28-byte records.
- Final E2 mapping: rotate library, click play highlighted song, hold shuffle. Move OLED FX/spectrum selection into main settings; preserve E1 stage controls and joystick play/pause.
- Add ALBUM tag parsing and parent-folder fallback, with album text in playing card and selected-album library header. Existing cache/deferred reads and bounded UTF-8 handling remain.
- Held settings adjustments repeat after 350 ms at 100 ms intervals. Main settings now have bass/mid/treble and OLED view; show V6 R2 20261013 LOG:OFF/ON.
- Remove adjustable brightness, PWM setup and bright NVS reads/writes; fix TFT BL HIGH and OLED contrast to 255. Ignore old bright preferences without disturbing FX/EQ.
- Normalize positive EQ boosts with preamp attenuation to reduce digital overload. Flat-EQ clean PCM remains unaffected; analog headphone/load distortion remains unverified.
- Extend host checks for reverb tail/decay at 8/44.1/48/192 kHz, dry allocation-failure fallback, no processing allocations, old/new persistence, E2 mapping/OLED settings, held EQ adjustment, no brightness read/write, ALBUM/folder fallback and positive-EQ headroom. Listening, wiring and runtime memory/timing still require hardware tests.


## R3: artist/album/song browser, lyrics and artwork

- Sort canonical paths and build fixed-size folder group indices. Browse album artists → albums → songs with joystick or E2, retain parent selection on back, and keep browsing focus independent of EOF/playback changes. Joystick left is now back navigation, not previous-track playback.
- Enlarge/scroll playing-card album names and add album heading/dividers. Fall back to the album-artist folder for song artist metadata. Equal album names under different artists remain separate groups.
- Add a bounded, allocation-free LRC parser and PSRAM cue storage: timestamp fractions, repeated timestamps, metadata/offset, stable ordering, empty cues and UTF-8 truncation. Load matching .lrc/.LRC once at stopped track transitions. OLED VIEW cycles FX/SPECTRUM/LYRICS, with active two-row cues and previous/next preview, source-time pause/SLOW behavior and paging.
- Add optional cached 56×56 folder JPEG or embedded FLAC front-cover JPEG, using the bundled attributed TJpgDec decoder. Check compressed-byte/dimension/metadata range bounds, preserve aspect ratio and omit failed/missing artwork without any placeholder/message. Allocate optional buffers once in PSRAM; do not read art during continuous playback.
- Show V6 R3 20261014 LOG:OFF/ON. Add real-header hierarchy/LRC/JPEG tests, synthetic JPEG fixtures, malformed/truncated cases and folder identity checks. Keep physical blank-panel, USB disconnect and listening/timing acceptance open.

- Rename the TFT header to THE DAW.

TFT names are converted from UTF-8 to the classic font before clipping or scrolling: common Latin accents and punctuation use readable equivalents, combining accents are omitted, and unsupported codepoints use ?. Original metadata and file paths remain unchanged. Full Unicode TFT glyphs are not included.
