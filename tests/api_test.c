/* api_test: the infovox230 library on its own (no Ivx230nt.dll; Windows and Linux).
 * Usage: api_test [bindir]
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "infovox230.h"

static long failures, checks;
static const char *bindir = "bin";

#define CHECK(cond, ...)                              \
    do {                                              \
        checks++;                                     \
        if (!(cond)) {                                \
            failures++;                               \
            printf("  FAIL line %d: ", __LINE__);     \
            printf(__VA_ARGS__);                      \
            printf("\n");                             \
        }                                             \
    } while (0)

static const char *const codes[] = {"am", "bl", "da", "du", "fi", "fr", "ge", "ic", "it", "no", "sp", "sw"};

static const char *lang_path(const char *code)
{
    static char path[512];

    snprintf(path, sizeof path, "%s/%srules.ivx", bindir, code);
    return path;
}

static unsigned char *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *b;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    b = malloc((size_t)*size);
    if (b != NULL && fread(b, 1, (size_t)*size, f) != (size_t)*size) {
        free(b);
        b = NULL;
    }
    fclose(f);
    return b;
}

/* a fresh engine with a language loaded */
static IvxEngine *engine(const char *code, int enc)
{
    int err = 0;
    IvxEngine *e = ivx_create(&err);

    if (e == NULL) {
        printf("ivx_create: %s\n", ivx_strerror(err));
        exit(2);
    }
    if (ivx_set(e, IVX_INPUT_ENCODING, enc) != 0 || ivx_load_language(e, lang_path(code)) != 0) {
        printf("cannot load %s\n", lang_path(code));
        exit(2);
    }
    return e;
}

/* ---- Task 1: lifecycle, settings, loading ---- */

static void test_lifecycle(void)
{
    int err = 1, k;
    IvxEngine *e = ivx_create(&err);

    CHECK(e != NULL && err == 0, "ivx_create: %p, err %d", (void *)e, err);
    CHECK(ivx_version() == IVX_VERSION, "ivx_version 0x%x", ivx_version());
    for (k = 0; k >= IVX_E_ARG; k--)
        CHECK(ivx_strerror(k) != NULL && ivx_strerror(k)[0] != 0
              && strcmp(ivx_strerror(k), ivx_strerror(-99)) != 0, "ivx_strerror(%d)", k);
    ivx_destroy(e);
    ivx_destroy(NULL);
}

static void test_settings(void)
{
    static const struct { int param, bad; } bad[] = {
        {IVX_VOICE, -1}, {IVX_VOICE, 10}, {IVX_RATE, -1}, {IVX_RATE, 101}, {IVX_PITCH, 101},
        {IVX_FD, -1}, {IVX_FRAME_RATE, 1}, {IVX_FRAME_RATE, 1001}, {IVX_SAMPLE_RATE, 7999},
        {IVX_SAMPLE_RATE, 48001}, {IVX_TEXT_MODE, 0x200}, {IVX_CHARSET, 0x20},
        {IVX_INPUT_ENCODING, 2}, {IVX_EVENT_MASK, 16}, {-1, 0}, {IVX_EVENT_MASK + 1, 0}};
    IvxEngine *e = ivx_create(NULL);
    int v, k;

    CHECK(ivx_get(e, IVX_VOICE, &v) == 0 && v == 0, "default voice %d", v);
    CHECK(ivx_get(e, IVX_FRAME_RATE, &v) == 0 && v == 100, "default frame rate %d", v);
    CHECK(ivx_get(e, IVX_SAMPLE_RATE, &v) == 0 && v == 22050, "default sample rate %d", v);
    CHECK(ivx_get(e, IVX_TEXT_MODE, &v) == 0 && v == 8, "default text mode %d", v);
    CHECK(ivx_get(e, IVX_CHARSET, &v) == 0 && v == 2, "default charset %d", v);
    CHECK(ivx_get(e, IVX_INPUT_ENCODING, &v) == 0 && v == IVX_ENC_UTF8, "default encoding %d", v);
    CHECK(ivx_get(e, IVX_EVENT_MASK, &v) == 0 && v == IVX_EVMASK_DEFAULT, "default mask %d", v);
    for (k = IVX_RATE; k <= IVX_FD; k++)
        CHECK(ivx_get(e, k, &v) == 0 && v >= 0 && v <= 100, "default param %d = %d", k, v);
    for (k = 0; k < (int)(sizeof bad / sizeof bad[0]); k++)
        CHECK(ivx_set(e, bad[k].param, bad[k].bad) == IVX_E_RANGE, "set %d %d", bad[k].param, bad[k].bad);
    CHECK(ivx_get(e, IVX_EVENT_MASK + 1, &v) == IVX_E_RANGE, "get unknown param");
    CHECK(ivx_get(e, IVX_RATE, NULL) == IVX_E_ARG, "get NULL value");
    CHECK(ivx_set(NULL, IVX_RATE, 50) == IVX_E_ARG, "set NULL engine");
    CHECK(ivx_set(e, IVX_RATE, 80) == 0 && ivx_get(e, IVX_RATE, &v) == 0 && v == 80, "queued rate %d", v);
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 16000) == 0 && ivx_get(e, IVX_SAMPLE_RATE, &v) == 0 && v == 16000,
          "sample rate %d", v);
    ivx_destroy(e);
}

static void test_load(void)
{
    IvxEngine *e = ivx_create(NULL);
    unsigned char *b, small[100] = {0};
    long size;
    int k;

    CHECK(ivx_load_language(e, "no/such/file.ivx") == IVX_E_IO, "missing file");
    CHECK(ivx_load_language(e, NULL) == IVX_E_ARG, "NULL path");
    CHECK(ivx_load_language_mem(e, small, sizeof small) == IVX_E_BADFILE, "short file");
    b = read_file(lang_path("sw"), &size);
    CHECK(b != NULL, "read %s", lang_path("sw"));
    if (b != NULL) {
        b[0x240] = b[0x241] = 'X';   /* build tag "XX": ivx_bind refuses it */
        CHECK(ivx_load_language_mem(e, b, (size_t)size) == IVX_E_BADFILE, "XX tag");
        b[0x240] = 'S';
        b[0x200 + 4 * 5] = 0x7f;     /* H[5] (big-endian) far past the end */
        CHECK(ivx_load_language_mem(e, b, (size_t)size) == IVX_E_BADFILE, "bad section offset");
        free(b);
    }
    for (k = 0; k < 12; k++)
        CHECK(ivx_load_language(e, lang_path(codes[k])) == 0, "load %s", codes[k]);
    ivx_destroy(e);
}

/* ---- helpers for streaming tests ---- */

/* cp1252 and UTF-8 versions of one text; a \x escape followed by a hex digit is split */
static const char SW[] = "Det h\xe4r \xe4r ett test av talsyntesen. Klockan \xe4r 12.30 och v\xe4"
                         "dret \xe4r fint.";
static const char SW_UTF8[] = "Det h\xc3\xa4r \xc3\xa4r ett test av talsyntesen. Klockan \xc3\xa4r 12.30 "
                              "och v\xc3\xa4" "dret \xc3\xa4r fint.";
static const char GE[] = "Guten Tag. Das ist ein kurzer Test.";

typedef struct {
    short *pcm;
    long n, cap;
    IvxEvent *ev;
    long nev, evcap;
    uint64_t base;      /* engine output position where this collection started */
} Out;

static void out_free(Out *o)
{
    free(o->pcm);
    free(o->ev);
    memset(o, 0, sizeof *o);
}

static void out_pcm(Out *o, const short *p, int n)
{
    if (o->n + n > o->cap) {
        o->cap = 2 * (o->n + n) + 4096;
        o->pcm = realloc(o->pcm, (size_t)o->cap * sizeof *o->pcm);
    }
    memcpy(o->pcm + o->n, p, (size_t)n * sizeof *p);
    o->n += n;
}

static void out_ev(Out *o, const IvxEvent *ev)
{
    if (o->nev == o->evcap) {
        o->evcap = 2 * o->evcap + 64;
        o->ev = realloc(o->ev, (size_t)o->evcap * sizeof *o->ev);
    }
    o->ev[o->nev++] = *ev;
}

/* one ivx_read plus the events it released; every event must be covered by audio */
static int read_some(IvxEngine *e, short *buf, int chunk, Out *o)
{
    IvxEvent ev;
    int n = ivx_read(e, buf, chunk);

    CHECK(n >= 0, "ivx_read: %s", ivx_strerror(n));
    if (n > 0)
        out_pcm(o, buf, n);
    while (ivx_next_event(e, &ev) == 1) {
        CHECK(ev.sample <= o->base + (uint64_t)o->n, "event at %lu released after %lu samples",
              (unsigned long)ev.sample, (unsigned long)(o->base + (uint64_t)o->n));
        out_ev(o, &ev);
    }
    return n;
}

