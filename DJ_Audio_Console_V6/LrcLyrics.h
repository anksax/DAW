#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct LrcEntry { uint32_t timeMs; char text[96]; };

inline void lrcTrimUTF8(char *text) {
  size_t n = strlen(text);
  while (n && (text[n-1] == ' ' || text[n-1] == '\r')) text[--n] = 0;
  if (!n) return;
  size_t start = n-1;
  while (start && (((uint8_t)text[start] & 0xc0) == 0x80)) --start;
  uint8_t lead = (uint8_t)text[start];
  size_t need = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
  if (n-start < need) text[start] = 0;
}

inline bool lrcTime(const char *start, const char *end, uint32_t &ms) {
  uint32_t minutes = 0, seconds = 0, fraction = 0; int digits = 0;
  const char *p = start;
  while (p < end && *p >= '0' && *p <= '9') {
    if (++digits > 4) return false;
    minutes = minutes*10 + (uint32_t)(*p++-'0');
  }
  if (!digits || p == end || *p++ != ':') return false;
  digits = 0;
  while (p < end && *p >= '0' && *p <= '9') {
    if (++digits > 2) return false;
    seconds = seconds*10 + (uint32_t)(*p++-'0');
  }
  if (!digits || seconds > 59) return false;
  if (p < end && *p == '.') {
    ++p; digits = 0;
    while (p < end && *p >= '0' && *p <= '9') {
      if (++digits > 3) return false;
      fraction = fraction*10 + (uint32_t)(*p++-'0');
    }
    if (!digits) return false;
    while (digits++ < 3) fraction *= 10;
  }
  if (p != end) return false;
  ms = (minutes*60+seconds)*1000+fraction;
  return true;
}

// Storage is supplied by the caller, allocated once in PSRAM on the board.
class LrcDocument {
public:
  void setStorage(LrcEntry *entries, size_t capacity) { data=entries; limit=capacity; clear(); }
  void clear() { count=0; truncated=false; offsetMs=0; }
  size_t size() const { return count; }
  bool wasTruncated() const { return truncated; }
  const LrcEntry *at(int index) const { return index>=0 && (size_t)index<count ? data+index : nullptr; }
  int lineAt(uint64_t ms) const {
    size_t lo=0, hi=count;
    while (lo<hi) { size_t mid=lo+(hi-lo)/2; if (data[mid].timeMs<=ms) lo=mid+1; else hi=mid; }
    return lo ? (int)lo-1 : -1;
  }
  bool parse(const char *text, size_t length) {
    clear(); if (!text || !data || !limit) return false;
    const char *p=text, *end=text+length;
    if (length>=3 && (uint8_t)p[0]==0xef && (uint8_t)p[1]==0xbb && (uint8_t)p[2]==0xbf) p+=3;
    while (p<end) {
      const char *line=p; while (p<end && *p!='\n') ++p;
      parseLine(line,p); if (p<end) ++p;
    }
    for (size_t i=0;i<count;++i) {
      int64_t shifted=(int64_t)data[i].timeMs+offsetMs;
      data[i].timeMs=(uint32_t)(shifted<0 ? 0 : shifted);
    }
    // Stable, bounded insertion sort avoids a hidden allocator on track change.
    for (size_t i=1;i<count;++i) {
      LrcEntry value=data[i]; size_t j=i;
      while (j && data[j-1].timeMs>value.timeMs) { data[j]=data[j-1]; --j; }
      data[j]=value;
    }
    return count!=0;
  }
private:
  LrcEntry *data=nullptr; size_t limit=0,count=0; bool truncated=false; int32_t offsetMs=0;
  void parseLine(const char *p, const char *end) {
    while (p<end && (*p==' ' || *p=='\t')) ++p;
    uint32_t stamps[16]; size_t times=0;
    while (p<end && *p=='[') {
      const char *close=p+1; while (close<end && *close!=']') ++close;
      if (close==end) return;
      uint32_t ms;
      if (lrcTime(p+1,close,ms)) {
        if (times<16) stamps[times++]=ms; else truncated=true;
      } else if (close-p>=9 && !memcmp(p,"[offset:",8)) {
        const char *q=p+8; int sign=1;
        if (q<close && (*q=='-' || *q=='+')) { if (*q=='-') sign=-1; ++q; }
        int32_t value=0; bool valid=q<close;
        while (q<close) { if (*q<'0' || *q>'9' || value>60000) { valid=false; break; } value=value*10+(*q++-'0'); }
        if (valid && value<=600000) offsetMs=sign*value;
      }
      p=close+1;
    }
    if (!times) return;
    while (p<end && (*p==' ' || *p=='\t')) ++p;
    char body[96]; size_t used=0;
    while (p<end && used<95) {
      // Strip enhanced word timestamps; line timing remains authoritative.
      if (*p=='<') {
        const char *close=p+1; while (close<end && *close!='>' && close-p<20) ++close;
        uint32_t ignored;
        if (close<end && *close=='>' && lrcTime(p+1,close,ignored)) { p=close+1; continue; }
      }
      body[used++]=(uint8_t)*p<32 ? ' ' : *p; ++p;
    }
    body[used]=0; lrcTrimUTF8(body);
    for (size_t i=0;i<times;++i) {
      if (count==limit) { truncated=true; break; }
      data[count].timeMs=stamps[i]; memcpy(data[count].text,body,strlen(body)+1); ++count;
    }
  }
};

// Two readable 21-byte rows, paged by source time (so pauses freeze paging).
inline void lrcVisibleRows(const char *text, uint64_t elapsedMs, char (&first)[22], char (&second)[22]) {
  char rows[8][22]={}; size_t count=0;
  const char *p=text ? text : "";
  while (*p && count<8) {
    while (*p==' ') ++p;
    if (!*p) break;
    size_t n=strlen(p); if (n>21) n=21;
    if (p[n] && n==21) { size_t split=n; while (split && p[split]!=' ') --split; if (split) n=split; }
    memcpy(rows[count],p,n); rows[count][n]=0; lrcTrimUTF8(rows[count]);
    size_t used=strlen(rows[count]); if (!used) { ++p; continue; }
    p+=used; ++count;
  }
  size_t pages=(count+1)/2, page=pages ? (size_t)((elapsedMs/3000)%pages) : 0;
  memcpy(first,rows[page*2],22);
  if (page*2+1<count) memcpy(second,rows[page*2+1],22); else second[0]=0;
}
