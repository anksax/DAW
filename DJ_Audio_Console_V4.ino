/*
  DJ AUDIO CONSOLE - AUDIOTOOLS EDITION
  =====================================
  ESP32-S3 N16R8
  AudioTools + libfoxenflac
  Real PCM effects through AudioEffectStream
  Real audio-reactive spectrum from decoded PCM

  Controls
  --------
  ENC1 rotate : effect parameter
  ENC1 click  : next effect
  ENC1 long   : spectrum / normal OLED + TFT visualizer
  ENC2 rotate : wet/dry
  ENC2 click  : play/pause
  ENC2 long   : FX bypass
  JOY up/down : select track
  JOY right   : play selected
  JOY left    : previous track
  FADER       : master volume

  Supported files in this build: FLAC
*/

#include <Arduino.h>
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

#include "AudioTools.h"
#include "AudioTools/Disk/AudioSourceSD.h"
#include "AudioTools/AudioCodecs/CodecFLACFoxen.h"

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
#define FADER_PIN   7

#define MAX_TRACKS 512
#define UI_TICK_MS 30
#define OLED_TICK_MS 100
#define TIMELINE_TICK_MS 250
#define FADER_TICK_MS 40
#define BUTTON_DEBOUNCE_MS 30
#define LONG_PRESS_MS 650

#define JOY_DEADZONE 500
#define JOY_TRIGGER 1400
#define FADER_MIN 80
#define FADER_MAX 4010

// ================================================================
// COLORS
// ================================================================

#define C_BG       0x0000
#define C_CARD     0x10A2
#define C_BORDER   0x4208
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
Adafruit_ST7789 tft(&spi, TFT_CS, TFT_DC, TFT_RST);
U8G2_SH1106_128X64_NONAME_F_HW_I2C oled(U8G2_R0, U8X8_PIN_NONE);

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

// AudioTools automatically propagates the decoder's real AudioInfo to the
// final output. This matters because FLAC files may be 44.1 kHz, 48 kHz,
// etc. The I2S output therefore follows the actual decoded sample rate.
AudioSourceSD source("/", ".flac", SD_CS, spi, true);
I2SStream i2s;
FLACDecoderFoxen decoder;
AudioEffectStream effects(i2s);

// Shared runtime state must be declared before the DSP objects.
bool visualizerMode = false;
volatile int spectrumBars[8] = {};
volatile int spectrumPeaks[8] = {};

volatile int fxMode = 0;
volatile int fxParam = 50;
volatile int fxMix = 80;
volatile bool fxEnabled = false;
volatile uint32_t fxRevision = 1;
volatile uint32_t activeSampleRate = 44100;

// ================================================================
// AUDIO FORMAT WATCHER
// ================================================================

class DJAudioInfoWatcher : public AudioInfoSupport {
public:
  void setAudioInfo(AudioInfo newInfo) override {
    if (!newInfo)
      return;

    info = newInfo;
    if (newInfo.sample_rate > 0)
      activeSampleRate = newInfo.sample_rate;

    fxRevision++;

    Serial.printf("[AUDIO] FORMAT %u Hz / %u ch / %u bit\n",
                  (unsigned)newInfo.sample_rate,
                  (unsigned)newInfo.channels,
                  (unsigned)newInfo.bits_per_sample);
  }

  AudioInfo audioInfo() override {
    return info;
  }

private:
  AudioInfo info;
};

DJAudioInfoWatcher audioInfoWatcher;

// ================================================================
// LIGHTWEIGHT REALTIME DJ DSP
// ================================================================
//
// The DSP is hosted by AudioTools' AudioEffectStream. We intentionally use
// lightweight integer DSP here rather than stacking several floating-point
// effects on every sample. FLAC decoding + SD I/O + TFT updates leave little
// real-time margin, and this keeps the effects audible without causing audio
// dropouts.

enum FXMode {
  FX_FILTER = 0,
  FX_ECHO,
  FX_FUZZ,
  FX_DISTORTION,
  FX_TREMOLO,
  FX_COMPRESSOR,
  FX_COUNT
};

const char *fxNames[] = {
  "FILTER",
  "ECHO",
  "FUZZ",
  "DISTORT",
  "TREMOLO",
  "COMPRESS"
};

static int16_t tremoloLUT[256];

void initTremoloLUT() {
  for (int i = 0; i < 256; ++i) {
    float a = 2.0f * PI * (float)i / 256.0f;
    tremoloLUT[i] = (int16_t)(sinf(a) * 32767.0f);
  }
}

class DJFXEffect : public AudioEffect {
public:
  DJFXEffect() {}

  DJFXEffect(const DJFXEffect &other)
    : AudioEffect(other) {
    // State is deliberately reset for every cloned stereo channel.
  }

  ~DJFXEffect() {
    if (echoBuffer) {
      free(echoBuffer);
      echoBuffer = nullptr;
    }
  }

  AudioEffect *clone() override {
    return new DJFXEffect(*this);
  }

  effect_t process(effect_t input) override {
    if (localRevision != fxRevision) {
      updateParameters();
      localRevision = fxRevision;
    }

    if (!fxEnabled)
      return input;

    int32_t in = (int32_t)input;
    int32_t wet = in;

    switch ((int)fxMode) {
      case FX_FILTER:
        wet = processFilter(in);
        break;
      case FX_ECHO:
        wet = processEcho(in);
        break;
      case FX_FUZZ:
        wet = processFuzz(in);
        break;
      case FX_DISTORTION:
        wet = processDistortion(in);
        break;
      case FX_TREMOLO:
        wet = processTremolo(in);
        break;
      case FX_COMPRESSOR:
        wet = processCompressor(in);
        break;
    }

    // Integer dry/wet mix. No floating point math in the audio callback.
    int32_t mix = constrain((int)fxMix, 0, 100);
    int32_t out = ((in * (100 - mix)) + (wet * mix)) / 100;

    if (out > 32767) out = 32767;
    if (out < -32768) out = -32768;

    return (effect_t)out;
  }

private:
  static constexpr size_t MAX_ECHO_MS = 180;

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

