/* SONORA host tests: chapter parsers, feed parsers, the data store.
 * Build: make -C utils/sonora/tests && ./utils/sonora/tests/test_sonora
 * Every test file is generated here, so nothing depends on downloads. */
#include "sonora_util.h"
#include "sonora_chapters.h"
#include "sonora_feed.h"
#include "sonora_data.h"
#include <sys/stat.h>
#include <dirent.h>

static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)
#define CHECK_STR(a, b) do { checks++; if (strcmp((a), (b))) { fails++; \
    fprintf(stderr, "FAIL %s:%d: \"%s\" != \"%s\"\n", __FILE__, __LINE__, (a), (b)); } } while (0)

static unsigned long fake_now = 1793600000;
unsigned long sonora_now(void) { return fake_now; }

static char tmpdir[256];

/* ------------------------------ byte buffers ------------------------------ */
struct buf { unsigned char d[1 << 16]; size_t n; };
static void put(struct buf *b, const void *p, size_t n) { memcpy(b->d + b->n, p, n); b->n += n; }
static void u8(struct buf *b, unsigned v) { b->d[b->n++] = (unsigned char)v; }
static void u16(struct buf *b, unsigned v) { u8(b, v >> 8); u8(b, v); }
static void u24(struct buf *b, unsigned v) { u8(b, v >> 16); u16(b, v & 0xffff); }
static void u32(struct buf *b, uint32_t v) { u16(b, v >> 16); u16(b, v & 0xffff); }
static void u64(struct buf *b, uint64_t v) { u32(b, (uint32_t)(v >> 32)); u32(b, (uint32_t)v); }
static void fourcc(struct buf *b, const char *s) { put(b, s, 4); }
/* atom: returns the offset of its size field so it can be patched */
static size_t open_atom(struct buf *b, const char *type) { size_t at = b->n; u32(b, 0); fourcc(b, type); return at; }
static void close_atom(struct buf *b, size_t at)
{
    uint32_t sz = (uint32_t)(b->n - at);
    b->d[at] = sz >> 24; b->d[at + 1] = sz >> 16; b->d[at + 2] = sz >> 8; b->d[at + 3] = sz;
}
static void save(const char *path, struct buf *b)
{
    FILE *f = fopen(path, "wb"); fwrite(b->d, 1, b->n, f); fclose(f);
}
static void save_text(const char *path, const char *text)
{
    FILE *f = fopen(path, "wb"); fputs(text, f); fclose(f);
}
static void path_in(char *out, const char *name) { snprintf(out, MAX_PATH, "%s/%s", tmpdir, name); }

/* ------------------------------- MP4 files -------------------------------- */
static const char *qt_titles[] = { "Opening", "The \xc3\x89lan", "Credits" };
static const uint32_t qt_delta[] = { 60000, 120000, 30000 };

static void tkhd(struct buf *b, uint32_t id, int ver)
{
    size_t a = open_atom(b, "tkhd");
    u8(b, ver); u24(b, 0);
    if (ver == 1) { u64(b, 0); u64(b, 0); } else { u32(b, 0); u32(b, 0); }
    u32(b, id);
    for (int i = 0; i < 16; i++) u32(b, 0);
    close_atom(b, a);
}
static void mdhd(struct buf *b, uint32_t ts)
{
    size_t a = open_atom(b, "mdhd"); u8(b, 0); u24(b, 0); u32(b, 0); u32(b, 0); u32(b, ts); u32(b, 0); u32(b, 0); close_atom(b, a);
}
static void hdlr(struct buf *b, const char *h)
{
    size_t a = open_atom(b, "hdlr"); u8(b, 0); u24(b, 0); u32(b, 0); fourcc(b, h); u32(b, 0); u32(b, 0); u32(b, 0); u8(b, 0); close_atom(b, a);
}