static void drain(IvxEngine *e, int chunk, Out *o)
{
    short *buf = malloc((size_t)chunk * sizeof *buf);

    while (read_some(e, buf, chunk, o) > 0)
        ;
    free(buf);
}

/* queue text word by word; with mark0 != 0 a mark (mark0, mark0 + 1, ...) goes before each word */
static void speak(IvxEngine *e, const char *text, uint32_t mark0)
{
    const char *p = text, *w;
    uint32_t id = mark0;

    while (*p) {
        w = p;
        while (*p && *p != ' ')
            p++;
        while (*p == ' ')
            p++;
        if (mark0 != 0)
            CHECK(ivx_mark(e, id++) == 0, "ivx_mark");
        CHECK(ivx_feed(e, w, (size_t)(p - w)) == 0, "ivx_feed");
    }
}

/* render with an event mask */
static void render_mask(const char *code, int enc, const char *text, uint32_t mark0, int chunk, int mask,
                        Out *o)
{
    IvxEngine *e = engine(code, enc);

    memset(o, 0, sizeof *o);
    CHECK(ivx_set(e, IVX_EVENT_MASK, mask) == 0, "event mask");
    speak(e, text, mark0);
    CHECK(ivx_flush(e) == 0, "ivx_flush");
    drain(e, chunk, o);
    ivx_destroy(e);
}

static void render(const char *code, int enc, const char *text, uint32_t mark0, int chunk, Out *o)
{
    render_mask(code, enc, text, mark0, chunk, IVX_EVMASK_DEFAULT, o);
}

static int same_audio(const Out *a, const Out *b)
{
    return a->n == b->n && memcmp(a->pcm, b->pcm, (size_t)a->n * sizeof *a->pcm) == 0;
}

static int same_events(const Out *a, const Out *b)
{
    long k;

    if (a->nev != b->nev)
        return 0;
    for (k = 0; k < a->nev; k++)
        if (a->ev[k].type != b->ev[k].type || a->ev[k].value != b->ev[k].value
            || a->ev[k].sample != b->ev[k].sample)
            return 0;
    return 1;
}

static long count_events(const Out *o, int type)
{
    long k, n = 0;

    for (k = 0; k < o->nev; k++)
        n += o->ev[k].type == type;
    return n;
}

static int count_words(const char *t)
{
    int n = 0;

    while (*t) {
        while (*t == ' ')
            t++;
        if (*t)
            n++;
        while (*t && *t != ' ')
            t++;
    }
    return n;
}

/* ---- Task 2: streaming ---- */

static void test_stream_errors(void)
{
    IvxEngine *e = ivx_create(NULL);
    short buf[16];
    IvxEvent ev;

    CHECK(ivx_feed(e, "Hej.", 4) == IVX_E_NOLANG, "feed without language");
    CHECK(ivx_feed_phonetic(e, "h", 1) == IVX_E_NOLANG, "phonetic without language");
    CHECK(ivx_mark(e, 1) == IVX_E_NOLANG, "mark without language");
    CHECK(ivx_flush(e) == IVX_E_NOLANG, "flush without language");
    CHECK(ivx_read(e, buf, 16) == IVX_E_NOLANG, "read without language");
    CHECK(ivx_read(NULL, buf, 16) == IVX_E_ARG, "read NULL engine");
    CHECK(ivx_feed(e, NULL, 3) == IVX_E_ARG, "feed NULL text");
    CHECK(ivx_next_event(e, NULL) == IVX_E_ARG, "next_event NULL");
    CHECK(ivx_synthesize(e, "a", 1, NULL, NULL) == IVX_E_ARG, "synthesize NULL callback");
    CHECK(ivx_next_event(e, &ev) == 0, "no events on a new engine");
    ivx_destroy(e);
    e = engine("sw", IVX_ENC_UTF8);
    CHECK(ivx_read(e, buf, 0) == 0, "read 0 samples");
    CHECK(ivx_read(e, NULL, 16) == IVX_E_ARG, "read NULL buffer");
    CHECK(ivx_read(e, buf, -1) == IVX_E_RANGE, "read negative count");
    CHECK(ivx_read(e, buf, 16) == 0, "read with nothing queued");
    ivx_cancel(e);
    ivx_cancel(e);
    ivx_cancel(NULL);
    ivx_destroy(e);
}

static void test_chunks(void)
{
    static const int chunks[] = {1, 7, 4096};
    Out ref, o;
    int k;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &ref);
    CHECK(ref.n > 22050 && ref.n % 441 == 0, "reference: %ld samples", ref.n);
    CHECK(count_events(&ref, IVX_EV_DONE) == 1 && ref.nev > 0 && ref.ev[ref.nev - 1].type == IVX_EV_DONE
          && ref.ev[ref.nev - 1].sample == (uint64_t)ref.n, "one DONE at the end");
    for (k = 0; k < 3; k++) {
        render("sw", IVX_ENC_CP1252, SW, 0, chunks[k], &o);
        CHECK(same_audio(&ref, &o) && same_events(&ref, &o), "chunk %d differs", chunks[k]);
        out_free(&o);
    }
    out_free(&ref);
}

static Out syn_out;
static int syn_calls, syn_stop_at;

static int IVX_CALL collect(void *user, const short *pcm, int n, const IvxEvent *ev, int nev)
{
    int k;

    (void)user;
    out_pcm(&syn_out, pcm, n);
    for (k = 0; k < nev; k++)
        out_ev(&syn_out, &ev[k]);
    return ++syn_calls == syn_stop_at;
}

/* cancels the engine (user) itself on its 3rd call and returns 0 */
static int IVX_CALL cancel_in_cb(void *user, const short *pcm, int n, const IvxEvent *ev, int nev)
{
    (void)pcm;
    (void)n;
    (void)ev;
    (void)nev;
    if (++syn_calls == 3)
        ivx_cancel((IvxEngine *)user);
    return 0;
}

static void test_synthesize(void)
{
    IvxEngine *e;
    Out ref;
    int r;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &ref);
    e = engine("sw", IVX_ENC_CP1252);
    memset(&syn_out, 0, sizeof syn_out);
    syn_calls = 0;
    syn_stop_at = -1;
    r = ivx_synthesize(e, SW, strlen(SW), collect, NULL);
    CHECK(r == 0 && same_audio(&ref, &syn_out) && same_events(&ref, &syn_out), "synthesize differs (%d)", r);
    out_free(&syn_out);
    syn_calls = 0;
    syn_stop_at = 3;
    r = ivx_synthesize(e, SW, strlen(SW), collect, NULL);
    CHECK(r == 0 && syn_calls == 3, "stop from callback: %d, %d calls", r, syn_calls);
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 16000) == 0, "idle after a stopped synthesize");
    out_free(&syn_out);
    /* the callback cancels the engine itself and returns 0: no DONE ever comes */
    syn_calls = 0;
    r = ivx_synthesize(e, SW, strlen(SW), cancel_in_cb, e);
    CHECK(r == 0 && syn_calls == 3, "cancel inside the callback: %d, %d calls", r, syn_calls);
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 22050) == 0, "idle after a cancel inside the callback");
    ivx_destroy(e);
    out_free(&ref);
}

