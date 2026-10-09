
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>
#include <map>
#include <string>
using std::min; using std::max;
template<class T> T constrain(T x,T a,T b){return std::min(std::max(x,a),b);}
constexpr float PI=3.14159265358979323846f;
using effect_t = int16_t;
using portMUX_TYPE=int;
#define portMUX_INITIALIZER_UNLOCKED 0
#define portENTER_CRITICAL(x) ((void)(x))
#define portEXIT_CRITICAL(x) ((void)(x))
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_8BIT 2
int allocations=0;
void *heap_caps_malloc(size_t n,int){++allocations;return malloc(n);}
struct AudioInfo {int sample_rate=0,channels=0,bits_per_sample=0;};
class AudioStream {
public:
 virtual ~AudioStream()=default;
 virtual void setAudioInfo(AudioInfo f){info=f;}
 virtual AudioInfo audioInfo(){return info;}
 virtual bool begin(){return true;}
 virtual void end(){}
 virtual size_t write(const uint8_t*,size_t n){return n;}
 virtual size_t write(uint8_t c){return write(&c,1);}
 virtual void writeSilence(size_t){}
 virtual int available(){return 0;}
 virtual int availableForWrite(){return 4096;}
protected: AudioInfo info;
};
class AudioEffect { public: virtual ~AudioEffect()=default; virtual effect_t process(effect_t x)=0; virtual AudioEffect *clone()=0; };
struct Sink:AudioStream {
 std::vector<int16_t> samples;
 size_t write(const uint8_t *p,size_t n) override {for(size_t i=0;i<n;i+=2){int16_t v;memcpy(&v,p+i,2);samples.push_back(v);} return n;}
} i2s;
// Host model of the typed stream's inspected API, not an ESP32 library build.
template<class T> class AudioEffectStreamT: public AudioStream {
public:
 explicit AudioEffectStreamT(AudioStream &out):out(out){}
 bool begin(AudioInfo f){info=f;return true;}
 bool begin() override{return true;}
 size_t write(const uint8_t *p,size_t n) override{return out.write(p,n);}
private: AudioStream &out;
};
struct AudioPlayer {template<class A,class B,class C> AudioPlayer(A&,B&,C&){} };
class Preferences {
public:
 std::map<std::string,std::vector<uint8_t>> data;
 bool begin(const char*,bool=false){return true;}
 size_t getBytesLength(const char *key){return data[key].size();}
 size_t getBytes(const char *key,void *p,size_t n){auto &v=data[key];size_t count=min(n,v.size());if(count)memcpy(p,v.data(),count);return count;}
 size_t putBytes(const char *key,const void *p,size_t n){auto q=(const uint8_t*)p;data[key].assign(q,q+n);return n;}
};
unsigned millis();
uint32_t micros(){static uint32_t time=0;return time+=10;}
#define TFT_BL 21
int lastDuty=-1;
struct OLED {void setContrast(uint8_t){}} oled;
struct SerialStub {template<class... T>void printf(const char*,T...){}} Serial;
#define DAW_LOG Serial
bool ledcWrite(int pin,uint32_t duty){assert(pin==TFT_BL && duty<=255);lastDuty=duty;return true;}
int source=0,decoder=0;
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



