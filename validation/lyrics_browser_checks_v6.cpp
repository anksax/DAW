#include "../DJ_Audio_Console_V6/DisplayText.h"
#include "../DJ_Audio_Console_V6/LrcLyrics.h"
#include "../DJ_Audio_Console_V6/LibraryBrowser.h"
#include <cassert>
#include <string>
#include <iostream>
int main(){
  char display[128];tftReadableText(u8"Beyoncé – Jóga…",display,sizeof(display));assert(!strcmp(display,"Beyonce - Joga..."));
  tftReadableText(u8"Café / 中文",display,sizeof(display));assert(!strcmp(display,"Cafe / ??"));
  tftReadableText("bad\xe2\x82",display,sizeof(display));assert(!strcmp(display,"bad?"));
  tftReadableText(u8"Ｃ\u2009Ｏ\u2009Ｎ T R O L",display,sizeof(display));assert(!strcmp(display,"C O N T R O L"));
  char tiny[2];tftReadableText(u8"Été",tiny,sizeof(tiny));assert(!strcmp(tiny,"E"));

 LrcEntry entries[256];LrcDocument doc;doc.setStorage(entries,256);
 std::string text="\xef\xbb\xbf[ti:Test]\r\n[00:20.000][00:01.50]Repeated\r\n[00:05]Second\n[00:10.1]\n[offset:+200]\n";
 assert(doc.parse(text.data(),text.size())&&doc.size()==4);
 assert(doc.at(0)->timeMs==1700&&doc.at(1)->timeMs==5200&&doc.at(2)->timeMs==10300&&doc.at(3)->timeMs==20200);
 assert(doc.lineAt(1699)==-1&&doc.lineAt(1700)==0&&doc.lineAt(10300)==2&&doc.at(2)->text[0]==0);
 assert(doc.lineAt(9000000000ULL)==3);assert(!doc.at(-1)&&!doc.at(4));
 text="[offset:-2000]\n[00:01]Early\n[00:01]Translation";assert(doc.parse(text.data(),text.size())&&doc.at(0)->timeMs==0&&doc.lineAt(0)==1);
 text="[00:01]<00:01.0>Hello <00:01.5>world";assert(doc.parse(text.data(),text.size())&&!strcmp(doc.at(0)->text,"Hello world"));
 for(auto bad:{"[12:99]Bad","[12345:00]Bad","[00:01.1234]Bad","[ti:Title]\nNo timed lyrics"})assert(!doc.parse(bad,strlen(bad)));
 text="[00:01]"+std::string(94,'x')+"\xc3\xa9";assert(doc.parse(text.data(),text.size())&&strlen(doc.at(0)->text)==94);
 char a[22],b[22];lrcVisibleRows("One two three four five six seven eight nine ten eleven twelve",0,a,b);assert(strlen(a)<=21&&strlen(b)<=21&&a[0]);
 char c[22],d[22];lrcVisibleRows("One two three four five six seven eight nine ten eleven twelve",3000,c,d);assert(strcmp(a,c));
 text.clear();for(int i=0;i<300;++i)text+="[00:01]Line\n";assert(doc.parse(text.data(),text.size())&&doc.size()==256&&doc.wasTruncated());
 uint32_t random=123;for(int i=0;i<2000;++i){std::string mutation(1000,' ');for(char &ch:mutation){random=random*1664525U+1013904223U;ch=(char)(random>>24);}doc.parse(mutation.data(),mutation.size());assert(doc.size()<=256);}
 const char *paths[]={"/Artist A/Album One/01.flac","/Artist A/Album One/02.flac","/Artist A/Album Two/01.flac","/Artist B/Album One/01.flac"};
 LibraryBrowser browser;browser.build(paths,4);assert(browser.view()==LibraryBrowser::ARTISTS&&browser.visibleCount()==2);
 browser.move(-1);assert(browser.selectedTrack()==3);browser.move(1);assert(browser.selectedTrack()==0);
 assert(browser.enter()==-1&&browser.view()==LibraryBrowser::ALBUMS&&browser.visibleCount()==2);
 browser.move(1);assert(browser.selectedTrack()==2);assert(browser.enter()==-1&&browser.visibleCount()==1);assert(browser.enter()==2);
 assert(browser.back()&&browser.view()==LibraryBrowser::ALBUMS&&browser.selectedRow()==1);assert(browser.back()&&browser.selectedRow()==0);assert(!browser.back());
 browser.enter();browser.move(-1);assert(browser.selectedTrack()==2);browser.move(1);browser.enter();assert(browser.visibleCount()==2);browser.move(1);assert(browser.enter()==1);
 browser.build(nullptr,0);browser.move(100);assert(browser.visibleCount()==0&&browser.selectedTrack()==-1&&browser.enter()==-1);
 assert(!LibraryBrowser::sameArtist(paths[0],paths[3]));assert(LibraryBrowser::sameArtist(paths[0],paths[2]));
 std::cout<<"PASS: LRC timestamps/offsets/repeats/blank cues/paging/bounds/2000 mutations; artist-album-song navigation/wrap/back/empty library\n";
}