  void ensureEchoBuffer(uint32_t sampleRate) {
    size_t needed = (size_t)((uint64_t)sampleRate * MAX_ECHO_MS / 1000ULL);
    needed = max((size_t)256, needed);

    if (echoBuffer && echoCapacity == needed)
      return;

    if (echoBuffer) {
      free(echoBuffer);
      echoBuffer = nullptr;
      echoCapacity = 0;
    }

    size_t bytes = needed * sizeof(int16_t);
    // Echo runs once per audio sample, so keep the delay line in internal RAM
    // rather than doing random PSRAM accesses in the real-time path.
    echoBuffer = (int16_t *)heap_caps_malloc(
      bytes,
      MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT
    );
    if (!echoBuffer)
      echoBuffer = (int16_t *)malloc(bytes);

    if (echoBuffer) {
      memset(echoBuffer, 0, bytes);
      echoCapacity = needed;
      echoWrite = 0;
    }
  }

  void updateParameters() {
    uint32_t sr = activeSampleRate;
    if (sr < 8000 || sr > 192000)
      sr = 44100;

    int p = constrain((int)fxParam, 0, 100);

    // ---------------- FILTER ----------------
    highPass = (p >= 50);
    float x;
    float cutoff;

    if (!highPass) {
      // Knob 0 -> open, knob 49 -> dark low-pass.
      x = (float)p / 49.0f;
      cutoff = 18000.0f * powf(180.0f / 18000.0f, x);
    } else {
      // Knob 50 -> nearly open HPF, knob 100 -> strong HPF.
      x = (float)(p - 50) / 50.0f;
      cutoff = 180.0f * powf(12000.0f / 180.0f, x);
    }

    float alpha =
      (2.0f * PI * cutoff) /
      ((float)sr + 2.0f * PI * cutoff);

    filterAlphaQ15 = constrain((int32_t)(alpha * 32768.0f), 1, 32767);

    // ---------------- ECHO ----------------
    ensureEchoBuffer(sr);
    if (echoCapacity > 0) {
      int maxMs = (int)MAX_ECHO_MS;
      int delayMs = 60 + (p * (maxMs - 60) / 100);
      echoDelaySamples = ((uint64_t)delayMs * sr) / 1000ULL;
      echoDelaySamples = constrain(echoDelaySamples, (size_t)1, echoCapacity - 1);
      echoFeedbackQ15 = 7000 + (p * 24000 / 100); // ~0.21 -> 0.95
    }

    // ---------------- TREMOLO ----------------
    int hz = 2 + p * 11 / 100;
    tremoloInc = (uint32_t)(((uint64_t)hz * 256ULL << 16) / sr);
    if (tremoloInc == 0) tremoloInc = 1;
    tremoloDepthQ15 = 9000 + p * 21000 / 100;

    // ---------------- COMPRESSOR ----------------
    compressorThreshold = 14000 - p * 100;
    if (compressorThreshold < (int32_t)2500) compressorThreshold = (int32_t)2500;
    compressorRatio = 2 + p * 6 / 100;
  }

  int32_t processFilter(int32_t in) {
    filterState += ((in - filterState) * filterAlphaQ15) >> 15;

    if (highPass)
      return in - filterState;

    return filterState;
  }

  int32_t processEcho(int32_t in) {
    if (!echoBuffer || echoCapacity < 2)
      return in;

    size_t readPos;
    if (echoWrite >= echoDelaySamples)
      readPos = echoWrite - echoDelaySamples;
    else
      readPos = echoCapacity + echoWrite - echoDelaySamples;

    int32_t delayed = echoBuffer[readPos];

    int32_t fed = in + ((delayed * echoFeedbackQ15) >> 15);
    fed = constrain(fed, -32768, 32767);

    echoBuffer[echoWrite] = (int16_t)fed;
    echoWrite++;
    if (echoWrite >= echoCapacity)
      echoWrite = 0;

    // Wet signal is mostly the delay, with a small direct component.
    return (in / 5) + ((delayed * 4) / 5);
  }

  int32_t processFuzz(int32_t in) {
    int32_t drive = 2 + ((int)fxParam * 10 / 100);
    int32_t y = in * drive;
    y = y / 2;

    int32_t threshold = 28000 - ((int)fxParam * 240);
    if (threshold < (int32_t)4000) threshold = (int32_t)4000;

    if (y > threshold)
      y = threshold + ((y - threshold) >> 3);
    else if (y < -threshold)
      y = -threshold + ((y + threshold) >> 3);

    return constrain(y, -32768, 32767);
  }

  int32_t processDistortion(int32_t in) {
    int32_t threshold = 26000 - ((int)fxParam * 250);
    if (threshold < (int32_t)1200) threshold = (int32_t)1200;

    if (in > threshold)
      return threshold;
    if (in < -threshold)
      return -threshold;

    return in;
  }

  int32_t processTremolo(int32_t in) {
    uint8_t idx = (uint8_t)(tremoloPhase >> 16);
    int32_t s = tremoloLUT[idx];

    // Modulation runs roughly from (1-depth) to 1.0.
    int32_t depth = tremoloDepthQ15;
    int32_t gain = 32768 - depth / 2 + (((s + 32767) * depth) >> 16);
    gain = constrain(gain, 3000, 32768);

    tremoloPhase += tremoloInc;
    return (in * gain) >> 15;
  }

  int32_t processCompressor(int32_t in) {
    int32_t a = abs(in);

    // Fast attack / slower release envelope.
    if (a > compressorEnvelope)
      compressorEnvelope += (a - compressorEnvelope) >> 2;
    else
      compressorEnvelope += (a - compressorEnvelope) >> 8;

    if (compressorEnvelope <= compressorThreshold)
      return in;

    int32_t over = compressorEnvelope - compressorThreshold;
    int32_t reduced = over / compressorRatio;
    int32_t target = compressorThreshold + reduced;

    int32_t gainQ15 =
      (int32_t)((int64_t)target * 32768LL /
                ((compressorEnvelope > 0) ? compressorEnvelope : (int32_t)1));

    return (in * gainQ15) >> 15;
  }
};

DJFXEffect djFX;

// ================================================================
// REAL PCM FFT VISUALIZER TAP
// ================================================================
//
// This stream sits between the AudioPlayer decoder and AudioEffectStream.
// It forwards the PCM untouched, while periodically analyzing a small block.
// The FFT is done outside the per-sample DSP effect, so enabling the visualizer
// no longer competes with the effect path for every sample/channel clone.

