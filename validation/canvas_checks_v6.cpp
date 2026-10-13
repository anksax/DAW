#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <iostream>
constexpr int MALLOC_CAP_SPIRAM=1,MALLOC_CAP_8BIT=2;
bool failAllocation=false;int allocations=0;
void *heap_caps_calloc(size_t count,size_t size,int caps){assert(caps==3);++allocations;return failAllocation?nullptr:calloc(count,size);}
void heap_caps_free(void *p){free(p);}
class Adafruit_GFX {
public:
 Adafruit_GFX(int,int){}virtual ~Adafruit_GFX()=default;
 virtual void drawPixel(int16_t,int16_t,uint16_t)=0;
 virtual void fillScreen(uint16_t)=0;
 virtual void drawFastHLine(int16_t,int16_t,int16_t,uint16_t)=0;
 virtual void drawFastVLine(int16_t,int16_t,int16_t,uint16_t)=0;
};
#include "../DJ_Audio_Console_V6/TFTCanvas.h"
void drain(TFTCanvas &canvas){int count=0;while(canvas.nextDirty()>=0){canvas.sent(canvas.nextDirty());assert(++count<=40);}}
int main(){
 TFTCanvas canvas;failAllocation=true;assert(!canvas.begin());assert(canvas.getBuffer()==nullptr);canvas.fillScreen(9);canvas.drawPixel(0,0,1);assert(canvas.nextDirty()==-1);
 failAllocation=false;assert(canvas.begin());assert(canvas.getBuffer());int before=allocations;assert(canvas.begin());assert(allocations==before);drain(canvas);
 canvas.drawPixel(239,319,123);assert(canvas.getBuffer()[240*320-1]==123 && canvas.nextDirty()==39);drain(canvas);
 canvas.drawPixel(-1,0,1);canvas.drawPixel(0,320,1);assert(canvas.nextDirty()==-1);
 canvas.drawFastHLine(-10,7,20,55);assert(canvas.nextDirty()==0);for(int x=0;x<10;++x)assert(canvas.getBuffer()[7*240+x]==55);drain(canvas);
 canvas.drawFastVLine(239,315,100,77);assert(canvas.nextDirty()==39);for(int y=315;y<320;++y)assert(canvas.getBuffer()[y*240+239]==77);drain(canvas);
 canvas.fillScreen(42);for(int i=0;i<240*320;++i)assert(canvas.getBuffer()[i]==42);drain(canvas);
 // Fair stripe rotation must leave one repeatedly updated stripe unable to starve the others.
 canvas.markAll();for(int i=0;i<40;++i){canvas.drawPixel(0,0,1);int stripe=canvas.nextDirty();assert(stripe==i);canvas.sent(stripe);}assert(canvas.nextDirty()==0);drain(canvas);
 uint32_t random=123;for(int i=0;i<10000;++i){random=random*1664525U+1013904223U;int16_t x=(int16_t)(random>>16);random=random*1664525U+1013904223U;int16_t y=(int16_t)(random>>16);canvas.drawPixel(x,y,2);canvas.drawFastHLine(x,y,300,3);canvas.drawFastVLine(x,y,400,4);}assert(allocations==before);
 std::cout<<"PASS: real PSRAM canvas allocation failure/retry, clipping/dirty stripes, fair refresh, 10000 boundary cases, no drawing allocations\n";
}