/* mdat first, moov last (the "not fast-start" layout); co64 if wide */
static void make_qt_m4b(const char *path, bool wide, bool utf16_title)
{
    static struct buf b; b.n = 0;
    size_t a = open_atom(&b, "ftyp"); fourcc(&b, "M4B "); u32(&b, 0); fourcc(&b, "isom"); close_atom(&b, a);
    size_t mdat = open_atom(&b, "mdat");
    uint32_t sample_off[3], sample_size[3];
    for (int i = 0; i < 3; i++)
    {
        sample_off[i] = (uint32_t)b.n;
        if (i == 1 && utf16_title)
        {   /* UTF-16 with BOM: "Élan" */
            static const unsigned char t[] = { 0xfe, 0xff, 0x00, 0xc9, 0x00, 'l', 0x00, 'a', 0x00, 'n' };
            u16(&b, sizeof(t)); put(&b, t, sizeof(t));
        }
        else
        {
            u16(&b, (unsigned)strlen(qt_titles[i])); put(&b, qt_titles[i], strlen(qt_titles[i]));
        }
        u32(&b, 12); fourcc(&b, "encd"); u32(&b, 256); /* the trailing atom real files carry */
        sample_size[i] = (uint32_t)b.n - sample_off[i];
    }
    for (int i = 0; i < 1000; i++) u8(&b, 0xaa); /* "audio" */
    close_atom(&b, mdat);

    size_t moov = open_atom(&b, "moov");
    a = open_atom(&b, "mvhd"); u8(&b, 0); u24(&b, 0); u32(&b, 0); u32(&b, 0); u32(&b, 1000); u32(&b, 210000);
    for (int i = 0; i < 20; i++) u32(&b, 0);
    close_atom(&b, a);
    /* track 1: audio, points at track 2 as its chapters */
    size_t t1 = open_atom(&b, "trak");
    tkhd(&b, 1, 0);
    a = open_atom(&b, "tref"); size_t c = open_atom(&b, "chap"); u32(&b, 2); close_atom(&b, c); close_atom(&b, a);
    size_t md = open_atom(&b, "mdia"); mdhd(&b, 44100); hdlr(&b, "soun");
    size_t mi = open_atom(&b, "minf"); size_t st = open_atom(&b, "stbl"); close_atom(&b, st); close_atom(&b, mi);
    close_atom(&b, md); close_atom(&b, t1);
    /* track 2: the chapter text track */
    size_t t2 = open_atom(&b, "trak");
    tkhd(&b, 2, 1);
    md = open_atom(&b, "mdia"); mdhd(&b, 1000); hdlr(&b, "text");
    mi = open_atom(&b, "minf"); st = open_atom(&b, "stbl");
    a = open_atom(&b, "stts"); u8(&b, 0); u24(&b, 0); u32(&b, 3);
    for (int i = 0; i < 3; i++) { u32(&b, 1); u32(&b, qt_delta[i]); }
    close_atom(&b, a);
    a = open_atom(&b, "stsz"); u8(&b, 0); u24(&b, 0); u32(&b, 0); u32(&b, 3);
    for (int i = 0; i < 3; i++) u32(&b, sample_size[i]);
    close_atom(&b, a);
    /* two chunks: sample 0 alone, samples 1 and 2 together */
    a = open_atom(&b, "stsc"); u8(&b, 0); u24(&b, 0); u32(&b, 2);
    u32(&b, 1); u32(&b, 1); u32(&b, 1);
    u32(&b, 2); u32(&b, 2); u32(&b, 1);
    close_atom(&b, a);
    a = open_atom(&b, wide ? "co64" : "stco"); u8(&b, 0); u24(&b, 0); u32(&b, 2);
    if (wide) { u64(&b, sample_off[0]); u64(&b, sample_off[1]); }
    else { u32(&b, sample_off[0]); u32(&b, sample_off[1]); }
    close_atom(&b, a);
    close_atom(&b, st); close_atom(&b, mi); close_atom(&b, md); close_atom(&b, t2);
    close_atom(&b, moov);
    save(path, &b);
}

