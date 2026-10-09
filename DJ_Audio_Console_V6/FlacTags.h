#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

struct TrackTags {
  char title[96] = {};
  char artist[96] = {};
  char albumArtist[96] = {};
};

// Reader implements File's read(uint8_t*,size_t), position(), size(), seek().
// No comment-sized allocation. Skip pictures/padding with bounded seeks.
template<class Reader> bool tagU32(Reader &f, uint32_t end, uint32_t &v) {
  if (f.position() > end || end - f.position() < 4) return false;
  uint8_t b[4]; if (f.read(b, 4) != 4) return false;
  v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
  return true;
}

inline void tagTrim(char *text) {
  size_t n = strlen(text);
  while (n && text[n - 1] == ' ') text[--n] = 0;
  // Remove an incomplete UTF-8 codepoint at a truncated buffer boundary.
  if (n) {
    size_t start = n - 1;
    while (start && (((uint8_t)text[start] & 0xc0) == 0x80)) --start;
    uint8_t lead = (uint8_t)text[start];
    size_t expected = lead >= 0xf0 ? 4 : lead >= 0xe0 ? 3 : lead >= 0xc0 ? 2 : 1;
    if (n - start < expected) text[start] = 0;
  }
}

template<class Reader> bool readTagComments(Reader &f, uint32_t end, TrackTags &tags) {
  uint32_t vendor, count;
  if (!tagU32(f, end, vendor) || vendor > end - f.position() || !f.seek(f.position() + vendor) ||
      !tagU32(f, end, count) || count > 512) return false;
  for (uint32_t i = 0; i < count; ++i) {
    uint32_t length;
    if (!tagU32(f, end, length) || length > end - f.position()) return false;
    uint32_t next = f.position() + length;
    uint8_t prefix[192]; size_t take = length < sizeof(prefix) - 1 ? length : sizeof(prefix) - 1;
    if (f.read(prefix, take) != (int)take) return false;
    prefix[take] = 0;
    if (!f.seek(next)) return false;
    size_t equals = 0;
    while (equals < take && prefix[equals] != '=') ++equals;
    if (!equals || equals >= take || equals > 32) continue;
    char key[33];
    for (size_t k = 0; k < equals; ++k) key[k] = prefix[k] >= 'a' && prefix[k] <= 'z' ? prefix[k] - 'a' + 'A' : prefix[k];
    key[equals] = 0;
    char *destination = !strcmp(key, "TITLE") ? tags.title : !strcmp(key, "ARTIST") ? tags.artist :
      (!strcmp(key, "ALBUMARTIST") || !strcmp(key, "ALBUM ARTIST")) ? tags.albumArtist : nullptr;
    if (!destination) continue;
    size_t offset = strlen(destination);
    if (offset && !strcmp(key, "ARTIST") && offset + 3 < 96) {
      memcpy(destination + offset, " / ", 3); offset += 3;
    } else if (offset) continue;
    for (size_t k = equals + 1; k < take && offset < 95; ++k) {
      destination[offset++] = prefix[k] < 32 ? ' ' : (char)prefix[k];
    }
    destination[offset] = 0; tagTrim(destination);
  }
  return true;
}

template<class Reader> bool readFLACTextTags(Reader &f, TrackTags &out) {
  out = TrackTags{};
  if (f.size() > UINT32_MAX || !f.seek(0)) return false;
  uint8_t sig[4];
  if (f.read(sig, 4) != 4 || memcmp(sig, "fLaC", 4)) return false;
  TrackTags parsed;
  for (int blocks = 0; blocks < 128; ++blocks) {
    uint32_t size = (uint32_t)f.size();
    if (f.position() > size || size - f.position() < 4) return false;
    uint8_t h[4]; if (f.read(h, 4) != 4) return false;
    uint32_t length = ((uint32_t)h[1] << 16) | ((uint32_t)h[2] << 8) | h[3];
    if (length > size - f.position()) return false;
    uint32_t end = f.position() + length;
    if ((h[0] & 127) == 4 && !readTagComments(f, end, parsed)) return false;
    if (!f.seek(end)) return false;
    if (h[0] & 128) { out = parsed; return true; }
  }
  return false;
}