static void test_encoding(void)
{
    static const struct { const char *utf8, *cp, *what; } pairs[] = {
        {"Det \xc3\xa4r v\xc3\xa4" "dret.", "Det \xe4r v\xe4" "dret.", "2-byte"},
        {"Det kostar 5 \xe2\x82\xac.", "Det kostar 5 \x80.", "euro"},
        {"Hon sa \xe2\x80\x99ja\xe2\x80\x99.", "Hon sa \x92ja\x92.", "quote"},
        {"Ett\xff tv\xc3\xa5.", "Ett  tv\xe5.", "invalid byte"},
        {"A\xc0\xaf" "B.", "A  B.", "overlong"},
        {"A\xe4 B.", "A  B.", "truncated"},
        {"A \xe4\xb8\xad B.", "A   B.", "outside cp1252"},
        {"A\xc2\x85" "B.", "A B.", "C1 control"},
        {"A\xed\xa0\x80" "B.", "A   B.", "surrogate"},
        {"A\xf4\x90\x80\x80" "B.", "A    B.", "above U+10FFFF"},
        {"Hej\x01 d\x02\x17\xc3\xa5.", "Hej d\xe5.", "escape chars removed"},
        {"Ett.\nTv\xc3\xa5.", "Ett.\rTv\xe5.", "LF becomes CR"}};
    IvxEngine *e;
    Out a, b;
    int k;

    for (k = 0; k < (int)(sizeof pairs / sizeof pairs[0]); k++) {
        render("sw", IVX_ENC_UTF8, pairs[k].utf8, 0, 441, &a);
        render("sw", IVX_ENC_CP1252, pairs[k].cp, 0, 441, &b);
        CHECK(a.n > 0 && same_audio(&a, &b), "encoding: %s", pairs[k].what);
        out_free(&a);
        out_free(&b);
    }
    /* a sequence split across ivx_feed calls */
    render("sw", IVX_ENC_UTF8, SW_UTF8, 0, 441, &a);
    e = engine("sw", IVX_ENC_UTF8);
    memset(&b, 0, sizeof b);
    CHECK(ivx_feed(e, "Det h\xc3", 6) == 0 && ivx_feed(e, "\xa4r ", 3) == 0
          && ivx_feed(e, SW_UTF8 + 9, strlen(SW_UTF8 + 9)) == 0 && ivx_flush(e) == 0, "split feed");
    drain(e, 441, &b);
    CHECK(same_audio(&a, &b), "encoding: split sequence");
    ivx_destroy(e);
    out_free(&a);
    out_free(&b);
    /* an unfinished sequence at a flush becomes a space */
    render("sw", IVX_ENC_CP1252, "Hej ", 0, 441, &a);
    e = engine("sw", IVX_ENC_UTF8);
    memset(&b, 0, sizeof b);
    CHECK(ivx_feed(e, "Hej\xc3", 4) == 0 && ivx_flush(e) == 0, "unfinished feed");
    drain(e, 441, &b);
    CHECK(same_audio(&a, &b), "encoding: unfinished sequence at flush");
    ivx_destroy(e);
    out_free(&a);
    out_free(&b);
    /* the SW texts are the same in both encodings */
    render("sw", IVX_ENC_UTF8, SW_UTF8, 0, 441, &a);
    render("sw", IVX_ENC_CP1252, SW, 0, 441, &b);
    CHECK(same_audio(&a, &b), "encoding: SW text");
    out_free(&a);
    out_free(&b);
}

static void test_busy(void)
{
    IvxEngine *e = engine("sw", IVX_ENC_UTF8);
    Out o;
    int v;

    CHECK(ivx_feed(e, "Hej.", 4) == 0, "feed");
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 16000) == IVX_E_BUSY, "sample rate while busy");
    CHECK(ivx_set(e, IVX_TEXT_MODE, 4) == IVX_E_BUSY, "text mode while busy");
    CHECK(ivx_load_language(e, lang_path("sw")) == IVX_E_BUSY, "load while busy");
    CHECK(ivx_set(e, IVX_RATE, 97) == 0 && ivx_get(e, IVX_RATE, &v) == 0 && v == 97, "queued rate");
    ivx_cancel(e);
    CHECK(ivx_get(e, IVX_RATE, &v) == 0 && v != 97, "cancel drops the queued rate (%d)", v);
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 16000) == 0, "sample rate after cancel");
    memset(&o, 0, sizeof o);
    CHECK(ivx_feed(e, "Hej.", 4) == 0 && ivx_flush(e) == 0, "feed again");
    drain(e, 441, &o);
    CHECK(o.n > 0 && ivx_set(e, IVX_SAMPLE_RATE, 22050) == 0, "idle after the text");
    out_free(&o);
    ivx_destroy(e);
}

/* text (and a mark) read into the Core without a flush keeps the engine busy: a load
 * would drop it and the mark would fire in a later, unrelated utterance */
static void test_busy_in_core(void)
{
    IvxEngine *e = engine("sw", IVX_ENC_CP1252);
    short buf[441];
    Out o;
    uint64_t pos;
    long k, n42 = 0;
    int n;

    memset(&o, 0, sizeof o);
    CHECK(ivx_feed(e, "Hej hopp ", 9) == 0 && ivx_mark(e, 42) == 0 && ivx_feed(e, "tralala ", 8) == 0,
          "feed without flush");
    do
        n = read_some(e, buf, 441, &o);
    while (n > 0);
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 16000) == IVX_E_BUSY, "sample rate with text in the Core");
    CHECK(ivx_load_language(e, lang_path("sw")) == IVX_E_BUSY, "load with text in the Core");
    CHECK(ivx_flush(e) == 0, "flush");
    drain(e, 441, &o);
    for (k = 0; k < o.nev; k++)
        n42 += o.ev[k].type == IVX_EV_MARK && o.ev[k].value == 42;
    CHECK(n42 == 1 && count_events(&o, IVX_EV_DONE) == 1, "mark 42 x%ld, %ld DONE", n42,
          count_events(&o, IVX_EV_DONE));
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 16000) == 0, "sample rate after the flush");
    CHECK(ivx_load_language(e, lang_path("sw")) == 0, "load after the flush");
    pos = o.base + (uint64_t)o.n;
    out_free(&o);
    /* a cancel mid-text leaves the engine idle */
    o.base = pos;
    CHECK(ivx_feed(e, "Hej hopp ", 9) == 0 && ivx_mark(e, 43) == 0, "feed again");
    do
        n = read_some(e, buf, 441, &o);
    while (n > 0);
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 22050) == IVX_E_BUSY, "busy before the cancel");
    ivx_cancel(e);
    CHECK(ivx_set(e, IVX_SAMPLE_RATE, 22050) == 0 && ivx_load_language(e, lang_path("sw")) == 0,
          "idle after a cancel mid-text");
    CHECK(ivx_feed(e, "Tralala.", 8) == 0 && ivx_flush(e) == 0, "feed after the cancel");
    drain(e, 441, &o);
    CHECK(count_events(&o, IVX_EV_MARK) == 0 && count_events(&o, IVX_EV_DONE) == 1,
          "after the cancel: %ld marks, %ld DONE", count_events(&o, IVX_EV_MARK),
          count_events(&o, IVX_EV_DONE));
    out_free(&o);
    ivx_destroy(e);
}