static void make_nero_m4b(const char *path, int ver)
{
    static struct buf b; b.n = 0;
    size_t a = open_atom(&b, "ftyp"); fourcc(&b, "M4A "); u32(&b, 0); close_atom(&b, a);
    size_t moov = open_atom(&b, "moov");
    a = open_atom(&b, "mvhd"); u8(&b, 0); u24(&b, 0); u32(&b, 0); u32(&b, 0); u32(&b, 600); u32(&b, 600 * 100); close_atom(&b, a);
    size_t ud = open_atom(&b, "udta");
    a = open_atom(&b, "chpl"); u8(&b, ver); u24(&b, 0); if (ver) u32(&b, 0);
    u8(&b, 3);
    const char *t[] = { "Intro", "Middle", "End" };
    const uint64_t s[] = { 0, 30ULL * 10000000, 75ULL * 10000000 };
    for (int i = 0; i < 3; i++) { u64(&b, s[i]); u8(&b, (unsigned)strlen(t[i])); put(&b, t[i], strlen(t[i])); }
    close_atom(&b, a); close_atom(&b, ud); close_atom(&b, moov);
    a = open_atom(&b, "mdat"); for (int i = 0; i < 64; i++) u8(&b, 0); close_atom(&b, a);
    save(path, &b);
}

static void test_mp4(void)
{
    char p[MAX_PATH];
    static struct sonora_chapter ch[SONORA_MAX_CHAPTERS];
    int src; uint32_t dur;

    path_in(p, "qt.m4b"); make_qt_m4b(p, false, false);
    int fd = open(p, O_RDONLY);
    int n = sonora_chapters_mp4(fd, ch, SONORA_MAX_CHAPTERS, &src, &dur);
    close(fd);
    CHECK(n == 3);
    CHECK(src == SCH_QUICKTIME);
    CHECK(dur == 210000);
    CHECK(ch[0].start_ms == 0 && ch[1].start_ms == 60000 && ch[2].start_ms == 180000);
    CHECK_STR(ch[0].title, "Opening");
    CHECK_STR(ch[1].title, "The \xc3\x89lan");
    CHECK_STR(ch[2].title, "Credits");

    path_in(p, "qt64.m4b"); make_qt_m4b(p, true, true);
    n = sonora_chapters_read(p, ch, SONORA_MAX_CHAPTERS, &src);
    CHECK(n == 3 && src == SCH_QUICKTIME);
    CHECK_STR(ch[1].title, "\xc3\x89lan");

    for (int v = 0; v <= 1; v++)
    {
        path_in(p, v ? "nero1.m4a" : "nero0.m4a"); make_nero_m4b(p, v);
        n = sonora_chapters_read(p, ch, SONORA_MAX_CHAPTERS, &src);
        CHECK(n == 3 && src == SCH_NERO);
        CHECK(ch[1].start_ms == 30000 && ch[2].start_ms == 75000);
        CHECK_STR(ch[2].title, "End");
    }

    /* max is respected */
    path_in(p, "qt.m4b");
    n = sonora_chapters_read(p, ch, 2, &src);
    CHECK(n == 2);

    /* not an MP4 at all */
    path_in(p, "junk.m4b"); save_text(p, "this is not an mp4 file, just text padding....");
    n = sonora_chapters_read(p, ch, SONORA_MAX_CHAPTERS, &src);
    CHECK(n == 0 && src == SCH_NONE);
    /* truncated: moov cut short */
    path_in(p, "qt.m4b");
    {
        FILE *f = fopen(p, "rb"); static unsigned char d[1 << 16]; size_t k = fread(d, 1, sizeof(d), f); fclose(f);
        char q[MAX_PATH]; path_in(q, "cut.m4b");
        FILE *g = fopen(q, "wb"); fwrite(d, 1, k - 40, g); fclose(g);
        n = sonora_chapters_read(q, ch, SONORA_MAX_CHAPTERS, &src);
        CHECK(n == 0); /* refuses rather than reading garbage */
    }
}

