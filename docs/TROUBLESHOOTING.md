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

Check Serial output for:
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
