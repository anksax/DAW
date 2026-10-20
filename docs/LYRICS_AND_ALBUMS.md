# Album folders, covers and synchronized lyrics

Use this layout on microSD (spaces and normal filenames are supported):

```text
Album Artist/Album/01 - Song.flac
Album Artist/Album/01 - Song.lrc
Album Artist/Album/02 - Another Song.flac
Album Artist/Album/02 - Another Song.lrc
Album Artist/Album/cover.jpg
```

Only FLAC tracks become selectable songs. Recursive scanning sorts canonical paths so album folders stay together. Zero-padded track numbers preserve the desired lexical song order. The browser starts in ALBUM ARTISTS, then ALBUMS, then SONGS. It derives groups from folders, so equal album names under different artists remain separate. Legacy root-level/album-only tracks still play, grouped under an unknown album artist when that folder level is absent. Empty folders without playable FLAC tracks are omitted.

## Navigation

| Control | Library action |
|---|---|
| E2 rotation | Browse the current artist/album/song level |
| E2 click | Enter folder / play song; selecting the playing song pauses/resumes |
| E2 double-click | Return SONGS → ALBUMS → ALBUM ARTISTS |
| E2 hold | Open/close Settings |
| E2 in Settings | Rotate selects a row; click toggles editing; rotate edits |
| Settings → PLAY ORDER | Clockwise selects shuffle; counterclockwise selects sequential |

Folder selection stays put when EOF advances playback; the playing card and lyrics still follow the actual playing track. SONGS has a cyan album heading/rule, and ALBUMS separates rows with horizontal lines. The playing card gives album names a large cyan line; long names scroll after a short initial pause. Cover art reduces the text field only when an image is successfully decoded.

## Lyrics

Put a UTF-8 `.lrc` beside each `.flac`, with exactly the same basename. `.LRC` is also checked. Hold E2 for Settings, rotate to **OLED VIEW**, click to edit, rotate to **LYRICS**, then click to finish and hold to exit. OLED VIEW cycles FX, SPECTRUM and LYRICS. E1 click returns to the FX page while selecting the next FX.

Example (original placeholder text):

```lrc
[ti:Song]
[ar:Artist]
[offset:0]
[00:05.00]First lyric line goes here
[00:12.50]Second lyric line goes here
[00:20.00][00:40.00]Repeated lyric line
[00:30.00]
```

The parser handles minute/second timestamps with zero to three fractional digits, multiple leading time tags, UTF-8 BOM/CRLF, metadata and global millisecond offset. In this implementation a **positive offset delays** lyrics and a negative offset advances them, clamped at zero. Enhanced word-time markers are stripped; word-by-word highlighting is not implemented. Untimed text is not automatically synchronized.

The OLED shows previous/current/next cues. The current cue has two larger rows; longer cues page every three source seconds. Empty timed cues show an instrumental interval. Pause freezes source timing/paging, and SLOW playback follows the same source-frame progress as the timeline. The timestamp tracks PCM queued to I2S; queued playback latency can require tuning `[offset:]`. This is not sample-accurate DAC playhead measurement.

Limits: one file up to **32 KiB**, **256 cues**, up to **16 timestamps per source line**, and **95 bytes of text per cue**. Over-limit cues/text are truncated safely. UTF-8 boundaries are preserved; visible glyph coverage depends on the OLED font (Latin text is the main target). Missing/oversized/untimed LRC has a short explanatory lyrics-page message, without affecting playback. Files are loaded once at track change while the decoder is stopped, not repeatedly during playback. Add/replace files before selecting/restarting a song. No online lyrics download is implemented.

## Album artwork

The player checks `cover.jpg`, `folder.jpg`, `cover.jpeg`, then `folder.jpeg` in the album folder. If none decodes, it checks JPEG FLAC PICTURE metadata, preferring picture type 3 (front cover). Baseline JPEG up to **512 KiB** and **2048×2048** is supported. It scales/aspect-fits to a **56×56** RGB565 thumbnail. PNG, progressive JPEG and other image formats are not supported by this decoder; convert folder covers to baseline JPEG if needed.

**No image placeholder, error label or empty artwork box is displayed.** Missing/invalid/unsupported covers silently use the full-width text layout. Successfully decoded art is reused while playing tracks from the same album folder. Loading a different album can add a bounded track-transition delay; cover reads/decompression occur while the old decoder is stopped, not alongside ongoing playback.

Lyrics entries/file scratch space and the thumbnail add about **63 KiB PSRAM**; JPEG decoding uses a 4 KiB internal workspace, and folder grouping/path pointers add around 4 KiB on the ESP32. Keep OPI PSRAM enabled. Failed optional lyric/art allocations do not disable audio. Keep **all** sketch files together, including the bundled JPEG `.c`/headers—no additional JPEG library install is needed.

TFT names are converted from UTF-8 to the classic font before clipping or scrolling: common Latin accents and punctuation use readable equivalents, combining accents are omitted, and unsupported codepoints use ?. Original metadata and file paths remain unchanged. Full Unicode TFT glyphs are not included.

Intentional letter spacing is retained. Unicode spacing characters use ordinary spaces, and fullwidth ASCII letters use their normal-width equivalents.
