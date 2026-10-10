#include <cassert>
#include <cstddef>
#include <cstdint>
#include <iostream>
class Print {
public:
 virtual ~Print()=default;
 virtual size_t write(uint8_t)=0;
 virtual size_t write(const uint8_t*,size_t)=0;
};
class Stream:public Print {
public:
 virtual int available()=0;
 virtual int read()=0;
 virtual int peek()=0;
 virtual int availableForWrite(){return 0;}
 virtual void flush()=0;
};
#include "../DJ_Audio_Console_V6/SerialDiagnostics.h"
struct Sink:Stream {
 int room=64,writes=0,flushes=0;bool shortWrite=false;
 size_t write(uint8_t)override{++writes;return 1;}
 size_t write(const uint8_t*,size_t n)override{++writes;return shortWrite ? 0 : n;}
 int available()override{return 0;}int read()override{return -1;}int peek()override{return -1;}
 int availableForWrite()override{return room;}void flush()override{++flushes;}
};
bool connected=false;bool ready(){return connected;}
int main(){
 Sink sink;BestEffortSerialLog log(sink,ready);uint8_t data[65]={};
 assert(log.write(data,4)==4 && sink.writes==0);
 connected=true;sink.room=0;assert(log.write(data,4)==4 && sink.writes==0);
 sink.room=64;assert(log.write(data,65)==65 && sink.writes==0);
 assert(log.write(data,4)==4);
 assert(sink.writes==(DAW_SERIAL_DIAGNOSTICS ? 1 : 0));
 int before=sink.writes;sink.shortWrite=true;assert(log.write(data,4)==4);
 assert(sink.writes==before+(DAW_SERIAL_DIAGNOSTICS ? 1 : 0));
 log.flush();assert(sink.flushes==0);assert(log.available()==0 && log.read()==-1 && log.peek()==-1);
 std::cout<<"PASS: diagnostics="<<DAW_SERIAL_DIAGNOSTICS<<" disconnected/full/oversized drops, no retry or flush\n";
}
