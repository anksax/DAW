# Bundled decoder attribution

The sketch includes `tjpgd.c`, `tjpgd.h` and `tjpgdcnf.h`, TJpgDec R0.03 by **ChaN (2021)**, from Bodmer's TJpg_Decoder source:

- https://github.com/Bodmer/TJpg_Decoder/tree/master/src
- https://elm-chan.org/fsw/tjpgd/00index.html

The original copyright/license notices remain in the source. Its permissive notice requires preserving that notice and disclaims warranty; these bundled files retain those terms independently of the repository's MIT license. Configuration selects RGB565, scaling and the 32-bit decoder mode; whitespace was normalized, and the upstream byte-swap option is left disabled for native RGB565 canvas pixels. Only the small decoder files are bundled, not the Arduino wrapper library.

Cover decoder tests use synthetic generated JPEG byte fixtures, not user artwork. Lyrics tests and documentation use original placeholder strings, not song lyrics.