static constexpr int FFT_N = 256;
static float fftCos[FFT_N / 2];
static float fftSin[FFT_N / 2];
static uint8_t fftBitReverse[FFT_N];
static int16_t visFrame[FFT_N] = {};
static volatile bool visFrameReady = false;
static portMUX_TYPE visMux = portMUX_INITIALIZER_UNLOCKED;
static float fftReal[FFT_N];
static float fftImag[FFT_N];

void initFFT() {
  for (int i = 0; i < FFT_N / 2; ++i) {
    float angle = -2.0f * PI * (float)i / (float)FFT_N;
    fftCos[i] = cosf(angle);
    fftSin[i] = sinf(angle);
  }

  int bits = 0;
  while ((1 << bits) < FFT_N)
    bits++;

  for (int i = 0; i < FFT_N; ++i) {
    int x = i;
    int r = 0;
    for (int b = 0; b < bits; ++b) {
      r = (r << 1) | (x & 1);
      x >>= 1;
    }
    fftBitReverse[i] = (uint8_t)r;
  }
}

class AudioVisualizerTap : public AudioStream {
public:
  explicit AudioVisualizerTap(AudioStream &out) : target(out) {}

  void setAudioInfo(AudioInfo newInfo) override {
    info = newInfo;
    target.setAudioInfo(newInfo);
  }

  AudioInfo audioInfo() override {
    return info;
  }

  bool begin() override {
    captureFill = 0;
    writeCounter = 0;
    return target.begin();
  }

  void end() override {
    captureFill = 0;
    target.end();
  }

  size_t write(const uint8_t *data, size_t len) override {
    if (!data || len == 0)
      return 0;

    // Forward immediately. The visualizer is a passive tap and can never
    // block the audio stream for FFT calculations.
    size_t result = target.write(data, len);

    if (!visualizerMode)
      return result;

    if (info.bits_per_sample != 16 || info.channels == 0)
      return result;

    // Capture one 256-sample window roughly every two large audio writes.
    // The FFT itself is deliberately performed by the UI core.
    writeCounter++;
    if ((writeCounter & 1U) != 0)
      return result;

    const size_t bytesPerFrame =
      (size_t)(info.bits_per_sample / 8) * info.channels;

    if (bytesPerFrame == 0)
      return result;

    size_t frames = len / bytesPerFrame;
    size_t take = min(frames, (size_t)(FFT_N - captureFill));
    const uint8_t *p = data;

    for (size_t i = 0; i < take; ++i) {
      const int16_t *samples =
        (const int16_t *)(p + i * bytesPerFrame);

      int32_t v = samples[0];
      if (info.channels >= 2)
        v = (v + samples[1]) / 2;

      capture[captureFill++] = (int16_t)v;
    }

    if (captureFill >= FFT_N) {
      portENTER_CRITICAL(&visMux);
      memcpy(visFrame, capture, sizeof(visFrame));
      visFrameReady = true;
      portEXIT_CRITICAL(&visMux);
      captureFill = 0;
    }

    return result;
  }

  size_t write(uint8_t c) override {
    return write(&c, 1);
  }

  void writeSilence(size_t len) override {
    target.writeSilence(len);
  }

  int available() override {
    return target.available();
  }

  int availableForWrite() override {
    return target.availableForWrite();
  }

private:
  AudioStream &target;
  AudioInfo info;
  int16_t capture[FFT_N] = {};
  size_t captureFill = 0;
  uint32_t writeCounter = 0;
};

AudioVisualizerTap visualizerTap(effects);
AudioPlayer player(source, visualizerTap, decoder);


void processVisualizerFrame() {
  if (!visualizerMode || !visFrameReady)
    return;

  int16_t localFrame[FFT_N];

  portENTER_CRITICAL(&visMux);
  memcpy(localFrame, visFrame, sizeof(localFrame));
  visFrameReady = false;
  portEXIT_CRITICAL(&visMux);

  for (int i = 0; i < FFT_N; ++i) {
    // Hann window. This is performed on the UI core, not the audio core.
    float w = 0.5f - 0.5f * cosf(
      2.0f * PI * (float)i / (float)(FFT_N - 1)
    );
    fftReal[i] = ((float)localFrame[i] / 32768.0f) * w;
    fftImag[i] = 0.0f;
  }

  // Bit-reversal + iterative radix-2 FFT.
  for (int i = 0; i < FFT_N; ++i) {
    int j = fftBitReverse[i];
    if (j > i) {
      float tr = fftReal[i];
      fftReal[i] = fftReal[j];
      fftReal[j] = tr;
    }
  }

  for (int len = 2; len <= FFT_N; len <<= 1) {
    int half = len >> 1;
    int step = FFT_N / len;

    for (int base = 0; base < FFT_N; base += len) {
      for (int j = 0; j < half; ++j) {
        int k = j * step;
        float wr = fftCos[k];
        float wi = fftSin[k];

        int a = base + j;
        int b = a + half;

        float br = fftReal[b];
        float bi = fftImag[b];

        float tr = wr * br - wi * bi;
        float ti = wr * bi + wi * br;

        float ar = fftReal[a];
        float ai = fftImag[a];

        fftReal[a] = ar + tr;
        fftImag[a] = ai + ti;
        fftReal[b] = ar - tr;
        fftImag[b] = ai - ti;
      }
    }
  }

  const float low[8] = {
    35, 80, 160, 320, 640, 1250, 2500, 5000
  };
  const float high[8] = {
    80, 160, 320, 640, 1250, 2500, 5000, 12000
  };

  float bandEnergy[8] = {};
  float sr = activeSampleRate > 0 ? (float)activeSampleRate : 44100.0f;

  for (int k = 1; k < FFT_N / 2; ++k) {
    float freq = (float)k * sr / (float)FFT_N;
    float power = fftReal[k] * fftReal[k] +
                  fftImag[k] * fftImag[k];

    for (int b = 0; b < 8; ++b) {
      if (freq >= low[b] && freq < high[b]) {
        bandEnergy[b] += power;
        break;
      }
    }
  }

  for (int b = 0; b < 8; ++b) {
    // sqrt is now off the audio task. Use a musical compression curve.
    float mag = sqrtf(bandEnergy[b]);
    float levelF = mag * 2.8f;
    levelF = constrain(levelF, 0.0f, 1.0f);
    int level = (int)(sqrtf(levelF) * 100.0f);

    int old = spectrumBars[b];
    int smoothed = (old * 2 + level) / 3;
    spectrumBars[b] = smoothed;

    if (smoothed > spectrumPeaks[b])
      spectrumPeaks[b] = smoothed;
  }
}

