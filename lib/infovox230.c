/* infovox230: the public API (include/infovox230.h) over the C port of Ivx230nt.dll.
 * Design: docs/superpowers/specs/2026-09-28-infovox230-library-api-design.md
 *
 * An engine owns one Core (tts_ctrl) and one mode-0 synth that pulls op_next_frame through
 * frame_cb. ivx_feed, ivx_mark, the queued settings and ivx_flush append to one queue;
 * ivx_read runs it through tp_putc with the engine's resend protocol (0x10012345) and
 * plays each utterance in 441-sample blocks, the loop tests/tts_cmp.c checks against the
 * DLL sample for sample.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "infovox230.h"
#include "tts.h"
#include "synth.h"
#include "ivxdict.h"

#define BLOCK   441                   /* samples per synt_generate call */
#define NPOOL   900                   /* pool nodes, as the engine creates (0x100104e6) */
#define NPARAMS (IVX_EVENT_MASK + 1)
#define NQUEUED (IVX_FRAME_RATE + 1)  /* parameters applied in stream order */

enum { Q_CHAR, Q_MARK, Q_SET, Q_FLUSH };

typedef struct {
    unsigned char kind;
    unsigned char ch;       /* Q_CHAR */
    short param;            /* Q_SET */
    int value;              /* Q_SET */
    uint32_t id;            /* Q_MARK */
} QItem;

typedef struct {
    uint16_t serial;        /* the Core's bookmark (tts_c20_mark) */
    uint32_t id;            /* the caller's id */
} Mark;

/* a UTF-8 decoder (Unicode table 3-7) */
typedef struct {
    uint32_t cp;            /* code point so far */
    int need;               /* continuation bytes still expected */
    unsigned char lo, hi;   /* allowed range of the next continuation byte */
} U8Dec;

/* a growable ring of fixed-size elements */
typedef struct {
    char *a;
    size_t head, len, cap, size;
} Fifo;

struct IvxEngine {
    Core *core;
    Synth *syn;
    unsigned char *ivx;         /* the bound language: file + 16 zero bytes, swapped */
    int val[NPARAMS];           /* latest value set per parameter */
    int applied[NQUEUED];       /* queued parameters as last applied */
    int qcount[NQUEUED];        /* settings of each parameter still in the queue */
    Fifo q;                     /* QItem */
    size_t qtext;               /* queue items other than settings */
    int have_pending;           /* a char (or the 0x17 of a flush) waits for tp_putc */
    int tries;
    int pending_flush;
    unsigned char pending_ch;
    U8Dec u8;                   /* caller text in UTF-8 */
    unsigned char last_in;      /* previous input char (LF after CR stays LF) */
    int flushed;                /* a flush went in since the last DONE */
    int in_text;                /* chars or marks went into the Core since the last flush */
    short stage[BLOCK];
    int stage_pos, stage_len;
    uint64_t delivered;         /* samples handed out by ivx_read */
    uint64_t gen_pos;           /* samples generated (delivered + left in stage) */
    uint64_t next_frame_pos;    /* where the next synth frame starts */
    uint64_t frame_pos;         /* where the frame being built starts */
    uint16_t serial;            /* last bookmark serial sent to the Core */
    Fifo marks;                 /* Mark: sent to the Core, not reported yet */
    Fifo ev;                    /* IvxEvent */
    unsigned long dones;        /* DONE points reached, whatever the event mask */
    int oom;                    /* an allocation failed inside a callback */
    IvxDict dict;               /* lib/ivxdict.c */
};

static void *fifo_at(Fifo *f, size_t k)
{
    return f->a + (f->head + k) % f->cap * f->size;
}

static int fifo_push(Fifo *f, const void *x)
{
    if (f->len == f->cap) {
        size_t cap = f->cap ? 2 * f->cap : 64, k;
        char *a = malloc(cap * f->size);

        if (a == NULL)
            return IVX_E_NOMEM;
        for (k = 0; k < f->len; k++)
            memcpy(a + k * f->size, fifo_at(f, k), f->size);
        free(f->a);
        f->a = a;
        f->head = 0;
        f->cap = cap;
    }
    memcpy(f->a + (f->head + f->len) % f->cap * f->size, x, f->size);
    f->len++;
    return 0;
}

