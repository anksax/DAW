# Development and acceptance testing

The current sketch is `DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino`; the root V4 file is historical. Use Arduino-ESP32 3.3.12 and the [saved board profile](../board-fqbn.txt). CI pins AudioTools to `630fa74ddec7f5ca9ede19b5b43a394c20b02229` and Foxen to `79042f31e1e35e91c6dacbe1fdcb69e71a152cb1`; these CI versions are not a claim about the user's installed libraries.

Run `bash validation/run_checks.sh`. Python extracts the current DSP/control code into a host API model; g++ runs it and the actual metadata/diagnostic headers with address/undefined-behavior sanitizers. Do not infer ESP32 timing or audio quality from these tests. CI separately compiles the real sketch and its headers.

## Hardware acceptance

| Area | Required check |
|---|---|
| Boot/library | Empty SD, single track, nested files, malformed/unsupported FLAC |
| Formats | 44.1/48 kHz, mono/stereo, 16/24-bit source, track rate changes |
| Transport | Current-song pause/resume, EOF next, shuffle no immediate repeat |
| Controls | E2 single/double/hold separation, E1 direction, nested back navigation, Settings edit/exit isolation |
| Persistence | Change strengths/mask/EQ, pause, wait two seconds, power cycle |
| Audio | Every effect individually, maximum stack, rapid SLOW/vocoder edits, silence and clipping |
| UI | OLED FX/spectrum, TFT title/artist/timeline, fixed backlight GPIO 21, missing tags |
| USB | Close monitor and IDE mid-song, reopen, compare USB wall-charger operation |
| Endurance | One hour playback with and without FX/visualization |

For diagnostic captures enable `DAW_SERIAL_DIAGNOSTICS=1` in `SerialDiagnostics.h` and rebuild, then return it to 0. Record complete board/core/library versions, power/connector arrangement, audio format, enabled FX and whether the event reboots the UI or only corrupts audio. Logs alone do not establish DMA underruns.

USB-disconnect distortion remains open. Serial diagnostics being off has not resolved the user's report. Priorities are reproducing it with standalone power, verifying boot versus stall, measuring display/decode latency and reducing timing pressure before adding features.

Future work includes seeking, playback queues, recording, MIDI, pitch-preserving time stretching and two-decoder mixing. Folder navigation is already implemented. None is implemented by V6.


## Current encoder-only revision

The owner-supplied `DAW_Encoder2(2).ino` is integrated under the existing V6 sketch name. Settings shows **V6 R4 20261019 LOG:OFF/ON**. E2 double-click returns one folder level and hold opens Settings. Test single-click deferral, bounce, click versus double-click/hold, current-song pause/resume and Settings row/edit isolation.

Native checks extract the actual button state machine separately from event-driven handler tests. They cover debounce, double-click timing, hold/re-arm, delayed second click, Settings wrapping/edit bounds and retained audio/metadata/lyrics/JPEG behavior. Host models do not measure FreeRTOS scheduling or physical switch behavior.

TFT drawing remains in a PSRAM canvas with short SPI stripes independent of the decoder/I2S mutex. Test controls while audio plays, canvas failure, maximum FX stack, sample-rate changes, delayed metadata, repaint and USB-disconnect behavior on the board. Test source-time lyrics during pause/SLOW, missing art/LRC and album-transition delays.

FX strengths/enables and EQ persist only after an idle save. Shuffle, OLED page, volume and selection remain session-only. Onboard SD wiring still requires exact-board verification. Historical joystick controls in the changelog are superseded by the README.