int encoderMux=0;volatile int encoder1Edges=0,encoder2Edges=0;
unsigned fakeMillis=2000;unsigned millis(){return fakeMillis;}
enum PlayState {STOPPED,PLAYING,PAUSED};PlayState sharedPlayState=STOPPED;
void *audioMutex=(void*)1;const int pdTRUE=1;
bool audioBusy=false,mutexHeld=false;
int xSemaphoreTake(void*,int){if(audioBusy)return 0;assert(!mutexHeld);mutexHeld=true;return 1;}void xSemaphoreGive(void*){assert(mutexHeld);mutexHeld=false;}
enum ButtonEvent {NO_EVENT,CLICK_EVENT,LONG_EVENT};
struct Button {ButtonEvent event=NO_EVENT;ButtonEvent update(uint32_t){auto r=event;event=NO_EVENT;return r;}} enc1Button,enc2Button,joyButton;
void fxChanged(){}
int pauseCalls=0;void togglePause(){++pauseCalls;}
bool playerDirty=false,browserDirty=false,volumeDirty=false,fxDirty=false;
const int JOY_X=1,JOY_Y=2,JOY_DEADZONE=500,JOY_TRIGGER=900;
int joyCenterX=2048,joyCenterY=2048,rawX=2048,rawY=2048;
int analogRead(int pin){return pin==JOY_X ? rawX : rawY;}
bool playerReady=true;
bool joyLatched=false;int trackCount=5,selectedTrack=0,playingTrack=-1;
int previousCalls=0,playCalls=0;void playTrack(int index){++previousCalls;selectedTrack=index;}void playSelected(){++playCalls;}
void previousTrack() {
  if (!trackCount) return;
  int current = playingTrack >= 0 ? playingTrack : selectedTrack;
  playTrack((current + trackCount - 1) % trackCount);
}
void changeFX(int delta) {
  portENTER_CRITICAL(&fxMux);
  fxMode = ((int)fxMode + delta % FX_COUNT + FX_COUNT) % FX_COUNT;
  fxParam = fxCharacterPreset[fxMode];
  fxMix = storedMix[fxMode];
  portEXIT_CRITICAL(&fxMux);
  fxChanged();
}
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
int draws=0;const int C_BG=0;struct TFT {void fillScreen(int){assert(mutexHeld);}} tft;
void drawHeader(){assert(mutexHeld);}void drawStaticCards(){assert(mutexHeld);}void drawPlayer(){assert(mutexHeld);++draws;}
void drawBrowser(){assert(mutexHeld);}void drawVolume(){assert(mutexHeld);}void drawTimeline(){assert(mutexHeld);}void drawMainSettings(){assert(mutexHeld);}
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
int main(){
 initTremoloLUT();initFFT();
 fakeMillis+=100;playerDirty=true;audioBusy=true;renderTFT();assert(draws==0 && playerDirty);
 fakeMillis+=100;audioBusy=false;renderTFT();assert(draws==1 && !mutexHeld);
 mainSettingsOpen=true;mainSettingsDirty=true;fakeMillis+=100;renderTFT();assert(!mutexHeld);mainSettingsOpen=false;
 {
  struct ShortSink:AudioStream {
   std::vector<uint8_t> bytes;int calls=0;
   size_t write(const uint8_t *p,size_t n)override{++calls;size_t written=calls==1?min(n,(size_t)64):n;bytes.insert(bytes.end(),p,p+written);return written;}
  } sink;
  MasterVolumeStream output(sink);masterVolumePercent=100;
  int16_t data[256];for(int i=0;i<256;++i)data[i]=i-128;
  assert(output.write((uint8_t*)data,sizeof(data))==sizeof(data));assert(sink.calls==2 && sink.bytes.size()==sizeof(data));assert(!memcmp(sink.bytes.data(),data,sizeof(data)));
 }
 {
  DJFXEffect effect;enabledFXMask=(1<<FX_RINGMOD);storedMix[FX_RINGMOD]=25;outputSampleRate=48000;++fxRevision;DJFXEffect::prepareAll();
  int first=effect.process(10000);assert(abs(first-10000)<50);
  for(int i=0;i<1000;++i)effect.process(10000);
  enabledFXMask=0;++fxRevision;DJFXEffect::prepareAll();
  for(int i=0;i<300;++i)effect.process(10000);
  assert(effect.process(10000)==10000);
 }
 {
  DJFXEffect effect;effect.prepare(192000,FX_CHORUS,100,100,true,12345);int before=allocations;
  for(int i=0;i<192000;++i)effect.process((int16_t)(20000*sin(2*PI*440*i/192000)));
  assert(allocations==before);
 }
 enabledFXMask=0;++fxRevision;DJFXEffect::prepareAll();masterVolumePercent=12;
 std::cout<<"PASS: TFT mutex defer/release, partial-output retry without PCM loss, five-ms stage fades, high-rate chorus stress\n";

 fxMode=FX_FILTER;fxMix=storedMix[fxMode];
 enc1Button.event=CLICK_EVENT;handleButtons();assert(fxMode==FX_ECHO && !visualizerMode);
 enc2Button.event=LONG_EVENT;handleButtons();assert(visualizerMode && oledPage==1 && fxMode==FX_ECHO);
 enc2Button.event=LONG_EVENT;handleButtons();assert(!visualizerMode && oledPage==0);
 encoder1Edges=-400;handleEncoders();assert(fxMix==25);
 encoder1Edges=400;handleEncoders();assert(fxMix==0);
 encoder2Edges=400;handleEncoders();assert(fxMix==0);
 for(int mode:{FX_PHASER,FX_FLANGER,FX_RINGMOD}){fxMode=mode;enc1Button.event=LONG_EVENT;handleButtons();}
 assert(activeAudioEffects(enabledFXMask)==3);
 fxMode=FX_VOCODER;enc1Button.event=LONG_EVENT;handleButtons();assert(!(enabledFXMask&(1<<FX_VOCODER)) && activeAudioEffects(enabledFXMask)==3);
 fxMode=FX_SLOW;enc1Button.event=LONG_EVENT;handleButtons();assert(enabledFXMask&(1<<FX_SLOW));
 storedMix[FX_SLOW]=25;assert(playbackRateFor(48000)==24000);
 enc1Button.event=LONG_EVENT;handleButtons();assert(playbackRateFor(48000)==48000);
 fxMode=FX_PHASER;enc1Button.event=LONG_EVENT;handleButtons();assert(activeAudioEffects(enabledFXMask)==2);
 joyButton.event=LONG_EVENT;handleButtons();assert(mainSettingsOpen && !pauseCalls);
 selectedSetting=0;screenBrightness=18;backlightReady=true;
 auto joy=[&](int dx,int dy){rawX=joyCenterX;rawY=joyCenterY;handleJoystick();rawX+=dx;rawY+=dy;handleJoystick();};
 joy(1600,0);assert(screenBrightness==19 && lastDuty==(int)brightnessDuty(19));
 handleJoystick();assert(screenBrightness==19); // held deflection does not repeat.
 joy(0,1600);assert(selectedSetting==1);
 joy(1600,0);assert(eqDB[0]==1 && !playCalls);
 joy(-1600,0);assert(eqDB[0]==0 && !previousCalls);
 for(int i=0;i<30;++i){joy(1600,0);}assert(eqDB[0]==12);
 for(int i=0;i<30;++i){joy(-1600,0);}assert(eqDB[0]==-12);
 int selection=selectedSetting;encoder1Edges=40;encoder2Edges=40;handleEncoders();assert(selectedSetting==selection && eqDB[0]==-12);
 enc1Button.event=CLICK_EVENT;handleButtons();assert(fxMode==FX_PHASER);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(shuffleMode && !pauseCalls);
 joyButton.event=CLICK_EVENT;handleButtons();assert(!mainSettingsOpen && !pauseCalls);
 fakeMillis+=100;handleJoystick();assert(!previousCalls); // exit whilst deflected must not skip a song.
 rawX=joyCenterX;rawY=joyCenterY;fakeMillis+=100;handleJoystick();assert(!joyLatched);
 joyButton.event=CLICK_EVENT;handleButtons();assert(pauseCalls==1);
 playingTrack=-1;
 auto move=[&](int dx,int dy){rawX=2048;rawY=2048;fakeMillis+=30;handleJoystick();rawX+=dx;rawY+=dy;fakeMillis+=30;handleJoystick();};
 selectedTrack=0;move(0,1000);assert(selectedTrack==1);move(0,-1000);assert(selectedTrack==0);move(0,-1000);assert(selectedTrack==4);
 int beforePrevious=previousCalls;move(-1000,0);assert(previousCalls==beforePrevious+1 && selectedTrack==3);
 int beforePlay=playCalls;move(1000,0);assert(playCalls==beforePlay+1);
 enabledFXMask=1<<FX_SLOW;storedMix[FX_SLOW]=25;masterOutput.setAudioInfo({48000,2,16});assert(outputSampleRate==24000);
 storedMix[FX_SLOW]=10;rateChangePending=true;rateChangedAt=fakeMillis;servicePlaybackRate();assert(rateChangePending && outputSampleRate==24000);
 fakeMillis+=200;servicePlaybackRate();assert(outputSampleRate==24000);
 storedMix[FX_SLOW]=5;rateChangedAt=fakeMillis;fakeMillis+=249;servicePlaybackRate();assert(outputSampleRate==24000);
 fakeMillis+=1;servicePlaybackRate();assert(!rateChangePending && outputSampleRate==43200);
 assert(brightnessDuty(1)==4 && brightnessDuty(25)==255);
 enabledFXMask=0;

 for(int slots:{8,9,13}) {
  fxPreferences.data.clear();std::vector<uint8_t> old(slots*2);
  for(int i=0;i<slots;++i){old[i]=(i+3)%26;old[i+slots]=(i+14)%26;}
  fxPreferences.data["values"]=old;loadFXSettings();
  for(int i=0;i<slots;++i){assert(storedParam[i]==fxCharacterPreset[i]);if(i!=FX_SLOW)assert(storedMix[i]==old[i+slots]);}
  if(slots>=9)assert(storedMix[FX_SLOW]==25-old[FX_SLOW]);
 }
 preferencesReady=true;screenBrightness=9;storedMix[FX_RINGMOD]=7;eqDB[2]=5;settingsPending=true;settingsChangedAt=0;
 sharedPlayState=PLAYING;saveFXSettingsWhenIdle();assert(settingsPending);
 sharedPlayState=PAUSED;saveFXSettingsWhenIdle();assert(!settingsPending);
 screenBrightness=25;storedMix[FX_RINGMOD]=25;eqDB[2]=0;loadFXSettings();assert(screenBrightness==9 && storedMix[FX_RINGMOD]==7 && eqDB[2]==5);
 eqDB[0]=eqDB[1]=eqDB[2]=0;enabledFXMask=0;++fxRevision;DJFXEffect::prepareAll();
 trackPCMBytes=0;trackTotalFrames=48000*10;trackSourceRate=48000;trackChannels=2;timelineCounting=true;
 int16_t timelineData[9600]={};masterOutput.write((uint8_t*)timelineData,sizeof(timelineData));
 auto pos=snapshotTimeline();assert(pos.frames==4800 && timelineWidth(pos,200)==2);
 timelineCounting=false;masterOutput.write((uint8_t*)timelineData,sizeof(timelineData));assert(snapshotTimeline().frames==4800);
 trackPCMBytes=trackTotalFrames*4+100;assert(snapshotTimeline().frames==trackTotalFrames && timelineWidth(snapshotTimeline(),200)==200);
 trackTotalFrames=0;assert(timelineWidth(snapshotTimeline(),200)==0);
 i2s.samples.clear();trackPCMBytes=0;
 std::cout<<"PASS: click/hold FX selection, single stage on/off layer, three-stage cap, joystick settings/re-arm/isolation, strength bounds, migration/persistence, PCM timeline/counting/clamp\n";

 initTremoloLUT();initFFT();
 for(int rate:{8000,24000,48000,192000}) {
  DJFXEffect voice;outputSampleRate=rate;enabledFXMask=1<<FX_VOCODER;
  for(int block=0;block<100;++block){
   storedMix[FX_VOCODER]=(uint8_t)(block%26);++fxRevision;DJFXEffect::prepareAll();
   for(int i=0;i<400;++i)voice.process(i%2 ? 32767 : -32768);
  }
  storedMix[FX_VOCODER]=25;++fxRevision;DJFXEffect::prepareAll();
  for(int i=0;i<rate;++i)voice.process(0);
  assert(abs((int)voice.process(0))<=2);
 }
 enabledFXMask=0;outputSampleRate=48000;
 std::cout<<"PASS: rapid vocoder strength changes and full-scale input at 8/24/48/192 kHz, release to silence\n";
 unsigned rev=10;
 for(int mode=0;mode<FX_COUNT;++mode){
  if(mode==FX_SLOW)continue;
  DJFXEffect effect;
  effect.prepare(48000,mode,80,100,false,++rev);
  for(int x=-32768;x<=32767;x+=257)assert(effect.process(x)==x);
  effect.prepare(48000,mode,80,0,true,++rev);
  for(int x=-32768;x<=32767;x+=257)assert(effect.process(x)==x);
 }
 for(int mode=0;mode<FX_COUNT;++mode){
  if(mode==FX_SLOW)continue;
  DJFXEffect effect;
  effect.prepare(48000,mode,80,100,true,++rev);
  int before=allocations,changed=0;
  for(int i=0;i<24000;++i){int16_t x=(int16_t)(24000*sin(2*PI*440*i/48000));if(effect.process(x)!=x)++changed;}
  assert(changed>1000);assert(allocations==before);
  for(int p=0;p<=100;p+=10){effect.prepare(192000,mode,p,100,true,++rev);effect.process(-32768);}
  assert(allocations==before);
 }
 std::cout<<"PASS: bypass and zero mix are bit-exact; all twelve DSP effects alter PCM; no allocations in processing or parameter updates\n";
 masterVolumePercent=12;
 int16_t loud[600];std::fill(std::begin(loud),std::end(loud),20000);
 assert(masterOutput.write((uint8_t*)loud,sizeof(loud))==sizeof(loud));
 assert(i2s.samples.size()==600);for(int16_t v:i2s.samples)assert(v==2400);
 std::cout<<"PASS: post-FX master gain processes chunk boundaries correctly\n";
 visualizerMode=true;
 const float tones[8]={60,110,220,440,880,1800,3000,4800};
 for(int rate:{44100,48000}){
  visualizerTap.setAudioInfo({rate,2,16});
  assert(i2s.audioInfo().sample_rate==rate);assert(i2s.audioInfo().channels==2);assert(i2s.audioInfo().bits_per_sample==16);
  for(int band=0;band<8;++band){
   std::fill(std::begin(spectrumBars),std::end(spectrumBars),0);
   std::fill(std::begin(spectrumPeaks),std::end(spectrumPeaks),0);
   for(int block=0;block<5;++block){
    int16_t data[FFT_N*VIS_DECIMATION*2];
    for(int i=0;i<FFT_N*VIS_DECIMATION;++i){int16_t v=(int16_t)(10000*sin(2*PI*tones[band]*i/rate));data[2*i]=v;data[2*i+1]=-v;}
    // Frame stays continuous across arbitrarily split stereo writes.
    for(size_t off=0;off<sizeof(data);){size_t n=min((size_t)284,sizeof(data)-off);visualizerTap.write((uint8_t*)data+off,n);off+=n;}
    processVisualizerFrame();
   }
   int highest=0;for(int b=1;b<8;++b)if(spectrumBars[b]>spectrumBars[highest])highest=b;
   if(highest!=band){std::cerr<<"Wrong band "<<band<<" -> "<<highest<<" at "<<rate<<"\n";return 1;}
   assert(spectrumBars[highest]>20 && spectrumBars[highest]<100);
  }
 }
 visualizerTap.setAudioInfo({48000,1,16});assert(i2s.audioInfo().channels==1);
 std::cout<<"PASS: eight tone bands at 44.1/48 kHz, no stereo cancellation, format propagation and mono change in host API model\n";
 return 0;
}
