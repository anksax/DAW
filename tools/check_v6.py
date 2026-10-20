from pathlib import Path
ROOT = Path(__file__).resolve().parents[1]
s=(ROOT / 'DJ_Audio_Console_V6/DJ_Audio_Console_V6.ino').read_text()
t=(ROOT / 'validation/_host_checks_base.cpp').read_text()
def extract(sig):
 start=s.index(sig)
 while True:
  pos=s.index('{',start)
  if ';' not in s[start:pos]:break
  start=s.index(sig,start+len(sig))
 end=pos+1;d=1
 while d:
  if s[end]=='{':d+=1
  if s[end]=='}':d-=1
  end+=1
 return s[start:end]
prefix=r'''
#include "../DJ_Audio_Console_V6/LibraryBrowser.h"
LibraryBrowser libraryBrowser;
const char *hostPaths[]={"/Artist/Album/00.flac","/Artist/Album/01.flac","/Artist/Album/02.flac","/Artist/Album/03.flac","/Artist/Album/04.flac"};
void resetSongs(){libraryBrowser.build(hostPaths,5);libraryBrowser.enter();libraryBrowser.enter();}
int encoderMux=0;volatile int encoder1Edges=0,encoder2Edges=0;
unsigned fakeMillis=2000;unsigned millis(){return fakeMillis;}
enum PlayState {STOPPED,PLAYING,PAUSED};PlayState sharedPlayState=STOPPED;
void *audioMutex=(void*)1;const int pdTRUE=1;
bool audioBusy=false,mutexHeld=false;
int xSemaphoreTake(void*,int){if(audioBusy)return 0;assert(!mutexHeld);mutexHeld=true;return 1;}void xSemaphoreGive(void*){assert(mutexHeld);mutexHeld=false;}
enum ButtonEvent {NO_EVENT,CLICK_EVENT,LONG_EVENT,DOUBLE_CLICK_EVENT};
struct Button {ButtonEvent event=NO_EVENT;ButtonEvent update(uint32_t){auto r=event;event=NO_EVENT;return r;}} enc1Button,enc2Button;
void fxChanged(){}
int pauseCalls=0;void togglePause(){++pauseCalls;}
bool playerDirty=false,browserDirty=false,volumeDirty=false,fxDirty=false;
bool playerReady=true;
int trackCount=5,selectedTrack=0,playingTrack=-1;
int previousCalls=0,playCalls=0;void playTrack(int index){++previousCalls;selectedTrack=index;}void playSelected(){++playCalls;}
'''
prefix+='\n'.join(extract(x) for x in ['void browseLibrary(int delta)', 'void selectLibraryItem()', 'void backLibrary()', 'void previousTrack()', 'void changeFX(int delta)', 'void adjustMainSetting(int delta)', 'void handleEncoders()', 'void handleButtons()', 'void loadFXSettings()', 'void saveFXSettingsWhenIdle()', 'void servicePlaybackRate()'])
prefix += '\nint draws=0;const int C_BG=0;\nint pdMS_TO_TICKS(int ms){return ms;}\nstruct TFT {\n static constexpr int WIDTH_PX=240,STRIPE_ROWS=8;\n uint16_t pixels[240*320]={};bool ready=true;int dirty=-1,sentCount=0;\n uint16_t *getBuffer(){return ready?pixels:nullptr;}\n void fillScreen(int){assert(!mutexHeld);dirty=0;}\n void markAll(){dirty=0;}\n int nextDirty(){return dirty;}\n void sent(int stripe){assert(!mutexHeld && stripe==dirty);dirty=-1;++sentCount;}\n} tft;\nusing TFTCanvas=TFT;\nint transfers=0;\nstruct Panel {void drawRGBBitmap(int,int,uint16_t*,int width,int rows){assert(!mutexHeld && width==240 && rows==8);++transfers;}} panel;\nvoid drawHeader(){assert(!mutexHeld);}void drawStaticCards(){assert(!mutexHeld);}void drawPlayer(){assert(!mutexHeld);++draws;}\nvoid drawBrowser(){assert(!mutexHeld);}void drawVolume(){assert(!mutexHeld);}void drawTimeline(){assert(!mutexHeld);}void drawAlbumName(){assert(!mutexHeld);}void drawMainSettings(){assert(!mutexHeld);}\n'
prefix += '\n' + extract('void renderTFT()') + '\n' + extract('void serviceTFT()')
tests=r'''
 initTremoloLUT();initFFT();resetSongs();
 fakeMillis+=100;playerDirty=true;audioBusy=true;renderTFT();assert(draws==1 && !playerDirty);
 tft.dirty=0;serviceTFT();assert(transfers==1 && tft.dirty==-1 && !mutexHeld);
 audioBusy=false;tft.dirty=1;serviceTFT();assert(transfers==2 && !mutexHeld);
 tft.ready=false;serviceTFT();assert(transfers==2);tft.ready=true;
 fakeMillis+=5000;serviceTFT();assert(transfers==3 && !mutexHeld);
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
 std::cout<<"PASS: TFT RAM redraw while audio busy, short stripe transfers independent of decoder/I2S lock, periodic repaint, partial-output retry without PCM loss, five-ms stage fades, high-rate chorus stress\n";

 fxMode=FX_FILTER;fxMix=storedMix[fxMode];
 enc1Button.event=CLICK_EVENT;handleButtons();assert(fxMode==FX_ECHO && !visualizerMode);
 enc2Button.event=LONG_EVENT;handleButtons();assert(mainSettingsOpen && !shuffleMode && !settingEditing && fxMode==FX_ECHO);
 enc2Button.event=LONG_EVENT;handleButtons();assert(!mainSettingsOpen && !shuffleMode && !visualizerMode && oledPage==0);
 encoder1Edges=-400;handleEncoders();assert(fxMix==25);
 encoder1Edges=400;handleEncoders();assert(fxMix==0);
 int beforeBrowse=selectedTrack;encoder2Edges=8;handleEncoders();assert(fxMix==0 && selectedTrack==(beforeBrowse+2)%trackCount);
 encoder2Edges=-12;handleEncoders();assert(selectedTrack==(beforeBrowse+trackCount-1)%trackCount);
 for(int mode:{FX_PHASER,FX_FLANGER,FX_RINGMOD}){fxMode=mode;enc1Button.event=LONG_EVENT;handleButtons();}
 assert(activeAudioEffects(enabledFXMask)==3);
 fxMode=FX_VOCODER;enc1Button.event=LONG_EVENT;handleButtons();assert(!(enabledFXMask&(1<<FX_VOCODER)) && activeAudioEffects(enabledFXMask)==3);
 fxMode=FX_SLOW;enc1Button.event=LONG_EVENT;handleButtons();assert(enabledFXMask&(1<<FX_SLOW));
 storedMix[FX_SLOW]=25;assert(playbackRateFor(48000)==24000);
 enc1Button.event=LONG_EVENT;handleButtons();assert(playbackRateFor(48000)==48000);
 fxMode=FX_PHASER;enc1Button.event=LONG_EVENT;handleButtons();assert(activeAudioEffects(enabledFXMask)==2);
 enc2Button.event=LONG_EVENT;handleButtons();assert(mainSettingsOpen && !pauseCalls);
 selectedSetting=0;eqDB[0]=0;
 int savedMix=fxMix,savedMode=fxMode,savedTrack=selectedTrack;
 encoder1Edges=-40;encoder2Edges=4;handleEncoders();
 assert(selectedSetting==1 && fxMix==savedMix && selectedTrack==savedTrack);
 enc1Button.event=CLICK_EVENT;handleButtons();assert(fxMode==savedMode);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(settingEditing);
 encoder2Edges=400;handleEncoders();assert(eqDB[1]==12 && selectedSetting==1);
 encoder2Edges=-400;handleEncoders();assert(eqDB[1]==-12);
 enc2Button.event=DOUBLE_CLICK_EVENT;handleButtons();assert(!settingEditing && mainSettingsOpen);
 encoder2Edges=-8;handleEncoders();assert(selectedSetting==4);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(settingEditing);
 encoder2Edges=4;handleEncoders();assert(shuffleMode && selectedSetting==4);
 encoder2Edges=-4;handleEncoders();assert(!shuffleMode);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(!settingEditing);
 encoder2Edges=-4;handleEncoders();assert(selectedSetting==3);
 enc2Button.event=CLICK_EVENT;handleButtons();
 encoder2Edges=4;handleEncoders();assert(oledPage==1 && visualizerMode);
 encoder2Edges=4;handleEncoders();assert(oledPage==2 && !visualizerMode);
 encoder2Edges=4;handleEncoders();assert(oledPage==0 && !visualizerMode);
 encoder2Edges=-4;handleEncoders();assert(oledPage==2 && !visualizerMode);
 int beforeSettingsExit=playCalls;
 enc2Button.event=LONG_EVENT;handleButtons();assert(!mainSettingsOpen && !settingEditing && playCalls==beforeSettingsExit);
 enc2Button.event=LONG_EVENT;handleButtons();assert(mainSettingsOpen && !settingEditing);
 enc2Button.event=DOUBLE_CLICK_EVENT;handleButtons();assert(!mainSettingsOpen);
 oledPage=0;visualizerMode=false;
 resetSongs();selectedTrack=0;playingTrack=-1;
 encoder2Edges=4;handleEncoders();assert(selectedTrack==1);
 int beforeBack=playCalls;
 enc2Button.event=DOUBLE_CLICK_EVENT;handleButtons();assert(libraryBrowser.view()==LibraryBrowser::ALBUMS && playCalls==beforeBack);
 enc2Button.event=DOUBLE_CLICK_EVENT;handleButtons();assert(libraryBrowser.view()==LibraryBrowser::ARTISTS && playCalls==beforeBack);
 enc2Button.event=DOUBLE_CLICK_EVENT;handleButtons();assert(libraryBrowser.view()==LibraryBrowser::ARTISTS);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(libraryBrowser.view()==LibraryBrowser::ALBUMS && playCalls==beforeBack);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(libraryBrowser.view()==LibraryBrowser::SONGS && playCalls==beforeBack);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(playCalls==beforeBack+1);
 playingTrack=selectedTrack;
 enc2Button.event=CLICK_EVENT;handleButtons();assert(pauseCalls==1 && playCalls==beforeBack+1);
 enc2Button.event=CLICK_EVENT;handleButtons();assert(pauseCalls==2);
 playingTrack=-1;
 std::cout<<"PASS: encoder-only library enter/back, current-song pause/resume, Settings row/edit isolation, EQ bounds, OLED cycling, shuffle in PLAY ORDER\n";
 enabledFXMask=1<<FX_SLOW;storedMix[FX_SLOW]=25;masterOutput.setAudioInfo({48000,2,16});assert(outputSampleRate==24000);
 storedMix[FX_SLOW]=10;rateChangePending=true;rateChangedAt=fakeMillis;servicePlaybackRate();assert(rateChangePending && outputSampleRate==24000);
 fakeMillis+=200;servicePlaybackRate();assert(outputSampleRate==24000);
 storedMix[FX_SLOW]=5;rateChangedAt=fakeMillis;fakeMillis+=249;servicePlaybackRate();assert(outputSampleRate==24000);
 fakeMillis+=1;servicePlaybackRate();assert(!rateChangePending && outputSampleRate==43200);
 enabledFXMask=0;

 for(int slots:{8,9,13,14}) {
  fxPreferences.data.clear();std::vector<uint8_t> old(slots*2);
  for(int i=0;i<slots;++i){old[i]=(i+3)%26;old[i+slots]=(i+14)%26;}
  fxPreferences.data["values"]=old;loadFXSettings();
  for(int i=0;i<slots;++i){assert(storedParam[i]==fxCharacterPreset[i]);if(i!=FX_SLOW)assert(storedMix[i]==old[i+slots]);}
  if(slots>=9)assert(storedMix[FX_SLOW]==25-old[FX_SLOW]);
 }
 preferencesReady=true;storedMix[FX_RINGMOD]=7;storedMix[FX_REVERB]=9;enabledFXMask=1<<FX_REVERB;eqDB[2]=5;settingsPending=true;settingsChangedAt=0;
 sharedPlayState=PLAYING;saveFXSettingsWhenIdle();assert(settingsPending);
 sharedPlayState=PAUSED;saveFXSettingsWhenIdle();assert(!settingsPending && !fxPreferences.data.count("bright"));
 storedMix[FX_RINGMOD]=25;eqDB[2]=0;fxPreferences.data["bright"]={1};loadFXSettings();assert(storedMix[FX_RINGMOD]==7 && storedMix[FX_REVERB]==9 && (enabledFXMask&(1<<FX_REVERB)) && eqDB[2]==5);
 eqDB[0]=eqDB[1]=eqDB[2]=0;enabledFXMask=0;++fxRevision;DJFXEffect::prepareAll();
 trackPCMBytes=0;trackTotalFrames=48000*10;trackSourceRate=48000;trackChannels=2;timelineCounting=true;
 int16_t timelineData[9600]={};masterOutput.write((uint8_t*)timelineData,sizeof(timelineData));
 auto pos=snapshotTimeline();assert(pos.frames==4800 && timelineWidth(pos,200)==2);
 timelineCounting=false;masterOutput.write((uint8_t*)timelineData,sizeof(timelineData));assert(snapshotTimeline().frames==4800);
 trackPCMBytes=trackTotalFrames*4+100;assert(snapshotTimeline().frames==trackTotalFrames && timelineWidth(snapshotTimeline(),200)==200);
 trackTotalFrames=0;assert(timelineWidth(snapshotTimeline(),200)==0);
 i2s.samples.clear();trackPCMBytes=0;
 std::cout<<"PASS: click/hold FX selection, single stage on/off layer, three-stage cap, encoder Settings isolation, strength bounds, migration/persistence, PCM timeline/counting/clamp\n";
'''
t=t.replace('int main(){',prefix+'\nint main(){'+tests)
(ROOT / 'validation/host_checks_v6.cpp').write_text(t)