// ================================================================
// FLAC STREAMINFO / NATIVE OUTPUT FORMAT
// ================================================================
//
// Read the FLAC STREAMINFO block before playback so I2S starts at the exact
// sample rate stored in the file. This avoids pitch/speed errors from forcing
// every file to 44.1 kHz.

bool readFLACStreamInfo(const String &path, AudioInfo &outInfo) {
  File f = SD.open(path.c_str(), FILE_READ);
  if (!f)
    return false;

  uint8_t sig[4];
  if (f.read(sig, 4) != 4 ||
      sig[0] != 'f' || sig[1] != 'L' ||
      sig[2] != 'a' || sig[3] != 'C') {
    f.close();
    return false;
  }

  while (f.available()) {
    uint8_t hdr[4];
    if (f.read(hdr, 4) != 4)
      break;

    bool last = (hdr[0] & 0x80) != 0;
    uint8_t type = hdr[0] & 0x7F;
    uint32_t len =
      ((uint32_t)hdr[1] << 16) |
      ((uint32_t)hdr[2] << 8) |
      (uint32_t)hdr[3];

    if (type == 0 && len >= 34) {
      uint8_t si[34];
      if (f.read(si, 34) != 34) {
        f.close();
        return false;
      }

      uint32_t sr =
        ((uint32_t)si[10] << 12) |
        ((uint32_t)si[11] << 4) |
        ((uint32_t)si[12] >> 4);

      uint16_t channels =
        (uint16_t)(((si[12] & 0x0E) >> 1) + 1);

      uint16_t bits =
        (uint16_t)((((uint16_t)(si[12] & 0x01)) << 4) |
                   ((uint16_t)si[13] >> 4)) + 1;

      if (sr == 0 || channels == 0 || bits == 0 || bits > 24) {
        f.close();
        return false;
      }

      outInfo.sample_rate = sr;
      outInfo.channels = channels;
      outInfo.bits_per_sample = bits;
      f.close();
      return true;
    }

    if (len > 1024 * 1024) {
      f.close();
      return false;
    }

    if (!f.seek(f.position() + len)) {
      f.close();
      return false;
    }

    if (last)
      break;
  }

  f.close();
  return false;
}

bool configureNativeFLACOutput(const String &path) {
  AudioInfo info;

  if (!readFLACStreamInfo(path, info)) {
    Serial.println("[AUDIO] Could not read FLAC STREAMINFO; keeping current format");
    return false;
  }

  Serial.printf("[FLAC] %u Hz / %u ch / %u bit\n",
                (unsigned)info.sample_rate,
                (unsigned)info.channels,
                (unsigned)info.bits_per_sample);

  if (info.bits_per_sample != 16) {
    Serial.println("[FLAC] This build requires 16-bit PCM output");
    return false;
  }

  if (info.channels > 2) {
    Serial.println("[FLAC] This build supports mono/stereo only");
    return false;
  }

  activeSampleRate = info.sample_rate;

  // Push the actual file format through the whole output chain. I2SStream
  // restarts its clock when the sample rate changes.
  effects.setAudioInfo(info);
  fxRevision++;
  return true;
}

// ================================================================
// PLAYBACK
// ================================================================

enum PlayState {
  STOPPED,
  PLAYING,
  PAUSED
};

PlayState playState = STOPPED;
volatile PlayState sharedPlayState = STOPPED;
int currentVolume = 12;
SemaphoreHandle_t audioMutex = nullptr;
TaskHandle_t audioTaskHandle = nullptr;

void markAllDirty();
void playSelected();
void previousTrack();
void nextTrack();

void audioTask(void *param) {
  (void)param;

  for (;;) {
    if (playerReady && sharedPlayState == PLAYING) {
      if (audioMutex && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
        player.copy();
        xSemaphoreGive(audioMutex);
      }
    } else {
      vTaskDelay(pdMS_TO_TICKS(2));
    }

    taskYIELD();
  }
}

// ================================================================
// INPUT
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
    pinMode(pin, INPUT_PULLUP);
    stable = rawLast = digitalRead(pin);
    changedAt = millis();
  }

  ButtonEvent update(uint32_t now) {
    bool raw = digitalRead(pin);

    if (raw != rawLast) {
      rawLast = raw;
      changedAt = now;
    }

    if (now - changedAt < BUTTON_DEBOUNCE_MS)
      return NO_EVENT;

    if (raw != stable) {
      stable = raw;

      if (stable == LOW) {
        pressedAt = now;
        longSent = false;
      } else {
        if (!longSent)
          return CLICK_EVENT;
      }
    }

    if (stable == LOW &&
        !longSent &&
        now - pressedAt >= LONG_PRESS_MS) {
      longSent = true;
      return LONG_EVENT;
    }

    return NO_EVENT;
  }
};

Button enc1Button{ENC1_SW};
Button enc2Button{ENC2_SW};

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

uint8_t encoderState(uint8_t clk, uint8_t dt) {
  return ((digitalRead(clk) ? 1 : 0) << 1) |
         (digitalRead(dt) ? 1 : 0);
}

void beginEncoders() {
  pinMode(ENC1_CLK, INPUT_PULLUP);
  pinMode(ENC1_DT, INPUT_PULLUP);
  pinMode(ENC2_CLK, INPUT_PULLUP);
  pinMode(ENC2_DT, INPUT_PULLUP);

  enc1State = encoderState(ENC1_CLK, ENC1_DT);
  enc2State = encoderState(ENC2_CLK, ENC2_DT);
}