static void fifo_pop(Fifo *f, void *x)
{
    if (x != NULL)
        memcpy(x, fifo_at(f, 0), f->size);
    f->head = (f->head + 1) % f->cap;
    f->len--;
}

static void fifo_clear(Fifo *f)
{
    f->head = f->len = 0;
}

/* nothing queued but settings, no char pending, no unflushed text in the Core (a load's
 * tp_reset_text would drop it), no audio playing or staged */
static int idle(const IvxEngine *e)
{
    return e->qtext == 0 && !e->have_pending && !e->in_text && !e->core->out_active
           && e->stage_pos == e->stage_len;
}

/* the synth's frame source: op_next_frame, noting where each frame starts. synt_generate
 * sets frame_left to the frame's length just before this call (src/synth.c:179) */
static short *frame_cb(void *arg)
{
    IvxEngine *e = arg;

    e->frame_pos = e->next_frame_pos;
    e->next_frame_pos += (uint64_t)(unsigned short)e->syn->frame_left;
    return op_next_frame(e->core);
}

int ivx_version(void)
{
    return IVX_VERSION;
}

const char *ivx_strerror(int err)
{
    switch (err) {
    case IVX_OK:        return "no error";
    case IVX_E_NOMEM:   return "out of memory";
    case IVX_E_IO:      return "cannot read the language file";
    case IVX_E_BADFILE: return "not a usable .ivx file";
    case IVX_E_BUSY:    return "engine busy (text queued or audio playing)";
    case IVX_E_NOLANG:  return "no language loaded";
    case IVX_E_RANGE:   return "unknown parameter or value out of range";
    case IVX_E_ARG:     return "invalid argument";
    }
    return "unknown error";
}

static void cb_bookmark(void *arg, int v);
static void cb_phonemes(void *arg, char *name);
static void cb_status(void *arg, int st);
static void dict_install(IvxEngine *e);

IvxEngine *ivx_create(int *err)
{
    IvxEngine *e = calloc(1, sizeof *e);

    if (e == NULL || tts_ctrl(&e->core, TTS_CREATE, (void *)NPOOL) != 0 || e->core == NULL
        || synt_ctrl((Synth *)&e->syn, 0, NULL) != 0 || e->syn == NULL) {
        ivx_destroy(e);
        if (err != NULL)
            *err = IVX_E_NOMEM;
        return NULL;
    }
    e->q.size = sizeof(QItem);
    e->marks.size = sizeof(Mark);
    e->ev.size = sizeof(IvxEvent);
    synt_ctrl(e->syn, SYNT_SET_FRAMECB, (void *)frame_cb);
    synt_ctrl(e->syn, SYNT_SET_FRAMEARG, e);
    synt_ctrl(e->syn, SYNT_SAMPLE_RATE, (void *)22050);
    tts_ctrl(e->core, TTS_CB_ARG, e);
    tts_ctrl(e->core, TTS_CB_BOOKMARK, (void *)cb_bookmark);
    tts_ctrl(e->core, TTS_CB_PHONEMES, (void *)cb_phonemes);
    tts_ctrl(e->core, TTS_CB_STATUS, (void *)cb_status);
    e->val[IVX_VOICE] = 0;
    e->val[IVX_RATE] = tts_ctrl(e->core, TTS_RATE, (void *)-1);
    e->val[IVX_PITCH] = tts_ctrl(e->core, TTS_PITCH, (void *)-1);
    e->val[IVX_FD] = tts_ctrl(e->core, TTS_FD, (void *)-1);
    e->val[IVX_FRAME_RATE] = 100;
    e->val[IVX_SAMPLE_RATE] = 22050;
    e->val[IVX_TEXT_MODE] = 8;
    e->val[IVX_CHARSET] = 2;
    e->val[IVX_INPUT_ENCODING] = IVX_ENC_UTF8;
    e->val[IVX_EVENT_MASK] = IVX_EVMASK_DEFAULT;
    memcpy(e->applied, e->val, sizeof e->applied);
    if (err != NULL)
        *err = IVX_OK;
    return e;
}

