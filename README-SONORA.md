# SONORA — audiobooks and podcasts for the Y1 Wheel edition

SONORA adds audiobooks and podcasts to the Wheel edition of Rockbox for the
Innioasis Y1. It uses the same playback engine, queue and sound settings as
music. It adds screens, and keeps its own small text files in
`.rockbox/sonora/`.

**Status:** built and tested in the desktop simulator, and compile- and
link-checked for the Y1. **Not yet run on a real Y1.**

## What it does

**Main menu**

- **Continue** — the last book and podcast episode you were listening to, plus
  "Resume music" if a book or episode took over your music queue. It only
  appears while something is half-listened.
- **Audiobooks** — In progress / All books / Authors / Finished / Settings.
- **Podcasts** — New episodes / Shows / Downloads / Import OPML /
  Refresh feeds / Remove played downloads / Settings.

**Book page**

- Resume (with time left), Play from the start, Chapters, Bookmarks.
- Speed per book.
- Mark as finished, or as not started again.
- Author, length, number of files.

**On Now Playing, while a book plays**

| Control | What it does |
|---|---|
| Prev / Next | Move by chapter: the M4B or MP3 chapter list, else one file = one chapter. With no chapters at all, skip back / ahead 30 s (both lengths can be changed). Holding Prev / Next still seeks as before. |
| Centre + Play | Add a bookmark (labelled with the chapter title) |
| Hold Centre | Options: Chapters, Add bookmark, Bookmarks, Speed, Sleep timer, Mark as finished |
| Sleep timer | 5–90 min, end of chapter or end of file. It fades out and **pauses**; it no longer switches the player off. |

**While a podcast plays**

- Prev / Next: back 15 s / ahead 30 s (both can be changed).
- Centre + Play: mark the episode as played.
- Speed is set per show.

**Automatic behaviour**

- Progress is saved on pause, stop, track change and every 60 s.
- When you come back after a pause of a minute or more, playback goes back
  10 s.
- A finished book stops, even with repeat on.
- Starting a book or episode saves the music queue so that Continue can bring
  it back.

**Kept out of music**

- Books and podcasts are not scrobbled.
- They don't count in the music listening stats.

## Folders

```
Audiobooks/                 (setting: Audiobooks folder; default HOME/Audiobooks)
  Some Book.m4b             one file = one book
  Some Book/01.mp3 02.mp3   one folder = one book; files play in natural order (2 before 10)
  Author Name/Book/...      an author folder holding books
Podcasts/                   (setting: Podcasts folder; default HOME/Podcasts)
  Show Name/episode.mp3     one folder per show
  subscriptions.opml        picked up by "Import OPML"
```

On the Y1, HOME is `/sdcard`.

Titles, authors and lengths come from the tags, and from the folder names when
there are none.

## Files SONORA writes

Everything goes in `.rockbox/sonora/`. All files are tab-separated text that
starts with `# sonora-format=1`. Each save goes to a temporary file and keeps a
`.bak` copy of the previous version.

| File | What | Kind |
|---|---|---|
| `progress.tsv` | position, speed, state (new / in progress / finished) per book and episode; the saved music queue's place | your state |
| `bookmarks.tsv` | bookmarks | your state |
| `podcasts.tsv` | shows: title, feed, speed, "keep newest N" | your state |
| `downloads.tsv` | **the manifest**: every episode SONORA downloaded, with its size | your state |
| `music-queue.m3u8` | the music queue saved when a book or episode took over | your state |
| `books.tsv` | library cache | safe to delete |
| `feeds/*.xml` | last copy of each feed | safe to delete |

## Your files are safe

- SONORA never writes into, renames or moves an audio file. Chapters, titles
  and bookmarks all live in its own files. It does not create `.bmark` or
  `.cue` files in your music folders. It does read cue sheets next to a file
  or in `.rockbox/cue/`.
- The only files SONORA ever deletes are podcast episodes it downloaded
  itself. Each one must:
  - be listed in `downloads.tsv`,
  - be inside the podcasts folder,
  - still have the size it was downloaded with,
  - be played to the end.

  Episodes you copied yourself are never deleted ("You copied this file:
  SONORA never deletes it"), and neither are unplayed ones. The port's own
  `poddl -delete` is not used.
- A download only ever writes to a name nothing on the card uses yet: the
  file and its `.part` are both checked, and over-long paths are refused
  rather than cut short.
- A deleted episode stays in the manifest as history (size -1), so a later
  refresh never offers it again.

## Network (off by default)

Podcast refresh and downloads only work after you turn on
*SONORA settings › Network features*. Then:

1. Wi-Fi is switched on only for the refresh, using the network in
   `.rockbox/wifi.cfg`, and switched off afterwards.
2. Each show's feed is read, and episodes newer than the ones offered last
   time are offered. The first time, only the newest 3 (or "keep N").
3. You confirm, and the episodes download through a `.part` file. They are
   recorded in the manifest.
4. Played episodes beyond a show's "keep newest N" are removed through the
   manifest rules above.

HTTPS uses Java's TLS 1.2 first, then the ROM's `gocurl` if it has one.
Redirects are followed.

**Not verified:** whether your Y1 has working Wi-Fi. The current port's
maintainers say it does; the archived older port said Wi-Fi was not enabled.
The desktop simulator has no network on purpose: it accepts only `file://`
feeds, so the whole refresh → download → clean-up path could be tested with
local files.

## Speed

Speed uses Rockbox's timestretch. If timestretch is off, choosing a speed
other than 1.0× offers to turn it on; on the Y1 that restarts Rockbox, as the
port's own Timestretch setting does. Speed is applied while a book or show
plays and goes back to the music speed afterwards. It is never written to
`config.cfg`.

## Known limits

- Chapters are read from:
  - MP4 / M4B (QuickTime chapter track or Nero `chpl`),
  - MP3 (ID3v2.3 / 2.4 `CHAP`),
  - cue sheets.

  **Not** read: Ogg / Opus `CHAPTERxxx` comments, and chapters inside each
  file of a multi-file book (each file counts as one chapter).
- Up to 400 chapters, 400 books, 500 files per book, 300 progress rows,
  300 bookmarks, 100 shows and 500 downloads.
- Podcast episode titles come from the file's tags, else the file name.
- The first library scan reads the tags of every book. Later scans use the
  cache.
- A book split into disc folders (`Book/CD1`, `Book/CD2`) is read as an
  author folder holding two books. Put all the files in one folder instead.
- Rockbox's own *Rewind on pause* setting still works as well, so turning it
  on adds its rewind to SONORA's.
- A refreshed feed with no publish dates offers its newest 3 (or "keep N")
  episodes on every refresh, minus those already in the manifest.
- Output is still 44.1 kHz / 16-bit; hi-res files are resampled.
- How timestretch performs on the Y1's MT6572 at 1.5–2× is unmeasured.
- Spanish and English translations are complete. Other languages show the
  English text for the new screens.

## Tests

- `utils/sonora/tests`: `make check` runs 105 checks.
  - Covers: chapter readers on generated MP4 (moov first/last, co64, UTF-16),
    ID3 v2.3/2.4 and cue files (120 tracks, multi-FILE); OPML/RSS
    (entities, CDATA, dates); the data store (atomic save, `.bak` fallback,
    format version) and the delete gate.
  - Runs under AddressSanitizer and UBSan.
- `utils/sonora/tests/real_files.sh` checks the chapter reader against M4B
  (both layouts) and MP3 files made by ffmpeg.
- `utils/wheel/tests/run.sh` includes `SonoraNet`: download, redirect, 404.