/* ------------------------------- ID3 files -------------------------------- */
static void syncsafe32(struct buf *b, uint32_t v)
{
    u8(b, (v >> 21) & 0x7f); u8(b, (v >> 14) & 0x7f); u8(b, (v >> 7) & 0x7f); u8(b, v & 0x7f);
}
static void id3_frame(struct buf *b, int major, const char *id, struct buf *body)
{
    fourcc(b, id);
    if (major == 4) syncsafe32(b, (uint32_t)body->n); else u32(b, (uint32_t)body->n);
    u16(b, 0);
    put(b, body->d, body->n);
}
static void make_id3(const char *path, int major)
{
    static struct buf tag, frame, sub;
    tag.n = 0;
    /* written out of order on purpose: CTOC order is not trusted */
    const uint32_t starts[] = { 120000, 0, 45000 };
    for (int i = 0; i < 3; i++)
    {
        frame.n = 0;
        char eid[8]; snprintf(eid, sizeof(eid), "ch%d", i);
        put(&frame, eid, strlen(eid) + 1);
        u32(&frame, starts[i]); u32(&frame, starts[i] + 1000); u32(&frame, 0xffffffff); u32(&frame, 0xffffffff);
        sub.n = 0;
        if (i == 0) { u8(&sub, 0); put(&sub, "Caf\xe9", 4); }               /* Latin-1 */
        else if (i == 1) { u8(&sub, 1); static const unsigned char t[] = { 0xff, 0xfe, 'P', 0, 'r', 0, 'o', 0, 0x3d, 0xd8, 0x00, 0xde, 0, 0 };
                           put(&sub, t, sizeof(t)); }                           /* UTF-16LE + emoji */
        else { u8(&sub, 3); put(&sub, "Mitte", 5); u8(&sub, 0); }              /* UTF-8 */
        id3_frame(&frame, major, "TIT2", &sub);
        id3_frame(&tag, major, "CHAP", &frame);
    }
    frame.n = 0; u8(&frame, 0); put(&frame, "Ignored", 7);
    id3_frame(&tag, major, "TIT2", &frame);
    for (int i = 0; i < 32; i++) u8(&tag, 0); /* padding */
    static struct buf b; b.n = 0;
    put(&b, "ID3", 3); u8(&b, major); u8(&b, 0); u8(&b, 0); syncsafe32(&b, (uint32_t)tag.n);
    put(&b, tag.d, tag.n);
    u8(&b, 0xff); u8(&b, 0xfb); for (int i = 0; i < 100; i++) u8(&b, 0);
    save(path, &b);
}

static void test_id3(void)
{
    char p[MAX_PATH];
    static struct sonora_chapter ch[SONORA_MAX_CHAPTERS];
    int src;
    for (int major = 3; major <= 4; major++)
    {
        path_in(p, major == 3 ? "v23.mp3" : "v24.mp3");
        make_id3(p, major);
        int n = sonora_chapters_read(p, ch, SONORA_MAX_CHAPTERS, &src);
        CHECK(n == 3 && src == SCH_ID3);
        CHECK(ch[0].start_ms == 0 && ch[1].start_ms == 45000 && ch[2].start_ms == 120000);
        CHECK_STR(ch[0].title, "Pro\xf0\x9f\x98\x80");
        CHECK_STR(ch[1].title, "Mitte");
        CHECK_STR(ch[2].title, "Caf\xc3\xa9");
    }
    path_in(p, "plain.mp3");
    save_text(p, "\xff\xfb no tag at all");
    CHECK(sonora_chapters_read(p, ch, SONORA_MAX_CHAPTERS, &src) == 0);
}

