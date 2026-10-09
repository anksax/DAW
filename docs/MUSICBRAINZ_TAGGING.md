# Add artist information using MusicBrainz Picard

1. Install MusicBrainz Picard from https://picard.musicbrainz.org/ on Windows.
2. Add your FLAC music folder to Picard. Test a small folder first.
3. If the songs already contain useful title/artist/album tags, select them and use Cluster and Lookup.
4. For files with missing or unreliable tags, use Scan. Picard creates an acoustic fingerprint and uses AcoustID to find linked MusicBrainz recordings. Network access is required on the PC.
5. Review the matches in the right pane. Check the recording, release, title and artist before saving; a database match is not guaranteed for every file. Correct unmatched files manually.
6. Disable automatic file moving/renaming if you want to preserve your current paths. The ESP can read the tags without renaming files.
7. Click Save to write title/artist/album metadata into the matched files.
8. Copy the tagged FLAC files to the SD card. Stop the ESP and remove power before removing/replacing the card, then restart it after insertion.
9. Browse the library; titles and artists should appear after the short metadata load. Artist unknown means the needed tags were absent or unreadable, not that a new online lookup was performed.

This firmware does not read cover-art images or query MusicBrainz online. The tagging workflow keeps network matching on the PC and playback offline. No file names or artists were guessed in this release.

Primary documentation:
- https://picard.musicbrainz.org/quick-start/
- https://picard-docs.musicbrainz.org/en/latest/usage/retrieve_scan.html
- https://picard-docs.musicbrainz.org/en/latest/tutorials/acoustid.html
- https://xiph.org/flac/format.html
