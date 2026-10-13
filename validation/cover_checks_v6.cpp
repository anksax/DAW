#include "../DJ_Audio_Console_V6/AlbumCover.h"
#include <cassert>
#include "fixtures/cover_fixtures.h"
#include <iterator>
#include <vector>
#include <algorithm>
#include <iostream>
struct Reader {
 std::vector<uint8_t> data;uint32_t pos=0;
 size_t size(){return data.size();}uint32_t position(){return pos;}
 bool seek(uint32_t p){if(p>data.size())return false;pos=p;return true;}
 int read(uint8_t *out,size_t n){n=std::min(n,data.size()-pos);if(n)memcpy(out,data.data()+pos,n);pos+=(uint32_t)n;return (int)n;}
};
Reader image(const char *name){return strstr(name,"red") ? Reader{std::vector<uint8_t>(coverRedJPEG,coverRedJPEG+sizeof(coverRedJPEG)),0} : Reader{std::vector<uint8_t>(coverWideJPEG,coverWideJPEG+sizeof(coverWideJPEG)),0};}
void be(std::vector<uint8_t>&b,uint32_t n){for(int shift:{24,16,8,0})b.push_back((n>>shift)&255);}
int main(){
 alignas(16) uint32_t work[1024];uint16_t pixels[56*56];
 Reader red=image("fixtures/cover_red.jpg");assert(red.size());CoverRange range{0,(uint32_t)red.size()};
 assert(CoverJPEG<Reader>::decode(red,range,pixels,work,sizeof(work)));assert((pixels[28*56+28]&0xf800)>0xa000);
 Reader wide=image("fixtures/cover_wide.jpg");range={0,(uint32_t)wide.size()};assert(CoverJPEG<Reader>::decode(wide,range,pixels,work,sizeof(work)));assert(pixels[0]==0&&pixels[28*56+28]!=0);
 std::vector<uint8_t> picture;be(picture,3);be(picture,10);const char *mime="image/jpeg";picture.insert(picture.end(),mime,mime+10);be(picture,0);for(uint32_t n:{16U,16U,24U,0U})be(picture,n);be(picture,red.size());picture.insert(picture.end(),red.data.begin(),red.data.end());
 Reader flac;flac.data={'f','L','a','C',0x86,(uint8_t)(picture.size()>>16),(uint8_t)(picture.size()>>8),(uint8_t)picture.size()};flac.data.insert(flac.data.end(),picture.begin(),picture.end());
 assert(findFlacJPEG(flac,range)&&range.length==red.size());assert(CoverJPEG<Reader>::decode(flac,range,pixels,work,sizeof(work)));
 for(size_t length=0;length<flac.data.size();length+=7){Reader cut=flac;cut.data.resize(length);CoverRange r;assert(!findFlacJPEG(cut,r));}
 red.data.resize(20);range={0,20};assert(!CoverJPEG<Reader>::decode(red,range,pixels,work,sizeof(work)));range={1,524289};assert(!CoverJPEG<Reader>::decode(red,range,pixels,work,sizeof(work)));
 for(int i=0;i<100;++i){Reader fuzz=image("red");size_t at=(size_t)(i*13)%fuzz.data.size();fuzz.data[at]^=(uint8_t)(i+1);CoverRange r{0,(uint32_t)fuzz.size()};CoverJPEG<Reader>::decode(fuzz,r,pixels,work,sizeof(work));}
 std::cout<<"PASS: real JPEG decoder thumbnails/aspect fit, embedded FLAC JPEG/range bounds, truncated-image rejection\n";
}