/* --------------------------------- cue ------------------------------------ */
static void test_cue(void)
{
    char p[MAX_PATH], audio[MAX_PATH];
    static struct sonora_chapter ch[SONORA_MAX_CHAPTERS];
    int src;
    /* a single-file book with 120 chapters: more than Rockbox's 99 */
    path_in(audio, "long book.mp3"); save_text(audio, "\xff\xfb");
    path_in(p, "long book.cue");
    FILE *f = fopen(p, "w");
    fprintf(f, "\xef\xbb\xbfPERFORMER \"Someone\"\nFILE \"long book.mp3\" MP3\n");
    for (int i = 0; i < 120; i++)
        fprintf(f, "  TRACK %03d AUDIO\n    TITLE \"Chapter %d\"\n    INDEX 01 %02d:%02d:%02d\n",
                i + 1, i + 1, (i * 61) / 60, (i * 61) % 60, i == 1 ? 37 : 0);
    fclose(f);
    int n = sonora_chapters_read(audio, ch, SONORA_MAX_CHAPTERS, &src);
    CHECK(n == 120 && src == SCH_CUE);
    CHECK_STR(ch[119].title, "Chapter 120");
    CHECK(ch[1].start_ms == 61000 + 37 * 1000 / 75);

    /* a cue describing two files: only our file's tracks */
    path_in(p, "two.cue");
    save_text(p, "FILE \"a.flac\" WAVE\n TRACK 01 AUDIO\n  TITLE \"A1\"\n  INDEX 01 00:00:00\n"
                 "FILE \"b.flac\" WAVE\n TRACK 02 AUDIO\n  TITLE \"B1\"\n  INDEX 00 00:00:00\n  INDEX 01 00:02:00\n"
                 " TRACK 03 AUDIO\n  TITLE \"B2\"\n  INDEX 01 01:00:00\n");
    char b_audio[MAX_PATH]; path_in(b_audio, "B.FLAC");
    n = sonora_chapters_cue(p, b_audio, ch, SONORA_MAX_CHAPTERS);
    CHECK(n == 2);
    CHECK_STR(ch[0].title, "B1");
    CHECK(ch[0].start_ms == 2000);

    /* the cue folder in .rockbox keeps the music folders untouched */
    mkdir(SONORA_CUE_DIR, 0777);
    path_in(audio, "elsewhere.m4b"); make_nero_m4b(audio, 0);
    /* nero gives 3; a better cue in .rockbox/cue gives 5 and wins */
    save_text(SONORA_CUE_DIR "/elsewhere.cue",
              "FILE \"elsewhere.m4b\" MP4\n"
              " TRACK 01 AUDIO\n  INDEX 01 00:00:00\n TRACK 02 AUDIO\n  INDEX 01 00:10:00\n"
              " TRACK 03 AUDIO\n  INDEX 01 00:20:00\n TRACK 04 AUDIO\n  INDEX 01 00:30:00\n"
              " TRACK 05 AUDIO\n  INDEX 01 00:40:00\n");
    n = sonora_chapters_read(audio, ch, SONORA_MAX_CHAPTERS, &src);
    CHECK(n == 3 && src == SCH_NERO); /* embedded chapters are trusted first */
    path_in(audio, "elsewhere.mp3"); save_text(audio, "\xff\xfb");
    save_text(SONORA_CUE_DIR "/elsewhere.cue",
              "FILE \"elsewhere.mp3\" MP3\n TRACK 01 AUDIO\n  INDEX 01 00:00:00\n TRACK 02 AUDIO\n  INDEX 01 00:10:00\n");
    n = sonora_chapters_read(audio, ch, SONORA_MAX_CHAPTERS, &src);
    CHECK(n == 2 && src == SCH_CUE && ch[1].start_ms == 10000);
}

/* --------------------------------- feeds ---------------------------------- */
struct opml_ctx { int n; char t[4][160]; char u[4][512]; };
static void opml_cb(const char *title, const char *url, void *ctx)
{
    struct opml_ctx *c = ctx;
    if (c->n < 4) { strlcpy(c->t[c->n], title, 160); strlcpy(c->u[c->n], url, 512); }
    c->n++;
}

struct rss_ctx { int n; struct sonora_rss_item it[8]; };
static bool rss_cb(const struct sonora_rss_item *it, void *ctx)
{
    struct rss_ctx *c = ctx;
    if (c->n < 8) c->it[c->n] = *it;
    c->n++;
    return true;
}

