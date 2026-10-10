/*
  DJ AUDIO CONSOLE - AUDIOTOOLS EDITION
  =====================================
  ESP32-S3 N16R8
  AudioTools + libfoxenflac
  Real PCM effects through AudioEffectStream
  Real audio-reactive spectrum from decoded PCM

  Controls
  --------
  ENC1 rotate : selected effect strength
  ENC1 click  : next effect
  ENC1 long   : selected effect ON/OFF
  ENC2 rotate : unused
  ENC2 click  : shuffle / sequential
  ENC2 long   : toggle FX / spectrum
  JOY up/down : select track
  JOY right   : play selected
  JOY left    : previous track
  JOY press   : play / pause (starts selected track when stopped)
  JOY hold    : main screen settings; joystick axes select/adjust
  FADER       : master volume

  Supported files in this build: FLAC

  Joystick button wiring:
  SW  -> GPIO 18
  GND -> GND

  FLAC:
  24-bit source FLAC is accepted.
  FLACDecoderFoxen converts decoded audio to 16-bit PCM by default.
*/

#include <Arduino.h>
#include <Preferences.h>
#include <esp_system.h>
#include <string.h>
#include <SPI.h>
#include <FS.h>
#include <SD.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>
#include <U8g2lib.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>
#include <esp_heap_caps.h>

#include "FlacTags.h"
#include "SerialDiagnostics.h"
#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSD.h"
#include "AudioTools/AudioCodecs/CodecFLACFoxen.h"

// Keep every type-defining include before the first function: Arduino inserts
// generated prototypes near that function.
bool readFLACStreamInfo(const String &path, AudioInfo &outInfo);
bool serialLogReady() { return (bool)Serial; }
BestEffortSerialLog debugSerial(Serial, serialLogReady);
#define DAW_LOG if (DAW_SERIAL_DIAGNOSTICS) debugSerial

// ================================================================
// PINS
// ================================================================

#define SD_CS       10
#define SD_MOSI     11
#define SD_SCK      12
#define SD_MISO     13

#define TFT_CS      3
#define TFT_DC      14
#define TFT_RST     15
#define TFT_BL      21

#define OLED_SDA    8
#define OLED_SCL    9

#define I2S_BCLK    4
#define I2S_LRCK    5
#define I2S_DOUT    6

#define ENC1_CLK    41
#define ENC1_DT     42
#define ENC1_SW     38

#define ENC2_CLK    39
#define ENC2_DT     40
#define ENC2_SW     47

#define JOY_X       1
#define JOY_Y       2
#define JOY_SW      18

#define FADER_PIN   7

#define MAX_TRACKS 512

#define UI_TICK_MS 30
#define OLED_TICK_MS 100
#define TIMELINE_TICK_MS 250
#define FADER_TICK_MS 40

#define BUTTON_DEBOUNCE_MS 30
#define LONG_PRESS_MS 650

#define JOY_DEADZONE 500
#define JOY_TRIGGER 900

#define FADER_MIN 80
#define FADER_MAX 4010

// ================================================================
// COLORS
// ================================================================

#define C_BG       0x0822
#define C_CARD     0x10A4
#define C_BORDER   0x21AA
#define C_WHITE    0xFFFF
#define C_MUTED    0x8410
#define C_CYAN     0x07FF
#define C_GREEN    0x07E0
#define C_YELLOW   0xFFE0
#define C_ORANGE   0xFD20
#define C_RED      0xF800
#define C_TRACK    0x2124

// ================================================================
// DISPLAY
// ================================================================

SPIClass spi(FSPI);

Adafruit_ST7789 tft(
  &spi,
  TFT_CS,
  TFT_DC,
  TFT_RST
);

U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(
  U8G2_R0,
  U8X8_PIN_NONE
);

// ================================================================
// TRACK DATABASE
// ================================================================

struct Track {
  String path;
  String name;
};

Track tracks[MAX_TRACKS];

uint16_t trackCount = 0;

int selectedTrack = 0;
int playingTrack = -1;

bool playerReady = false;

bool playerDirty = true;
bool fxDirty = true;
bool browserDirty = true;
bool volumeDirty = true;

// ================================================================
// AUDIO TOOLS PIPELINE
// ================================================================

AudioSourceSD source(
  "/",
  ".flac",
  SD_CS,
  spi,
  true
);

I2SStream i2s;

// Foxen defaults to 16-bit decoded PCM output.
// Therefore 24-bit FLAC source files can still be decoded
// into the 16-bit PCM path used by DSP and I2S.
FLACDecoderFoxen decoder;

// Master volume is applied AFTER nonlinear DSP, not before it.
uint32_t playbackRateFor(uint32_t sourceRate);
volatile uint32_t outputSampleRate = 44100;
volatile bool rateChangePending = false;
uint32_t rateChangedAt = 0;
volatile int masterVolumePercent = 12;
volatile uint32_t outputWaitUsTotal = 0, shortOutputWrites = 0;
volatile uint32_t maxPipelineCpuUs = 0, maxPipelineFrames = 0, maxAudioCopyUs = 0, maxDisplayHoldUs = 0;
static portMUX_TYPE timelineMux = portMUX_INITIALIZER_UNLOCKED;
volatile uint64_t trackPCMBytes = 0, trackTotalFrames = 0;
volatile uint32_t trackSourceRate = 44100;
volatile uint8_t trackChannels = 2;
volatile bool timelineCounting = false;
uint64_t inspectedFLACTotalFrames = 0;
struct TimelineSnapshot { uint64_t frames, total; uint32_t sourceRate; };
// Explicit prototypes avoid Arduino auto-prototype ordering before this type.
TimelineSnapshot snapshotTimeline();
int timelineWidth(const TimelineSnapshot &value, int width);
TimelineSnapshot snapshotTimeline() {
  portENTER_CRITICAL(&timelineMux);
  TimelineSnapshot value{trackPCMBytes / (2 * max((int)trackChannels, 1)), trackTotalFrames, trackSourceRate};
  portEXIT_CRITICAL(&timelineMux);
  if (value.total) value.frames = min(value.frames, value.total);
  return value;
}
int timelineWidth(const TimelineSnapshot &value, int width) {
  return value.total ? (int)(value.frames * width / value.total) : 0;
}
class MasterVolumeStream : public AudioStream {
public:
  explicit MasterVolumeStream(AudioStream &output) : output(output) {}
  void setAudioInfo(AudioInfo format) override {
    AudioStream::setAudioInfo(format);
    AudioInfo physical = format;
    physical.sample_rate = playbackRateFor(format.sample_rate);
    outputSampleRate = physical.sample_rate;
    output.setAudioInfo(physical);
  }
  size_t write(const uint8_t *data, size_t len) override {
    if (!data || len % sizeof(int16_t)) return 0;
    const int gain = masterVolumePercent;
    size_t consumed = 0;
    while (consumed < len) {
      size_t count = min((len - consumed) / sizeof(int16_t), (size_t)256);
      for (size_t i = 0; i < count; ++i) {
        int16_t sample;
        memcpy(&sample, data + consumed + i * 2, sizeof(sample));
        scratch[i] = (int16_t)((int32_t)sample * gain / 100);
      }
      size_t bytes = count * sizeof(int16_t);
      size_t completed = 0;
      for (int attempt = 0; completed < bytes && attempt < 3; ++attempt) {
        uint32_t started = micros();
        size_t requested = bytes - completed;
        size_t written = output.write((const uint8_t *)scratch + completed, requested);
        outputWaitUsTotal += micros() - started;
        if (written != requested) ++shortOutputWrites;
        completed += written; consumed += written;
        portENTER_CRITICAL(&timelineMux);
        if (timelineCounting) trackPCMBytes += written;
        portEXIT_CRITICAL(&timelineMux);
        if (!written) break;
      }
      if (completed != bytes) break;
    }
    return consumed;
  }
  int availableForWrite() override { return output.availableForWrite(); }
  void writeSilence(size_t len) override { output.writeSilence(len); }
private:
  AudioStream &output;
  int16_t scratch[256];
};
MasterVolumeStream masterOutput(i2s);
AudioEffectStreamT<int16_t> effects(masterOutput);

// ================================================================
// SHARED AUDIO STATE
// ================================================================

volatile bool visualizerMode = false;
static portMUX_TYPE fxMux = portMUX_INITIALIZER_UNLOCKED;

volatile int spectrumBars[8] = {};
volatile int spectrumPeaks[8] = {};

volatile int fxMode = 0;
volatile int fxParam = 12;
volatile int fxMix = 18;

// Stage mask is the only FX on/off control; no separate master bypass.
constexpr bool fxEnabled = true;

volatile uint32_t fxRevision = 1;
volatile uint32_t activeSampleRate = 44100;
volatile bool audioFormatPending = false;

// ================================================================
// AUDIO FORMAT WATCHER
// ================================================================

class DJAudioInfoWatcher : public AudioInfoSupport {
public:

  void setAudioInfo(AudioInfo newInfo) override {

    if (!newInfo) {
      return;
    }

    info = newInfo;

    if (newInfo.sample_rate > 0) {
      activeSampleRate = newInfo.sample_rate;
    }

    portENTER_CRITICAL(&fxMux);
    fxRevision++;
    portEXIT_CRITICAL(&fxMux);
    audioFormatPending = true;

    // Format diagnostics are emitted during track selection, outside player.copy().
  }

  AudioInfo audioInfo() override {
    return info;
  }

private:

  AudioInfo info;
};

DJAudioInfoWatcher audioInfoWatcher;

// ================================================================
// DSP
// ================================================================

enum FXMode {
  FX_FILTER = 0,
  FX_ECHO,
  FX_FUZZ,
  FX_DISTORTION,
  FX_TREMOLO,
  FX_COMPRESSOR,
  FX_CHORUS,
  FX_BITCRUSHER,
  FX_SLOW, // Keep existing persisted ID 8.
  FX_PHASER,
  FX_FLANGER,
  FX_VOCODER,
  FX_RINGMOD,
  FX_COUNT
};

const char *fxNames[] = {
  "FILTER",
  "ECHO",
  "FUZZ",
  "DISTORT",
  "TREMOLO",
  "COMPRESS",
  "CHORUS",
  "BITCRUSH",
  "SLOW",
  "PHASER",
  "FLANGER",
  "VOCODER",
  "RING MOD"
};
static_assert(FX_COUNT <= 16, "FX mask storage is 16 bits");

const uint8_t fxCharacterPreset[FX_COUNT] = {12, 12, 16, 15, 12, 12, 12, 16, 25, 12, 18, 12, 10};
uint8_t storedParam[FX_COUNT] = {12, 12, 16, 15, 12, 12, 12, 16, 25, 12, 18, 12, 10};
uint8_t storedMix[FX_COUNT] = {18, 18, 18, 18, 18, 18, 18, 18, 0, 18, 18, 25, 18};
uint16_t enabledFXMask = 0;
int8_t eqDB[3] = {};
int oledPage = 0; // FX or spectrum only
bool mainSettingsOpen = false, mainFrameDirty = false, mainSettingsDirty = true;
bool mainSettingsLayoutDirty = true;
int selectedSetting = 0; // TFT brightness, bass, mid, treble
uint8_t screenBrightness = 18;
bool backlightReady = false;
uint32_t brightnessDuty(uint8_t level) {
  uint32_t value = (uint32_t)constrain((int)level, 1, 25);
  return 4 + (value * value * 251 + 312) / 625;
}
void applyScreenBrightness() {
  uint32_t duty = brightnessDuty(screenBrightness);
  bool written = backlightReady && ledcWrite(TFT_BL, duty);
  oled.setContrast((uint8_t)(16 + (constrain((int)screenBrightness, 1, 25) - 1) * 239 / 24));
  DAW_LOG.printf("[BACKLIGHT] pin=%d level=%u duty=%u attached=%d write=%d\n",
    TFT_BL, (unsigned)screenBrightness, (unsigned)duty, backlightReady, written);
}
void toggleMainSettings() {
  mainSettingsOpen = !mainSettingsOpen;
  mainFrameDirty = true; mainSettingsDirty = true; mainSettingsLayoutDirty = true;
}
uint32_t stackLimitUntil = 0;
uint16_t inspectedFLACBlockSize = 0;
bool shuffleMode = false;
bool settingsPending = false;
uint32_t settingsChangedAt = 0;
Preferences fxPreferences;
bool preferencesReady = false;
volatile bool advancePending = false;
uint32_t eofWaitStarted = 0;

static int16_t tremoloLUT[256];

uint32_t playbackRateFor(uint32_t sourceRate) {
  portENTER_CRITICAL(&fxMux);
  bool slow = fxEnabled && (enabledFXMask & (1U << FX_SLOW));
  int speed = 100 - storedMix[FX_SLOW] * 2;
  portEXIT_CRITICAL(&fxMux);
  uint32_t result = slow ? (uint32_t)((uint64_t)sourceRate * speed / 100) : sourceRate;
  return max(result, (uint32_t)8000);
}

int activeAudioEffects(uint16_t mask) {
  int count = 0;
  for (int i = 0; i < FX_COUNT; ++i) if (i != FX_SLOW && (mask & (1U << i))) ++count;
  return count;
}

// ================================================================
// TREMOLO TABLE
// ================================================================

