#pragma once
#include <stdint.h>
#include <stddef.h>
#include <string.h>

// Tracks are sorted by canonical path; path strings must outlive this model.
class LibraryBrowser {
public:
  enum Level { ARTISTS, ALBUMS, SONGS };
  static constexpr int MAX_ITEMS=512;
  void build(const char *const *paths, int tracks) {
    path=paths; count=tracks<0 ? 0 : tracks>MAX_ITEMS ? MAX_ITEMS : tracks;
    artists=albums=0; level=ARTISTS; artist=album=song=0;
    for (int i=0;i<count;++i) {
      if (!i || !sameArtist(path[i-1],path[i])) artistStart[artists++]=(uint16_t)i;
      if (!i || !sameAlbum(path[i-1],path[i])) albumStart[albums++]=(uint16_t)i;
    }
  }
  Level view() const { return level; }
  int visibleCount() const {
    if (!count) return 0;
    if (level==ARTISTS) return artists;
    if (level==ALBUMS) return albumLimit()-firstAlbum();
    return endAlbum()-albumStart[album];
  }
  int selectedRow() const { return level==ARTISTS ? artist : level==ALBUMS ? album-firstAlbum() : song; }
  int rowTrack(int row) const {
    if (row<0 || row>=visibleCount()) return -1;
    return level==ARTISTS ? artistStart[row] : level==ALBUMS ? albumStart[firstAlbum()+row] : albumStart[album]+row;
  }
  int selectedTrack() const { return rowTrack(selectedRow()); }
  void move(int delta) {
    int n=visibleCount();if (!n) return;
    int next=(selectedRow()+delta%n+n)%n;
    if (level==ARTISTS) artist=next;
    else if (level==ALBUMS) album=firstAlbum()+next;
    else song=next;
  }
  // Enter folder; at the song level return the track to play.
  int enter() {
    if (!count) return -1;
    if (level==ARTISTS) { album=firstAlbum();level=ALBUMS;return -1; }
    if (level==ALBUMS) { song=0;level=SONGS;return -1; }
    return selectedTrack();
  }
  bool back() {
    if (level==ARTISTS) return false;
    level=level==SONGS ? ALBUMS : ARTISTS;return true;
  }
  static size_t artistLength(const char *p) {
    if (!p) return 0;
    const char *last=strrchr(p,'/');if (!last) return 0;
    while (last>p && last[-1]!='/') --last;
    return last>p ? (size_t)(last-p-1) : 0;
  }
  static bool sameArtist(const char *a,const char *b) {
    if (!a || !b) return false;
    size_t an=artistLength(a),bn=artistLength(b);return an==bn && !memcmp(a,b,an);
  }
  static bool sameAlbum(const char *a,const char *b) {
    if (!a || !b) return false;
    const char *ae=strrchr(a,'/'),*be=strrchr(b,'/');
    size_t an=ae ? (size_t)(ae-a) : 0,bn=be ? (size_t)(be-b) : 0;
    return an==bn && !memcmp(a,b,an);
  }
private:
  const char *const *path=nullptr;
  uint16_t artistStart[MAX_ITEMS]={},albumStart[MAX_ITEMS]={};
  int count=0,artists=0,albums=0,artist=0,album=0,song=0;
  Level level=ARTISTS;
  int endArtist() const { return artist+1<artists ? artistStart[artist+1] : count; }
  int firstAlbum() const { int i=0;while (i<albums && albumStart[i]<artistStart[artist]) ++i;return i; }
  int albumLimit() const { int i=firstAlbum();while (i<albums && albumStart[i]<endArtist()) ++i;return i; }
  int endAlbum() const { return album+1<albums ? albumStart[album+1] : count; }
};