static void test_feeds(void)
{
    const char *opml =
        "<?xml version=\"1.0\"?><opml version=\"2.0\"><body>"
        "<outline text=\"Folder\">"
        "<outline type=\"rss\" text=\"Tea &amp; Talk\" xmlUrl=\"https://ex.org/tea.xml?a=1&amp;b=2\"/>"
        "<outline title='Single quotes' text=\"ignored\" xmlUrl='http://ex.org/sq'/>"
        "<outline text=\"No feed here\"/>"
        "<outline xmlUrl=\"https://ex.org/untitled\"></outline>"
        "</outline></body></opml>";
    struct opml_ctx oc; memset(&oc, 0, sizeof(oc));
    int n = sonora_opml_parse(opml, strlen(opml), opml_cb, &oc);
    CHECK(n == 3 && oc.n == 3);
    CHECK_STR(oc.t[0], "Tea & Talk");
    CHECK_STR(oc.u[0], "https://ex.org/tea.xml?a=1&b=2");
    CHECK_STR(oc.t[1], "Single quotes");
    CHECK_STR(oc.t[2], "https://ex.org/untitled");

    const char *rss =
        "<rss><channel><title><![CDATA[Night & Day Radio]]></title><image><title>logo</title></image>"
        "<item><title>Ep. 2 &#8211; Rain</title><guid isPermaLink=\"false\">abc-2</guid>"
        "<pubDate>Mon, 02 Nov 2026 10:00:00 +0100</pubDate><itunes:duration>1:02:03</itunes:duration>"
        "<description><![CDATA[<p>Notes with <b>tags</b> &amp; </title> text</p>]]></description>"
        "<enclosure length=\"1234\" type=\"audio/mpeg\" url=\"https://cdn.ex.org/e2.mp3?x=1\"/></item>"
        "<item><title>Video only</title><enclosure url=\"https://ex.org/v.mp4\" type=\"video/mp4\"/></item>"
        "<item><title>Ep. 1</title><pubDate>Sun, 1 Nov 26 09:00 GMT</pubDate>"
        "<itunes:duration>754</itunes:duration>"
        "<enclosure url='https://ex.org/e1.m4a' /></item>"
        "</channel></rss>";
    struct rss_ctx rc; memset(&rc, 0, sizeof(rc));
    char title[96];
    n = sonora_rss_parse(rss, strlen(rss), title, sizeof(title), rss_cb, &rc);
    CHECK(n == 2);
    CHECK_STR(title, "Night & Day Radio");
    CHECK_STR(rc.it[0].title, "Ep. 2 \xe2\x80\x93 Rain");
    CHECK_STR(rc.it[0].guid, "abc-2");
    CHECK_STR(rc.it[0].url, "https://cdn.ex.org/e2.mp3?x=1");
    CHECK(rc.it[0].length == 1234);
    CHECK(rc.it[0].duration_s == 3723);
    CHECK(rc.it[0].pub_epoch == 1793610000L); /* 2026-11-02 09:00 UTC */
    CHECK_STR(rc.it[0].desc, "Notes with tags & text"); /* HTML tags dropped, CDATA kept whole */
    CHECK_STR(rc.it[1].guid, "https://ex.org/e1.m4a"); /* no guid: the url */
    CHECK(rc.it[1].duration_s == 754);
    CHECK(rc.it[1].pub_epoch == sonora_rfc822_time("01 Nov 2026 09:00:00 +0000"));
    CHECK(sonora_rfc822_time("Thu, 01 Jan 1970 00:00:00 GMT") == 0);
    CHECK(sonora_rfc822_time("garbage") == 0);
    CHECK(sonora_duration_secs("62:03") == 3723);
}

