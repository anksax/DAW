#include "../DJ_Audio_Console_V6/FlacTags.h"
#include <algorithm>
#include <cassert>
#include <iostream>
#include <vector>
#include <string>
struct Reader {
 std::vector<uint8_t> data;uint32_t pos=0;size_t reads=0;
 uint32_t position(){return pos;}size_t size(){return data.size();}
 bool seek(uint32_t p){if(p>data.size())return false;pos=p;return true;}
 int read(uint8_t *to,size_t n){n=std::min(n,data.size()-pos);if(n)memcpy(to,data.data()+pos,n);pos+=(uint32_t)n;reads+=n;return (int)n;}
};
void u32(std::vector<uint8_t> &b,uint32_t v){for(int i=0;i<4;++i)b.push_back((v>>(i*8))&255);}
void block(std::vector<uint8_t> &b,int type,bool last,const std::vector<uint8_t> &value){auto n=value.size();b.push_back(type|(last?128:0));b.push_back(n>>16);b.push_back(n>>8);b.push_back(n);b.insert(b.end(),value.begin(),value.end());}
Reader make(const std::vector<std::string> &tags,bool picture=false){
 Reader f;f.data={'f','L','a','C'};block(f.data,0,false,std::vector<uint8_t>(34));
 if(picture)block(f.data,6,false,std::vector<uint8_t>(100000,42));
 std::vector<uint8_t> comment;u32(comment,3);comment.insert(comment.end(),{'a','b','c'});u32(comment,tags.size());
 for(auto &t:tags){u32(comment,t.size());comment.insert(comment.end(),t.begin(),t.end());}
 block(f.data,4,true,comment);return f;
}
int main(){
 TrackTags tags;
 auto f=make({"TITLE=Example","artist=One","ARTIST=Two","ALBUMARTIST=Various Artists"},true);
 assert(readFLACTextTags(f,tags));assert(!strcmp(tags.title,"Example")&&!strcmp(tags.artist,"One / Two")&&!strcmp(tags.albumArtist,"Various Artists"));assert(f.reads<1000);
 f=make({"ALBUM ARTIST=Fallback"});assert(readFLACTextTags(f,tags)&&tags.artist[0]==0&&!strcmp(tags.albumArtist,"Fallback"));
 f=make({"TITLE="+std::string(94,'x')+"\xc3\xa9"});assert(readFLACTextTags(f,tags)&&strlen(tags.title)==94);
 f=make({"TITLE=Before"});f.data.pop_back();assert(!readFLACTextTags(f,tags)&&!tags.title[0]);
 f=make({"TITLE=Good"});f.data[42+4]=0xff;f.data[42+5]=0xff;f.data[42+6]=0xff;f.data[42+7]=0xff;assert(!readFLACTextTags(f,tags));
 f=make({});assert(readFLACTextTags(f,tags)&&!tags.artist[0]);
 f=make(std::vector<std::string>(513,"ARTIST=x"));assert(!readFLACTextTags(f,tags));
 uint32_t random=1;
 for(int test=0;test<5000;++test){
  f=make({"TITLE=Candidate","ARTIST=Artist"},test%2);
  for(int i=0;i<8;++i){random=random*1664525U+1013904223U;size_t index=random%f.data.size();random=random*1664525U+1013904223U;f.data[index]=random>>24;}
  if(test%3==0)f.data.resize(f.data.size()/2);
  readFLACTextTags(f,tags);
 }
 std::cout<<"PASS: title/artist/album-artist tags, repeated artists, case-insensitive keys, picture skip, UTF-8 truncation, malformed bounds, 5000 fuzz cases\n";
}