void ivx_destroy(IvxEngine *e)
{
    if (e == NULL)
        return;
    if (e->syn != NULL)
        synt_ctrl(e->syn, SYNT_DESTROY, NULL);
    if (e->core != NULL)
        tts_ctrl(e->core, TTS_DESTROY, NULL);
    free(e->ivx);
    free(e->q.a);
    free(e->marks.a);
    free(e->ev.a);
    dict_clear(&e->dict);
    free(e);
}

static uint32_t be32(const unsigned char *p)
{
    return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3];
}

/* the engine's load sequence (CLangResource::Load 0x10016490): swap, charset, bind, mode */
int ivx_load_language_mem(IvxEngine *e, const void *ivx, size_t n)
{
    static const int offsets[] = {3, 5, 6, 8, 9, 10, 11, 12, 13, 14, 15};   /* H[i] that are offsets */
    const unsigned char *h = (const unsigned char *)ivx + 0x200;
    unsigned char *b;
    size_t k;
    int r;

    if (e == NULL || ivx == NULL)
        return IVX_E_ARG;
    if (!idle(e))
        return IVX_E_BUSY;
    if (n < 0x200 + sizeof(IvxBody))
        return IVX_E_BADFILE;
    for (k = 0; k < sizeof offsets / sizeof offsets[0]; k++)
        if (be32(h + 4 * offsets[k]) > n - 0x200)
            return IVX_E_BADFILE;
    if (n > SIZE_MAX - 16)
        return IVX_E_NOMEM;
    b = malloc(n + 16);
    if (b == NULL)
        return IVX_E_NOMEM;
    memcpy(b, ivx, n);
    memset(b + n, 0, 16);   /* lex_lookup can read one byte past the lexicon */
    tts_ctrl(NULL, TTS_IVX_SWAP, b + 0x200);
    tts_ctrl(e->core, TTS_CHARSET, (void *)(intptr_t)e->val[IVX_CHARSET]);
    r = tts_ctrl(e->core, TTS_BIND, b + 0x200);
    dict_install(e);   /* reinstall cb_excdict: harmless whether or not this bind cleared it */
    dict_reset(&e->dict);   /* tts_c03_bind resets the text even when the bind fails */
    if (r != 0) {      /* the old language stays bound */
        free(b);
        return IVX_E_BADFILE;
    }
    free(e->ivx);
    e->ivx = b;
    fifo_clear(&e->marks);   /* the bind reset the Core's text; no mark can be reached now */
    tts_ctrl(e->core, TTS_MODE, (void *)(intptr_t)e->val[IVX_TEXT_MODE]);
    return IVX_OK;
}

int ivx_load_language(IvxEngine *e, const char *path)
{
    FILE *f;
    unsigned char *b;
    long size;
    int r;

    if (e == NULL || path == NULL)
        return IVX_E_ARG;
    f = fopen(path, "rb");
    if (f == NULL)
        return IVX_E_IO;
    if (fseek(f, 0, SEEK_END) != 0 || (size = ftell(f)) < 0 || fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return IVX_E_IO;
    }
    b = malloc((size_t)size + 1);
    if (b == NULL) {
        fclose(f);
        return IVX_E_NOMEM;
    }
    r = fread(b, 1, (size_t)size, f) == (size_t)size ? ivx_load_language_mem(e, b, (size_t)size)
                                                     : IVX_E_IO;
    fclose(f);
    free(b);
    return r;
}