int readEncoder(uint8_t clk, uint8_t dt,
                uint8_t &state, int &acc) {
  uint8_t next = encoderState(clk, dt);

  if (next == state)
    return 0;

  int step = QUAD[(state << 2) | next];
  state = next;

  if (step == 0) {
    acc = 0;
    return 0;
  }

  acc += step;

  if (acc >= 2) {
    acc = 0;
    return 1;
  }

  if (acc <= -2) {
    acc = 0;
    return -1;  }

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

  for (int i = 0; i < 32; i++) {
    sx += analogRead(JOY_X);
    sy += analogRead(JOY_Y);
    delay(3);
  }

  joyCenterX = sx / 32;
  joyCenterY = sy / 32;
}

void handleJoystick() {
  static uint32_t last = 0;

  if (millis() - last < 25)
    return;

  last = millis();

  if (trackCount == 0)
    return;

  int x = analogRead(JOY_X);
  int y = analogRead(JOY_Y);

  int dx = x - joyCenterX;
  int dy = y - joyCenterY;

  if (abs(dx) < JOY_DEADZONE &&
      abs(dy) < JOY_DEADZONE) {
    joyLatched = false;
    return;
  }

  if (joyLatched)
    return;

  if (max(abs(dx), abs(dy)) < JOY_TRIGGER)
    return;

  joyLatched = true;

  if (abs(dy) > abs(dx)) {
    if (dy > 0)
      selectedTrack++;
    else
      selectedTrack--;

    if (selectedTrack >= trackCount)
      selectedTrack = 0;

    if (selectedTrack < 0)
      selectedTrack = trackCount - 1;

    markAllDirty();
    return;
  }

  if (dx < 0)
    previousTrack();
  else
    playSelected();
}

// ================================================================
// SD LIBRARY
// ================================================================

void scanDirectory(File dir) {
  while (trackCount < MAX_TRACKS) {
    File entry = dir.openNextFile();
    if (!entry) break;

    if (entry.isDirectory()) {
      scanDirectory(entry);
      entry.close();
      continue;
    }

    String name = entry.name();
    String lower = name;
    lower.toLowerCase();

    if (lower.endsWith(".flac")) {
      tracks[trackCount].path = entry.path();
      tracks[trackCount].name = name;

      Serial.printf("[SD] %u: %s\n",
                    trackCount + 1,
                    tracks[trackCount].path.c_str());

      trackCount++;
    }

    entry.close();
  }
}

void scanLibrary() {
  trackCount = 0;

  File root = SD.open("/");

  if (!root || !root.isDirectory()) {
    Serial.println("[SD] Cannot open root");
    return;
  }

  // Diagnostic listing: proves the card is readable even when no FLACs
  // match the scanner.
  File probe = root.openNextFile();
  uint16_t visible = 0;
  while (probe && visible < 20) {
    Serial.printf("[SD] %s%s\n",
                  probe.isDirectory() ? "DIR  " : "FILE ",
                  probe.path() ? probe.path() : "(null)");
    probe.close();
    probe = root.openNextFile();
    visible++;
  }
  if (probe) probe.close();
  root.close();

  // Re-open because the diagnostic listing consumed the directory iterator.
  root = SD.open("/");
  if (!root || !root.isDirectory()) {
    Serial.println("[SD] Cannot re-open root");
    return;
  }

  scanDirectory(root);
  root.close();

  Serial.printf("[SD] FLAC tracks: %u\n", trackCount);

  if (trackCount > 0) {
    Serial.println("[SD] Recursive scanner found playable FLAC paths:");
    for (uint16_t i = 0; i < min((uint16_t)8, trackCount); i++) {
      Serial.printf("[SD]   #%u %s\n", i + 1, tracks[i].path.c_str());
    }
  }
}

// ================================================================
// PLAYBACK
// ================================================================

void markAllDirty();

void playTrack(int index) {
  if (trackCount == 0 || !playerReady)
    return;

  index = constrain(index, 0, (int)trackCount - 1);
  selectedTrack = index;

  Serial.printf("[PLAY] %s\n", tracks[index].path.c_str());

  if (audioMutex &&
      xSemaphoreTake(audioMutex, pdMS_TO_TICKS(1000)) != pdTRUE) {
    Serial.println("[PLAY] Audio mutex timeout");
    return;
  }

  sharedPlayState = STOPPED;
  player.setActive(false);

  if (!configureNativeFLACOutput(tracks[index].path)) {
    playingTrack = -1;
    playState = STOPPED;
    if (audioMutex) xSemaphoreGive(audioMutex);
    markAllDirty();
    return;
  }

  bool ok = player.setPath(tracks[index].path.c_str());

  if (ok) {
    playingTrack = index;
    player.setActive(true);
    playState = PLAYING;
    sharedPlayState = PLAYING;
  } else {
    Serial.println("[PLAY] Failed to open FLAC");
    playingTrack = -1;
    playState = STOPPED;
    sharedPlayState = STOPPED;
  }

  if (audioMutex)
    xSemaphoreGive(audioMutex);

  markAllDirty();
}

void playSelected() {
  playTrack(selectedTrack);
}

void togglePause() {
  if (playingTrack < 0 || !playerReady)
    return;

  if (audioMutex && xSemaphoreTake(audioMutex, pdMS_TO_TICKS(200)) != pdTRUE)
    return;

  if (playState == PLAYING) {
    player.setActive(false);
    playState = PAUSED;
    sharedPlayState = PAUSED;
  } else if (playState == PAUSED) {
    player.setActive(true);
    playState = PLAYING;
    sharedPlayState = PLAYING;
  }

  if (audioMutex)
    xSemaphoreGive(audioMutex);

  markAllDirty();
}

void nextTrack() {
  if (trackCount == 0)
    return;

  int next = selectedTrack + 1;
  if (next >= trackCount)
    next = 0;

  playTrack(next);
}

void previousTrack() {
  if (trackCount == 0)
    return;

  int prev = selectedTrack - 1;
  if (prev < 0)
    prev = trackCount - 1;

  playTrack(prev);
}

// ================================================================
// EFFECT CONTROLS
// ================================================================

void fxChanged() {
  fxRevision++;
  markAllDirty();
}