static void test_interleave(void)
{
    Out sa, sb, ia, ib;
    IvxEngine *a, *b;
    short buf[300];
    int da = 0, db = 0;

    render_mask("sw", IVX_ENC_CP1252, SW, 1000, 441, IVX_EVMASK_ALL, &sa);
    render_mask("ge", IVX_ENC_CP1252, GE, 2000, 441, IVX_EVMASK_ALL, &sb);
    CHECK(count_events(&sa, IVX_EV_MARK) == count_words(SW) && count_events(&sa, IVX_EV_PHONEME) > 20
          && count_events(&sb, IVX_EV_MARK) == count_words(GE) && count_events(&sb, IVX_EV_PHONEME) > 20,
          "interleave references: marks %ld/%ld, phonemes %ld/%ld", count_events(&sa, IVX_EV_MARK),
          count_events(&sb, IVX_EV_MARK), count_events(&sa, IVX_EV_PHONEME), count_events(&sb, IVX_EV_PHONEME));
    a = engine("sw", IVX_ENC_CP1252);
    b = engine("ge", IVX_ENC_CP1252);
    CHECK(ivx_set(a, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0 && ivx_set(b, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0,
          "event masks");
    memset(&ia, 0, sizeof ia);
    memset(&ib, 0, sizeof ib);
    speak(a, SW, 1000);
    speak(b, GE, 2000);
    ivx_flush(a);
    ivx_flush(b);
    while (!da || !db) {
        if (!da)
            da = read_some(a, buf, 300, &ia) == 0;
        if (!db)
            db = read_some(b, buf, 300, &ib) == 0;
    }
    CHECK(same_audio(&sa, &ia) && same_events(&sa, &ia), "interleaved sw differs");
    CHECK(same_audio(&sb, &ib) && same_events(&sb, &ib), "interleaved ge differs");
    ivx_destroy(a);
    ivx_destroy(b);
    out_free(&sa);
    out_free(&sb);
    out_free(&ia);
    out_free(&ib);
}

static void test_load_failure(void)
{
    IvxEngine *e = engine("sw", IVX_ENC_CP1252);
    unsigned char *b;
    long size;
    Out ref, o;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &ref);
    b = read_file(lang_path("ge"), &size);
    b[0x240] = b[0x241] = 'X';
    CHECK(ivx_load_language_mem(e, b, (size_t)size) == IVX_E_BADFILE, "XX load");
    free(b);
    memset(&o, 0, sizeof o);
    speak(e, SW, 0);
    ivx_flush(e);
    drain(e, 441, &o);
    CHECK(same_audio(&ref, &o), "a failed load changed the bound language");
    ivx_destroy(e);
    out_free(&ref);
    out_free(&o);
}

static void render_set(int param, int value, Out *o)
{
    IvxEngine *e = engine("sw", IVX_ENC_CP1252);

    memset(o, 0, sizeof *o);
    CHECK(ivx_set(e, param, value) == 0, "set %d %d", param, value);
    speak(e, SW, 0);
    ivx_flush(e);
    drain(e, 441, o);
    ivx_destroy(e);
}

/* sw, all events: how 0 = nothing (an empty flush), 1 = sym as phonetic input,
 * 2 = sym as plain text */
static void render_input(const char *sym, int how, Out *o)
{
    IvxEngine *e = engine("sw", IVX_ENC_CP1252);

    memset(o, 0, sizeof *o);
    CHECK(ivx_set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0, "event mask");
    if (how == 1)
        CHECK(ivx_feed_phonetic(e, sym, strlen(sym)) == 0, "phonetic feed");
    else if (how == 2)
        CHECK(ivx_feed(e, sym, strlen(sym)) == 0, "text feed");
    CHECK(ivx_flush(e) == 0, "flush");
    drain(e, 441, o);
    ivx_destroy(e);
}

#define PHON_SYM "H[J"   /* Swedish "hej" in the .ivx phoneme names */

static void test_settings_effect(void)
{
    Out def, fast, frames, empty, ph, txt;
    long k;
    int have_h = 0, have_j = 0;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &def);
    render_set(IVX_RATE, 90, &fast);
    render_set(IVX_FRAME_RATE, 200, &frames);
    /* TTS_RATE (voice DR) adjusts AC/AH/AN amplitude per frame (src/opsmooth.c
     * op_next_frame); segment durations come only from the .ivx rule bytecode
     * (src/opstart.c op_seg_durations), so the sample count does not change */
    CHECK(fast.n == def.n && !same_audio(&def, &fast), "rate 90 did not change the audio (%ld)", fast.n);
    CHECK(frames.n < def.n * 3 / 4, "frame rate 200: %ld samples vs %ld", frames.n, def.n);
    out_free(&def);
    out_free(&fast);
    out_free(&frames);
    /* phonetic input (the language's phoneme names, KIM notation) is spoken: more audio
     * than an empty flush, its phonemes among the PHONEME events, and not the same audio
     * as the same bytes read as text */
    render_input(PHON_SYM, 0, &empty);
    render_input(PHON_SYM, 1, &ph);
    render_input(PHON_SYM, 2, &txt);
    CHECK(count_events(&ph, IVX_EV_DONE) == 1, "phonetic input: DONE");
    CHECK(ph.n > empty.n, "phonetic input: %ld samples, empty flush %ld", ph.n, empty.n);
    for (k = 0; k < ph.nev; k++)
        if (ph.ev[k].type == IVX_EV_PHONEME) {
            have_h |= (ph.ev[k].value & 0xff) == 'H';
            have_j |= (ph.ev[k].value & 0xff) == 'J';
        }
    CHECK(have_h && have_j, "phonetic input: phonemes H %d, J %d", have_h, have_j);
    CHECK(txt.n > 0 && !same_audio(&ph, &txt), "phonetic input sounds like the text");
    out_free(&empty);
    out_free(&ph);
    out_free(&txt);
}

/* apply()'s voice->rate/pitch/FD cascade: cmd[] indexing and the qcount guard
 * that lets an explicitly queued rate/pitch/FD win over the voice's own */
static void test_voice(void)
{
    IvxEngine *e;
    Out audio[5], one;
    int rate[5], pitch[5], fd[5];
    int v, k, differ, before;

    for (v = 0; v < 5; v++) {
        e = engine("sw", IVX_ENC_CP1252);
        CHECK(ivx_set(e, IVX_VOICE, v) == 0, "set voice %d", v);
        memset(&audio[v], 0, sizeof audio[v]);
        speak(e, SW, 0);
        CHECK(ivx_flush(e) == 0, "flush voice %d", v);
        drain(e, 441, &audio[v]);
        CHECK(ivx_get(e, IVX_RATE, &rate[v]) == 0 && rate[v] >= 0 && rate[v] <= 100,
              "voice %d rate %d", v, rate[v]);
        CHECK(ivx_get(e, IVX_PITCH, &pitch[v]) == 0 && pitch[v] >= 0 && pitch[v] <= 100,
              "voice %d pitch %d", v, pitch[v]);
        CHECK(ivx_get(e, IVX_FD, &fd[v]) == 0 && fd[v] >= 0 && fd[v] <= 100,
              "voice %d fd %d", v, fd[v]);
        ivx_destroy(e);
    }
    differ = 0;
    for (v = 1; v < 5; v++)
        if (rate[v] != rate[0] || pitch[v] != pitch[0] || fd[v] != fd[0])
            differ = 1;
    CHECK(differ, "voices 0..4 all give the same rate/pitch/fd");
    CHECK(!same_audio(&audio[0], &audio[3]), "voice 3 audio same as voice 0");
    for (v = 0; v < 5; v++)
        out_free(&audio[v]);

    /* a rate queued right after a voice change overrides only the rate the voice set */
    e = engine("sw", IVX_ENC_CP1252);
    CHECK(ivx_set(e, IVX_VOICE, 3) == 0, "set voice 3");
    CHECK(ivx_set(e, IVX_RATE, 97) == 0, "queue rate 97 after voice 3");
    memset(&one, 0, sizeof one);
    speak(e, SW, 0);
    CHECK(ivx_flush(e) == 0, "flush");
    drain(e, 441, &one);
    CHECK(ivx_get(e, IVX_RATE, &k) == 0 && k == 97, "rate stays 97 after voice 3 (%d)", k);
    CHECK(ivx_get(e, IVX_PITCH, &k) == 0 && k == pitch[3], "pitch matches voice 3 alone (%d vs %d)", k, pitch[3]);
    CHECK(ivx_get(e, IVX_FD, &k) == 0 && k == fd[3], "fd matches voice 3 alone (%d vs %d)", k, fd[3]);
    out_free(&one);
    ivx_destroy(e);

    /* ivx_get reflects a queued voice immediately, but rate stays the old applied
     * value until a read actually applies the voice change */
    e = engine("sw", IVX_ENC_CP1252);
    CHECK(ivx_get(e, IVX_RATE, &before) == 0, "rate before voice change");
    CHECK(ivx_set(e, IVX_VOICE, 3) == 0 && ivx_get(e, IVX_VOICE, &v) == 0 && v == 3,
          "voice set immediately");
    CHECK(ivx_get(e, IVX_RATE, &k) == 0 && k == before, "rate unchanged before a read (%d vs %d)", k, before);
    ivx_destroy(e);
}

/* ---- Task 3: events ---- */

static void test_marks(void)
{
    static const int chunks[] = {1, 7, 4096};
    Out ref, plain, o;
    long k, m = 0, distinct = 0;
    uint64_t last = 0, prev_mark = (uint64_t)-1;
    int c, words = count_words(SW);

    render("sw", IVX_ENC_CP1252, SW, 1000, 441, &ref);
    render("sw", IVX_ENC_CP1252, SW, 0, 441, &plain);
    CHECK(same_audio(&ref, &plain), "marks changed the audio");
    CHECK(count_events(&ref, IVX_EV_MARK) == words, "%ld marks for %d words",
          count_events(&ref, IVX_EV_MARK), words);
    for (k = 0; k < ref.nev; k++) {
        CHECK(ref.ev[k].sample >= last, "event %ld goes back in time", k);
        last = ref.ev[k].sample;
        if (ref.ev[k].type == IVX_EV_MARK) {
            CHECK(ref.ev[k].value == 1000 + (uint32_t)m, "mark %ld has id %lu", m,
                  (unsigned long)ref.ev[k].value);
            distinct += ref.ev[k].sample != prev_mark;
            prev_mark = ref.ev[k].sample;
            m++;
        }
    }
    CHECK(distinct > words / 2, "only %ld distinct mark positions for %d words", distinct, words);
    CHECK(ref.nev > 0 && ref.ev[ref.nev - 1].type == IVX_EV_DONE, "DONE is last");
    for (c = 0; c < 3; c++) {
        render("sw", IVX_ENC_CP1252, SW, 1000, chunks[c], &o);
        CHECK(same_audio(&ref, &o) && same_events(&ref, &o), "marks: chunk %d differs", chunks[c]);
        out_free(&o);
    }
    out_free(&ref);
    out_free(&plain);
}

