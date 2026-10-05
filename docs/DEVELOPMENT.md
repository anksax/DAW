# Development Roadmap and Engineering Scope

## Project direction

The aim is to evolve the current ESP32-S3 lossless player into a compact embedded DJ / audio workstation with real controls, real DSP and useful performance features.

The immediate focus is **stability first**. Features are only valuable if FLAC playback remains uninterrupted and the UI remains responsive.

## Current architecture

The firmware currently separates responsibilities as follows:

- main loop / Core 1:
  - controls
  - TFT updates
  - OLED updates
  - visualizer processing
  - state management
- dedicated audio task / Core 0:
  - AudioPlayer copy loop
  - FLAC decode feed
  - DSP stream
  - I2S output

This separation exists to reduce interference between display rendering and audio timing.

## Current working scope

### Storage and library
- recursive SD directory traversal
- FLAC file discovery
- direct playback by path
- up to 512 indexed tracks

### Playback
- FLAC decode through FLACDecoderFoxen
- I2S output
- play / pause
- previous track
- selected-track playback
- master volume

### UI
- 240x320 ST7789 TFT
- 128x64 SH1106 OLED
- track browser
- FX status
- volume display
- spectrum mode

### Control surface
- two rotary encoders
- long-press actions
- joystick navigation
- analog volume fader

### DSP
- filter
- echo
- fuzz
- distortion
- tremolo
- compressor
- wet/dry control
- runtime bypass

### Visualization
- real decoded PCM capture
- FFT-based analysis
- eight bands
- peak indicators
- TFT and OLED rendering

## Development phases

### Phase 1: playback stability

Priority: highest

Goals:
- remove all underruns
- prevent boot loops
- validate every buffer boundary
- verify frame alignment through AudioEffectStream
- handle no-file conditions safely
- handle corrupted or unsupported FLAC files cleanly
- test 44.1 kHz and 48 kHz libraries
- test track-to-track sample-rate changes
- profile SD throughput
- profile CPU usage while DSP is enabled
- minimize shared SPI contention

Exit criteria:
- one-hour continuous playback with no stutter
- FX bypass and FX enabled both stable
- switching tracks cannot crash the system
- empty SD card cannot crash the system
- unsupported file cannot crash the system

### Phase 2: DSP quality

Goals:
- calibrate effect parameter ranges
- add headroom management
- prevent clipping between effects
- improve compressor attack/release behavior
- tune echo feedback safely
- improve filter response
- make effect mix perceptually useful
- evaluate stereo-linked vs per-channel behavior
- consider more efficient fixed-point implementations

Possible additions:
- chorus
- flanger
- bitcrusher
- simple reverb
- EQ
- limiter

Any new effect must be accepted only if it does not compromise real-time playback.

### Phase 3: visualizer

Goals:
- validate FFT against known test tones
- normalize across different recording levels
- improve low-frequency response
- improve band spacing
- smooth peaks without lag
- reduce visualizer CPU cost
- disable unnecessary analysis when the visualizer is hidden
- measure rendering cost on TFT

Potential future modes:
- spectrum
- waveform
- VU meters
- stereo meters
- peak / RMS display

### Phase 4: player UX

Planned:
- metadata parsing
- artist
- album
- title
- duration
- timeline
- seek / scrub
- folder browser
- queue
- shuffle
- repeat one
- repeat all
- favorites
- persistent settings

### Phase 5: format support

Current stable target:
- FLAC

Future candidates:
- WAV
- MP3
- AAC

Format support should use clean decoder abstraction rather than duplicating the player logic.

### Phase 6: MIDI and performance control

Potential:
- USB-MIDI
- DIN MIDI via UART
- MIDI CC mapping
- assignable encoder functions
- external footswitch / trigger inputs
- host control from DAW software
- performance presets

### Phase 7: recording and sampling

Longer-term research:
- line input / ADC codec
- sample capture
- one-shot sample triggering
- loop recording
- simple overdub
- clip launching

This may require a different audio codec or dedicated ADC path.

### Phase 8: hardware integration

Potential:
- custom PCB
- better power regulation
- integrated headphone amplifier
- hardware mute
- improved output filtering
- enclosure
- panel-mount controls
- EMI-conscious layout
- battery operation

## Engineering constraints

### CPU budget
FLAC decoding, DSP, FFT and display rendering all compete for CPU time. Audio continuity always wins over animation quality.

### Memory
The board has 8 MB PSRAM, but latency-sensitive DSP buffers should not automatically be moved to PSRAM. Random-access delay lines can perform better in internal RAM when size allows.

### SPI
The TFT and SD card share SPI. Full-screen redraws during playback should be avoided.

### Audio format
The current DSP code assumes a 16-bit processing path. 24-bit FLAC support requires deliberate handling rather than silently treating it as 16-bit.

### Concurrency
Player state changes must remain synchronized with the dedicated audio task.

## Testing matrix

Each significant firmware release should be tested with:

| Case | Test |
|---|---|
| Empty card | UI boots, no crash |
| One FLAC in root | detected and plays |
| FLACs in nested folders | recursively detected |
| 44.1 kHz FLAC | correct pitch / speed |
| 48 kHz FLAC | correct pitch / speed |
| mono FLAC | expected output behavior |
| stereo FLAC | expected output behavior |
| unsupported bit depth | graceful rejection |
| FX bypass | clean uninterrupted playback |
| filter | no stutter |
| echo | no stutter |
| fuzz | no stutter |
| distortion | no stutter |
| tremolo | no stutter |
| compressor | no stutter |
| visualizer hidden | minimum CPU load |
| visualizer active | no audible dropout |
| rapid encoder movement | no crash |
| rapid track change | no crash |
| SD removal | graceful failure if possible |

## Definition of a stable release

A build should only be described as stable when:
- it compiles against the documented library/core versions
- it survives long playback testing
- it has no repeatable boot loop
- it has no repeatable DSP underrun
- all current controls work
- all current display modes work
- supported FLAC formats play at correct speed
- unsupported formats fail cleanly

Until then, the repository remains correctly labelled **active development**.