int ivx_set(IvxEngine *e, int param, int value)
{
    static const int lo[NPARAMS] = {0, 0, 0, 0, 2, 8000, 0, 0, 0, 0};
    static const int hi[NPARAMS] = {9, 100, 100, 100, 1000, 48000, 0x1ff, 0x1f, 1, IVX_EVMASK_ALL};
    QItem it = {Q_SET, 0, 0, 0, 0};

    if (e == NULL)
        return IVX_E_ARG;
    if (param < 0 || param >= NPARAMS || value < lo[param] || value > hi[param])
        return IVX_E_RANGE;
    if (param < NQUEUED) {
        it.param = (short)param;
        it.value = value;
        if (fifo_push(&e->q, &it) != 0)
            return IVX_E_NOMEM;
        e->qcount[param]++;
        e->val[param] = value;
        return IVX_OK;
    }
    if (!idle(e))
        return IVX_E_BUSY;
    switch (param) {
    case IVX_SAMPLE_RATE:
        if (value != e->val[param])
            synt_ctrl(e->syn, SYNT_SAMPLE_RATE, (void *)(intptr_t)value);
        break;
    case IVX_TEXT_MODE:   /* applied now if a language is bound, else by the load */
        if (e->ivx != NULL)
            tts_ctrl(e->core, TTS_MODE, (void *)(intptr_t)value);
        break;
    case IVX_CHARSET:
        if (e->ivx != NULL)
            tts_ctrl(e->core, TTS_CHARSET, (void *)(intptr_t)value);
        break;
    case IVX_INPUT_ENCODING:
        e->u8.need = 0;
        break;
    case IVX_EVENT_MASK:   /* out_flags bit 1: phoneme names during synthesis */
        tts_ctrl(e->core, TTS_OUT_FLAGS, (void *)(intptr_t)((value & (1 << IVX_EV_PHONEME)) ? 2 : 0));
        break;
    }
    e->val[param] = value;
    return IVX_OK;
}

int ivx_get(IvxEngine *e, int param, int *value)
{
    if (e == NULL || value == NULL)
        return IVX_E_ARG;
    if (param < 0 || param >= NPARAMS)
        return IVX_E_RANGE;
    *value = e->val[param];
    return IVX_OK;
}

/* ---- events ---- */

static void push_event(IvxEngine *e, int type, uint32_t value, uint64_t sample)
{
    IvxEvent ev;

    if (!(e->val[IVX_EVENT_MASK] & (1 << type)))
        return;
    ev.type = type;
    ev.value = value;
    ev.sample = sample;
    if (fifo_push(&e->ev, &ev) != 0)
        e->oom = 1;
}

/* ---- text input ---- */

/* Unicode code points of cp1252 0x80..0x9f (0 = unassigned) */
static const uint16_t cp1252_hi[32] = {
    0x20ac, 0, 0x201a, 0x0192, 0x201e, 0x2026, 0x2020, 0x2021,
    0x02c6, 0x2030, 0x0160, 0x2039, 0x0152, 0, 0x017d, 0,
    0, 0x2018, 0x2019, 0x201c, 0x201d, 0x2022, 0x2013, 0x2014,
    0x02dc, 0x2122, 0x0161, 0x203a, 0x0153, 0, 0x017e, 0x0178};

static int push_char(IvxEngine *e, unsigned char ch)
{
    QItem it = {Q_CHAR, 0, 0, 0, 0};

    it.ch = ch;
    if (fifo_push(&e->q, &it) != 0)
        return IVX_E_NOMEM;
    e->qtext++;
    return IVX_OK;
}

/* one cp1252 char of caller text: the escape chars are removed (0x01 esc_mark, 0x02 esc_phon,
 * 0x17 end of text), and LF becomes CR unless it follows a CR, as the old ivxsay did */
static int put_byte(IvxEngine *e, unsigned char b)
{
    unsigned char prev = e->last_in;

    e->last_in = b;
    if (b == 0x01 || b == 0x02 || b == 0x17)
        return IVX_OK;
    if (b == '\n' && prev != '\r')
        b = '\r';
    return push_char(e, b);
}

/* a code point as cp1252; a blank outside it */
static unsigned char cp1252_of(uint32_t cp)
{
    int k;

    if (cp < 0x80 || (cp >= 0xa0 && cp <= 0xff))
        return (unsigned char)cp;
    for (k = 0; k < 32; k++)
        if (cp1252_hi[k] == cp)
            return (unsigned char)(0x80 + k);
    return ' ';
}

/* one byte of UTF-8; returns how many code points it completes (0-2): a bad or
 * unfinished sequence becomes one blank, and the byte that ended it starts afresh */