/* marks with no segments between them: cb_bookmark's do/while coalesces several older
 * marks onto the segment that carries the newest one. This happens twice here: marks
 * 1-3 (queued before any text) share the first segment, and marks 5-6 (queued after all
 * the text, right before the flush) share a later one -- ivx_flush's sentence-end
 * processing (ctl_word_end's word_end_sym "@." and the M5/M2 rerun) always adds at
 * least one more segment carrying the newest pending mark_hi, so those two marks are
 * never left to the idle-cleanup loop in ivx_read (confirmed with a throwaway probe:
 * even ivx_mark + ivx_flush with NO text at all reports the mark at sample 0, with DONE
 * only at sample 10143, because trailing frames still follow that segment). See the fix
 * notes in task-3-report.md for why the check below asks for "before DONE", not "at
 * DONE" as first written. */
static void test_marks_coalesced(void)
{
    static const char T1[] = "Det h\xe4r \xe4r ett test. ";
    static const char T2[] = "Klockan \xe4r tolv.";
    IvxEngine *e, *pe;
    Out o, plain;
    uint64_t msamp[6], last = 0, done_samp = 0;
    long k, nmark = 0;
    int have_done = 0;

    e = engine("sw", IVX_ENC_CP1252);
    CHECK(ivx_mark(e, 1) == 0, "mark 1");
    CHECK(ivx_mark(e, 2) == 0, "mark 2");
    CHECK(ivx_mark(e, 3) == 0, "mark 3");
    CHECK(ivx_feed(e, T1, strlen(T1)) == 0, "feed T1");
    CHECK(ivx_mark(e, 4) == 0, "mark 4");
    CHECK(ivx_feed(e, T2, strlen(T2)) == 0, "feed T2");
    CHECK(ivx_mark(e, 5) == 0, "mark 5");
    CHECK(ivx_mark(e, 6) == 0, "mark 6");
    CHECK(ivx_flush(e) == 0, "flush");
    memset(&o, 0, sizeof o);
    drain(e, 441, &o);
    for (k = 0; k < o.nev; k++) {
        CHECK(o.ev[k].sample >= last, "event %ld goes back in time", k);
        last = o.ev[k].sample;
        if (o.ev[k].type == IVX_EV_MARK) {
            CHECK(o.ev[k].value == (uint32_t)(nmark + 1), "mark %ld has id %lu", nmark,
                  (unsigned long)o.ev[k].value);
            if (nmark < 6)
                msamp[nmark] = o.ev[k].sample;
            nmark++;
        } else if (o.ev[k].type == IVX_EV_DONE) {
            have_done = 1;
            done_samp = o.ev[k].sample;
        }
    }
    CHECK(nmark == 6, "%ld mark events, expected 6", nmark);
    CHECK(have_done && o.nev > 0 && o.ev[o.nev - 1].type == IVX_EV_DONE, "DONE is last");
    if (nmark == 6) {
        CHECK(msamp[0] == msamp[1] && msamp[1] == msamp[2],
              "marks 1-3 not on one sample (%lu, %lu, %lu)", (unsigned long)msamp[0],
              (unsigned long)msamp[1], (unsigned long)msamp[2]);
        CHECK(msamp[3] > msamp[2], "mark 4 (%lu) not after marks 1-3 (%lu)",
              (unsigned long)msamp[3], (unsigned long)msamp[2]);
        CHECK(msamp[4] == msamp[5], "marks 5,6 not coalesced onto one sample (%lu vs %lu)",
              (unsigned long)msamp[4], (unsigned long)msamp[5]);
        CHECK(msamp[4] > msamp[3] && msamp[4] < done_samp,
              "marks 5,6 (%lu) not strictly between mark 4 (%lu) and DONE (%lu)",
              (unsigned long)msamp[4], (unsigned long)msamp[3], (unsigned long)done_samp);
    }

    pe = engine("sw", IVX_ENC_CP1252);
    memset(&plain, 0, sizeof plain);
    CHECK(ivx_feed(pe, T1, strlen(T1)) == 0 && ivx_feed(pe, T2, strlen(T2)) == 0
          && ivx_flush(pe) == 0, "feed plain");
    drain(pe, 441, &plain);
    CHECK(same_audio(&o, &plain), "marks changed the audio (coalesced)");
    ivx_destroy(pe);
    out_free(&plain);
    ivx_destroy(e);
    out_free(&o);
}

static void test_phonemes(void)
{
    IvxEngine *e = engine("sw", IVX_ENC_CP1252);
    Out def, o;
    long k, n;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &def);
    memset(&o, 0, sizeof o);
    CHECK(ivx_set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0, "event mask");
    speak(e, SW, 0);
    ivx_flush(e);
    drain(e, 441, &o);
    n = count_events(&o, IVX_EV_PHONEME);
    CHECK(n > 20, "%ld phoneme events", n);
    for (k = 0; k < o.nev; k++)
        if (o.ev[k].type == IVX_EV_PHONEME)
            CHECK((o.ev[k].value & 0xff) >= 0x20 && (o.ev[k].value & 0xff) < 0x7f,   /* ' ' = word boundary */
                  "phoneme name 0x%lx", (unsigned long)o.ev[k].value);
    CHECK(same_audio(&def, &o), "phoneme events changed the audio");
    ivx_destroy(e);
    out_free(&o);
    /* with no events at all, ivx_synthesize still ends */
    e = engine("sw", IVX_ENC_CP1252);
    CHECK(ivx_set(e, IVX_EVENT_MASK, 0) == 0, "mask 0");
    memset(&syn_out, 0, sizeof syn_out);
    syn_calls = 0;
    syn_stop_at = -1;
    CHECK(ivx_synthesize(e, SW, strlen(SW), collect, NULL) == 0 && syn_out.nev == 0
          && same_audio(&def, &syn_out), "synthesize with mask 0");
    out_free(&syn_out);
    ivx_destroy(e);
    out_free(&def);
}

/* ---- Task 4: cancel ---- */

static void test_cancel(void)
{
    static const char SW2[] = "Nu kommer en ny mening efter avbrottet.";
    short buf[441];
    IvxEvent ev;
    int trial;
    long k;

    for (trial = 0; trial < 20; trial++) {
        IvxEngine *e = engine("sw", IVX_ENC_CP1252);
        long target = 441L * (1 + trial * 37 % 60);
        Out o, o2;
        int n;

        memset(&o, 0, sizeof o);
        speak(e, SW, 1000);
        ivx_flush(e);
        do
            n = read_some(e, buf, 441, &o);
        while (n > 0 && o.n < target);
        ivx_cancel(e);
        CHECK(ivx_next_event(e, &ev) == 0, "trial %d: event after cancel", trial);
        CHECK(ivx_set(e, IVX_SAMPLE_RATE, 22050) == 0, "trial %d: not idle after cancel", trial);
        memset(&o2, 0, sizeof o2);
        o2.base = (uint64_t)o.n;
        speak(e, SW2, 5000);
        ivx_flush(e);
        drain(e, 441, &o2);
        CHECK(o2.n > 0, "trial %d: no audio after cancel", trial);
        CHECK(count_events(&o2, IVX_EV_DONE) == 1, "trial %d: %ld DONE", trial,
              count_events(&o2, IVX_EV_DONE));
        for (k = 0; k < o2.nev; k++) {
            CHECK(o2.ev[k].sample >= o2.base, "trial %d: event before the cancel point", trial);
            if (o2.ev[k].type == IVX_EV_MARK)
                CHECK(o2.ev[k].value >= 5000, "trial %d: old mark %lu after cancel", trial,
                      (unsigned long)o2.ev[k].value);
        }
        out_free(&o);
        out_free(&o2);
        ivx_destroy(e);
    }
}

/* ---- input the original DLL crashes on (the guards in src/opstart.c and src/tpchar.c) ---- */

static uint32_t lcg;

static unsigned char rnd8(void)
{
    lcg = lcg * 1103515245u + 12345u;
    return (unsigned char)(lcg >> 16);
}

/* text, then the SW sentence; the sentence must still be spoken in full (not bit for bit
 * as on a fresh engine: the synth and rule state carry over from one text to the next) */
