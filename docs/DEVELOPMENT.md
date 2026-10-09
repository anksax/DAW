# Development and acceptance testing

The current sketch is `DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino`; the root V4 file is historical. Use Arduino-ESP32 3.3.12 and the [saved board profile](../board-fqbn.txt). CI pins AudioTools to `630fa74ddec7f5ca9ede19b5b43a394c20b02229` and Foxen to `79042f31e1e35e91c6dacbe1fdcb69e71a152cb1`; these CI versions are not a claim about the user's installed libraries.

Run `bash validation/run_checks.sh`. Python extracts the current DSP/control code into a host API model; g++ runs it and the actual metadata/diagnostic headers with address/undefined-behavior sanitizers. Do not infer ESP32 timing or audio quality from these tests. CI separately compiles the real sketch and its headers.

## Hardware acceptance

| Area | Required check |
|---|---|
| Boot/library | Empty SD, single track, nested files, malformed/unsupported FLAC |
| Formats | 44.1/48 kHz, mono/stereo, 16/24-bit source, track rate changes |
| Transport | Pause/resume position, EOF next, shuffle no immediate repeat, previous |
| Controls | Click versus hold, clockwise E1 behavior, joystick neutral/re-arm, settings exit |
| Persistence | Change strengths/mask/EQ/brightness, pause, wait two seconds, power cycle |
| Audio | Every effect individually, maximum stack, rapid SLOW/vocoder edits, silence and clipping |
| UI | OLED FX/spectrum, TFT title/artist/timeline, brightness GPIO 21, missing tags |
| USB | Close monitor and IDE mid-song, reopen, compare USB wall-charger operation |
| Endurance | One hour playback with and without FX/visualization |

For diagnostic captures enable `DAW_SERIAL_DIAGNOSTICS=1` in `SerialDiagnostics.h` and rebuild, then return it to 0. Record complete board/core/library versions, power/connector arrangement, audio format, enabled FX and whether the event reboots the UI or only corrupts audio. Logs alone do not establish DMA underruns.

USB-disconnect distortion remains open. Serial diagnostics being off has not resolved the user's report. Priorities are reproducing it with standalone power, verifying boot versus stall, measuring display/decode latency and reducing timing pressure before adding features.

Future work includes seeking, queue/folder navigation, recording, MIDI, pitch-preserving time stretching and two-decoder mixing. None is implemented by V6.