static int u8_byte(U8Dec *d, unsigned char b, uint32_t cp[2])
{
    int n = 0;

    if (d->need != 0) {
        if (b >= d->lo && b <= d->hi) {
            d->cp = d->cp << 6 | (b & 0x3f);
            d->lo = 0x80;
            d->hi = 0xbf;
            if (--d->need != 0)
                return 0;
            cp[0] = d->cp;
            return 1;
        }
        d->need = 0;
        cp[n++] = ' ';
    }
    d->lo = 0x80;
    d->hi = 0xbf;
    if (b < 0x80) {
        cp[n++] = b;
    } else if (b >= 0xc2 && b <= 0xdf) {
        d->need = 1;
        d->cp = b & 0x1f;
    } else if (b >= 0xe0 && b <= 0xef) {
        d->need = 2;
        d->cp = b & 0x0f;
        if (b == 0xe0)
            d->lo = 0xa0;
        if (b == 0xed)
            d->hi = 0x9f;
    } else if (b >= 0xf0 && b <= 0xf4) {
        d->need = 3;
        d->cp = b & 0x07;
        if (b == 0xf0)
            d->lo = 0x90;
        if (b == 0xf4)
            d->hi = 0x8f;
    } else {
        cp[n++] = ' ';
    }
    return n;
}

static int put_utf8(IvxEngine *e, unsigned char b)
{
    uint32_t cp[2];
    int n = u8_byte(&e->u8, b, cp), k, r = IVX_OK;

    for (k = 0; k < n && r == IVX_OK; k++)
        r = put_byte(e, cp1252_of(cp[k]));
    return r;
}

/* an unfinished UTF-8 sequence becomes a space */
static int end_utf8(IvxEngine *e)
{
    if (e->u8.need == 0)
        return IVX_OK;
    e->u8.need = 0;
    return put_byte(e, ' ');
}

int ivx_feed(IvxEngine *e, const char *text, size_t len)
{
    const unsigned char *p = (const unsigned char *)text;
    size_t k;
    int r = IVX_OK;

    if (e == NULL || (text == NULL && len != 0))
        return IVX_E_ARG;
    if (e->ivx == NULL)
        return IVX_E_NOLANG;
    for (k = 0; k < len && r == IVX_OK; k++)
        r = e->val[IVX_INPUT_ENCODING] == IVX_ENC_UTF8 ? put_utf8(e, p[k]) : put_byte(e, p[k]);
    return r;
}

/* esc_phon, the symbols, esc_phon: the engine's phonetic input (REFERENCE §5.5.1) */
int ivx_feed_phonetic(IvxEngine *e, const char *sym, size_t len)
{
    const unsigned char *p = (const unsigned char *)sym;
    size_t k;
    int r;

    if (e == NULL || (sym == NULL && len != 0))
        return IVX_E_ARG;
    if (e->ivx == NULL)
        return IVX_E_NOLANG;
    if ((r = end_utf8(e)) != 0 || (r = push_char(e, e->core->esc_phon)) != 0)
        return r;
    for (k = 0; k < len && r == IVX_OK; k++)
        r = put_byte(e, p[k]);
    return r != IVX_OK ? r : push_char(e, e->core->esc_phon);
}

int ivx_mark(IvxEngine *e, uint32_t id)
{
    QItem it = {Q_MARK, 0, 0, 0, 0};

    if (e == NULL)
        return IVX_E_ARG;
    if (e->ivx == NULL)
        return IVX_E_NOLANG;
    it.id = id;
    if (fifo_push(&e->q, &it) != 0)
        return IVX_E_NOMEM;
    e->qtext++;
    return IVX_OK;
}

int ivx_flush(IvxEngine *e)
{
    QItem it = {Q_FLUSH, 0, 0, 0, 0};
    int r;

    if (e == NULL)
        return IVX_E_ARG;
    if (e->ivx == NULL)
        return IVX_E_NOLANG;
    if ((r = end_utf8(e)) != 0)
        return r;
    if (fifo_push(&e->q, &it) != 0)
        return IVX_E_NOMEM;
    e->qtext++;
    return IVX_OK;
}

/* ---- the read loop ---- */