void changeFX(int delta) {
  int m = fxMode + delta;

  if (m < 0)
    m = FX_COUNT - 1;

  if (m >= FX_COUNT)
    m = 0;

  fxMode = m;
  fxParam = 50;
  fxMix = 70;

  fxChanged();

  Serial.printf("[FX] %s\n", fxNames[fxMode]);
}

void handleEncoders() {
  int e1 = readEncoder(
    ENC1_CLK, ENC1_DT,
    enc1State, enc1Acc
  );

  if (e1) {
    fxParam += e1 * 2;
    fxParam = constrain(fxParam, 0, 100);
    fxChanged();
  }

  int e2 = readEncoder(
    ENC2_CLK, ENC2_DT,
    enc2State, enc2Acc
  );

  if (e2) {
    fxMix += e2 * 2;
    fxMix = constrain(fxMix, 0, 100);
    fxChanged();
  }
}

void handleButtons() {
  uint32_t now = millis();

  ButtonEvent b1 = enc1Button.update(now);
  ButtonEvent b2 = enc2Button.update(now);

  if (b1 == CLICK_EVENT)
    changeFX(1);

  if (b1 == LONG_EVENT) {
    visualizerMode = !visualizerMode;
    markAllDirty();
  }

  if (b2 == CLICK_EVENT)
    togglePause();

  if (b2 == LONG_EVENT) {
    fxEnabled = !fxEnabled;
    fxChanged();
  }
}

// ================================================================
// VOLUME FADER
// ================================================================

float faderFiltered = -1;

void updateFader() {
  static uint32_t last = 0;

  if (millis() - last < FADER_TICK_MS)
    return;

  last = millis();

  int raw = 0;

  for (int i = 0; i < 4; i++)
    raw += analogRead(FADER_PIN);

  raw /= 4;

  if (faderFiltered < 0)
    faderFiltered = raw;

  faderFiltered =
    faderFiltered * 0.82f +
    raw * 0.18f;

  // Reverse the physical fader direction.
  // LOW ADC value = 100% volume, HIGH ADC value = 0% volume.
  int volume = map(
    (int)faderFiltered,
    FADER_MAX,
    FADER_MIN,
    0,
    100
  );

  volume = constrain(volume, 0, 100);

  if (volume != currentVolume) {
    currentVolume = volume;

    if (playerReady) {
      player.setVolume(
        (float)currentVolume / 100.0f
      );
    }

    volumeDirty = true;
  }
}

// ================================================================
// VISUALIZER
// ================================================================

void decaySpectrum() {
  for (int i = 0; i < 8; i++) {
    if (spectrumBars[i] > 0)
      spectrumBars[i] = max(0, spectrumBars[i] - 1);

    if (spectrumPeaks[i] > spectrumBars[i]) {
      int peakTarget = spectrumPeaks[i] - 2;
      int barNow = spectrumBars[i];
      spectrumPeaks[i] = (peakTarget > barNow) ? peakTarget : barNow;
    }

    if (playState != PLAYING) {
      spectrumBars[i] =
        (spectrumBars[i] * 7) / 8;

      spectrumPeaks[i] =
        (spectrumPeaks[i] * 7) / 8;
    }
  }
}

void drawSpectrumTFT() {
  tft.fillRect(12, 35, 216, 83, C_CARD);

  tft.setTextSize(1);
  tft.setTextColor(C_CYAN);
  tft.setCursor(17, 43);
  tft.print("REAL PCM SPECTRUM");

  int baseY = 112;
  int barW = 22;

  for (int i = 0; i < 8; i++) {
    int h = map(
      constrain(spectrumBars[i], 0, 100),
      0, 100,
      0, 62
    );

    int peak = map(
      constrain(spectrumPeaks[i], 0, 100),
      0, 100,
      0, 62
    );

    int x = 17 + i * 26;

    if (h > 0)
      tft.fillRect(
        x,
        baseY - h,
        barW - 3,
        h,
        C_CYAN
      );

    if (peak > 0)
      tft.drawFastHLine(
        x,
        baseY - peak,
        barW - 3,
        C_WHITE
      );
  }
}

void renderOLED() {
  oled.clearBuffer();
  oled.setFont(u8g2_font_5x7_tf);

  if (visualizerMode) {
    oled.drawStr(0, 7, "REAL PCM SPECTRUM");

    for (int i = 0; i < 8; i++) {
      int h = map(
        constrain(spectrumBars[i], 0, 100),
        0, 100,
        0, 44
      );

      int x = i * 16;

      if (h > 0)
        oled.drawBox(
          x + 2,
          55 - h,
          11,
          h
        );

      int p = map(
        constrain(spectrumPeaks[i], 0, 100),
        0, 100,
        0, 44
      );

      if (p > 0)
        oled.drawHLine(
          x + 2,
          55 - p,
          11
        );
    }

    oled.setCursor(0, 63);
    oled.print("ACTUAL DECODED PCM");
  } else {
    oled.drawStr(0, 7, "DJ FX");

    oled.setCursor(92, 7);
    oled.print(fxEnabled ? "ON" : "OFF");

    oled.drawHLine(0, 10, 128);

    oled.setCursor(0, 20);
    oled.print(fxNames[fxMode]);

    oled.setCursor(0, 31);
    oled.printf("P:%3d", fxParam);

    oled.setCursor(70, 31);
    oled.printf("M:%3d", fxMix);

    oled.drawFrame(0, 38, 128, 7);

    int xbar = map(fxParam, 0, 100, 0, 124);

    if (xbar > 0)
      oled.drawBox(2, 40, xbar, 3);

    oled.setCursor(0, 54);
    oled.print("E1 PARAM   E2 MIX");

    oled.setCursor(0, 63);
    oled.print("E1 CLICK NEXT FX");
  }

  oled.sendBuffer();
}

// ================================================================
// TFT
// ================================================================

void markAllDirty() {
  playerDirty = true;
  fxDirty = true;
  browserDirty = true;
  volumeDirty = true;
}

void drawHeader() {
  tft.fillRect(0, 0, 240, 24, C_CARD);
  tft.drawFastHLine(0, 24, 240, C_BORDER);

  tft.setTextColor(C_WHITE);
  tft.setTextSize(1);
  tft.setCursor(10, 8);
  tft.print("DJ AUDIO CONSOLE");

  tft.setTextColor(C_MUTED);
  tft.setCursor(181, 8);
  tft.print("S3");
}