/* ------------------------------- data store ------------------------------- */
static void test_store(void)
{
    char dir[MAX_PATH], p[MAX_PATH];
    path_in(dir, "store"); mkdir(dir, 0777);
    sonora_data_init(dir);
    CHECK(!sonora_progress_load()); /* nothing yet */

    struct sonora_progress *a = sonora_progress_get("/Audiobooks/Long Way", SK_BOOK_C);
    strlcpy(a->cur, "/Audiobooks/Long Way/02 Part.mp3", sizeof(a->cur));
    a->pos_ms = 1234; a->total_ms = 99999; a->speed = 12500; a->state = SP_PLAYING; a->when = 10;
    strlcpy(a->title, "Long\tWay\nHome", sizeof(a->title)); /* tabs never reach the file */
    struct sonora_progress *e = sonora_progress_get("/Podcasts/Show/ep.mp3", SK_EPISODE_C);
    e->when = 20; e->state = SP_DONE;
    CHECK(sonora_progress_save());
    CHECK(sonora_progress_save()); /* second save leaves a .bak */
    CHECK(sonora_progress_load());
    CHECK(sonora_progress_count() == 2);
    a = sonora_progress_find("/Audiobooks/Long Way");
    CHECK(a && a->pos_ms == 1234 && a->speed == 12500 && a->state == SP_PLAYING);
    CHECK(a && !strchr(a->title, '\t') && !strchr(a->title, '\n'));
    int idx[8];
    CHECK(sonora_progress_recent(0, 0, idx, 8) == 2 && sonora_progress_at(idx[0])->when == 20);
    CHECK(sonora_progress_recent(SK_BOOK_C, SP_PLAYING, idx, 8) == 1);

    /* crash between the two renames: only the .bak is left */
    sonora_data_path("progress.tsv", p, sizeof(p));
    remove(p);
    CHECK(sonora_progress_load() && sonora_progress_count() == 2);
    /* a file from a newer SONORA is not read (and not overwritten here) */
    save_text(p, "# sonora-format=2\nb\tx\n");
    char bak[MAX_PATH]; snprintf(bak, sizeof(bak), "%s.bak", p); remove(bak);
    CHECK(!sonora_progress_load());
    /* someone else's file is not read either */
    save_text(p, "kind\tkey\n");
    CHECK(!sonora_progress_load());

    /* bookmarks sorted by position */
    sonora_bookmarks_load();
    struct sonora_bookmark *b1 = sonora_bookmark_add("/Audiobooks/X"); b1->done_ms = 500;
    struct sonora_bookmark *b2 = sonora_bookmark_add("/Audiobooks/X"); b2->done_ms = 100;
    strlcpy(b2->label, "Start", sizeof(b2->label));
    sonora_bookmark_add("/Audiobooks/Y");
    CHECK(sonora_bookmarks_save() && sonora_bookmarks_load());
    int bm[8];
    CHECK(sonora_bookmarks_for("/Audiobooks/X", bm, 8) == 2);
    CHECK(sonora_bookmark_at(bm[0])->done_ms == 100);
    CHECK_STR(sonora_bookmark_at(bm[0])->label, "Start");

    /* shows */
    sonora_shows_load();
    struct sonora_show *s = sonora_show_add("Tea & Talk");
    strlcpy(s->url, "https://ex.org/tea.xml", sizeof(s->url)); s->speed = 11000; s->keep = 5; s->last_pub = 1793610000L;
    CHECK(sonora_shows_save() && sonora_shows_load());
    CHECK(sonora_show_find_url("https://ex.org/tea.xml") && sonora_show_find("Tea & Talk")->keep == 5 && sonora_show_find("Tea & Talk")->last_pub == 1793610000L);

    /* the delete gate */
    char managed[MAX_PATH], ep[MAX_PATH], mine[MAX_PATH];
    path_in(managed, "Podcasts"); mkdir(managed, 0777);
    snprintf(p, sizeof(p), "%s/Tea", managed); mkdir(p, 0777);
    snprintf(ep, sizeof(ep), "%s/Tea/ep1.mp3", managed); save_text(ep, "0123456789");
    snprintf(mine, sizeof(mine), "%s/Tea/copied by me.mp3", managed); save_text(mine, "0123456789");
    sonora_downloads_load();
    struct sonora_download *d = sonora_download_add(ep); d->size = 10;
    CHECK(sonora_downloads_save() && sonora_downloads_load());
    CHECK(sonora_download_may_delete(ep, managed));
    CHECK(!sonora_download_may_delete(mine, managed));            /* not ours */
    CHECK(!sonora_download_may_delete(ep, "/somewhere/else"));    /* outside */
    CHECK(!sonora_download_may_delete(ep, ""));
    save_text(ep, "replaced by the user, different size");
    CHECK(!sonora_download_may_delete(ep, managed));              /* changed */
    char sneaky[MAX_PATH]; snprintf(sneaky, sizeof(sneaky), "%s/../Tea/ep1.mp3", managed);
    d = sonora_download_add(sneaky); d->size = 10;
    CHECK(!sonora_download_may_delete(sneaky, managed));

    /* removed episodes stay as history: never offered or deleted again */
    save_text(ep, "0123456789");
    d = sonora_download_find(ep); d->size = 10; strlcpy(d->guid, "g-1", sizeof(d->guid));
    CHECK(sonora_download_may_delete(ep, managed));
    int before = sonora_downloads_present();
    sonora_download_removed(ep);
    CHECK(!sonora_download_may_delete(ep, managed));
    CHECK(sonora_download_find_guid("g-1") != NULL);
    CHECK(sonora_downloads_present() == before - 1);
    CHECK(sonora_downloads_save() && sonora_downloads_load());
    CHECK(sonora_download_find(ep) && sonora_download_find(ep)->size == -1);
    d = sonora_download_add(ep); /* downloaded again under the same name */
    CHECK(d && d->size == 0 && !d->guid[0]);
    /* a full manifest makes room by forgetting removed history, never live rows */
    for (int i = 0; sonora_downloads_count() < SONORA_MAX_DOWNLOADS; i++)
    {
        char p2[MAX_PATH]; snprintf(p2, sizeof(p2), "%s/Tea/old%d.mp3", managed, i);
        struct sonora_download *o = sonora_download_add(p2);
        o->size = (i % 2) ? 5 : -1; fake_now++; o->when = (uint32_t)fake_now;
    }
    int live = sonora_downloads_present();
    CHECK(sonora_download_add("/x/new.mp3") != NULL);
    CHECK(sonora_downloads_present() == live + 1); /* a removed row made room */
    for (int i = 0; i < sonora_downloads_count(); i++)
        if (sonora_download_at(i)->size == -1 && !strcmp(sonora_download_at(i)->path, "/x/new.mp3"))
            CHECK(0);
    char p0[MAX_PATH]; snprintf(p0, sizeof(p0), "%s/Tea/old0.mp3", managed);
    CHECK(sonora_download_find(p0) == NULL); /* the oldest removed one went */
}

