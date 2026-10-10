# V6 troubleshooting updates

## Distortion after closing Serial Monitor / IDE (open issue)

The user still reports this after disabling firmware and AudioTools serial diagnostics. The logging mitigation has not fixed the observed event. Do not assume a USB reset or firmware underrun without evidence.

1. Verify upload completed successfully and settings show V6 20261010 LOG:OFF. Sketch/headers compile into one firmware image, not separate ESP32 files. A failed compile/upload can leave an old build running. DAW_SERIAL_DIAGNOSTICS defaults to 0 in SerialDiagnostics.h.
2. Play the same file with FX disabled, then compare monitor-close behavior with standalone power from a USB wall charger.
3. Note whether the UI shows the boot screen/stops playback or remains responsive while audio corrupts.
4. Record which USB socket is used (native USB versus USB-to-UART), and the power/DAC ground arrangement.
5. For a capture, enable diagnostics and rebuild, save the complete boot/performance log, then turn diagnostics off again. A reconnect may itself reset the board, so it cannot prove when the earlier reset occurred.

## Compiler / Windows setup

Keep both FlacTags.h and SerialDiagnostics.h beside the V6 .ino. Extract the ZIP before opening. For “bootloader.bin was unexpected at this time”, try C:\Arduino\DJ_Audio_Console_V6 and collect verbose compile output if it persists. AudioTools headers must precede the first function so Arduino-generated prototypes can see AudioInfo. The unsigned ramp arithmetic avoids max() deduction conflicts on ESP32. “Multiple libraries found for SD.h” is informational if the ESP32 core SD library is selected.

## Brightness and joystick

Use joystick hold to open settings, up/down to select a row and left/right to adjust; return to neutral after each step. Keep the stick neutral at startup. Enable diagnostics for [JOY] raw/center values and [BACKLIGHT] attach/write status. Brightness controls PWM on GPIO 21 and OLED contrast; physical TFT BL wiring is required.

## Speed and FX

With SLOW off, the decoder and physical I2S rates should match. SLOW deliberately changes speed and pitch; edits apply after 250 ms without adjustment and an individual transition may still be audible. Up to three PCM stages plus SLOW are allowed. Vocoder output is intentionally softer; listening validation remains pending.

---

## Earlier general troubleshooting

# Troubleshooting

## Serial monitor

Use:

- baud: 115200
- upload speed: 921600 is fine if reliable

Upload speed and Serial baud do not need to match.

## SD card mounts but 0 FLAC files are found

Check:
- extension is .flac
- card filesystem is readable by the ESP32 SD library
- files are not zero-byte or hidden metadata stubs
- recursive scanner output in Serial Monitor
- directory names are readable

The scanner supports subfolders.

## Playback sounds slower or lower-pitched

This usually indicates a sample-rate mismatch.

With opt-in diagnostics enabled, check Serial output for:
- FLAC format sample rate
- I2S sample rate

They should correspond.

Typical values:
- 44100 Hz
- 48000 Hz

Do not force every track to 44100 Hz if the library contains 48000 Hz files.

## Playback stutters

Likely sources:
- too much TFT drawing
- DSP workload
- FFT workload
- SD latency
- SPI contention
- verbose logging
- poor SD card
- delay buffer memory behavior
- task starvation

Recommended debugging order:

1. disable visualizer
2. bypass FX
3. test one known-good 16-bit stereo FLAC
4. reduce TFT activity
5. test SD at 20 MHz, then 4 MHz
6. compare playback with each individual effect
7. check free heap / PSRAM
8. inspect serial logs for format changes

## Assert failure in AudioEffectStream

A frame-alignment assertion means the effect stream received a byte length that is not aligned to the active PCM sample/channels format.

Do not silence the assertion without finding the alignment issue.

Verify:
- bits per sample
- channel count
- write length
- format propagation
- decoder output format

## Boot loop when no tracks exist

The player should not be started on an empty source.

The current firmware intentionally bypasses the audio engine when the recursive scanner returns zero tracks.

## foxen-flac.h missing

Install the codec dependency used by CodecFLACFoxen:
- codec-libfoxenflac

The AudioTools wrapper alone does not provide the underlying Foxen FLAC header.

## Multiple SD.h libraries found

Arduino may report several installed SD libraries.

The intended one for this project is the SD library bundled with the installed Espressif ESP32 core.

If compilation succeeds and the output says the ESP32 package SD library is used, the warning is usually informational.

## File::path() compile error with c_str()

On Arduino-ESP32 3.x, File::path() returns a const char pointer. Do not call .c_str() on it.

Correct style:

~~~cpp
Serial.println(file.path());
~~~

## max() type mismatch errors

Modern toolchains are stricter about C++ template type matching.

Avoid mixing:
- int
- int32_t
- volatile int

Use explicit casts or direct conditional logic when necessary.

## UI works but effects seem inaudible

Check:
- FX is enabled, not bypassed
- wet/dry mix is above zero
- current effect parameter is not at a neutral setting
- AudioEffectStream is actually in the output chain
- the cloned effect instances share or observe runtime control state

## Visualizer does not react

Check:
- PCM capture is actually called from the live audio stream
- visualizer frame-ready flag changes
- FFT task / function is running
- active sample rate is valid
- bars are not being immediately decayed to zero
- rendering is not clearing the graph after drawing

## PSRAM

Expected for N16R8:
- approximately 8 MB PSRAM

Board setting:
- OPI PSRAM

If PSRAM is missing, verify board configuration before debugging the application.

## Good first test file

For the cleanest baseline:
- FLAC
- 16-bit
- stereo
- 44.1 kHz or 48 kHz
- ordinary music file
- stored directly in the SD root

Once that is stable, test nested folders and mixed sample rates.

## Reporting a bug

Include:
- full compiler error or full boot log
- exact firmware commit
- ESP32 core version
- AudioTools version
- codec-libfoxenflac version
- FLAC sample rate
- FLAC bit depth
- FLAC channel count
- whether FX was enabled
- whether visualizer was active
