#pragma once
#include <stdint.h>
#include <stddef.h>
// The TFT's classic font is byte-oriented. Convert complete UTF-8 codepoints
// before clipping/scrolling; never modify tags or filesystem paths.
inline size_t tftReadableText(const char *input,char *output,size_t capacity) {
  if(!capacity)return 0;
  size_t used=0;const unsigned char *p=(const unsigned char *)input;
  while(*p && used+1<capacity) {
    uint32_t cp=*p++;bool valid=true;
    if(cp>=128) {
      int extra=cp>=0xc2 && cp<=0xdf ? 1 : cp>=0xe0 && cp<=0xef ? 2 : cp>=0xf0 && cp<=0xf4 ? 3 : -1;
      if(extra<0)valid=false;
      else {
        uint32_t minimum=extra==1 ? 0x80 : extra==2 ? 0x800 : 0x10000;
        cp &= extra==1 ? 31 : extra==2 ? 15 : 7;
        for(int i=0;i<extra;++i) { if((*p&0xc0)!=0x80) {valid=false;break;} cp=(cp<<6)|(*p++&63); }
        if(cp<minimum||cp>0x10ffff||(cp>=0xd800&&cp<=0xdfff))valid=false;
      }
    }
    const char *replacement="?";char ascii[2]={(char)cp,0};
    if(!valid)replacement="?";
    else if(cp>=32&&cp<=126)replacement=ascii;
    else if(cp<32||cp==127)replacement=" ";
    else if(cp>=0x300&&cp<=0x36f)replacement="";
    else if(cp==0xa0||(cp>=0x2000&&cp<=0x200a)||cp==0x202f||cp==0x205f||cp==0x3000)replacement=" ";
    else if(cp>=0xff01&&cp<=0xff5e) {ascii[0]=(char)(cp-0xfee0);replacement=ascii;}
    else if(cp==0xfeff||cp==0x200b)replacement="";
    else if(cp>=0xc0&&cp<=0xc5)replacement="A";
    else if(cp==0xc6)replacement="AE";
    else if(cp==0xc7)replacement="C";
    else if(cp>=0xc8&&cp<=0xcb)replacement="E";
    else if(cp>=0xcc&&cp<=0xcf)replacement="I";
    else if(cp==0xd0)replacement="D";
    else if(cp==0xd1)replacement="N";
    else if((cp>=0xd2&&cp<=0xd6)||cp==0xd8)replacement="O";
    else if(cp>=0xd9&&cp<=0xdc)replacement="U";
    else if(cp==0xdd)replacement="Y";
    else if(cp==0xde)replacement="TH";
    else if(cp==0xdf)replacement="ss";
    else if(cp>=0xe0&&cp<=0xe5)replacement="a";
    else if(cp==0xe6)replacement="ae";
    else if(cp==0xe7)replacement="c";
    else if(cp>=0xe8&&cp<=0xeb)replacement="e";
    else if(cp>=0xec&&cp<=0xef)replacement="i";
    else if(cp==0xf0)replacement="d";
    else if(cp==0xf1)replacement="n";
    else if((cp>=0xf2&&cp<=0xf6)||cp==0xf8)replacement="o";
    else if(cp>=0xf9&&cp<=0xfc)replacement="u";
    else if(cp==0xfd||cp==0xff)replacement="y";
    else if(cp==0xfe)replacement="th";
    else if(cp==0x152)replacement="OE";
    else if(cp==0x153)replacement="oe";
    else if(cp>=0x2010&&cp<=0x2015)replacement="-";
    else if(cp==0x2018||cp==0x2019)replacement="'";
    else if(cp==0x201c||cp==0x201d)replacement="\"";
    else if(cp==0x2026)replacement="...";
    for(const char *r=replacement;*r&&used+1<capacity;++r)output[used++]=*r;
  }
  output[used]=0;return used;
}