static void apply(IvxEngine *e, int param, int value)
{
    static const int cmd[] = {TTS_SET_VOICE, TTS_RATE, TTS_PITCH, TTS_FD};
    int p;

    e->qcount[param]--;
    e->applied[param] = value;
    if (param == IVX_FRAME_RATE) {
        synt_ctrl(e->syn, SYNT_FRAME_RATE, (void *)(intptr_t)value);
        return;
    }
    tts_ctrl(e->core, cmd[param], (void *)(intptr_t)value);
    if (param == IVX_VOICE)   /* a voice brings its own rate, pitch and FD */
        for (p = IVX_RATE; p <= IVX_FD; p++) {
            e->applied[p] = tts_ctrl(e->core, cmd[p], (void *)-1);
            if (e->qcount[p] == 0)
                e->val[p] = e->applied[p];
        }
}

/* one queue item, or one more try of the pending char. The engine's protocol
 * (0x10012345): after 2 (synth busy) the same char goes again once the audio has played,
 * at most 8 tries in all; the read loop plays the audio before calling this again */
static void step(IvxEngine *e)
{
    QItem it;
    Mark m;
    int r;

    if (!e->have_pending) {
        fifo_pop(&e->q, &it);
        if (it.kind == Q_SET) {
            apply(e, it.param, it.value);
            return;
        }
        e->qtext--;
        if (it.kind != Q_FLUSH)
            e->in_text = 1;
        if (it.kind == Q_MARK) {
            /* the SAPI engine's way (0x10011304, 0x10012a85): a 16-bit serial through
             * TTS_MARK, mapped back when cb_bookmark reports it. tts_c20_mark adds 1, and a
             * zero mark is never reported, so serials run 1..0xffff */
            e->serial = (uint16_t)(e->serial % 0xffff + 1);
            m.serial = e->serial;
            m.id = it.id;
            if (fifo_push(&e->marks, &m) != 0)
                e->oom = 1;
            tts_ctrl(e->core, TTS_MARK, (void *)(intptr_t)(e->serial - 1));
            return;
        }
        e->pending_flush = it.kind == Q_FLUSH;
        e->pending_ch = e->pending_flush ? 0x17 : it.ch;
        e->have_pending = 1;
        e->tries = 0;
    }
    dict_before_putc(&e->dict, e->core);
    r = tp_putc(e->core, e->pending_ch);
    dict_after_putc(&e->dict, e->core);
    if (r == 2 && ++e->tries < 8)
        return;
    e->have_pending = 0;
    if (e->pending_flush) {
        e->flushed = 1;
        e->in_text = 0;
    }
}

int ivx_read(IvxEngine *e, short *pcm, int max)
{
    Mark m;
    int n = 0, k;

    if (e == NULL || (pcm == NULL && max > 0))
        return IVX_E_ARG;
    if (max < 0)
        return IVX_E_RANGE;
    if (e->ivx == NULL)
        return IVX_E_NOLANG;
    if (e->oom) {
        e->oom = 0;
        return IVX_E_NOMEM;
    }
    while (n < max) {
        if (e->stage_pos < e->stage_len) {
            k = e->stage_len - e->stage_pos;
            if (k > max - n)
                k = max - n;
            memcpy(pcm + n, e->stage + e->stage_pos, (size_t)k * sizeof *pcm);
            e->stage_pos += k;
            e->delivered += (uint64_t)k;
            n += k;
        } else if (e->core->out_active) {
            synt_generate(e->syn, e->stage, BLOCK);
            e->stage_pos = 0;
            e->stage_len = BLOCK;
            e->gen_pos += BLOCK;
        } else if (e->have_pending || e->q.len != 0) {
            step(e);
        } else {
            if (e->flushed) {
                /* marks with no segments after them (end of text) are reached now */
                while (e->marks.len != 0) {
                    fifo_pop(&e->marks, &m);
                    push_event(e, IVX_EV_MARK, m.id, e->gen_pos);
                }
                push_event(e, IVX_EV_DONE, 0, e->gen_pos);
                e->flushed = 0;
                e->dones++;
            }
            break;
        }
    }
    return n;
}

/* events are released once the audio up to them has been read */
int ivx_next_event(IvxEngine *e, IvxEvent *ev)
{
    if (e == NULL || ev == NULL)
        return IVX_E_ARG;
    if (e->ev.len == 0 || ((IvxEvent *)fifo_at(&e->ev, 0))->sample > e->delivered)
        return 0;
    fifo_pop(&e->ev, ev);
    return 1;
}

