from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
s = (ROOT / 'DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino').read_text()
stub = r'''
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
'''
master=s[s.index('// Master volume is applied AFTER'):s.index('// ================================================================\n// AUDIO FORMAT WATCHER')]
dsp=s[s.index('enum FXMode'):s.index('// ================================================================\n// FFT VISUALIZER')]
fft=s[s.index('static constexpr int FFT_N'):s.index('// ================================================================\n// FLAC STREAMINFO')]
tests=r'''
int main(){
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
'''
(ROOT / 'validation/_host_checks_base.cpp').write_text(stub+master+dsp+fft+tests)
print('Extracted changed DSP, master gain, PCM tap and FFT directly from firmware.')