static void robust_run(const char *code, const char *text, size_t len, const char *what)
{
    IvxEngine *e = engine(code, IVX_ENC_CP1252);
    Out o, tail, ref;

    memset(&o, 0, sizeof o);
    memset(&tail, 0, sizeof tail);
    CHECK(ivx_feed(e, text, len) == 0 && ivx_flush(e) == 0, "%s %s: feed", code, what);
    drain(e, 4096, &o);
    CHECK(count_events(&o, IVX_EV_DONE) == 1, "%s %s: %ld DONE", code, what, count_events(&o, IVX_EV_DONE));
    tail.base = (uint64_t)o.n;
    speak(e, SW, 0);
    ivx_flush(e);
    drain(e, 4096, &tail);
    render(code, IVX_ENC_CP1252, SW, 0, 4096, &ref);
    CHECK(tail.n > 0 && count_events(&tail, IVX_EV_DONE) == 1, "%s %s: no speech after it", code, what);
    CHECK(tail.n >= ref.n * 3 / 4, "%s %s: the next sentence is cut short (%ld samples vs %ld fresh)",
          code, what, tail.n, ref.n);
    out_free(&o);
    out_free(&tail);
    out_free(&ref);
    ivx_destroy(e);
}

static void test_robust(void)
{
    static char buf[20000];
    char word[4000];
    const char *p;
    size_t n = 0, i;
    int k;
    IvxEngine *e;
    Out o;

    /* random bytes, fed raw: German reaches the dur_point guard (src/opstart.c), am, du and
     * sp the text compaction guard (src/tpchar.c) */
    for (k = 0; k < 12; k++) {
        lcg = (uint32_t)k * 31u + 1u;
        for (i = 0; i < sizeof buf; i++)
            buf[i] = (char)rnd8();
        robust_run(codes[k], buf, sizeof buf, "random bytes");
    }
    /* one unbroken "word" of about 3,300 letters (the letters of SW; a digit would end the
     * token): it runs into the text buffer guard (src/tpchar.c) */
    while (n < 3300)
        for (p = SW; *p && n < sizeof word - 2; p++)
            if ((*p >= 'a' && *p <= 'z') || (*p >= 'A' && *p <= 'Z') || (unsigned char)*p >= 0xc0)
                word[n++] = *p;
    word[n++] = '.';
    for (k = 0; k < 12; k++)
        robust_run(codes[k], word, n, "long word");
    /* voices 2 and 4 drive K1/K2 below the frequency table in 9 languages (the resonator
     * guard in src/synth.c; the port could fault reading before the table) */
    for (k = 0; k < 24; k++) {
        e = engine(codes[k / 2], IVX_ENC_CP1252);
        memset(&o, 0, sizeof o);
        CHECK(ivx_set(e, IVX_VOICE, k % 2 ? 4 : 2) == 0, "%s voice: set", codes[k / 2]);
        speak(e, strcmp(codes[k / 2], "sw") == 0 ? SW
                 : "This is a test of the speech synthesizer. The time is 12.30 and the weather is fine.", 0);
        ivx_flush(e);
        drain(e, 441, &o);
        CHECK(o.n > 22050 && count_events(&o, IVX_EV_DONE) == 1, "%s voice %d: %ld samples, %ld DONE",
              codes[k / 2], k % 2 ? 4 : 2, o.n, count_events(&o, IVX_EV_DONE));
        out_free(&o);
        ivx_destroy(e);
    }
    /* Danish phonetic "2 in text mode 12: the M5 REPL of 23 phonemes whose count wraps
     * (src/cwelem.c guard; the DLL crashes at 0x10021942) ends with status 0x10 */
    e = engine("da", IVX_ENC_CP1252);
    memset(&o, 0, sizeof o);
    CHECK(ivx_set(e, IVX_TEXT_MODE, 12) == 0 && ivx_feed_phonetic(e, "\"2", 2) == 0 && ivx_flush(e) == 0,
          "da phonetic quote: feed");
    drain(e, 4096, &o);
    CHECK(count_events(&o, IVX_EV_DONE) == 1, "da phonetic quote: %ld DONE", count_events(&o, IVX_EV_DONE));
    for (i = 0; i < (size_t)o.nev && !(o.ev[i].type == IVX_EV_ERROR && o.ev[i].value == 0x10); i++)
        ;
    CHECK(i < (size_t)o.nev, "da phonetic quote: no error event 0x10");
    out_free(&o);
    ivx_destroy(e);
}

/* ---- dictionaries ---- */

static const char AM[] = "This is a test of the speech synthesizer. The time is 12.30 and the weather is fine.";

typedef struct {
    const char *word, *rep;
    int flags;
} DictDef;

/* add entries to an engine */
static void dict_defs(IvxEngine *e, const DictDef *d, int nd)
{
    int k;

    for (k = 0; k < nd; k++)
        CHECK(ivx_dict_add(e, d[k].word, d[k].rep, d[k].flags) == 0, "ivx_dict_add \"%s\" \"%s\" %d",
              d[k].word, d[k].rep, d[k].flags);
}

/* all events; text is fed whole */
static void render_dict(const char *code, int enc, const DictDef *d, int nd, const char *text, Out *o)
{
    IvxEngine *e = engine(code, enc);

    memset(o, 0, sizeof *o);
    CHECK(ivx_set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0, "event mask");
    dict_defs(e, d, nd);
    CHECK(ivx_feed(e, text, strlen(text)) == 0 && ivx_flush(e) == 0, "feed");
    drain(e, 441, o);
    ivx_destroy(e);
}

/* text with the entries sounds exactly like expect without them (same events too), and
 * with differs = 1, unlike text without them */
static void dict_same(const char *code, int enc, const DictDef *d, int nd, const char *text,
                      const char *expect, int differs, const char *what)
{
    Out a, b, c;

    render_dict(code, enc, d, nd, text, &a);
    render_dict(code, enc, NULL, 0, expect, &b);
    CHECK(a.n > 0 && same_audio(&a, &b) && same_events(&a, &b), "%s: %ld samples, expected %ld", what,
          a.n, b.n);
    CHECK(count_events(&a, IVX_EV_ERROR) == 0 && count_events(&a, IVX_EV_DONE) == 1, "%s: %ld errors",
          what, count_events(&a, IVX_EV_ERROR));
    if (differs) {
        render_dict(code, enc, NULL, 0, text, &c);
        CHECK(!same_audio(&a, &c), "%s: the entry changed nothing", what);
        out_free(&c);
    }
    out_free(&a);
    out_free(&b);
}

static void test_dict_text(void)
{
    static const DictDef usa[] = {{"USA", "U S A", IVX_DICT_TEXT}};
    static const DictDef och[] = {{"och", "eller", IVX_DICT_TEXT}};
    static const DictDef the[] = {{"the", "a", IVX_DICT_TEXT}};

    dict_same("sw", IVX_ENC_CP1252, usa, 1, "Jag bor i USA nu.", "Jag bor i U S A nu.", 1, "sw USA");
    dict_same("am", IVX_ENC_CP1252, usa, 1, "I live in the USA now.", "I live in the U S A now.", 1, "am USA");
    dict_same("sw", IVX_ENC_CP1252, och, 1, "Kalle och Lisa kommer.", "Kalle eller Lisa kommer.", 1, "sw och");
    dict_same("am", IVX_ENC_CP1252, the, 1, AM,
              "This is a test of a speech synthesizer. a time is 12.30 and a weather is fine.", 1, "am the");
}

static void test_dict_phonetic(void)
{
    static const DictDef hej[] = {{"hej", PHON_SYM, IVX_DICT_PHONETIC}};
    IvxEngine *e;
    Out a, b;

    render_dict("sw", IVX_ENC_CP1252, hej, 1, "Jag sa hej till dig.", &a);
    e = engine("sw", IVX_ENC_CP1252);
    memset(&b, 0, sizeof b);
    CHECK(ivx_set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0, "event mask");
    CHECK(ivx_feed(e, "Jag sa ", 7) == 0 && ivx_feed_phonetic(e, PHON_SYM, strlen(PHON_SYM)) == 0
          && ivx_feed(e, " till dig.", 10) == 0 && ivx_flush(e) == 0, "phonetic feed");
    drain(e, 441, &b);
    ivx_destroy(e);
    CHECK(a.n > 0 && same_audio(&a, &b) && same_events(&a, &b), "phonetic entry: %ld samples, expected %ld",
          a.n, b.n);
    out_free(&a);
    out_free(&b);
}

