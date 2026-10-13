#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include "tjpgd.h"

static constexpr int COVER_SIZE=56;
struct CoverRange { uint32_t start=0, length=0; };
template<class Reader> bool coverBE32(Reader &f,uint32_t end,uint32_t &value) {
  if (f.position()>end || end-f.position()<4)return false;
  uint8_t b[4];if(f.read(b,4)!=4)return false;
  value=((uint32_t)b[0]<<24)|((uint32_t)b[1]<<16)|((uint32_t)b[2]<<8)|b[3];return true;
}
template<class Reader> bool findFlacJPEG(Reader &f,CoverRange &range) {
  range=CoverRange{};if(f.size()>UINT32_MAX || !f.seek(0))return false;
  uint8_t signature[4];if(f.read(signature,4)!=4 || memcmp(signature,"fLaC",4))return false;
  for(int blocks=0;blocks<128;++blocks) {
    uint32_t size=(uint32_t)f.size();if(f.position()>size || size-f.position()<4)return false;
    uint8_t header[4];if(f.read(header,4)!=4)return false;
    uint32_t length=((uint32_t)header[1]<<16)|((uint32_t)header[2]<<8)|header[3];
    if(length>size-f.position())return false;
    uint32_t end=f.position()+length;
    if((header[0]&127)==6) {
      uint32_t type,mimeLength,description,ignored,dataLength;
      if(!coverBE32(f,end,type)||!coverBE32(f,end,mimeLength)||mimeLength>end-f.position())return false;
      char mime[32]={};size_t take=mimeLength<31 ? mimeLength : 31;uint32_t next=f.position()+mimeLength;
      if(f.read((uint8_t *)mime,take)!=(int)take||!f.seek(next)||!coverBE32(f,end,description)||description>end-f.position()||!f.seek(f.position()+description))return false;
      for(int i=0;i<4;++i)if(!coverBE32(f,end,ignored))return false;
      if(!coverBE32(f,end,dataLength)||dataLength>end-f.position())return false;
      if(!strcmp(mime,"image/jpeg") && dataLength && dataLength<=524288) {
        if(type==3) {range.start=f.position();range.length=dataLength;return true;}
        if(!range.length) {range.start=f.position();range.length=dataLength;}
      }
    }
    if(!f.seek(end))return false;
    if(header[0]&128)return range.length!=0;
  }
  return false;
}

template<class Reader> class CoverJPEG {
public:
  static bool decode(Reader &file,CoverRange range,uint16_t *pixels,void *workspace,size_t workspaceBytes) {
    if(!pixels||!workspace||!range.length||range.length>524288||range.start>file.size()||range.length>file.size()-range.start||!file.seek(range.start))return false;
    Context ctx{&file,range.start+range.length,pixels,0,0,0,0};JDEC decoder={};
    if(jd_prepare(&decoder,input,workspace,workspaceBytes,&ctx)!=JDR_OK||!decoder.width||!decoder.height||decoder.width>2048||decoder.height>2048)return false;
    int scale=0;while(scale<3 && (decoder.width>>scale)>112 && (decoder.height>>scale)>112)++scale;
    ctx.width=decoder.width>>scale;ctx.height=decoder.height>>scale;
    ctx.drawWidth=ctx.width>=ctx.height ? COVER_SIZE : (COVER_SIZE*ctx.width/ctx.height);
    ctx.drawHeight=ctx.height>=ctx.width ? COVER_SIZE : (COVER_SIZE*ctx.height/ctx.width);
    if(!ctx.drawWidth)ctx.drawWidth=1;
    if(!ctx.drawHeight)ctx.drawHeight=1;
    for(int i=0;i<COVER_SIZE*COVER_SIZE;++i)pixels[i]=0;
    decoder.swap=0;return jd_decomp(&decoder,output,(uint8_t)scale)==JDR_OK;
  }
private:
  struct Context {Reader *file;uint32_t end;uint16_t *pixels;int width,height,drawWidth,drawHeight;};
  static size_t input(JDEC *decoder,uint8_t *buffer,size_t bytes) {
    Context &ctx=*(Context *)decoder->device;uint32_t pos=ctx.file->position();if(pos>ctx.end)return 0;
    if(bytes>ctx.end-pos)bytes=ctx.end-pos;
    if(buffer)return ctx.file->read(buffer,bytes);
    return ctx.file->seek(pos+(uint32_t)bytes) ? bytes : 0;
  }
  static int output(JDEC *decoder,void *bitmap,JRECT *rect) {
    Context &ctx=*(Context *)decoder->device;uint16_t *source=(uint16_t *)bitmap;
    int width=rect->right-rect->left+1,xOffset=(COVER_SIZE-ctx.drawWidth)/2,yOffset=(COVER_SIZE-ctx.drawHeight)/2;
    for(int y=0;y<ctx.drawHeight;++y) {
      int sy=y*ctx.height/ctx.drawHeight;if(sy<rect->top||sy>rect->bottom)continue;
      for(int x=0;x<ctx.drawWidth;++x) {
        int sx=x*ctx.width/ctx.drawWidth;if(sx<rect->left||sx>rect->right)continue;
        ctx.pixels[(y+yOffset)*COVER_SIZE+x+xOffset]=source[(sy-rect->top)*width+sx-rect->left];
      }
    }
    return 1;
  }
};