/* the engine's stop (0x100119b1-0x100119e1): reset 1 (text), reset 2 (output), then drop
 * the synth's current frame */
void ivx_cancel(IvxEngine *e)
{
    int p;

    if (e == NULL)
        return;
    fifo_clear(&e->q);
    fifo_clear(&e->marks);
    fifo_clear(&e->ev);
    e->qtext = 0;
    e->have_pending = 0;
    e->flushed = 0;
    e->in_text = 0;
    e->oom = 0;
    e->u8.need = 0;
    e->last_in = 0;
    e->stage_pos = e->stage_len = 0;
    for (p = 0; p < NQUEUED; p++) {
        e->qcount[p] = 0;
        e->val[p] = e->applied[p];
    }
    dict_reset(&e->dict);
    tts_ctrl(e->core, TTS_RESET, (void *)1);
    tts_ctrl(e->core, TTS_RESET, (void *)2);
    synt_ctrl(e->syn, SYNT_FLUSH, NULL);
    e->gen_pos = e->delivered;
    e->next_frame_pos = e->delivered;
}

int ivx_synthesize(IvxEngine *e, const char *text, size_t len, IvxAudioCb cb, void *user)
{
    short buf[BLOCK];
    IvxEvent evs[32];
    unsigned long dones;
    int n, n_last, nev, r;

    if (e == NULL || cb == NULL || (text == NULL && len != 0))
        return IVX_E_ARG;
    if (e->ivx == NULL)
        return IVX_E_NOLANG;
    dones = e->dones;
    if ((r = ivx_feed(e, text, len)) != 0 || (r = ivx_flush(e)) != 0)
        return r;
    /* ends at the DONE, or when a read finds the engine idle without one (cb called
     * ivx_cancel); the events of the last read still reach cb */
    do {
        n = n_last = ivx_read(e, buf, BLOCK);
        if (n < 0)
            return n;
        do {
            for (nev = 0; nev < 32 && ivx_next_event(e, &evs[nev]) == 1; nev++)
                ;
            if ((n > 0 || nev > 0) && cb(user, buf, n, evs, nev) != 0) {
                ivx_cancel(e);
                return IVX_OK;
            }
            n = 0;
        } while (nev == 32);
    } while (n_last > 0 && e->dones == dones);
    return IVX_OK;
}

/* ---- engine callbacks (cb_arg = the engine) ---- */

/* op_emit_bookmark (0x1001de90) reports (mark_lo low byte << 16) | (short)mark_hi, so the
 * serial is the low 16 bits. Marks queued with no segments between them share one
 * segment's serial: every older mark is reported with the one reached. The high-byte
 * report (esc_mark only, never used here) matches no serial in the queue. */
static void cb_bookmark(void *arg, int v)
{
    IvxEngine *e = arg;
    uint16_t serial = (uint16_t)v;
    size_t k;
    Mark m;

    for (k = 0; k < e->marks.len && ((Mark *)fifo_at(&e->marks, k))->serial != serial; k++)
        ;
    if (k == e->marks.len)
        return;
    do {
        fifo_pop(&e->marks, &m);
        push_event(e, IVX_EV_MARK, m.id, e->frame_pos);
    } while (m.serial != serial);
}

/* op_emit_phonemes (0x1001e890) with out_flags bit 1: one segment name per call */
static void cb_phonemes(void *arg, char *name)
{
    IvxEngine *e = arg;
    const unsigned char *s = (const unsigned char *)name;

    push_event(e, IVX_EV_PHONEME, s[0] == 0 ? 0 : (uint32_t)s[0] | (uint32_t)s[1] << 8, e->frame_pos);
}

/* tp_putc's status codes (text side: nothing is staged when tp_putc runs) */
static void cb_status(void *arg, int st)
{
    IvxEngine *e = arg;

    push_event(e, IVX_EV_ERROR, (uint32_t)st, e->gen_pos);
}

/* tp_word's dictionary callbacks (tts_ctrl 0x14, 0x15; lib/ivxdict.c) */
static int cb_userdict(void *arg, char *word, char **out)
{
    IvxEngine *e = arg;

    return dict_user(&e->dict, e->core, word, out);
}