static void test_dict_case(void)
{
    static const DictDef usa[] = {{"usa", "U S A", IVX_DICT_TEXT}};
    static const DictDef us[] = {{"US", "United States", IVX_DICT_MATCH_CASE}};
    static const DictDef both[] = {{"us", "you", IVX_DICT_TEXT}, {"US", "United States", IVX_DICT_MATCH_CASE}};
    static const DictDef aa[] = {{"\xe5sa", "Lisa", IVX_DICT_TEXT}};                 /* "asa", a-ring */
    static const DictDef oe[] = {{"\x9cuvre", "chose", IVX_DICT_TEXT}};             /* "oeuvre", oe ligature */
    static const DictDef u8[] = {{"h\xc3\xa4r", "d\xc3\xa4r", IVX_DICT_TEXT}};      /* "har" -> "dar", UTF-8 */
    static const DictDef cp[] = {{"h\xe4r", "d\xe4r", IVX_DICT_TEXT}};              /* the same in cp1252 */
    Out a, b;

    dict_same("am", IVX_ENC_CP1252, usa, 1, "USA and Usa and usa.", "U S A and U S A and U S A.", 1,
              "case-insensitive");
    dict_same("am", IVX_ENC_CP1252, us, 1, "The US and us.", "The United States and us.", 1, "exact case");
    dict_same("am", IVX_ENC_CP1252, both, 2, "The US and us and Us.", "The United States and you and you.", 1,
              "exact before case-insensitive");
    dict_same("sw", IVX_ENC_CP1252, aa, 1, "Hej \xc5SA och \xc5sa.", "Hej Lisa och Lisa.", 1, "fold a-ring");
    /* French keeps the oe ligature (0x8c 0x9c) inside a word */
    dict_same("fr", IVX_ENC_CP1252, oe, 1, "Une \x8cuvre et une \x9cuvre.", "Une chose et une chose.", 1,
              "fold oe");
    render_dict("sw", IVX_ENC_UTF8, u8, 1, SW_UTF8, &a);
    render_dict("sw", IVX_ENC_CP1252, cp, 1, SW, &b);
    CHECK(a.n > 0 && same_audio(&a, &b) && same_events(&a, &b), "UTF-8 entry differs from cp1252");
    out_free(&a);
    out_free(&b);
}

static void test_dict_final(void)
{
    static const DictDef cat[] = {{"cat", "cat dog", IVX_DICT_TEXT}};
    static const DictDef chain[] = {{"one", "two", IVX_DICT_TEXT}, {"two", "three", IVX_DICT_TEXT}};
    static const DictDef stop[] = {{"cat", "cat. The dog", IVX_DICT_TEXT}, {"dog", "puppy", IVX_DICT_TEXT}};
    static const char S1[] = "The cat sat on the mat with another cat and a big cat. ";
    static const char S2[] = "The cat dog sat on the mat with another cat dog and a big cat dog. ";
    static char text[4000], expect[6000];
    int k;

    dict_same("am", IVX_ENC_CP1252, cat, 1, "I see a cat now.", "I see a cat dog now.", 1, "cat -> cat dog");
    dict_same("am", IVX_ENC_CP1252, chain, 2, "I have one and two.", "I have two and three.", 1,
              "one -> two -> three");
    /* many sentences: the engine's text buffer is compacted many times, and the spans must
     * follow their text or "cat dog" would grow again */
    text[0] = expect[0] = 0;
    for (k = 0; k < 40; k++) {
        strcat(text, S1);
        strcat(expect, S2);
    }
    dict_same("am", IVX_ENC_CP1252, cat, 1, text, expect, 1, "spans across compaction");
    /* a replacement that ends a sentence: the engine compacts its buffer while the rest of
     * the replacement is unread, and "dog" must stay final */
    dict_same("am", IVX_ENC_CP1252, stop, 2, "I see a cat now. The dog is here.",
              "I see a cat. The dog now. The puppy is here.", 1, "span cut by compaction");
}

static void test_dict_limits(void)
{
    static char big[257], word[130], u8word[260];
    DictDef d[1];
    IvxEngine *e = engine("sw", IVX_ENC_CP1252);
    Out o;
    int k;

    /* three 255-byte replacements (one word and blanks, few rule nodes) in one sentence
     * overflow the engine's text buffer (0x2a0 bytes) */
    memset(big, ' ', 255);
    big[0] = 'l';
    big[1] = 'a';
    d[0].word = "x";
    d[0].rep = big;
    d[0].flags = IVX_DICT_TEXT;
    render_dict("sw", IVX_ENC_CP1252, d, 1, "Ett x x x.", &o);
    for (k = 0; k < o.nev && !(o.ev[k].type == IVX_EV_ERROR && o.ev[k].value == 0x71); k++)
        ;
    CHECK(k < o.nev, "a replacement that does not fit: no error 0x71");
    CHECK(count_events(&o, IVX_EV_DONE) == 1 && o.ev[o.nev - 1].type == IVX_EV_DONE, "no fit: DONE");
    out_free(&o);

    CHECK(ivx_dict_add(NULL, "a", "b", 0) == IVX_E_ARG, "add NULL engine");
    CHECK(ivx_dict_add(e, NULL, "b", 0) == IVX_E_ARG, "add NULL word");
    CHECK(ivx_dict_add(e, "a", NULL, 0) == IVX_E_ARG, "add NULL replacement");
    CHECK(ivx_dict_remove(NULL, "a", 0) == IVX_E_ARG, "remove NULL engine");
    CHECK(ivx_dict_remove(e, NULL, 0) == IVX_E_ARG, "remove NULL word");
    ivx_dict_clear(NULL);
    CHECK(ivx_dict_add(e, "", "b", 0) == IVX_E_RANGE, "empty word");
    CHECK(ivx_dict_add(e, "e-mail", "b", 0) == IVX_E_RANGE, "e-mail");
    CHECK(ivx_dict_add(e, "R2D2", "b", 0) == IVX_E_RANGE, "R2D2");
    CHECK(ivx_dict_add(e, "two words", "b", 0) == IVX_E_RANGE, "two words");
    CHECK(ivx_dict_add(e, "a.b", "b", 0) == IVX_E_RANGE, "a.b");
    CHECK(ivx_dict_add(e, "don't", "do not", 0) == 0, "apostrophe");
    CHECK(ivx_dict_add(e, "\x8akoda", "bil", 0) == 0, "0x8a (a letter in some charsets)");
    CHECK(ivx_dict_add(e, "a@b", "b", 0) == 0, "@ (inside a word in every language)");
    memset(word, 'a', 128);
    word[128] = 0;
    CHECK(ivx_dict_add(e, word, "b", 0) == IVX_E_RANGE, "128 letters");
    word[127] = 0;
    CHECK(ivx_dict_add(e, word, "b", 0) == 0, "127 letters");
    CHECK(ivx_dict_add(e, "a", "", 0) == IVX_E_RANGE, "empty replacement");
    CHECK(ivx_dict_add(e, "a", big, 0) == 0, "255-byte replacement");
    big[255] = 'a';
    CHECK(ivx_dict_add(e, "a", big, 0) == IVX_E_RANGE, "256-byte replacement");
    CHECK(ivx_dict_add(e, "a", "H[J", IVX_DICT_PHONETIC) == 0, "phonetic");
    CHECK(ivx_dict_add(e, "a", "H[J H[J", IVX_DICT_PHONETIC) == IVX_E_RANGE, "phonetic blank");
    CHECK(ivx_dict_add(e, "a", "H\x01J", IVX_DICT_PHONETIC) == IVX_E_RANGE, "phonetic escape");
    CHECK(ivx_dict_add(e, "a", "two words", IVX_DICT_FALLBACK) == IVX_E_RANGE, "fallback two words");
    CHECK(ivx_dict_add(e, "a", "b", IVX_DICT_FALLBACK | IVX_DICT_PHONETIC) == IVX_E_RANGE, "fallback phonetic");
    CHECK(ivx_dict_add(e, "a", "b", 8) == IVX_E_RANGE, "unknown flag");
    CHECK(ivx_dict_remove(e, "a", IVX_DICT_MATCH_CASE) == IVX_E_RANGE, "remove: other identity");
    CHECK(ivx_dict_remove(e, "A", IVX_DICT_PHONETIC) == 0, "remove (PHONETIC ignored, case folded)");
    CHECK(ivx_dict_remove(e, "a", 0) == IVX_E_RANGE, "remove twice");
    CHECK(ivx_dict_remove(e, "zzz", 0) == IVX_E_RANGE, "remove missing");
    CHECK(ivx_dict_remove(e, "e-mail", 0) == IVX_E_RANGE, "remove bad word");
    CHECK(ivx_dict_remove(e, "a", 8) == IVX_E_RANGE, "remove unknown flag");
    ivx_destroy(e);
    /* 127 letters after conversion, 254 bytes of UTF-8 */
    e = engine("sw", IVX_ENC_UTF8);
    for (k = 0; k < 127; k++) {
        u8word[2 * k] = (char)0xc3;
        u8word[2 * k + 1] = (char)0xa4;
    }
    u8word[254] = 0;
    CHECK(ivx_dict_add(e, u8word, "b", 0) == 0, "127 UTF-8 letters");
    ivx_destroy(e);
}