/* --------------------------------- util ----------------------------------- */
static int cmp(const void *a, const void *b) { return sonora_natcmp(*(char **)a, *(char **)b); }
static void test_util(void)
{
    const char *v[] = { "Part 10.mp3", "part 2.mp3", "Part 1.mp3", "Intro.mp3", "Part 02b.mp3" };
    qsort(v, 5, sizeof(v[0]), cmp);
    CHECK_STR(v[0], "Intro.mp3"); CHECK_STR(v[1], "Part 1.mp3"); CHECK_STR(v[2], "part 2.mp3");
    CHECK_STR(v[3], "Part 02b.mp3"); CHECK_STR(v[4], "Part 10.mp3");
    char out[64];
    sonora_xml_text("  a&lt;b &amp;&#x41;&#66; <i>x</i>\n y  ", 37, out, sizeof(out));
    CHECK_STR(out, "a<b &AB x y");
    sonora_safe_name("What? Who: Me/You.. ", out, sizeof(out));
    CHECK_STR(out, "What_ Who_ Me_You");
    sonora_safe_name("..NET Rocks", out, sizeof(out));
    CHECK_STR(out, "NET Rocks"); /* never a hidden file */
    sonora_safe_name("...", out, sizeof(out));
    CHECK_STR(out, "untitled");
    sonora_fmt_time(out, sizeof(out), 3723000); CHECK_STR(out, "1:02:03");
    sonora_fmt_time(out, sizeof(out), 61000); CHECK_STR(out, "1:01");
    CHECK(sonora_is_audio("x.M4B") && !sonora_is_audio("x.jpg") && !sonora_is_audio("noext"));
    char line[] = "a\tb\t\tc";
    char *f[8];
    CHECK(sonora_split(line, f, 8) == 4 && !strcmp(f[2], "") && !strcmp(f[3], "c"));
}

int main(void)
{
    snprintf(tmpdir, sizeof(tmpdir), "/tmp/sonora-test-%d", (int)getpid());
    mkdir(tmpdir, 0777);
    test_util();
    test_mp4();
    test_id3();
    test_cue();
    test_feeds();
    test_store();
    printf("%d checks, %d failed\n", checks, fails);
    return fails ? 1 : 0;
}