static char *cb_excdict(void *arg, char *key, int unused)
{
    IvxEngine *e = arg;

    (void)unused;
    return dict_exc(&e->dict, e->core, key);
}

/* ---- dictionary ---- */

/* the callbacks only while entries need them: with none the engine runs exactly as the
 * DLL-verified loop does, and an exception callback changes the out-of-nodes retry
 * (tpstr.c:247,268) */
static void dict_install(IvxEngine *e)
{
    tts_ctrl(e->core, TTS_CB_USERDICT, e->dict.count != 0 ? (void *)cb_userdict : NULL);
    tts_ctrl(e->core, TTS_CB_EXCDICT, e->dict.nfallback != 0 ? (void *)cb_excdict : NULL);
}

/* caller text in the input encoding to cp1252, control chars as blanks; the length, or -1
 * if it is longer than cap (out has cap + 1 bytes) */
static int dict_text(const IvxEngine *e, const char *s, unsigned char *out, int cap)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char b[2];
    uint32_t cp[2];
    U8Dec d;
    int n = 0, m, k;

    memset(&d, 0, sizeof d);
    for (;; p++) {
        if (e->val[IVX_INPUT_ENCODING] == IVX_ENC_CP1252) {
            if (*p == 0)
                break;
            b[0] = *p;
            m = 1;
        } else {
            if (*p == 0 && d.need == 0)
                break;
            if (*p == 0) {   /* an unfinished sequence at the end */
                cp[0] = ' ';
                m = 1;
            } else {
                m = u8_byte(&d, *p, cp);
            }
            for (k = 0; k < m; k++)
                b[k] = cp1252_of(cp[k]);
        }
        for (k = 0; k < m; k++) {
            if (n == cap)
                return -1;
            out[n++] = b[k] < 0x20 ? ' ' : b[k];
        }
        if (*p == 0)
            break;
    }
    out[n] = 0;
    return n;
}

/* a key or respelling: 1..DICT_WORD bytes that can be one word (dict_is_word), else -1 */
static int dict_word(const IvxEngine *e, const char *word, unsigned char *out)
{
    int n = dict_text(e, word, out, DICT_WORD);

    return n > 0 && dict_is_word(out, (size_t)n) ? n : -1;
}

#define DICT_FLAGS (IVX_DICT_PHONETIC | IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE)

int ivx_dict_add(IvxEngine *e, const char *word, const char *replacement, int flags)
{
    unsigned char key[DICT_WORD + 1], rep[DICT_REP + 1];
    const unsigned char *p = (const unsigned char *)replacement;
    int n, m, r;

    if (e == NULL || word == NULL || replacement == NULL)
        return IVX_E_ARG;
    if ((flags & ~DICT_FLAGS) != 0 || ((flags & IVX_DICT_PHONETIC) && (flags & IVX_DICT_FALLBACK))
        || (n = dict_word(e, word, key)) < 0)
        return IVX_E_RANGE;
    if (flags & IVX_DICT_PHONETIC) {
        for (m = 0; p[m] != 0; m++)
            if (m == DICT_REP || p[m] <= ' ')   /* blanks and control chars (0x01 0x02 0x17) */
                return IVX_E_RANGE;
        memcpy(rep, p, (size_t)m);
    } else if (flags & IVX_DICT_FALLBACK) {
        m = dict_word(e, replacement, rep);
    } else {
        m = dict_text(e, replacement, rep, DICT_REP);
    }
    if (m <= 0)
        return IVX_E_RANGE;
    r = dict_add(&e->dict, key, (size_t)n, rep, (size_t)m, flags);
    dict_install(e);
    return r;
}

int ivx_dict_remove(IvxEngine *e, const char *word, int flags)
{
    unsigned char key[DICT_WORD + 1];
    int n, r;

    if (e == NULL || word == NULL)
        return IVX_E_ARG;
    if ((flags & ~DICT_FLAGS) != 0 || (n = dict_word(e, word, key)) < 0)
        return IVX_E_RANGE;
    r = dict_remove(&e->dict, key, (size_t)n, flags);
    dict_install(e);
    return r;
}

void ivx_dict_clear(IvxEngine *e)
{
    if (e == NULL)
        return;
    dict_clear(&e->dict);
    dict_install(e);
}