void drawStaticCards() {
  tft.fillRoundRect(8, 31, 224, 91, 5, C_CARD);
  tft.drawRoundRect(8, 31, 224, 91, 5, C_BORDER);

  tft.fillRoundRect(8, 128, 224, 39, 5, C_CARD);
  tft.drawRoundRect(8, 128, 224, 39, 5, C_BORDER);

  tft.fillRoundRect(8, 173, 224, 53, 5, C_CARD);
  tft.drawRoundRect(8, 173, 224, 53, 5, C_BORDER);

  tft.fillRoundRect(8, 232, 224, 67, 5, C_CARD);
  tft.drawRoundRect(8, 232, 224, 67, 5, C_BORDER);
}

void drawPlayer() {
  tft.fillRect(12, 35, 216, 83, C_CARD);

  tft.setTextSize(1);
  tft.setTextColor(
    playState == PLAYING ? C_GREEN :
    playState == PAUSED ? C_YELLOW :
    C_MUTED
  );

  tft.setCursor(17, 43);

  if (playState == PLAYING)
    tft.print("PLAYING");
  else if (playState == PAUSED)
    tft.print("PAUSED");
  else
    tft.print("STOPPED");

  tft.setTextColor(C_WHITE);
  tft.setTextSize(2);
  tft.setCursor(17, 61);

  String name = "NO TRACK";

  if (playingTrack >= 0 &&
      playingTrack < trackCount)
    name = tracks[playingTrack].name;
  else if (trackCount > 0)
    name = tracks[selectedTrack].name;

  if (name.length() > 18)
    name = name.substring(0, 16) + "..";

  tft.print(name);

  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);
  tft.setCursor(17, 88);
  tft.print("FLAC / AudioTools + Foxen");

  tft.setCursor(17, 109);
  tft.print("JOY SELECT  RIGHT PLAY");
}

void drawFX() {
  tft.fillRect(12, 177, 216, 45, C_CARD);

  tft.setTextSize(1);

  tft.setTextColor(
    fxEnabled ? C_GREEN : C_MUTED
  );

  tft.setCursor(17, 185);
  tft.print(fxEnabled ? "FX ON" : "FX OFF");

  tft.setTextColor(C_WHITE);
  tft.setCursor(70, 185);
  tft.print(fxNames[fxMode]);

  tft.setTextColor(C_CYAN);
  tft.setCursor(17, 201);
  tft.printf("PARAM:%03d  MIX:%03d",
             fxParam, fxMix);

  tft.setTextColor(C_MUTED);
  tft.setCursor(17, 216);

  switch (fxMode) {
    case FX_FILTER:
      tft.print(fxParam < 50 ? "LOW PASS" : "HIGH PASS");
      break;
    case FX_ECHO:
      tft.print("REAL DELAY / FEEDBACK");
      break;
    case FX_FUZZ:
      tft.print("REAL FUZZ");
      break;
    case FX_DISTORTION:
      tft.print("REAL CLIP DISTORTION");
      break;
    case FX_TREMOLO:
      tft.print("REAL AMPLITUDE MODULATION");
      break;
    case FX_COMPRESSOR:
      tft.print("REAL DYNAMIC COMPRESSOR");
      break;
  }
}

void drawBrowser() {
  tft.fillRect(12, 131, 216, 33, C_CARD);

  tft.setTextSize(1);
  tft.setTextColor(C_MUTED);

  tft.setCursor(16, 141);
  tft.printf("TRACK %d / %d",
             trackCount ? selectedTrack + 1 : 0,
             trackCount);

  tft.setCursor(16, 155);

  if (trackCount)
    tft.print(tracks[selectedTrack].name);
  else
    tft.print("NO FLAC FILES");
}

void drawVolume() {
  tft.fillRect(12, 235, 216, 61, C_CARD);

  tft.setTextColor(C_MUTED);
  tft.setTextSize(1);
  tft.setCursor(17, 244);
  tft.print("MASTER VOLUME");

  tft.setTextColor(C_GREEN);
  tft.setCursor(190, 244);
  tft.printf("%03d", currentVolume);

  tft.fillRoundRect(
    17, 257, 202, 9, 3, C_TRACK
  );

  int width = map(
    currentVolume, 0, 100,
    0, 202
  );

  if (width > 0)
    tft.fillRoundRect(
      17, 257, width, 9, 3, C_GREEN
    );

  tft.setTextColor(C_MUTED);
  tft.setCursor(17, 279);
  tft.print("FADER = MASTER");
}

void renderTFT() {
  static uint32_t lastVisualizerDraw = 0;

  if (visualizerMode) {
    // TFT updates are comparatively slow. Limit the visualizer to ~20 FPS
    // instead of redrawing it on every loop iteration while audio is playing.
    if (millis() - lastVisualizerDraw >= 50) {
      lastVisualizerDraw = millis();
      drawSpectrumTFT();
    }
    playerDirty = false;
  } else if (playerDirty) {
    drawPlayer();
    playerDirty = false;
  }

  if (fxDirty) {
    drawFX();
    fxDirty = false;
  }

  if (browserDirty) {
    drawBrowser();
    browserDirty = false;
  }

  if (volumeDirty) {
    drawVolume();
    volumeDirty = false;
  }
}

// ================================================================
// SETUP
// ================================================================