void initTremoloLUT() {

  for (int i = 0; i < 256; ++i) {

    float a =
      2.0f *
      PI *
      (float)i /
      256.0f;

    tremoloLUT[i] =
      (int16_t)(
        sinf(a) *
        32767.0f
      );
  }
}

// ================================================================
// REALTIME DSP EFFECT
// ================================================================

class DJFXEffect : public AudioEffect {

public:

  DJFXEffect() { initState(); }
  void prepare(uint32_t sr, int mode, int param, int mix, bool enabled, uint32_t revision) {
    stackMask = enabled ? (1U << mode) : 0;
    for (int i = 0; i < FX_COUNT; ++i) { parameters[i] = param; mixes[i] = mix; }
    localMode = mode;
    localParam = param;
    localMix = mix;
    localEnabled = enabled;
    if (localRevision != revision || localSampleRate != sr) {
      localSampleRate = sr;
      updateParameters();
      localRevision = revision;
    }
    refreshStageList();
  }
  static void prepareAll() {
    uint8_t params[FX_COUNT], mixes[FX_COUNT];
    int8_t gains[3];
    portENTER_CRITICAL(&fxMux);
    memcpy(params, storedParam, sizeof(params));
    memcpy(mixes, storedMix, sizeof(mixes));
    memcpy(gains, eqDB, sizeof(gains));
    uint16_t mask = enabledFXMask;
    bool enabled = fxEnabled;
    uint32_t revision = fxRevision, sr = outputSampleRate;
    portEXIT_CRITICAL(&fxMux);
    for (DJFXEffect *instance : instances) if (instance) {
      instance->stackMask = enabled ? mask : 0;
      bool coefficientsDirty = instance->localSampleRate != sr;
      for (int i = 0; i < FX_COUNT; ++i) {
        if (instance->parameters[i] != params[i] * 4) coefficientsDirty = true;
        instance->parameters[i] = params[i] * 4;
        instance->mixes[i] = mixes[i] * 4;
      }
      instance->localEnabled = enabled;
      if (instance->localRevision != revision || instance->localSampleRate != sr) {
        instance->localSampleRate = sr;
        if (coefficientsDirty) instance->updateParameters();
        instance->eqLowAlpha = (int32_t)(32768.0f * (2 * PI * 200) / (sr + 2 * PI * 200));
        instance->eqHighAlpha = (int32_t)(32768.0f * (2 * PI * 4000) / (sr + 2 * PI * 4000));
        for (int i = 0; i < 3; ++i) instance->eqGain[i] = (int32_t)(32768.0f * powf(10.0f, gains[i] / 20.0f));
        instance->localRevision = revision;
        instance->refreshStageList();
      }
    }
  }

  DJFXEffect(const DJFXEffect &other)
    : AudioEffect(other) {
    initState();
  }

  ~DJFXEffect() {
    if (flangerBuffer) free(flangerBuffer);
    if (chorusBuffer) free(chorusBuffer);
    for (auto &instance : instances) if (instance == this) instance = nullptr;

    if (echoBuffer) {

      free(echoBuffer);

      echoBuffer = nullptr;
    }
  }

  AudioEffect *clone() override {

    return new DJFXEffect(*this);
  }

  effect_t process(effect_t input) override {
    int32_t value = input;
    // Fixed order, max three audible FX enabled from the UI. Slow is clock-based.
    if (localEnabled) for (int slot = 0; slot < activeStageCount; ++slot) {
      int stage = activeStages[slot];
      int32_t &wetLevel = currentWetQ15[stage];
      int32_t targetLevel = targetWetQ15[stage];
      int32_t stepQ15 = stage == FX_VOCODER ? max((int32_t)1, wetStepQ15 / 4) : wetStepQ15;
      if (wetLevel < targetLevel) wetLevel = min(wetLevel + stepQ15, targetLevel);
      else if (wetLevel > targetLevel) wetLevel = max(wetLevel - stepQ15, targetLevel);
      if (!wetLevel && !targetLevel) continue;
      localParam = parameters[stage];
      int32_t wet = value;
      switch (stage) {
        case FX_FILTER: wet = processFilter(value); break;
        case FX_ECHO: wet = processEcho(value); break;
        case FX_FUZZ: wet = processFuzz(value); break;
        case FX_DISTORTION: wet = processDistortion(value); break;
        case FX_TREMOLO: wet = processTremolo(value); break;
        case FX_COMPRESSOR: wet = processCompressor(value); break;
        case FX_CHORUS: wet = processChorus(value); break;
        case FX_BITCRUSHER: wet = processBitcrusher(value); break;
        case FX_PHASER: wet = processPhaser(value); break;
        case FX_FLANGER: wet = processFlanger(value); break;
        case FX_VOCODER: wet = processVocoder(value); break;
        case FX_RINGMOD: wet = processRingMod(value); break;
      }
      wet = constrain(wet, (int32_t)-32768, (int32_t)32767);
      value = (value * (32768 - wetLevel) + wet * wetLevel) / 32768;
    }
    // Complementary 3-band tone EQ; independent of the FX bypass switch.
    eqLowState += ((value - eqLowState) * eqLowAlpha) >> 15;
    eqHighState += ((value - eqHighState) * eqHighAlpha) >> 15;
    if (eqGain[0] == 32768 && eqGain[1] == 32768 && eqGain[2] == 32768) return (effect_t)value;
    int32_t low = eqLowState, mid = eqHighState - eqLowState, high = value - eqHighState;
    int64_t result = (int64_t)low * eqGain[0] + (int64_t)mid * eqGain[1] + (int64_t)high * eqGain[2];
    return (effect_t)constrain((int32_t)(result >> 15), (int32_t)-32768, (int32_t)32767);
  }

private:

  static constexpr size_t MAX_ECHO_MS = 180;
  // Reserve 180 ms at 48 kHz per channel; higher rates use a shorter delay.
  static constexpr size_t ECHO_CAPACITY = 8640;
  static DJFXEffect *instances[3]; // prototype + two stereo clones
  int localMode = FX_FILTER, localParam = 50, localMix = 80;
  bool localEnabled = false;
  uint32_t localSampleRate = 44100;
  uint16_t stackMask = 1;
  int parameters[FX_COUNT] = {}, mixes[FX_COUNT] = {};
  uint8_t activeStages[FX_COUNT] = {}, activeStageCount = 0;
  int32_t currentWetQ15[FX_COUNT] = {}, targetWetQ15[FX_COUNT] = {};
  int32_t wetStepQ15 = 150;
  void refreshStageList() {
    activeStageCount = 0;
    uint32_t rampSamples = localSampleRate / 200;
    if (rampSamples == 0) rampSamples = 1;
    wetStepQ15 = (int32_t)(32768 / rampSamples);
    if (wetStepQ15 < 1) wetStepQ15 = 1;
    for (int stage = 0; stage < FX_COUNT; ++stage) {
      targetWetQ15[stage] = localEnabled && (stackMask & (1U << stage)) ? mixes[stage] * 32768 / 100 : 0;
      if (stage != FX_SLOW && (targetWetQ15[stage] || currentWetQ15[stage])) activeStages[activeStageCount++] = stage;
    }
  }
  int32_t eqLowState = 0, eqHighState = 0;
  int32_t eqLowAlpha = 500, eqHighAlpha = 10000;
  int32_t eqGain[3] = {32768, 32768, 32768};
  int16_t *chorusBuffer = nullptr;
  static constexpr size_t CHORUS_CAPACITY = 2048;
  size_t chorusWrite = 0;
  uint32_t chorusPhase = 0, chorusIncrement = 1;
  int chorusBaseSamples = 480, chorusDepthSamples = 120;
  int crushBits = 12, crushPeriod = 1, crushCounter = 0;
  int32_t crushHeld = 0;
  int32_t processChorus(int32_t input) {
    if (!chorusBuffer || CHORUS_CAPACITY < 2) return input;
    int32_t modulation = tremoloLUT[(uint8_t)(chorusPhase >> 16)];
    int32_t delayQ15 = chorusBaseSamples * 32768 + modulation * chorusDepthSamples;
    int delay = constrain((int)(delayQ15 >> 15), 1, (int)CHORUS_CAPACITY - 2);
    int32_t fraction = delayQ15 & 32767;
    size_t read = chorusWrite >= (size_t)delay ? chorusWrite - delay : CHORUS_CAPACITY + chorusWrite - delay;
    size_t older = read ? read - 1 : CHORUS_CAPACITY - 1;
    int32_t delayed = ((int32_t)chorusBuffer[read] * (32768 - fraction) +
                       (int32_t)chorusBuffer[older] * fraction) >> 15;
    chorusBuffer[chorusWrite] = (int16_t)input;
    if (++chorusWrite == CHORUS_CAPACITY) chorusWrite = 0;
    chorusPhase += chorusIncrement;
    return (input + delayed) / 2;
  }
  int32_t processBitcrusher(int32_t input) {
    if (crushCounter <= 0) {
      int32_t step = 1 << (16 - crushBits);
      crushHeld = (input / step) * step;
      crushCounter = crushPeriod;
    }
    --crushCounter;
    return crushHeld;
  }

  // Phaser: four allpass sections mixed with dry audio create moving notches.
  int32_t phaserState[4] = {};
  uint32_t phaserPhase = 0, phaserIncrement = 1;
  int32_t phaserMin = -32000, phaserMax = -16000;
  int32_t processPhaser(int32_t input) {
    int32_t sweep = (int32_t)tremoloLUT[(uint8_t)(phaserPhase >> 16)] + 32768;
    int32_t a = phaserMin + (int32_t)(((int64_t)(phaserMax - phaserMin) * sweep) >> 16);
    int32_t value = input;
    for (int i = 0; i < 4; ++i) {
      int32_t output = (int32_t)(((int64_t)a * value) >> 15) + phaserState[i];
      phaserState[i] = value - (int32_t)(((int64_t)a * output) >> 15);
      value = output;
    }
    phaserPhase += phaserIncrement;
    return constrain((input + value) / 2, (int32_t)-32768, (int32_t)32767);
  }

  // Flanger has independent memory, so stacking it with chorus/echo is valid.
  static constexpr size_t FLANGER_CAPACITY = 2048;
  int16_t *flangerBuffer = nullptr;
  size_t flangerWrite = 0;
  uint32_t flangerPhase = 0, flangerIncrement = 1;
  int flangerBase = 120, flangerDepth = 100;
  int32_t processFlanger(int32_t input) {
    if (!flangerBuffer) return input;
    int32_t delayQ15 = flangerBase * 32768 +
      (int32_t)tremoloLUT[(uint8_t)(flangerPhase >> 16)] * flangerDepth;
    int delay = constrain((int)(delayQ15 >> 15), 1, (int)FLANGER_CAPACITY - 2);
    int32_t fraction = delayQ15 & 32767;
    size_t read = (flangerWrite + FLANGER_CAPACITY - delay) % FLANGER_CAPACITY;
    size_t older = (read + FLANGER_CAPACITY - 1) % FLANGER_CAPACITY;
    int32_t delayed = ((int32_t)flangerBuffer[read] * (32768 - fraction) +
      (int32_t)flangerBuffer[older] * fraction) >> 15;
    flangerBuffer[flangerWrite] = (int16_t)constrain(input + delayed / 2, (int32_t)-32768, (int32_t)32767);
    flangerWrite = (flangerWrite + 1) % FLANGER_CAPACITY;
    flangerPhase += flangerIncrement;
    return (input + delayed) / 2;
  }

  uint32_t ringPhase = 0, ringIncrement = 1;
  int32_t processRingMod(int32_t input) {
    int32_t carrier = tremoloLUT[(uint8_t)(ringPhase >> 16)];
    ringPhase += ringIncrement;
    return (input * carrier) >> 15;
  }

  // Six broad channel bands, built from differences of adjacent lowpasses.
  // Track PCM is the modulator; a local saw oscillator is the carrier.
  // This is a lightweight channel vocoder, not vocal stem extraction.
  static constexpr int VOCODER_EDGES = 7;
  int32_t vocoderAlpha[VOCODER_EDGES] = {};
  int32_t vocoderVoice[VOCODER_EDGES] = {}, vocoderSynth[VOCODER_EDGES] = {};
  int32_t vocoderEnvelope[VOCODER_EDGES - 1] = {};
  int32_t vocoderAttack = 100, vocoderRelease = 10;
  uint32_t vocoderPhase = 0, vocoderIncrement = 1;
  static void followVocoderLP(int32_t target, int32_t &state, int32_t alpha) {
    // Targets/states are PCM16-bounded and alpha <= 32768: product fits int32.
    int32_t step = (target - state) * alpha;
    // Round toward the target so fixed-point states reach zero in silence.
    state += (int32_t)(step < 0 ? -((-step + 32767) / 32768) : (step + 32767) / 32768);
  }
  int32_t processVocoder(int32_t input) {
    // Four-harmonic carrier avoids the full-band discontinuity of a raw saw.
    int32_t carrier = (tremoloLUT[(uint8_t)(vocoderPhase >> 24)]
      + tremoloLUT[(uint8_t)((vocoderPhase * 2U) >> 24)] / 2
      + tremoloLUT[(uint8_t)((vocoderPhase * 3U) >> 24)] / 3
      + tremoloLUT[(uint8_t)((vocoderPhase * 4U) >> 24)] / 4) / 2;
    vocoderPhase += vocoderIncrement;
    for (int i = 0; i < VOCODER_EDGES; ++i) {
      followVocoderLP(input, vocoderVoice[i], vocoderAlpha[i]);
      followVocoderLP(carrier, vocoderSynth[i], vocoderAlpha[i]);
    }
    int64_t sum = 0;
    for (int i = 0; i < VOCODER_EDGES - 1; ++i) {
      int32_t voice = vocoderVoice[i + 1] - vocoderVoice[i];
      int32_t target = voice < 0 ? -voice : voice;
      int32_t delta = target - vocoderEnvelope[i];
      // Gate tiny filter residues, and round release away from zero.
      if (target < 16) target = 0;
      delta = target - vocoderEnvelope[i];
      int64_t step = (int64_t)delta * (delta > 0 ? vocoderAttack : vocoderRelease);
      vocoderEnvelope[i] += (int32_t)(step < 0 ? -((-step + 32767) / 32768) : step / 32768);
      sum += (int64_t)(vocoderSynth[i + 1] - vocoderSynth[i]) * vocoderEnvelope[i];
    }
    int32_t output = (int32_t)(sum / 8192); // Leave headroom for stacked effects.
    if (output > 24000) output = 24000 + (output - 24000) / 4;
    if (output < -24000) output = -24000 + (output + 24000) / 4;
    return constrain(output, (int32_t)-32768, (int32_t)32767);
  }