static void test_dict_clear(void)
{
    static const DictDef d[] = {{"och", "eller", 0}, {"test", "prov", IVX_DICT_MATCH_CASE}};
    IvxEngine *e;
    Out ref, o;
    int round;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &ref);
    /* round 0: ivx_dict_clear; round 1: removing the last entry */
    for (round = 0; round < 2; round++) {
        e = engine("sw", IVX_ENC_CP1252);
        dict_defs(e, d, round ? 1 : 2);
        if (round)
            CHECK(ivx_dict_remove(e, "och", 0) == 0, "remove the last entry");
        else
            ivx_dict_clear(e);
        memset(&o, 0, sizeof o);
        speak(e, SW, 0);
        ivx_flush(e);
        drain(e, 441, &o);
        CHECK(same_audio(&ref, &o) && same_events(&ref, &o), "an emptied dictionary changed the audio (%d)",
              round);
        out_free(&o);
        ivx_destroy(e);
    }
    out_free(&ref);
}

static void test_dict_fallback(void)
{
    static const DictDef blorf[] = {{"blorf", "plarf", IVX_DICT_FALLBACK}};
    static const DictDef aa[] = {{"blorf", "sn\xe5rp", IVX_DICT_FALLBACK}};         /* a-ring */
    static const DictDef known[] = {{"och", "plarf", IVX_DICT_FALLBACK}};
    static const DictDef exact[] = {{"Blorf", "plarf", IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE}};
    static const DictDef zorgle[] = {{"zorgle", "zoggle", IVX_DICT_FALLBACK}};
    static const DictDef over[] = {{"blorf", "plarf", IVX_DICT_FALLBACK}, {"blorf", "huset", IVX_DICT_TEXT}};

    dict_same("sw", IVX_ENC_CP1252, blorf, 1, "Det var en blorf i huset.", "Det var en plarf i huset.", 1,
              "sw fallback");
    dict_same("sw", IVX_ENC_CP1252, aa, 1, "Det var en blorf i huset.", "Det var en sn\xe5rp i huset.", 1,
              "sw fallback with a-ring");
    dict_same("am", IVX_ENC_CP1252, zorgle, 1, "I saw a zorgle today.", "I saw a zoggle today.", 1,
              "am fallback");
    /* the lexicon knows "och": a fallback entry changes nothing */
    dict_same("sw", IVX_ENC_CP1252, known, 1, "Kalle och Lisa kommer.", "Kalle och Lisa kommer.", 0,
              "fallback for a known word");
    dict_same("sw", IVX_ENC_CP1252, exact, 1, "Jag och Blorf och blorf.", "Jag och plarf och blorf.", 1,
              "exact-case fallback");
    /* an override comes before the fallback */
    dict_same("sw", IVX_ENC_CP1252, over, 2, "Det var en blorf i huset.", "Det var en huset i huset.", 1,
              "override before fallback");
}

static void test_dict_lifetime(void)
{
    static const DictDef d[] = {{"och", "eller", 0}, {"blorf", "plarf", IVX_DICT_FALLBACK}};
    static const char T[] = "Kalle och en blorf.", X[] = "Kalle eller en plarf.";
    static const char PRE[] = "Nu kommer en ny mening efter avbrottet.";
    IvxEngine *e;
    Out ref, o, pre;
    unsigned char *b;
    long size;
    short buf[441];
    int round, n;

    render("sw", IVX_ENC_CP1252, X, 0, 441, &ref);
    /* round 0: load am then sw again; round 1: a failed load; round 2: entries added after
     * the text was fed */
    for (round = 0; round < 3; round++) {
        e = engine("sw", IVX_ENC_CP1252);
        if (round < 2)
            dict_defs(e, d, 2);
        if (round == 0)
            CHECK(ivx_load_language(e, lang_path("am")) == 0 && ivx_load_language(e, lang_path("sw")) == 0,
                  "language switch");
        if (round == 1) {
            b = read_file(lang_path("ge"), &size);
            b[0x240] = b[0x241] = 'X';
            CHECK(ivx_load_language_mem(e, b, (size_t)size) == IVX_E_BADFILE, "failed load");
            free(b);
        }
        memset(&o, 0, sizeof o);
        speak(e, T, 0);
        ivx_flush(e);
        if (round == 2)
            dict_defs(e, d, 2);
        drain(e, 441, &o);
        CHECK(same_audio(&ref, &o), "entries lost (round %d)", round);
        out_free(&o);
        ivx_destroy(e);
    }
    out_free(&ref);
    /* ivx_cancel: the same cancelled text on two engines, then T with the entries against
     * X without them */
    for (round = 0; round < 2; round++) {
        e = engine("sw", IVX_ENC_CP1252);
        if (round == 0)
            dict_defs(e, d, 2);
        memset(&pre, 0, sizeof pre);
        speak(e, PRE, 0);
        ivx_flush(e);
        do
            n = read_some(e, buf, 441, &pre);
        while (n > 0 && pre.n < 441 * 20);
        ivx_cancel(e);
        memset(round ? &o : &ref, 0, sizeof o);
        (round ? &o : &ref)->base = (uint64_t)pre.n;
        speak(e, round ? X : T, 0);
        ivx_flush(e);
        drain(e, 441, round ? &o : &ref);
        out_free(&pre);
        ivx_destroy(e);
    }
    CHECK(ref.n > 0 && same_audio(&ref, &o), "entries lost by ivx_cancel");
    out_free(&ref);
    out_free(&o);
}

/* random bytes (as test_robust) with overrides, phonetic entries and fallbacks installed.
 * sp needs the span shift (dict_before_putc/apply_shift): without it the "o" replacement is
 * read again after a compaction and grows until the sentence after it is cut short */
static void test_dict_robust(void)
{
    static const DictDef d[] = {
        {"och", "eller", 0}, {"the", "a", 0}, {"und", "oder", 0}, {"de", "la", 0}, {"e", "et", 0},
        {"A", "H[J", IVX_DICT_PHONETIC | IVX_DICT_MATCH_CASE}, {"o", "o o o o o o o o", 0},
        {"a", "zorg", IVX_DICT_FALLBACK}, {"b", "blorf", IVX_DICT_FALLBACK},
        {"x", "xyzzy", IVX_DICT_FALLBACK}, {"q", "kv", IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE}};
    static char buf[20000];
    IvxEngine *e;
    Out o;
    size_t i;
    int k;

    for (k = 0; k < 12; k++) {
        lcg = (uint32_t)k * 31u + 7u;
        for (i = 0; i < sizeof buf; i++)
            buf[i] = (char)rnd8();
        e = engine(codes[k], IVX_ENC_CP1252);
        dict_defs(e, d, (int)(sizeof d / sizeof d[0]));
        memset(&o, 0, sizeof o);
        CHECK(ivx_feed(e, buf, sizeof buf) == 0 && ivx_flush(e) == 0, "%s: feed", codes[k]);
        drain(e, 4096, &o);
        CHECK(count_events(&o, IVX_EV_DONE) == 1, "%s dictionary, random bytes: %ld DONE", codes[k],
              count_events(&o, IVX_EV_DONE));
        o.base += (uint64_t)o.n;
        o.n = 0;
        o.nev = 0;
        speak(e, k == 11 ? SW : AM, 0);   /* am and sp say little of the Swedish sentence */
        ivx_flush(e);
        drain(e, 4096, &o);
        CHECK(o.n > 22050 && count_events(&o, IVX_EV_DONE) == 1, "%s dictionary: no speech after it (%ld samples)",
              codes[k], o.n);
        out_free(&o);
        ivx_destroy(e);
    }
}

int main(int argc, char **argv)
{
    if (argc > 1)
        bindir = argv[1];
    setvbuf(stdout, NULL, _IONBF, 0);
    test_lifecycle();
    test_settings();
    test_load();
    test_stream_errors();
    test_chunks();
    test_synthesize();
    test_encoding();
    test_busy();
    test_busy_in_core();
    test_interleave();
    test_load_failure();
    test_settings_effect();
    test_voice();
    test_marks();
    test_marks_coalesced();
    test_phonemes();
    test_cancel();
    test_robust();
    test_dict_text();
    test_dict_phonetic();
    test_dict_case();
    test_dict_final();
    test_dict_limits();
    test_dict_clear();
    test_dict_fallback();
    test_dict_lifetime();
    test_dict_robust();
    printf("api_test: %ld checks\n%ld failures\n", checks, failures);
    return failures != 0;
}