void setup() {
  Serial.begin(115200);
  delay(300);

  AudioLogger::instance().begin(Serial, AudioLogger::Error);

  Serial.println();
  Serial.println("==========================================");
  Serial.println("DJ AUDIO CONSOLE - AUDIOTOOLS");
  Serial.println("NATIVE FLAC / REAL DSP / REAL FFT");
  Serial.println("==========================================");

  Serial.printf("[MEM] PSRAM: %s\n", psramFound() ? "YES" : "NO");
  if (psramFound()) {
    Serial.printf("[MEM] PSRAM size: %u bytes\n", ESP.getPsramSize());
    Serial.printf("[MEM] Free PSRAM: %u bytes\n", ESP.getFreePsram());
  }

  setCpuFrequencyMhz(240);

  analogReadResolution(12);
  analogSetAttenuation(ADC_11db);

  beginEncoders();
  initTremoloLUT();
  initFFT();
  enc1Button.begin();
  enc2Button.begin();
  calibrateJoystick();

  Wire.begin(OLED_SDA, OLED_SCL);
  Wire.setClock(400000);
  oled.begin();

  spi.begin(SD_SCK, SD_MISO, SD_MOSI, -1);

  pinMode(TFT_CS, OUTPUT);
  pinMode(SD_CS, OUTPUT);
  digitalWrite(TFT_CS, HIGH);
  digitalWrite(SD_CS, HIGH);

  ledcAttach(TFT_BL, 5000, 8);
  ledcWrite(TFT_BL, 180);

  tft.init(240, 320);
  tft.setRotation(0);
  tft.invertDisplay(false);

  if (!SD.begin(SD_CS, spi, 20000000)) {
    Serial.println("[SD] 20 MHz failed, retrying 4 MHz");
    if (!SD.begin(SD_CS, spi, 4000000))
      Serial.println("[SD] FAILED");
    else
      Serial.println("[SD] OK @ 4 MHz");
  } else {
    Serial.println("[SD] OK @ 20 MHz");
  }

  scanLibrary();

  // UI is brought up before audio so the console always remains usable.
  tft.fillScreen(C_BG);
  drawHeader();
  drawStaticCards();
  drawPlayer();
  drawBrowser();
  drawFX();
  drawVolume();
  renderOLED();

  playerDirty = false;
  browserDirty = false;
  fxDirty = false;
  volumeDirty = false;

  if (trackCount == 0) {
    Serial.println("[WARNING] No FLAC files found. Audio engine bypassed.");
    playerReady = false;
    Serial.println("[SYSTEM] READY");
    return;
  }

  // Read the first FLAC header before initializing I2S. This prevents the
  // common 48 kHz -> 44.1 kHz speed/pitch mismatch.
  AudioInfo firstInfo;
  if (!readFLACStreamInfo(tracks[0].path, firstInfo)) {
    Serial.println("[AUDIO] Could not read first FLAC STREAMINFO");
    Serial.println("[AUDIO] Audio engine bypassed; UI remains active.");
    playerReady = false;
    Serial.println("[SYSTEM] READY");
    return;
  }

  Serial.printf("[FLAC] FIRST FORMAT %u Hz / %u ch / %u bit\n",
                (unsigned)firstInfo.sample_rate,
                (unsigned)firstInfo.channels,
                (unsigned)firstInfo.bits_per_sample);

  if (firstInfo.bits_per_sample != 16 || firstInfo.channels > 2) {
    Serial.println("[AUDIO] First FLAC must be 16-bit mono/stereo in this build.");
    Serial.println("[SYSTEM] READY");
    return;
  }

  auto cfg = i2s.defaultConfig(TX_MODE);
  cfg.pin_bck = I2S_BCLK;
  cfg.pin_ws = I2S_LRCK;
  cfg.pin_data = I2S_DOUT;
  cfg.sample_rate = firstInfo.sample_rate;
  cfg.channels = firstInfo.channels;
  cfg.bits_per_sample = firstInfo.bits_per_sample;
  cfg.buffer_size = 1024;
  cfg.buffer_count = 12;

  if (!i2s.begin(cfg)) {
    Serial.println("[I2S] FAILED");
    Serial.println("[SYSTEM] READY");
    return;
  }

  Serial.printf("[I2S] OK @ %u Hz\n", (unsigned)firstInfo.sample_rate);

  effects.addEffect(djFX);
  effects.setAudioInfo(firstInfo);

  if (!effects.begin(cfg)) {
    Serial.println("[FX] AudioEffectStream FAILED");
    Serial.println("[SYSTEM] READY");
    return;
  }
  Serial.println("[FX] AudioEffectStream OK");

  player.setBufferSize(8192);
  player.setAutoFade(false);
  player.setDelayIfOutputFull(2);
  player.setVolume((float)currentVolume / 100.0f);
  player.addNotifyAudioChange(&audioInfoWatcher);

  // Do not select AudioSourceSD's numerical index. Use our recursive paths.
  player.begin(-1, false);

  if (!player.setPath(tracks[0].path.c_str())) {
    Serial.println("[PLAYER] Initial FLAC path failed");
    Serial.println("[SYSTEM] READY");
    return;
  }

  playerReady = true;
  player.setActive(false);
  playingTrack = -1;
  selectedTrack = 0;
  playState = STOPPED;
  sharedPlayState = STOPPED;

  audioMutex = xSemaphoreCreateMutex();
  if (!audioMutex) {
    Serial.println("[AUDIO] Mutex allocation FAILED");
    playerReady = false;
    Serial.println("[SYSTEM] READY");
    return;
  }

  BaseType_t taskOK = xTaskCreatePinnedToCore(
    audioTask,
    "audioTask",
    16384,
    nullptr,
    4,
    &audioTaskHandle,
    0
  );

  if (taskOK != pdPASS) {
    Serial.println("[AUDIO] Audio task creation FAILED");
    playerReady = false;
    vSemaphoreDelete(audioMutex);
    audioMutex = nullptr;
    Serial.println("[SYSTEM] READY");
    return;
  }

  Serial.printf("[PLAYER] DIRECT FLAC READY: %s\n",
                tracks[0].path.c_str());
  Serial.println("[AUDIO] Dedicated playback task running on core 0");
  Serial.println("[SYSTEM] READY");
  Serial.println("[SYSTEM] ENC2 CLICK = PLAY/PAUSE");
  Serial.println("[SYSTEM] ENC1 CLICK = NEXT FX");
  Serial.println("[SYSTEM] ENC1 LONG = REAL VISUALIZER");
  Serial.println("[SYSTEM] ENC2 LONG = FX BYPASS");
}

// ================================================================
// LOOP
// ================================================================

void loop() {
  processVisualizerFrame();
  handleEncoders();
  handleButtons();
  handleJoystick();
  updateFader();

  renderTFT();

  static uint32_t lastOLED = 0;

  if (millis() - lastOLED >= OLED_TICK_MS) {
    lastOLED = millis();

    decaySpectrum();
    renderOLED();
  }

  yield();
}