  void initState() {
    for (auto &instance : instances) if (!instance) { instance = this; break; }
    flangerBuffer = (int16_t *)heap_caps_malloc(FLANGER_CAPACITY * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (flangerBuffer) memset(flangerBuffer, 0, FLANGER_CAPACITY * sizeof(int16_t));
    chorusBuffer = (int16_t *)heap_caps_malloc(CHORUS_CAPACITY * sizeof(int16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (chorusBuffer) memset(chorusBuffer, 0, CHORUS_CAPACITY * sizeof(int16_t));
    echoBuffer = (int16_t *)heap_caps_malloc(ECHO_CAPACITY * sizeof(int16_t),
                                            MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (echoBuffer) {
      echoCapacity = ECHO_CAPACITY;
      memset(echoBuffer, 0, echoCapacity * sizeof(int16_t));
    }
  }

  int16_t *echoBuffer = nullptr;

  size_t echoCapacity = 0;
  size_t echoWrite = 0;
  size_t echoDelaySamples = 4410;

  int32_t echoFeedbackQ15 = 18000;

  int32_t filterState = 0;
  int32_t filterAlphaQ15 = 2000;

  bool highPass = false;

  uint32_t tremoloPhase = 0;
  uint32_t tremoloInc = 1;

  int32_t tremoloDepthQ15 = 18000;

  int32_t compressorEnvelope = 0;
  int32_t compressorThreshold = 10000;
  int32_t compressorRatio = 4;

  uint32_t localRevision = 0;

  // --------------------------------------------------------------
  // ECHO MEMORY
  // --------------------------------------------------------------

  void ensureEchoBuffer(uint32_t sampleRate) {
    (void)sampleRate; // storage is reserved before the audio task starts
  }

  // --------------------------------------------------------------
  // PARAMETER UPDATE
  // --------------------------------------------------------------

  void updateParameters() {

    uint32_t sr =
      localSampleRate;

    if (
      sr < 8000 ||
      sr > 192000
    ) {

      sr = 44100;
    }

    int p =
      constrain(
        parameters[FX_FILTER],
        0,
        100
      );

    // FILTER

    highPass =
      (p >= 50);

    float x;
    float cutoff;

    if (!highPass) {

      x =
        (float)p /
        49.0f;

      cutoff =
        18000.0f *
        powf(
          180.0f /
          18000.0f,
          x
        );

    } else {

      x =
        (float)(p - 50) /
        50.0f;

      cutoff =
        180.0f *
        powf(
          12000.0f /
          180.0f,
          x
        );
    }

    float alpha =
      (
        2.0f *
        PI *
        cutoff
      ) /
      (
        (float)sr +
        2.0f *
        PI *
        cutoff
      );

    filterAlphaQ15 =
      constrain(
        (int32_t)(
          alpha *
          32768.0f
        ),
        1,
        32767
      );

    p = parameters[FX_ECHO];
    // ECHO

    ensureEchoBuffer(sr);

    if (echoCapacity > 0) {

      int maxMs =
        (int)MAX_ECHO_MS;

      int delayMs =
        60 +
        (
          p *
          (maxMs - 60) /
          100
        );

      echoDelaySamples =
        (
          (uint64_t)delayMs *
          sr
        ) /
        1000ULL;

      echoDelaySamples =
        constrain(
          echoDelaySamples,
          (size_t)1,
          echoCapacity - 1
        );

      echoFeedbackQ15 =
        7000 +
        (
          p *
          24000 /
          100
        );
    }

    p = parameters[FX_TREMOLO];
    // TREMOLO

    int hz =
      2 +
      p *
      11 /
      100;

    tremoloInc =
      (uint32_t)(
        (
          (uint64_t)hz *
          256ULL
          << 16
        ) /
        sr
      );

    if (tremoloInc == 0) {
      tremoloInc = 1;
    }

    tremoloDepthQ15 =
      9000 +
      p *
      21000 /
      100;

    p = parameters[FX_COMPRESSOR];
    // COMPRESSOR

    compressorThreshold =
      14000 -
      p *
      100;

    if (
      compressorThreshold <
      (int32_t)2500
    ) {

      compressorThreshold =
        (int32_t)2500;
    }

    compressorRatio =
      2 +
      p *
      6 /
      100;
    p = parameters[FX_CHORUS];
    chorusIncrement = (uint32_t)((0.3f + p * 0.017f) * 256.0f * 65536.0f / sr);
    chorusBaseSamples = (int)((uint64_t)sr * 15 / 1000);
    chorusDepthSamples = (int)((uint64_t)sr * (1 + p / 25) / 1000);
    // Keep the entire modulated delay inside storage at high sample rates.
    // Clamping only the read index would turn the modulation into fractional jitter.
    chorusDepthSamples = constrain(chorusDepthSamples, 1, (int)(CHORUS_CAPACITY - 4) / 3);
    chorusBaseSamples = constrain(chorusBaseSamples, chorusDepthSamples + 2,
      (int)CHORUS_CAPACITY - chorusDepthSamples - 2);
    p = parameters[FX_BITCRUSHER];
    crushBits = 14 - p * 10 / 100;
    crushPeriod = 1 + p * 11 / 100;

    p = parameters[FX_PHASER];
    phaserIncrement = max((uint32_t)1, (uint32_t)((0.1f + p * 0.039f) * 16777216.0f / sr));
    float w = tanf(PI * min(250.0f, sr * 0.2f) / sr);
    phaserMin = (int32_t)(32768 * (w - 1) / (w + 1));
    w = tanf(PI * min(3500.0f, sr * 0.4f) / sr);
    phaserMax = (int32_t)(32768 * (w - 1) / (w + 1));
    p = parameters[FX_FLANGER];
    flangerIncrement = max((uint32_t)1, (uint32_t)(0.25f * 16777216.0f / sr));
    flangerDepth = max(1, (int)((uint64_t)sr * (20 + p * 4) / 100000));
    flangerDepth = min(flangerDepth, (int)(FLANGER_CAPACITY - 4) / 2);
    flangerBase = flangerDepth + 2;
    p = parameters[FX_RINGMOD];
    ringIncrement = (uint32_t)((uint64_t)(30 + p * 20) * 16777216 / sr);
    p = parameters[FX_VOCODER];
    vocoderIncrement = (uint32_t)((uint64_t)(70 + p * 2) * 4294967296ULL / sr);
    const int edges[VOCODER_EDGES] = {80, 160, 400, 1000, 2200, 4500, 8000};
    for (int i = 0; i < VOCODER_EDGES; ++i) {
      float frequency = min((float)edges[i], sr * 0.45f);
      float omega = 2 * PI * frequency;
      vocoderAlpha[i] = (int32_t)(32768 * omega / (sr + omega));
    }
    vocoderAttack = max(1, (int)(32768.0f / (1 + sr * 0.003f)));
    vocoderRelease = max(1, (int)(32768.0f / (1 + sr * 0.04f)));
  }

  // --------------------------------------------------------------
  // FILTER
  // --------------------------------------------------------------

  int32_t processFilter(int32_t in) {

    filterState +=
      (
        (in - filterState) *
        filterAlphaQ15
      ) >> 15;

    if (highPass) {

      return
        in -
        filterState;
    }

    return filterState;
  }

  // --------------------------------------------------------------
  // ECHO
  // --------------------------------------------------------------

  int32_t processEcho(int32_t in) {

    if (
      !echoBuffer ||
      echoCapacity < 2
    ) {

      return in;
    }

    size_t readPos;

    if (
      echoWrite >=
      echoDelaySamples
    ) {

      readPos =
        echoWrite -
        echoDelaySamples;

    } else {

      readPos =
        echoCapacity +
        echoWrite -
        echoDelaySamples;
    }

    int32_t delayed =
      echoBuffer[readPos];

    int32_t fed =
      in +
      (
        (
          delayed *
          echoFeedbackQ15
        ) >> 15
      );

    fed =
      constrain(
        fed,
        -32768,
        32767
      );

    echoBuffer[echoWrite] =
      (int16_t)fed;

    echoWrite++;

    if (
      echoWrite >=
      echoCapacity
    ) {

      echoWrite = 0;
    }

    return
      (in / 5) +
      (
        delayed *
        4 /
        5
      );
  }

  // --------------------------------------------------------------
  // FUZZ
  // --------------------------------------------------------------

  int32_t processFuzz(int32_t in) {

    int32_t drive =
      2 +
      (
        (int)localParam *
        10 /
        100
      );

    int32_t y =
      in *
      drive;

    y =
      y /
      2;

    int32_t threshold =
      28000 -
      (
        (int)localParam *
        240
      );

    if (
      threshold <
      (int32_t)4000
    ) {

      threshold =
        (int32_t)4000;
    }

    if (y > threshold) {

      y =
        threshold +
        (
          (y - threshold)
          >> 3
        );

    } else if (
      y < -threshold
    ) {

      y =
        -threshold +
        (
          (y + threshold)
          >> 3
        );
    }

    return
      constrain(
        y,
        -32768,
        32767
      );
  }

  // --------------------------------------------------------------
  // DISTORTION
  // --------------------------------------------------------------

  int32_t processDistortion(int32_t in) {

    int32_t threshold =
      26000 -
      (
        (int)localParam *
        250
      );

    if (
      threshold <
      (int32_t)1200
    ) {

      threshold =
        (int32_t)1200;
    }

    if (in > threshold) {

      return threshold;
    }

    if (in < -threshold) {

      return -threshold;
    }

    return in;
  }

  // --------------------------------------------------------------
  // TREMOLO
  // --------------------------------------------------------------

  int32_t processTremolo(int32_t in) {

    uint8_t idx =
      (uint8_t)(
        tremoloPhase >>
        16
      );

    int32_t s =
      tremoloLUT[idx];

    int32_t depth =
      tremoloDepthQ15;

    int32_t gain =
      32768 -
      depth / 2 +
      (
        (
          (s + 32767) *
          depth
        ) >> 16
      );

    gain =
      constrain(
        gain,
        3000,
        32768
      );

    tremoloPhase +=
      tremoloInc;

    return
      (
        in *
        gain
      ) >> 15;
  }

  // --------------------------------------------------------------
  // COMPRESSOR
  // --------------------------------------------------------------

  int32_t processCompressor(int32_t in) {

    int32_t a =
      abs(in);

    if (
      a >
      compressorEnvelope
    ) {

      compressorEnvelope +=
        (
          a -
          compressorEnvelope
        ) >> 2;

    } else {

      compressorEnvelope +=
        (
          a -
          compressorEnvelope
        ) >> 8;
    }

    if (
      compressorEnvelope <=
      compressorThreshold
    ) {

      return in;
    }

    int32_t over =
      compressorEnvelope -
      compressorThreshold;

    int32_t reduced =
      over /
      compressorRatio;

    int32_t target =
      compressorThreshold +
      reduced;

    int32_t denominator =
      compressorEnvelope > 0
        ? compressorEnvelope
        : 1;

    int32_t gainQ15 =
      (int32_t)(
        (int64_t)target *
        32768LL /
        denominator
      );

    return
      (
        in *
        gainQ15
      ) >> 15;
  }
};

DJFXEffect *DJFXEffect::instances[3] = {};
DJFXEffect djFX;

// ================================================================
// FFT VISUALIZER
// ================================================================

static constexpr int FFT_N = 1024;
static constexpr int VIS_DECIMATION = 4;
static float fftWindow[FFT_N];
static uint32_t visFrameSampleRate = 44100;

static float fftCos[FFT_N / 2];
static float fftSin[FFT_N / 2];

static uint16_t fftBitReverse[FFT_N];

static int16_t visFrame[FFT_N] = {};

static volatile bool visFrameReady = false;

static portMUX_TYPE visMux =
  portMUX_INITIALIZER_UNLOCKED;

static float fftReal[FFT_N];
static float fftImag[FFT_N];

// ================================================================
// FFT INIT
// ================================================================

void initFFT() {

  for (
    int i = 0;
    i < FFT_N / 2;
    ++i
  ) {

    float angle =
      -2.0f *
      PI *
      (float)i /
      (float)FFT_N;

    fftCos[i] =
      cosf(angle);

    fftSin[i] =
      sinf(angle);
  }

  int bits = 0;

  while (
    (1 << bits) <
    FFT_N
  ) {

    bits++;
  }

  for (
    int i = 0;
    i < FFT_N;
    ++i
  ) {

    int x = i;
    int r = 0;

    for (
      int b = 0;
      b < bits;
      ++b
    ) {

      r =
        (r << 1) |
        (x & 1);

      x >>= 1;
    }

    fftBitReverse[i] = (uint16_t)r;
    fftWindow[i] = 0.5f - 0.5f * cosf(2.0f * PI * i / (FFT_N - 1));
  }
}

// ================================================================
// PCM VISUALIZER TAP
// ================================================================

class AudioVisualizerTap : public AudioStream {

public:

  explicit AudioVisualizerTap(
    AudioStream &out
  )
    : target(out) {
  }

  void setAudioInfo(AudioInfo newInfo) override {
    if (newInfo.bits_per_sample != 16 || newInfo.channels < 1 || newInfo.channels > 2)
      return;
    bool changed = info.sample_rate != newInfo.sample_rate ||
                   info.channels != newInfo.channels || info.bits_per_sample != 16;
    bool channelsChanged = info.channels != newInfo.channels;
    info = newInfo;
    if (changed) {
      captureFill = 0;
      decimationCount = 0;
      decimationSum = 0;
      portENTER_CRITICAL(&visMux);
      visFrameReady = false;
      portEXIT_CRITICAL(&visMux);
    }
    effects.setAudioInfo(newInfo);
    if (channelsChanged) effects.begin(newInfo); // rebuild per-channel DSP clones
    masterOutput.setAudioInfo(newInfo); // explicitly reaches I2S clock/slots
    activeSampleRate = newInfo.sample_rate;
  }

  AudioInfo audioInfo() override {

    return info;
  }

  bool begin() override {

    captureFill = 0;

    writeCounter = 0;

    return
      target.begin();
  }

  void end() override {

    captureFill = 0;

    target.end();
  }

  size_t write(const uint8_t *data, size_t len) override {
    if (!data || !len) return 0;
    uint32_t started = micros(), waited = outputWaitUsTotal;
    DJFXEffect::prepareAll();
    // Capture BEFORE forwarding: some AudioTools versions modify input buffers.
    if (visualizerMode && info.bits_per_sample == 16 && info.channels > 0) {
      const size_t stride = sizeof(int16_t) * info.channels;
      for (size_t offset = 0; offset + stride <= len; offset += stride) {
        // Use left channel to avoid cancellation of out-of-phase stereo audio.
        int16_t sample;
        memcpy(&sample, data + offset, sizeof(sample));
        decimationSum += sample;
        if (++decimationCount < VIS_DECIMATION) continue;
        capture[captureFill++] = (int16_t)(decimationSum / VIS_DECIMATION);
        decimationSum = 0;
        decimationCount = 0;
        if (captureFill == FFT_N) {
          portENTER_CRITICAL(&visMux);
          if (!visFrameReady) {
            memcpy(visFrame, capture, sizeof(visFrame));
            visFrameSampleRate = outputSampleRate;
            visFrameReady = true;
          }
          portEXIT_CRITICAL(&visMux);
          captureFill = 0;
        }
      }
    } else {
      captureFill = 0;
      decimationSum = 0;
      decimationCount = 0;
    }
    size_t result = target.write(data, len);
    uint32_t elapsed = micros() - started, outputWait = outputWaitUsTotal - waited;
    uint32_t cpu = elapsed > outputWait ? elapsed - outputWait : 0;
    if (cpu > maxPipelineCpuUs) {
      maxPipelineCpuUs = cpu;
      maxPipelineFrames = (uint32_t)(len / (2 * max((int)info.channels, 1)));
    }
    return result;
  }

  size_t write(
    uint8_t c
  ) override {

    return
      write(
        &c,
        1
      );
  }

  void writeSilence(
    size_t len
  ) override {

    target.writeSilence(
      len
    );
  }

  int available() override {

    return
      target.available();
  }

  int availableForWrite() override {

    return
      target.availableForWrite();
  }

private:

  AudioStream &target;

  AudioInfo info;

  int16_t capture[FFT_N] = {};

  size_t captureFill = 0;

  uint32_t writeCounter = 0;
  int32_t decimationSum = 0;
  int decimationCount = 0;
};

AudioVisualizerTap visualizerTap(
  effects
);

AudioPlayer player(
  source,
  visualizerTap,
  decoder
);

// ================================================================
// PROCESS FFT
// ================================================================

void processVisualizerFrame() {
  if (!visualizerMode) return;
  int16_t localFrame[FFT_N];
  uint32_t frameRate;
  portENTER_CRITICAL(&visMux);
  if (!visFrameReady) { portEXIT_CRITICAL(&visMux); return; }
  memcpy(localFrame, visFrame, sizeof(localFrame));
  frameRate = visFrameSampleRate;
  visFrameReady = false;
  portEXIT_CRITICAL(&visMux);
  float mean = 0;
  for (int i = 0; i < FFT_N; ++i) mean += localFrame[i];
  mean /= FFT_N;
  for (int i = 0; i < FFT_N; ++i) {
    fftReal[i] = (localFrame[i] - mean) / 32768.0f * fftWindow[i];
    fftImag[i] = 0;
  }
  for (int i = 0; i < FFT_N; ++i) {
    int j = fftBitReverse[i];
    if (j > i) { float v = fftReal[i]; fftReal[i] = fftReal[j]; fftReal[j] = v; }
  }
  for (int len = 2; len <= FFT_N; len <<= 1) {
    int half = len / 2, step = FFT_N / len;
    for (int base = 0; base < FFT_N; base += len) {
      for (int j = 0; j < half; ++j) {
        int a = base + j, b = a + half, k = j * step;
        float tr = fftCos[k] * fftReal[b] - fftSin[k] * fftImag[b];
        float ti = fftCos[k] * fftImag[b] + fftSin[k] * fftReal[b];
        float ar = fftReal[a], ai = fftImag[a];
        fftReal[a] = ar + tr; fftImag[a] = ai + ti;
        fftReal[b] = ar - tr; fftImag[b] = ai - ti;
      }
    }
  }
  // First-order decimation filter has limited alias rejection. Upper bands
  // are indicative energy, not calibrated measurements above 4 kHz.
  const float edges[9] = {35, 80, 160, 320, 640, 1250, 2500, 4000, 6000};
  float peakPower[8] = {};
  float analysisRate = (float)frameRate / VIS_DECIMATION;
  const float amplitudeScale = 4.0f / (FFT_N - 1); // Hann coherent gain
  for (int k = 1; k < FFT_N / 2; ++k) {
    float frequency = k * analysisRate / FFT_N;
    float power = fftReal[k] * fftReal[k] + fftImag[k] * fftImag[k];
    for (int b = 0; b < 8; ++b) {
      if (frequency >= edges[b] && frequency < edges[b + 1]) {
        if (power > peakPower[b]) peakPower[b] = power;
        break;
      }
    }
  }
  for (int b = 0; b < 8; ++b) {
    float amplitude = sqrtf(peakPower[b]) * amplitudeScale;
    float db = 20.0f * log10f(max(amplitude, 0.000001f));
    int level = (int)constrain((db + 60.0f) * (100.0f / 60.0f), 0.0f, 100.0f);
    int old = spectrumBars[b];
    int smooth = level > old ? (old + level * 2) / 3 : (old * 4 + level) / 5;
    spectrumBars[b] = smooth;
    if (smooth > spectrumPeaks[b]) spectrumPeaks[b] = smooth;
  }
}

// ================================================================
// FLAC STREAMINFO
// ================================================================

bool readFLACStreamInfo(
  const String &path,
  AudioInfo &outInfo
) {

  File f =
    SD.open(
      path.c_str(),
      FILE_READ
    );

  if (!f) {

    return false;
  }

  uint8_t sig[4];

  if (
    f.read(sig, 4) != 4 ||
    sig[0] != 'f' ||
    sig[1] != 'L' ||
    sig[2] != 'a' ||
    sig[3] != 'C'
  ) {

    f.close();

    return false;
  }

  while (
    f.available()
  ) {

    uint8_t hdr[4];

    if (
      f.read(
        hdr,
        4
      ) != 4
    ) {

      break;
    }

    bool last =
      (
        hdr[0] &
        0x80
      ) != 0;

    uint8_t type =
      hdr[0] &
      0x7F;

    uint32_t len =
      (
        (uint32_t)hdr[1]
        << 16
      ) |
      (
        (uint32_t)hdr[2]
        << 8
      ) |
      (uint32_t)hdr[3];

    if (
      type == 0 &&
      len >= 34
    ) {

      uint8_t si[34];

      if (
        f.read(
          si,
          34
        ) != 34
      ) {

        f.close();

        return false;
      }

      uint32_t sr =
        (
          (uint32_t)si[10]
          << 12
        ) |
        (
          (uint32_t)si[11]
          << 4
        ) |
        (
          (uint32_t)si[12]
          >> 4
        );

      uint16_t channels =
        (uint16_t)(
          (
            (
              si[12] &
              0x0E
            ) >> 1
          ) + 1
        );

      uint16_t bits =
        (uint16_t)(
          (
            (
              (
                (uint16_t)(
                  si[12] &
                  0x01
                )
              ) << 4
            ) |
            (
              (uint16_t)si[13]
              >> 4
            )
          ) + 1
        );

      if (
        sr == 0 ||
        channels == 0 ||
        bits == 0 ||
        bits > 32
      ) {

        f.close();

        return false;
      }

      inspectedFLACTotalFrames = ((uint64_t)(si[13] & 15) << 32) |
        ((uint64_t)si[14] << 24) | ((uint64_t)si[15] << 16) | ((uint64_t)si[16] << 8) | si[17];
      inspectedFLACBlockSize = (uint16_t)(((uint16_t)si[2] << 8) | si[3]);
      outInfo.sample_rate =
        sr;

      outInfo.channels =
        channels;

      outInfo.bits_per_sample =
        bits;

      f.close();

      return true;
    }

    if (
      len >
      1024 *
      1024
    ) {

      f.close();

      return false;
    }

    if (
      !f.seek(
        f.position() +
        len
      )
    ) {

      f.close();

      return false;
    }

    if (last) {

      break;
    }
  }

  f.close();

  return false;
}

// ================================================================
// UPDATE OUTPUT FOR TRACK
// ================================================================

bool configureNativeFLACOutput(
  const String &path
) {

  AudioInfo sourceInfo;

  if (
    !readFLACStreamInfo(
      path,
      sourceInfo
    )
  ) {

    DAW_LOG.println(
      "[AUDIO] Could not read FLAC STREAMINFO"
    );

    return false;
  }

  DAW_LOG.printf(
    "[FLAC] SOURCE %u Hz / %u ch / %u bit\n",
    (unsigned)sourceInfo.sample_rate,
    (unsigned)sourceInfo.channels,
    (unsigned)sourceInfo.bits_per_sample
  );

  if (
    sourceInfo.channels == 0 ||
    sourceInfo.channels > 2
  ) {

    DAW_LOG.println(
      "[FLAC] Mono/stereo only"
    );

    return false;
  }

  // IMPORTANT:
  // The file itself may be 24 bit.
  // Foxen outputs 16-bit PCM by default.

  AudioInfo pcmInfo =
    sourceInfo;

  pcmInfo.bits_per_sample =
    16;

  activeSampleRate =
    pcmInfo.sample_rate;

  DAW_LOG.printf(
    "[PCM] OUTPUT %u Hz / %u ch / %u bit\n",
    (unsigned)pcmInfo.sample_rate,
    (unsigned)pcmInfo.channels,
    (unsigned)pcmInfo.bits_per_sample
  );

  player.setAudioInfo(pcmInfo);

  portENTER_CRITICAL(&fxMux);
  fxRevision++;
  portEXIT_CRITICAL(&fxMux);
  return true;
}

// ================================================================
// PLAYBACK STATE
// ================================================================

enum PlayState {
  STOPPED,
  PLAYING,
  PAUSED
};

PlayState playState =
  STOPPED;

volatile PlayState sharedPlayState =
  STOPPED;

int currentVolume = 3; // 0..25 UI units

SemaphoreHandle_t audioMutex =
  nullptr;

TaskHandle_t audioTaskHandle =
  nullptr;

// ================================================================
// FORWARD DECLARATIONS
// ================================================================

void markAllDirty();
void playSelected();
void previousTrack();
void nextTrack();

// ================================================================
// AUDIO TASK
// ================================================================

void audioTask(void *param) {
  (void)param;
  uint32_t lastIdleYield = millis();
  for (;;) {
    if (playerReady && sharedPlayState == PLAYING) {
      if (audioMutex && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        if (sharedPlayState == PLAYING) {
          uint32_t copyStarted = micros();
          size_t copied = player.copy();
          uint32_t copyElapsed = micros() - copyStarted;
          if (copyElapsed > maxAudioCopyUs) maxAudioCopyUs = copyElapsed;
          Stream *stream = player.getStream();
          bool exhausted = stream && stream->available() == 0;
          if (!copied && exhausted) {
            if (!eofWaitStarted) eofWaitStarted = millis();
            // Allow pending decode/output buffers to finish, then ask UI to switch.
            if (millis() - eofWaitStarted >= 200) {
              player.stop();
              sharedPlayState = STOPPED;
              advancePending = true;
            }
          } else eofWaitStarted = 0;
        }
        xSemaphoreGive(audioMutex);
      }
      // Blocking I2S writes already pace audio; don't add 2 ms per compressed chunk.
      // Periodic one-tick yield still lets the core-0 idle task service its watchdog.
      if (millis() - lastIdleYield >= 10) { vTaskDelay(1); lastIdleYield = millis(); }
      else taskYIELD();
    } else vTaskDelay(pdMS_TO_TICKS(2));
  }
}

// ================================================================
// BUTTON ENGINE
// ================================================================

enum ButtonEvent {
  NO_EVENT,
  CLICK_EVENT,
  LONG_EVENT
};

struct Button {

  uint8_t pin;

  bool stable = HIGH;
  bool rawLast = HIGH;

  bool longSent = false;

  uint32_t changedAt = 0;
  uint32_t pressedAt = 0;

  void begin() {

    pinMode(
      pin,
      INPUT_PULLUP
    );

    stable =
      rawLast =
      digitalRead(pin);

    changedAt =
      millis();
  }

  ButtonEvent update(
    uint32_t now
  ) {

    bool raw =
      digitalRead(pin);

    if (
      raw !=
      rawLast
    ) {

      rawLast =
        raw;

      changedAt =
        now;
    }

    if (
      now -
      changedAt <
      BUTTON_DEBOUNCE_MS
    ) {

      return NO_EVENT;
    }

    if (
      raw !=
      stable
    ) {

      stable =
        raw;

      if (
        stable ==
        LOW
      ) {

        pressedAt =
          now;

        longSent =
          false;

      } else {

        if (
          !longSent
        ) {

          return CLICK_EVENT;
        }
      }
    }

    if (
      stable ==
      LOW &&
      !longSent &&
      now -
      pressedAt >=
      LONG_PRESS_MS
    ) {

      longSent =
        true;

      return LONG_EVENT;
    }

    return NO_EVENT;
  }
};

Button enc1Button{
  ENC1_SW
};

Button enc2Button{
  ENC2_SW
};

Button joyButton{
  JOY_SW
};

// ================================================================
// ENCODERS
// ================================================================

const int8_t QUAD[16] = {
  0, -1, 1, 0,
  1, 0, 0, -1,
  -1, 0, 0, 1,
  0, 1, -1, 0
};

uint8_t enc1State = 0;
uint8_t enc2State = 0;

int enc1Acc = 0;
int enc2Acc = 0;
static portMUX_TYPE encoderMux = portMUX_INITIALIZER_UNLOCKED;
volatile int encoder1Edges = 0, encoder2Edges = 0;
volatile uint8_t encoder1ISRState = 0, encoder2ISRState = 0;
void ARDUINO_ISR_ATTR encoder1ISR() {
  uint8_t next = ((digitalRead(ENC1_CLK) ? 1 : 0) << 1) | (digitalRead(ENC1_DT) ? 1 : 0);
  portENTER_CRITICAL_ISR(&encoderMux);
  int step = QUAD[(encoder1ISRState << 2) | next];
  encoder1Edges += step;
  encoder1ISRState = next;
  portEXIT_CRITICAL_ISR(&encoderMux);
}
void ARDUINO_ISR_ATTR encoder2ISR() {
  uint8_t next = ((digitalRead(ENC2_CLK) ? 1 : 0) << 1) | (digitalRead(ENC2_DT) ? 1 : 0);
  portENTER_CRITICAL_ISR(&encoderMux);
  int step = QUAD[(encoder2ISRState << 2) | next];
  encoder2Edges += step;
  encoder2ISRState = next;
  portEXIT_CRITICAL_ISR(&encoderMux);
}

uint8_t encoderState(
  uint8_t clk,
  uint8_t dt
) {

  return
    (
      (
        digitalRead(clk)
          ? 1
          : 0
      ) << 1
    ) |
    (
      digitalRead(dt)
        ? 1
        : 0
    );
}

void beginEncoders() {

  pinMode(
    ENC1_CLK,
    INPUT_PULLUP
  );

  pinMode(
    ENC1_DT,
    INPUT_PULLUP
  );

  pinMode(
    ENC2_CLK,
    INPUT_PULLUP
  );

  pinMode(
    ENC2_DT,
    INPUT_PULLUP
  );

  enc1State =
    encoderState(
      ENC1_CLK,
      ENC1_DT
    );

  enc2State =
    encoderState(
      ENC2_CLK,
      ENC2_DT
    );
  encoder1ISRState = enc1State;
  encoder2ISRState = enc2State;
  attachInterrupt(digitalPinToInterrupt(ENC1_CLK), encoder1ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC1_DT), encoder1ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC2_CLK), encoder2ISR, CHANGE);
  attachInterrupt(digitalPinToInterrupt(ENC2_DT), encoder2ISR, CHANGE);
}

int readEncoder(
  uint8_t clk,
  uint8_t dt,
  uint8_t &state,
  int &acc
) {

  uint8_t next =
    encoderState(
      clk,
      dt
    );

  if (
    next ==
    state
  ) {

    return 0;
  }

  int step =
    QUAD[
      (state << 2) |
      next
    ];

  state =
    next;

  if (
    step == 0
  ) {

    acc = 0;

    return 0;
  }

  acc +=
    step;

  if (
    acc >= 2
  ) {

    acc = 0;

    return 1;
  }

  if (
    acc <= -2
  ) {

    acc = 0;

    return -1;
  }

  return 0;
}

// ================================================================
// JOYSTICK
// ================================================================

int joyCenterX = 2048;
int joyCenterY = 2048;

bool joyLatched = false;

void calibrateJoystick() {

  long sx = 0;
  long sy = 0;

  for (
    int i = 0;
    i < 32;
    i++
  ) {

    sx +=
      analogRead(JOY_X);

    sy +=
      analogRead(JOY_Y);

    delay(3);
  }

  joyCenterX =
    sx / 32;

  joyCenterY =
    sy / 32;
  // Do not learn an end-stop as neutral when the stick is held at boot.
  if (joyCenterX < 900 || joyCenterX > 3195) joyCenterX = 2048;
  if (joyCenterY < 900 || joyCenterY > 3195) joyCenterY = 2048;

  DAW_LOG.printf(
    "[JOY] center X=%d Y=%d\n",
    joyCenterX,
    joyCenterY
  );
}

// ================================================================
// JOYSTICK AXES
// ================================================================

void handleJoystick() {
  static bool settingsLatched = false, wasInSettings = false;
  if (mainSettingsOpen) {
    wasInSettings = true;
    int dx = analogRead(JOY_X) - joyCenterX, dy = analogRead(JOY_Y) - joyCenterY;
    if (abs(dx) < JOY_DEADZONE && abs(dy) < JOY_DEADZONE) { settingsLatched = false; return; }
    if (settingsLatched || max(abs(dx), abs(dy)) < JOY_TRIGGER) return;
    settingsLatched = true;
    if (abs(dy) > abs(dx)) {
      selectedSetting = (selectedSetting + (dy > 0 ? 1 : 3)) % 4;
    } else {
      int delta = dx > 0 ? 1 : -1;
      if (!selectedSetting) {
        screenBrightness = (uint8_t)constrain((int)screenBrightness + delta, 1, 25);
        applyScreenBrightness();
      } else {
        portENTER_CRITICAL(&fxMux);
        int band = selectedSetting - 1;
        eqDB[band] = (int8_t)constrain((int)eqDB[band] + delta, -12, 12);
        ++fxRevision;
        portEXIT_CRITICAL(&fxMux);
      }
      settingsPending = true; settingsChangedAt = millis();
    }
    mainSettingsDirty = true; return;
  }
  settingsLatched = false;
  if (wasInSettings) { joyLatched = true; wasInSettings = false; }

  static uint32_t last = 0;

  if (
    millis() -
    last <
    25
  ) {

    return;
  }

  last =
    millis();

  if (
    trackCount == 0
  ) {

    return;
  }

  int x =
    analogRead(
      JOY_X
    );

  int y =
    analogRead(
      JOY_Y
    );

  int dx =
    x -
    joyCenterX;

  int dy =
    y -
    joyCenterY;

  if (
    abs(dx) <
    JOY_DEADZONE &&
    abs(dy) <
    JOY_DEADZONE
  ) {

    joyLatched =
      false;

    return;
  }

  if (
    joyLatched
  ) {

    return;
  }

  int largestAxis =
    abs(dx) >
    abs(dy)
      ? abs(dx)
      : abs(dy);

  if (
    largestAxis <
    JOY_TRIGGER
  ) {

    return;
  }

  joyLatched =
    true;

  // UP / DOWN

  if (
    abs(dy) >
    abs(dx)
  ) {

    if (
      dy > 0
    ) {

      selectedTrack++;

    } else {

      selectedTrack--;
    }

    if (
      selectedTrack >=
      trackCount
    ) {

      selectedTrack = 0;
    }

    if (
      selectedTrack < 0
    ) {

      selectedTrack =
        trackCount - 1;
    }

    DAW_LOG.printf("[JOY] browse raw=%d,%d selected=%d\n", x, y, selectedTrack);
    browserDirty =
      true;

    if (playingTrack < 0) playerDirty = true;

    return;
  }

  // LEFT / RIGHT

  if (
    dx < 0
  ) {

    previousTrack();

  } else {

    playSelected();
  }
}

// ================================================================
// SD SCANNER
// ================================================================

void scanDirectory(
  File dir
) {

  while (
    trackCount <
    MAX_TRACKS
  ) {

    File entry =
      dir.openNextFile();

    if (!entry) {

      break;
    }

    if (
      entry.isDirectory()
    ) {

      scanDirectory(
        entry
      );

      entry.close();

      continue;
    }

    String name =
      entry.name();

    String lower =
      name;

    lower.toLowerCase();

    if (
      lower.endsWith(
        ".flac"
      )
    ) {

      tracks[trackCount].path =
        entry.path();

      tracks[trackCount].name =
        name;

      DAW_LOG.printf(
        "[SD] %u: %s\n",
        trackCount + 1,
        tracks[trackCount].path.c_str()
      );

      trackCount++;
    }

    entry.close();
  }
}

// ================================================================
// LIBRARY SCAN
// ================================================================

void scanLibrary() {

  trackCount = 0;

  File root =
    SD.open("/");

  if (
    !root ||
    !root.isDirectory()
  ) {

    DAW_LOG.println(
      "[SD] Cannot open root"
    );

    return;
  }

  File probe =
    root.openNextFile();

  uint16_t visible = 0;

  while (
    probe &&
    visible < 20
  ) {

    DAW_LOG.printf(
      "[SD] %s%s\n",
      probe.isDirectory()
        ? "DIR  "
        : "FILE ",
      probe.path()
        ? probe.path()
        : "(null)"
    );

    probe.close();

    probe =
      root.openNextFile();

    visible++;
  }

  if (probe) {

    probe.close();
  }

  root.close();

  root =
    SD.open("/");

  if (
    !root ||
    !root.isDirectory()
  ) {

    DAW_LOG.println(
      "[SD] Cannot re-open root"
    );

    return;
  }

  scanDirectory(
    root
  );

  root.close();

  DAW_LOG.printf(
    "[SD] FLAC tracks: %u\n",
    trackCount
  );

  if (
    trackCount > 0
  ) {

    DAW_LOG.println(
      "[SD] Recursive scanner found playable FLAC paths:"
    );

    uint16_t countToPrint =
      trackCount < 8
        ? trackCount
        : 8;

    for (
      uint16_t i = 0;
      i < countToPrint;
      i++
    ) {

      DAW_LOG.printf(
        "[SD]   #%u %s\n",
        i + 1,
        tracks[i].path.c_str()
      );
    }
  }
}

// ================================================================
// PLAY TRACK
// ================================================================

void playTrack(int index) {
  if (!trackCount || !playerReady) return;
  index = constrain(index, 0, (int)trackCount - 1);
  if (!audioMutex || xSemaphoreTake(audioMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    DAW_LOG.println("[PLAY] Audio mutex timeout");
    return;
  }
  AudioInfo checked;
  if (!readFLACStreamInfo(tracks[index].path, checked) || checked.channels < 1 ||
      checked.channels > 2 || checked.sample_rate < 8000 || checked.sample_rate > 192000 ||
      inspectedFLACBlockSize > 5120) {
    xSemaphoreGive(audioMutex);
    DAW_LOG.printf("[TRACK] Unsupported/corrupt FLAC: %s\n", tracks[index].path.c_str());
    if (sharedPlayState == STOPPED) { playState = STOPPED; markAllDirty(); }
    return;
  }
  DAW_LOG.printf("[SWITCH] %d -> %d, heap=%u internal=%u block=%u\n", playingTrack, index,
    (unsigned)ESP.getFreeHeap(), (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    (unsigned)inspectedFLACBlockSize);
  sharedPlayState = STOPPED;
  advancePending = false;
  eofWaitStarted = 0;
  player.stop();
  portENTER_CRITICAL(&timelineMux); timelineCounting = false; portEXIT_CRITICAL(&timelineMux);
  selectedTrack = index;
  bool ok = player.setPath(tracks[index].path.c_str());
  if (ok) ok = configureNativeFLACOutput(tracks[index].path);
  if (ok) {
    playingTrack = index;
    portENTER_CRITICAL(&timelineMux);
    trackPCMBytes = 0; trackTotalFrames = inspectedFLACTotalFrames;
    trackSourceRate = checked.sample_rate; trackChannels = checked.channels; timelineCounting = true;
    portEXIT_CRITICAL(&timelineMux);
    player.play();
    playState = PLAYING;
    sharedPlayState = PLAYING;
  } else {
    player.stop();
    playingTrack = -1;
    portENTER_CRITICAL(&timelineMux); trackPCMBytes = trackTotalFrames = 0; timelineCounting = false; portEXIT_CRITICAL(&timelineMux);
    playState = STOPPED;
    sharedPlayState = STOPPED;
  }
  xSemaphoreGive(audioMutex);
  DAW_LOG.printf("[PLAY] %s: %s\n", ok ? "PLAYING" : "FAILED", tracks[index].path.c_str());
  markAllDirty();
}

// ================================================================
// PLAY SELECTED
// ================================================================

void playSelected() {

  playTrack(
    selectedTrack
  );
}

// ================================================================
// PLAY / PAUSE
// ================================================================

void togglePause() {

  if (
    !playerReady ||
    trackCount == 0
  ) {

    DAW_LOG.println(
      "[TRANSPORT] Player not ready / no tracks"
    );

    return;
  }

  // If stopped, button becomes PLAY.
  if (
    playState == STOPPED ||
    playingTrack < 0
  ) {

    DAW_LOG.println(
      "[TRANSPORT] PLAY selected track"
    );

    playSelected();

    return;
  }

  PlayState previousSharedState =
    sharedPlayState;

  // Prevent new player.copy() calls before locking.
  if (
    playState ==
    PLAYING
  ) {

    sharedPlayState =
      PAUSED;
  }

  if (
    audioMutex &&
    xSemaphoreTake(
      audioMutex,
      pdMS_TO_TICKS(1000)
    ) != pdTRUE
  ) {

    DAW_LOG.println(
      "[TRANSPORT] Audio mutex timeout"
    );

    sharedPlayState =
      previousSharedState;

    return;
  }

  if (
    playState ==
    PLAYING
  ) {

    player.stop();

    playState =
      PAUSED;

    sharedPlayState =
      PAUSED;

    DAW_LOG.println(
      "[TRANSPORT] PAUSED"
    );

  } else if (
    playState ==
    PAUSED
  ) {

    player.play();

    playState =
      PLAYING;

    sharedPlayState =
      PLAYING;

    DAW_LOG.println(
      "[TRANSPORT] RESUMED"
    );
  }

  if (audioMutex) {

    xSemaphoreGive(
      audioMutex
    );
  }

  markAllDirty();
}

// ================================================================
// NEXT TRACK
// ================================================================

void nextTrack() {
  if (!trackCount) return;
  int current = playingTrack >= 0 ? playingTrack : selectedTrack;
  int next = (current + 1) % trackCount;
  if (shuffleMode && trackCount > 1) {
    next = (int)(esp_random() % (trackCount - 1));
    if (next >= current) ++next; // do not repeat the current track immediately
  }
  playTrack(next);
}

// ================================================================
// PREVIOUS TRACK
// ================================================================

void previousTrack() {
  if (!trackCount) return;
  int current = playingTrack >= 0 ? playingTrack : selectedTrack;
  playTrack((current + trackCount - 1) % trackCount);
}

// ================================================================
// FX CONTROLS
// ================================================================

void fxChanged() {
  fxDirty = true;
}

void changeFX(int delta) {
  portENTER_CRITICAL(&fxMux);
  fxMode = ((int)fxMode + delta % FX_COUNT + FX_COUNT) % FX_COUNT;
  fxParam = fxCharacterPreset[fxMode];
  fxMix = storedMix[fxMode];
  portEXIT_CRITICAL(&fxMux);
  fxChanged();
}

// ================================================================
// ENCODER ROTATION
// ================================================================

void handleEncoders() {
  portENTER_CRITICAL(&encoderMux);
  int e1 = -(encoder1Edges / 4);
  encoder1Edges %= 4; encoder2Edges %= 4; // E2 rotation has no assigned action.
  portEXIT_CRITICAL(&encoderMux);
  if (!e1 || mainSettingsOpen || oledPage != 0) return;
  portENTER_CRITICAL(&fxMux);
  fxMix = constrain((int)fxMix + e1, 0, 25);
  storedMix[fxMode] = (uint8_t)fxMix;
  ++fxRevision;
  if (fxMode == FX_SLOW) { rateChangePending = true; rateChangedAt = millis(); }
  portEXIT_CRITICAL(&fxMux);
  settingsPending = true; settingsChangedAt = millis(); fxChanged();
}

// ================================================================
// BUTTON HANDLING
// ================================================================

void handleButtons() {
  uint32_t now = millis();
  ButtonEvent b1 = enc1Button.update(now), b2 = enc2Button.update(now), joy = joyButton.update(now);
  if (!mainSettingsOpen && b1 == CLICK_EVENT) {
    changeFX(1); oledPage = 0; visualizerMode = false;
  }
  if (!mainSettingsOpen && b2 == LONG_EVENT) {
    oledPage = 1 - oledPage; visualizerMode = oledPage == 1;
  }
  if (b2 == CLICK_EVENT) { shuffleMode = !shuffleMode; playerDirty = true; }
  if (!mainSettingsOpen && b1 == LONG_EVENT) {
    portENTER_CRITICAL(&fxMux);
    uint16_t bit = 1U << fxMode;
    if ((enabledFXMask & bit) || fxMode == FX_SLOW || activeAudioEffects(enabledFXMask) < 3)
      enabledFXMask ^= bit;
    else stackLimitUntil = now + 1200;
    ++fxRevision;
    portEXIT_CRITICAL(&fxMux);
    if (fxMode == FX_SLOW) { rateChangePending = true; rateChangedAt = now; }
    settingsPending = true; settingsChangedAt = now; fxChanged();
  }
  if (joy == LONG_EVENT) toggleMainSettings();
  if (joy == CLICK_EVENT) {
    if (mainSettingsOpen) toggleMainSettings();
    else togglePause();
  }
}

// ================================================================
// FADER
// ================================================================

float faderFiltered =
  -1;

void updateFader() {
  static uint32_t last = 0;
  if (millis() - last < FADER_TICK_MS) return;
  last = millis();
  int raw = 0;
  for (int i = 0; i < 4; ++i) raw += analogRead(FADER_PIN);
  raw /= 4;
  if (faderFiltered < 0) faderFiltered = raw;
  faderFiltered = faderFiltered * 0.82f + raw * 0.18f;
  int volume = constrain((int)map((int)faderFiltered, FADER_MAX, FADER_MIN, 0, 25), 0, 25);
  if (volume != currentVolume) {
    currentVolume = volume;
    masterVolumePercent = volume * 4;
    volumeDirty = true;
  }
}

// ================================================================
// SPECTRUM DECAY
// ================================================================

void decaySpectrum() {

  for (
    int i = 0;
    i < 8;
    i++
  ) {

    int barNow =
      spectrumBars[i];

    if (
      barNow > 0
    ) {

      spectrumBars[i] =
        barNow - 1;
    }

    if (
      spectrumPeaks[i] >
      spectrumBars[i]
    ) {

      int peakTarget =
        spectrumPeaks[i] -
        2;

      int currentBar =
        spectrumBars[i];

      spectrumPeaks[i] =
        peakTarget >
        currentBar
          ? peakTarget
          : currentBar;
    }

    if (
      playState !=
      PLAYING
    ) {

      spectrumBars[i] =
        (
          spectrumBars[i] *
          7
        ) / 8;

      spectrumPeaks[i] =
        (
          spectrumPeaks[i] *
          7
        ) / 8;
    }
  }
}

// ================================================================
// TFT SPECTRUM
// ================================================================



// ================================================================
// OLED
// ================================================================

void renderOLED() {
  oled.clearBuffer(); oled.setFont(u8g2_font_5x7_tf);
  if (oledPage == 1) {
    oled.drawStr(0, 7, "PCM SPECTRUM");
    for (int i = 0; i < 8; ++i) {
      int h = constrain((int)spectrumBars[i], 0, 100) * 40 / 100;
      if (h) oled.drawBox(i * 16 + 2, 53 - h, 11, h);
    }
    oled.drawStr(0, 63, "HOLD E2: FX PAGE");
  } else {
    oled.setCursor(0, 8); oled.print(fxNames[fxMode]);
    oled.setCursor(105, 8); oled.print((enabledFXMask & (1U << fxMode)) ? "ON" : "OFF");
    oled.drawHLine(0, 12, 128);
    oled.setCursor(0, 26); oled.printf("STRENGTH %d/25", (int)fxMix);
    oled.drawFrame(0, 31, 128, 7);
    if (fxMix) oled.drawBox(2, 33, (int)fxMix * 124 / 25, 3);
    oled.setCursor(0, 49);
    if (fxMode == FX_SLOW) oled.printf("SPEED %d%%", 100 - (int)fxMix * 2);
    else oled.printf("CHAIN %d/3  E1 HOLD ON", activeAudioEffects(enabledFXMask));
    oled.setCursor(0, 63); oled.print((int32_t)(stackLimitUntil - millis()) > 0 ? "LIMIT: 3 AUDIO FX" : "E1 NEXT / E2 SPECTRUM");
  }
  oled.sendBuffer();
}

// ================================================================
// UI DIRTY
// ================================================================

void markAllDirty() {

  playerDirty =
    true;

  fxDirty =
    true;

  browserDirty =
    true;

  volumeDirty =
    true;
}

// ================================================================
// TFT HEADER
// ================================================================

void drawHeader() {
  tft.fillRect(0, 0, 240, 30, C_BG);
  tft.setTextSize(2);
  tft.setTextColor(C_WHITE);
  tft.setCursor(12, 8);
  tft.print("ANKSAX / AUDIO");
  tft.drawFastHLine(12, 29, 216, C_CYAN);
}

// ================================================================
// TFT CARDS
// ================================================================

void drawStaticCards() {
  tft.fillRoundRect(8, 39, 224, 112, 8, C_CARD);
  tft.drawRoundRect(8, 39, 224, 112, 8, C_BORDER);
  tft.fillRoundRect(8, 161, 224, 100, 8, C_CARD);
  tft.drawRoundRect(8, 161, 224, 100, 8, C_BORDER);
  tft.fillRoundRect(8, 271, 224, 48, 8, C_CARD);
  tft.drawRoundRect(8, 271, 224, 48, 8, C_BORDER);
}

// ================================================================
// PLAYER CARD
// ================================================================


struct CachedTrackTags { int index = -1; TrackTags tags; };
CachedTrackTags metadataCache[8];
unsigned nextMetadataSlot = 0;
const TrackTags *cachedTags(int index) {
  for (auto &slot : metadataCache) if (slot.index == index) return &slot.tags;
  return nullptr;
}
void serviceTrackMetadata() {
  if (!trackCount || mainSettingsOpen || !audioMutex) return;
  int first = constrain(selectedTrack - 1, 0, max(0, (int)trackCount - 3));
  int wanted[4] = {playingTrack >= 0 ? playingTrack : selectedTrack, first, first + 1, first + 2};
  for (int index : wanted) {
    if (index < 0 || index >= trackCount || cachedTags(index)) continue;
    if (xSemaphoreTake(audioMutex, 0) != pdTRUE) return;
    TrackTags tags;
    File file = SD.open(tracks[index].path.c_str(), FILE_READ);
    if (file) { readFLACTextTags(file, tags); file.close(); }
    xSemaphoreGive(audioMutex);
    auto &slot = metadataCache[nextMetadataSlot++ % 8]; slot.index = index; slot.tags = tags;
    if (index == (playingTrack >= 0 ? playingTrack : selectedTrack)) playerDirty = true;
    if (index >= first && index < first + 3) browserDirty = true;
    return; // One file per UI iteration; don't scan the whole SD during playback.
  }
}
String displayArtist(int index) {
  const TrackTags *tags = cachedTags(index);
  if (!tags) return "Reading artist...";
  if (tags->artist[0]) return String(tags->artist);
  if (tags->albumArtist[0]) return String(tags->albumArtist);
  return "Artist unknown";
}

String displayTrackName(int index) {
  if (index < 0 || index >= trackCount) return "NO TRACK";
  const TrackTags *tags = cachedTags(index);
  if (tags && tags->title[0]) return String(tags->title);
  String text = tracks[index].name;
  if (text.endsWith(".flac") || text.endsWith(".FLAC")) text = text.substring(0, text.length() - 5);
  return text;
}

void drawTimeline() {
  TimelineSnapshot value = snapshotTimeline();
  uint32_t elapsed = value.sourceRate ? (uint32_t)(value.frames / value.sourceRate) : 0;
  uint32_t duration = value.sourceRate ? (uint32_t)(value.total / value.sourceRate) : 0;
  char left[16], right[16];
  auto formatTime = [](char *dest, size_t capacity, uint32_t seconds) {
    if (seconds >= 3600) snprintf(dest, capacity, "%u:%02u:%02u", (unsigned)(seconds / 3600), (unsigned)((seconds / 60) % 60), (unsigned)(seconds % 60));
    else snprintf(dest, capacity, "%02u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
  };
  formatTime(left, sizeof(left), elapsed);
  if (value.total) formatTime(right, sizeof(right), duration); else strcpy(right, "--:--");
  tft.fillRect(18, 129, 204, 18, C_CARD);
  tft.setTextSize(1); tft.setTextColor(C_MUTED);
  tft.setCursor(18, 130); tft.print(left);
  tft.setCursor(222 - (int)strlen(right) * 6, 130); tft.print(right);
  tft.fillRoundRect(18, 141, 204, 5, 2, C_TRACK);
  int width = timelineWidth(value, 204);
  if (width) tft.fillRoundRect(18, 141, width, 5, 2, C_CYAN);
}

void drawPlayer() {
  tft.fillRect(12, 43, 216, 104, C_CARD);
  tft.setTextSize(1);
  tft.setTextColor(playState == PLAYING ? C_GREEN : C_YELLOW);
  tft.setCursor(18, 49);
  tft.print(playState == PLAYING ? "NOW PLAYING" : playState == PAUSED ? "PAUSED" : "READY TO PLAY");
  tft.setTextColor(C_CYAN);
  tft.setCursor(155, 49);
  tft.print(shuffleMode ? "SHUFFLE" : "IN ORDER");
  String name = displayTrackName(playingTrack >= 0 ? playingTrack : selectedTrack);
  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(18, 65);
  tft.print(name.substring(0, 17));
  tft.setCursor(18, 84);
  tft.print(name.substring(17, 34));
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.setCursor(18, 105);
  tft.print(displayArtist(playingTrack >= 0 ? playingTrack : selectedTrack).substring(0, 34));
  tft.setCursor(18, 118);
  tft.printf("%u > %u Hz /16", (unsigned)activeSampleRate, (unsigned)outputSampleRate);
}

// ================================================================
// FX CARD
// ================================================================



// ================================================================
// BROWSER CARD
// ================================================================

void drawBrowser() {
  tft.fillRect(12, 165, 216, 92, C_CARD);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.setCursor(18, 172);
  tft.printf("LIBRARY %d/%u", trackCount ? selectedTrack + 1 : 0, (unsigned)trackCount);
  if (!trackCount) {
    tft.setCursor(18, 200); tft.print("Insert SD with FLAC files"); return;
  }
  int first = constrain(selectedTrack - 1, 0, max(0, (int)trackCount - 3));
  for (int row = 0; row < 3; ++row) {
    int index = first + row;
    if (index >= trackCount) break;
    int y = 188 + row * 23;
    bool selected = index == selectedTrack;
    if (selected) tft.fillRoundRect(16, y - 2, 208, 22, 4, C_TRACK);
    tft.setTextColor(selected ? C_CYAN : C_WHITE);
    tft.setCursor(20, y + 3);
    tft.print(index == playingTrack ? "> " : "  ");
    String text = displayTrackName(index);
    if (text.length() > 30) text = text.substring(0, 28) + "..";
    tft.print(text);
    tft.setTextColor(C_MUTED); tft.setCursor(32, y + 12);
    tft.print(displayArtist(index).substring(0, 30));
  }
}

// ================================================================
// VOLUME CARD
// ================================================================

void drawVolume() {
  tft.fillRect(12, 275, 216, 40, C_CARD);
  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.setCursor(18, 279);
  tft.print("MASTER");
  tft.setTextColor(C_CYAN);
  tft.setCursor(176, 279);
  tft.printf("%d/25", currentVolume);
  tft.fillRoundRect(18, 292, 204, 6, 3, C_TRACK);
  int width = currentVolume * 204 / 25;
  if (width) tft.fillRoundRect(18, 292, width, 6, 3, C_CYAN);
  tft.setTextColor(C_MUTED);
  tft.setCursor(18, 307);
  tft.print("HOLD JOY: SETTINGS");
}

// ================================================================
// TFT RENDER
// ================================================================

void drawMainSettings() {
  static int lastSelected = -1, lastValues[4] = {-100, -100, -100, -100};
  bool full = mainSettingsLayoutDirty;
  if (full) {
    tft.setTextSize(1); tft.setTextColor(C_MUTED);
    tft.setCursor(14, 40); tft.print("DISPLAY & SOUND");
    tft.setCursor(14, 267); tft.print(DAW_SERIAL_DIAGNOSTICS ? "V6 20261010 LOG:ON" : "V6 20261010 LOG:OFF");
    tft.setCursor(14, 282); tft.print("JOY UP/DOWN: SELECT");
    tft.setCursor(14, 298); tft.print("L/R ADJUST  CLICK BACK");
  }
  const char *labels[4] = {"BRIGHTNESS", "BASS", "MID", "TREBLE"};
  for (int row = 0; row < 4; ++row) {
    int raw = row ? eqDB[row - 1] : screenBrightness;
    bool selectionChanged = selectedSetting != lastSelected && (row == selectedSetting || row == lastSelected);
    if (!full && !selectionChanged && lastValues[row] == raw) continue;
    lastValues[row] = raw;
    int y = 59 + row * 53;
    tft.fillRoundRect(10, y, 220, 45, 7, row == selectedSetting ? C_TRACK : C_CARD);
    tft.drawRoundRect(10, y, 220, 45, 7, row == selectedSetting ? C_CYAN : C_BORDER);
    tft.setTextSize(1); tft.setTextColor(row == selectedSetting ? C_CYAN : C_MUTED);
    tft.setCursor(20, y + 8); tft.print(labels[row]);
    tft.setTextSize(2); tft.setTextColor(C_WHITE); tft.setCursor(20, y + 22);
    if (!row) tft.printf("%d/25", (int)screenBrightness);
    else tft.printf("%+d dB", (int)eqDB[row - 1]);
    int value = row ? eqDB[row - 1] + 12 : screenBrightness;
    int total = row ? 24 : 25;
    tft.fillRoundRect(123, y + 25, 94, 5, 2, C_BORDER);
    int width = value * 94 / total;
    if (width) tft.fillRoundRect(123, y + 25, width, 5, 2, C_CYAN);
  }
  lastSelected = selectedSetting; mainSettingsLayoutDirty = false;
}

void renderTFT() {
  static uint32_t last = 0;
  if (millis() - last < 80) return;
  last = millis();
  if (audioMutex && xSemaphoreTake(audioMutex, 0) != pdTRUE) return;
  uint32_t displayStarted = micros();
  if (mainFrameDirty) {
    tft.fillScreen(C_BG); drawHeader();
    if (!mainSettingsOpen) {
      drawStaticCards(); playerDirty = browserDirty = volumeDirty = true;
    }
    mainFrameDirty = false;
  }
  if (mainSettingsOpen) {
    if (mainSettingsDirty) { drawMainSettings(); mainSettingsDirty = false; }
    uint32_t held = micros() - displayStarted;
    if (held > maxDisplayHoldUs) maxDisplayHoldUs = held;
    if (audioMutex) xSemaphoreGive(audioMutex);
    return;
  }
  static uint32_t lastTimeline = 0;
  if (playerDirty) { drawPlayer(); drawTimeline(); playerDirty = false; lastTimeline = millis(); }
  else if (millis() - lastTimeline >= 250) { drawTimeline(); lastTimeline = millis(); }
  if (browserDirty) { drawBrowser(); browserDirty = false; }
  if (volumeDirty) { drawVolume(); volumeDirty = false; }
  fxDirty = false; // OLED renders FX independently
  uint32_t held = micros() - displayStarted;
  if (held > maxDisplayHoldUs) maxDisplayHoldUs = held;
  if (audioMutex) xSemaphoreGive(audioMutex);
}

// ================================================================
// SETUP
// ================================================================

void loadFXSettings();

void setup() {
  loadFXSettings();
  fxParam = storedParam[fxMode]; fxMix = storedMix[fxMode];

  Serial.begin(
    115200
  );

#if defined(ARDUINO_USB_CDC_ON_BOOT) && ARDUINO_USB_CDC_ON_BOOT
  // A positive timeout avoids historical zero-timeout HWCDC bugs.
  Serial.setTxTimeoutMs(1);
#endif
  delay(300);
  DAW_LOG.printf("[BOOT] reset_reason=%d (1=power-on,4=panic,5=int-WDT,6=task-WDT,9=brownout)\n", (int)esp_reset_reason());

  AudioLogger::instance().begin(
    debugSerial,
    AudioLogger::Error
  );

  DAW_LOG.println();

  DAW_LOG.println(
    "=========================================="
  );

  DAW_LOG.println(
    "DJ AUDIO CONSOLE - AUDIOTOOLS"
  );

  DAW_LOG.println(
    "24-BIT FLAC INPUT / PCM16 DSP / REAL FFT"
  );

  DAW_LOG.println(
    "=========================================="
  );

  // --------------------------------------------------------------
  // MEMORY
  // --------------------------------------------------------------

  DAW_LOG.printf(
    "[MEM] PSRAM: %s\n",
    psramFound()
      ? "YES"
      : "NO"
  );

  if (
    psramFound()
  ) {

    DAW_LOG.printf(
      "[MEM] PSRAM size: %u bytes\n",
      ESP.getPsramSize()
    );

    DAW_LOG.printf(
      "[MEM] Free PSRAM: %u bytes\n",
      ESP.getFreePsram()
    );
  }

  setCpuFrequencyMhz(
    240
  );

  // --------------------------------------------------------------
  // ADC
  // --------------------------------------------------------------

  analogReadResolution(
    12
  );

  analogSetAttenuation(
    ADC_11db
  );

  // --------------------------------------------------------------
  // CONTROLS
  // --------------------------------------------------------------

  beginEncoders();

  initTremoloLUT();

  initFFT();

  enc1Button.begin();

  enc2Button.begin();

  joyButton.begin();

  calibrateJoystick();

  // --------------------------------------------------------------
  // OLED
  // --------------------------------------------------------------

  Wire.begin(
    OLED_SDA,
    OLED_SCL
  );

  Wire.setClock(
    400000
  );

  oled.begin();

  // --------------------------------------------------------------
  // SPI
  // --------------------------------------------------------------

  spi.begin(
    SD_SCK,
    SD_MISO,
    SD_MOSI,
    -1
  );

  pinMode(
    TFT_CS,
    OUTPUT
  );

  pinMode(
    SD_CS,
    OUTPUT
  );

  digitalWrite(
    TFT_CS,
    HIGH
  );

  digitalWrite(
    SD_CS,
    HIGH
  );

  // --------------------------------------------------------------
  // TFT
  // --------------------------------------------------------------

  backlightReady = ledcAttach(
    TFT_BL,
    5000,
    8
  );

  applyScreenBrightness();

  tft.init(
    240,
    320
  );

  tft.setRotation(
    0
  );

  tft.invertDisplay(
    false
  );

  // --------------------------------------------------------------
  // SD
  // --------------------------------------------------------------

  bool sdOK =
    SD.begin(
      SD_CS,
      spi,
      20000000
    );

  if (
    !sdOK
  ) {

    DAW_LOG.println(
      "[SD] 20 MHz failed, retrying 4 MHz"
    );

    sdOK =
      SD.begin(
        SD_CS,
        spi,
        4000000
      );

    if (
      sdOK
    ) {

      DAW_LOG.println(
        "[SD] OK @ 4 MHz"
      );

    } else {

      DAW_LOG.println(
        "[SD] FAILED"
      );
    }

  } else {

    DAW_LOG.println(
      "[SD] OK @ 20 MHz"
    );
  }

  if (
    sdOK
  ) {

    scanLibrary();

  } else {

    trackCount = 0;
  }

  // --------------------------------------------------------------
  // UI
  // --------------------------------------------------------------

  tft.fillScreen(
    C_BG
  );

  drawHeader();

  drawStaticCards();

  drawPlayer();

  drawBrowser();



  drawVolume();

  renderOLED();

  playerDirty =
    false;

  browserDirty =
    false;

  fxDirty =
    false;

  volumeDirty =
    false;

  // --------------------------------------------------------------
  // NO TRACKS
  // --------------------------------------------------------------

  if (
    trackCount == 0
  ) {

    DAW_LOG.println(
      "[WARNING] No FLAC files found. Audio engine bypassed."
    );

    playerReady =
      false;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  // --------------------------------------------------------------
  // READ FIRST FLAC FORMAT
  // --------------------------------------------------------------

  AudioInfo firstInfo;

  if (
    !readFLACStreamInfo(
      tracks[0].path,
      firstInfo
    )
  ) {

    DAW_LOG.println(
      "[AUDIO] Could not read first FLAC STREAMINFO"
    );

    DAW_LOG.println(
      "[AUDIO] Audio engine bypassed; UI remains active."
    );

    playerReady =
      false;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  DAW_LOG.printf(
    "[FLAC] FIRST FORMAT %u Hz / %u ch / %u bit\n",
    (unsigned)firstInfo.sample_rate,
    (unsigned)firstInfo.channels,
    (unsigned)firstInfo.bits_per_sample
  );

  if (
    firstInfo.channels == 0 ||
    firstInfo.channels > 2 || inspectedFLACBlockSize > 5120 ||
    firstInfo.sample_rate < 8000 || firstInfo.sample_rate > 192000
  ) {

    DAW_LOG.println(
      "[AUDIO] First FLAC must be mono/stereo."
    );

    playerReady =
      false;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  // --------------------------------------------------------------
  // FOXEN OUTPUT IS PCM16
  // --------------------------------------------------------------

  AudioInfo firstPcmInfo =
    firstInfo;

  firstPcmInfo.bits_per_sample =
    16;

  activeSampleRate =
    firstPcmInfo.sample_rate;

  DAW_LOG.printf(
    "[PCM] INITIAL OUTPUT %u Hz / %u ch / %u bit\n",
    (unsigned)firstPcmInfo.sample_rate,
    (unsigned)firstPcmInfo.channels,
    (unsigned)firstPcmInfo.bits_per_sample
  );

  // --------------------------------------------------------------
  // I2S
  // --------------------------------------------------------------

  auto cfg =
    i2s.defaultConfig(
      TX_MODE
    );

  cfg.pin_bck =
    I2S_BCLK;

  cfg.pin_ws =
    I2S_LRCK;

  cfg.pin_data =
    I2S_DOUT;

  cfg.sample_rate =
    firstPcmInfo.sample_rate;

  cfg.channels =
    firstPcmInfo.channels;

  cfg.bits_per_sample =
    16;

  cfg.buffer_size =
    1024;

  cfg.buffer_count =
    12;

  if (
    !i2s.begin(
      cfg
    )
  ) {

    DAW_LOG.println(
      "[I2S] FAILED"
    );

    playerReady =
      false;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  DAW_LOG.printf(
    "[I2S] OK @ %u Hz / %u ch / 16 bit PCM\n",
    (unsigned)firstPcmInfo.sample_rate,
    (unsigned)firstPcmInfo.channels
  );

  // --------------------------------------------------------------
  // DSP
  // --------------------------------------------------------------



  effects.setAudioInfo(
    firstPcmInfo
  );

  if (
    !effects.begin(
      cfg
    )
  ) {

    DAW_LOG.println(
      "[FX] AudioEffectStream FAILED"
    );

    playerReady =
      false;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  effects.addEffect(djFX);
  DAW_LOG.printf("[FX] PCM16 effect chain registered: %u effect(s)\n", (unsigned)effects.size());

  // --------------------------------------------------------------
  // PLAYER
  // --------------------------------------------------------------

  player.setBufferSize(2048);

  player.setAutoFade(
    false
  );

  player.setAutoNext(
    false
  );

  player.setDelayIfOutputFull(
    2
  );

  player.setVolume(1.0f);
  masterVolumePercent = currentVolume * 4;

  player.addNotifyAudioChange(
    &audioInfoWatcher
  );

  // Initializes the player chain without automatically
  // selecting an AudioSourceSD numerical index.
  decoder.set32Bit(false);
  player.begin(-1, false);
  player.setAutoNext(false);
  player.setAudioInfo(firstPcmInfo);

  if (
    !player.setPath(
      tracks[0].path.c_str()
    )
  ) {

    DAW_LOG.println(
      "[PLAYER] Initial FLAC path failed"
    );

    playerReady =
      false;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  // --------------------------------------------------------------
  // INITIAL STOPPED STATE
  // --------------------------------------------------------------

  player.stop();

  playerReady =
    true;

  playingTrack =
    -1;

  selectedTrack =
    0;

  playState =
    STOPPED;

  sharedPlayState =
    STOPPED;

  // --------------------------------------------------------------
  // AUDIO MUTEX
  // --------------------------------------------------------------

  audioMutex =
    xSemaphoreCreateMutex();

  if (
    !audioMutex
  ) {

    DAW_LOG.println(
      "[AUDIO] Mutex allocation FAILED"
    );

    playerReady =
      false;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  // --------------------------------------------------------------
  // AUDIO TASK
  // --------------------------------------------------------------

  BaseType_t taskOK =
    xTaskCreatePinnedToCore(
      audioTask,
      "audioTask",
      16384,
      nullptr,
      4,
      &audioTaskHandle,
      0
    );

  if (
    taskOK !=
    pdPASS
  ) {

    DAW_LOG.println(
      "[AUDIO] Audio task creation FAILED"
    );

    playerReady =
      false;

    vSemaphoreDelete(
      audioMutex
    );

    audioMutex =
      nullptr;

    DAW_LOG.println(
      "[SYSTEM] READY"
    );

    return;
  }

  // --------------------------------------------------------------
  // READY
  // --------------------------------------------------------------

  DAW_LOG.printf(
    "[PLAYER] DIRECT FLAC READY: %s\n",
    tracks[0].path.c_str()
  );

  DAW_LOG.println(
    "[AUDIO] Dedicated playback task running on core 0"
  );

  DAW_LOG.println(
    "[SYSTEM] READY"
  );

  DAW_LOG.println(
    "[SYSTEM] JOY PRESS = PLAY/PAUSE"
  );

  DAW_LOG.println(
    "[SYSTEM] ENC2 CLICK = SHUFFLE / SEQUENTIAL"
  );

  DAW_LOG.println(
    "[SYSTEM] JOY UP/DOWN = SELECT TRACK"
  );

  DAW_LOG.println(
    "[SYSTEM] JOY RIGHT = PLAY SELECTED"
  );

  DAW_LOG.println(
    "[SYSTEM] JOY LEFT = PREVIOUS TRACK"
  );

  DAW_LOG.println(
    "[SYSTEM] E1 CLICK NEXT FX / E1 HOLD STAGE ON-OFF / E2 HOLD SPECTRUM / JOY SETTINGS"
  );

  DAW_LOG.println(
    "[SYSTEM] ENC1 LONG = SELECTED FX ON/OFF"
  );

  DAW_LOG.println(
    "[SYSTEM] ENC2 LONG = FX / SPECTRUM"
  );
}

// ================================================================
// LOOP
// ================================================================

void loadFXSettings() {
  preferencesReady = fxPreferences.begin("daw-fx-v2", false);
  if (!preferencesReady) return;
  size_t length = fxPreferences.getBytesLength("values");
  uint8_t values[FX_COUNT * 2];
  if ((length == 16 || length == 18 || length == sizeof(values)) && fxPreferences.getBytes("values", values, length) == length) {
    int slots = length / 2;
    for (int i = 0; i < slots; ++i) {
      storedParam[i] = min(values[i], (uint8_t)25);
      storedMix[i] = min(values[i + slots], (uint8_t)25);
    }
  }
  if (fxPreferences.getBytesLength("eq") == sizeof(eqDB)) fxPreferences.getBytes("eq", eqDB, sizeof(eqDB));
  for (int i = 0; i < 3; ++i) eqDB[i] = (int8_t)constrain((int)eqDB[i], -12, 12);
  if (fxPreferences.getBytesLength("mask") == sizeof(enabledFXMask)) fxPreferences.getBytes("mask", &enabledFXMask, sizeof(enabledFXMask));
  enabledFXMask &= (1U << FX_COUNT) - 1;
  // Older/corrupt settings cannot exceed the supported stack limit.
  while (activeAudioEffects(enabledFXMask) > 3)
    for (int i = FX_COUNT - 1; i >= 0; --i) if (i != FX_SLOW && (enabledFXMask & (1U << i))) { enabledFXMask &= ~(1U << i); break; }
  uint8_t simpleVersion = 0;
  if (fxPreferences.getBytesLength("simple") == sizeof(simpleVersion)) fxPreferences.getBytes("simple", &simpleVersion, sizeof(simpleVersion));
  if (simpleVersion != 1) {
    // Old SLOW parameter 25 meant normal speed; strength 0 now means normal.
    storedMix[FX_SLOW] = 25 - storedParam[FX_SLOW];
    settingsPending = true; settingsChangedAt = millis();
  }
  memcpy(storedParam, fxCharacterPreset, sizeof(storedParam));
  if (fxPreferences.getBytesLength("bright") == sizeof(screenBrightness)) fxPreferences.getBytes("bright", &screenBrightness, sizeof(screenBrightness));
  screenBrightness = (uint8_t)constrain((int)screenBrightness, 1, 25);
  fxParam = storedParam[fxMode]; fxMix = storedMix[fxMode];
}

void saveFXSettingsWhenIdle() {
  if (!preferencesReady || !settingsPending || sharedPlayState == PLAYING ||
      advancePending || millis() - settingsChangedAt < 1500) return;
  if (audioMutex && xSemaphoreTake(audioMutex, 0) != pdTRUE) return;
  uint8_t values[FX_COUNT * 2];
  memcpy(values, storedParam, FX_COUNT);
  memcpy(values + FX_COUNT, storedMix, FX_COUNT);
  // Flash writes only while paused/stopped; avoid stalls during live playback.
  bool saved = fxPreferences.putBytes("values", values, sizeof(values)) == sizeof(values);
  if (saved) saved = fxPreferences.putBytes("eq", eqDB, sizeof(eqDB)) == sizeof(eqDB);
  if (saved) saved = fxPreferences.putBytes("mask", &enabledFXMask, sizeof(enabledFXMask)) == sizeof(enabledFXMask);
  if (saved) saved = fxPreferences.putBytes("bright", &screenBrightness, sizeof(screenBrightness)) == sizeof(screenBrightness);
  uint8_t simpleVersion = 1;
  if (saved) saved = fxPreferences.putBytes("simple", &simpleVersion, sizeof(simpleVersion)) == sizeof(simpleVersion);
  if (audioMutex) xSemaphoreGive(audioMutex);
  if (saved) settingsPending = false;
}

void servicePlaybackRate() {
  if (!rateChangePending || !playerReady || !audioMutex) return;
  if ((uint32_t)(millis() - rateChangedAt) < 250) return;
  if (xSemaphoreTake(audioMutex, 0) != pdTRUE) return;
  rateChangePending = false;
  AudioInfo sourceInfo = masterOutput.audioInfo();
  uint32_t desiredRate = playbackRateFor(sourceInfo.sample_rate);
  if (desiredRate != outputSampleRate) {
    masterOutput.setAudioInfo(sourceInfo);
    DAW_LOG.printf("[SLOW] applied rate=%u\n", (unsigned)outputSampleRate);
  }
  xSemaphoreGive(audioMutex);
  playerDirty = true;
}

void reportAudioPerformance() {
  static uint32_t last = 0;
  if (millis() - last < 10000 || !audioMutex || xSemaphoreTake(audioMutex, 0) != pdTRUE) return;
  last = millis();
  uint32_t pipeline = maxPipelineCpuUs, frames = maxPipelineFrames, copy = maxAudioCopyUs, shortWrites = shortOutputWrites;
  maxPipelineCpuUs = maxPipelineFrames = maxAudioCopyUs = shortOutputWrites = 0;
  xSemaphoreGive(audioMutex);
  DAW_LOG.printf("[PERF] pipelineCpuMaxUs=%u pipelineFrames=%u copyMaxUs=%u shortI2SWrites=%u displayHoldMaxUs=%u rate=%u stages=%d\n",
    (unsigned)pipeline, (unsigned)frames, (unsigned)copy, (unsigned)shortWrites, (unsigned)maxDisplayHoldUs,
    (unsigned)outputSampleRate, activeAudioEffects(enabledFXMask));
  maxDisplayHoldUs = 0;
}

void reportRuntimeHealth() {
  static uint32_t last = 0;
  if (millis() - last < 10000) return;
  last = millis();
  DAW_LOG.printf("[HEALTH] heap=%u min=%u internal=%u audioStackFree=%u track=%d state=%d\n",
    (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMinFreeHeap(),
    (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
    audioTaskHandle ? (unsigned)uxTaskGetStackHighWaterMark(audioTaskHandle) : 0,
    playingTrack, (int)playState);
  DAW_LOG.printf("[JOY] raw=%d,%d center=%d,%d selected=%d settings=%d\n",
    analogRead(JOY_X), analogRead(JOY_Y), joyCenterX, joyCenterY, selectedTrack, mainSettingsOpen);
}

void reportAudioFormat() {
  if (!audioFormatPending || !audioMutex) return;
  if (xSemaphoreTake(audioMutex, 0) != pdTRUE) return;
  AudioInfo decoded = player.audioInfo();
  AudioInfo output = i2s.audioInfo();
  audioFormatPending = false;
  xSemaphoreGive(audioMutex);
  playerDirty = true;
  DAW_LOG.printf("[FORMAT] decoder=%u/%u/%u I2S-config=%u/%u/%u\n",
    (unsigned)decoded.sample_rate, (unsigned)decoded.channels, (unsigned)decoded.bits_per_sample,
    (unsigned)output.sample_rate, (unsigned)output.channels, (unsigned)output.bits_per_sample);
}

void loop() {
  reportAudioFormat();
  if (advancePending) {
    advancePending = false;
    DAW_LOG.printf("[EOF] Finished track %d; advancing (shuffle=%d)\n", playingTrack, shuffleMode);
    nextTrack();
  }
  servicePlaybackRate();
  reportRuntimeHealth();
  reportAudioPerformance();
  saveFXSettingsWhenIdle();

  processVisualizerFrame();

  handleEncoders();

  handleButtons();

  handleJoystick();

  updateFader();

  serviceTrackMetadata();
  renderTFT();

  static uint32_t lastOLED =
    0;

  if (
    millis() -
    lastOLED >=
    OLED_TICK_MS
  ) {

    lastOLED =
      millis();

    decaySpectrum();

    renderOLED();
  }

  yield();
}