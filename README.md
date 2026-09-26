# DAW

## ESP32-S3 Digital Audio Workstation

A standalone hardware Digital Audio Workstation (DAW) built around the
**ESP32-S3-WROOM-1 DevKit N16R8**.

The project is designed to combine local lossless/compressed audio playback,
hardware controls, a graphical interface, and eventually MIDI/USB-MIDI
functionality into a compact standalone system.

---

## Project Status

🚧 **Currently in active development**

The current development priority is the **audio pipeline**.

The core audio path is already working:

```text
Micro SD Card
     │
     │ SPI / FSPI
     ▼
 ESP32-S3
     │
     │ FLAC / MP3 decoding
     ▼
 PCM Audio
     │
     │ I2S
     ▼
 UDA1334A DAC
     │
     ▼
Headphones / Amplifier