/* Differential test: the segment lists (src/tpelem.c) and the frame generator
 * (src/opsmooth.c, opstart.c, opbatch.c) against bin/Ivx230nt.dll, on real text in every
 * language.
 *
 * The DLL's tpelem entry points used by the text pipeline and its op_start_utterance /
 * ivx_load_param_tables are patched to jump to hooks. A hook copies the Core and pool
 * into an image (harness.c), calls the original through a trampoline, runs the port on
 * the image and compares the return value, every dword of the Core and the pool, and the
 * callback output. Every op_next_frame call is checked the same way. At each utterance
 * start the segment list is also pushed through opb_save / opb_init / opb_restore and all
 * its frames on cloned state, DLL and port side by side.
 * Voice, rate, pitch and FD are changed between words, bookmarks and the phoneme callback
 * are on, and a second pass runs with out_mode 1 and feac forced on some frames.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"

#define VA_LOAD_TABLES      0x1001dc40
#define VA_OP_START         0x1001dd40
#define VA_EMIT_PHONEMES    0x1001e890
#define VA_FREE_SEGS_HELD   0x1001e930
#define VA_FREE_HELD        0x1001e990
#define VA_SEG_HOLD         0x1001ea50
#define VA_SEG_UNHOLD       0x1001eaa0
#define VA_COPY_HELD        0x1001eb00
#define VA_TEXT_TO_SEGS     0x1001ecb0
#define VA_OPB_INIT         0x10019780
#define VA_OPB_RESTORE      0x100199e0
#define VA_OPB_SAVE         0x10019af0
#define VA_OPB_FREE         0x10019cc0
#define VA_MEM_FREE         0x1000f320
#define SMALL_POOL          60

typedef short(__cdecl *core_s_fn)(Core *);
typedef void(__cdecl *core_v_fn)(Core *);
typedef short(__cdecl *text_fn)(Core *, char, unsigned char *, unsigned char *);
typedef void(__cdecl *emit_fn)(Core *, Seg *, Seg *, char *, int);
typedef short(__cdecl *opb_init_fn)(Core *, Core **);
typedef short(__cdecl *opb_restore_fn)(Core *, OpBatch *);
typedef short(__cdecl *opb_save_fn)(Core *, OpBatch **);
typedef short(__cdecl *opb_free_fn)(OpBatch *);
typedef void(__cdecl *mem_free_fn)(void *);

static core_s_fn tr_load_tables, tr_op_start, tr_copy_held;
static core_v_fn tr_free_segs_held, tr_free_held, tr_seg_hold, tr_seg_unhold;
static text_fn tr_text_to_segs;
static emit_fn tr_emit_phonemes;

static const char *cur_lang;
static long failures;
static int max_reports = 20;
static int depth;           /* > 0: a check is running, nested hooks just forward */
static Image shadow;

/* ---- callbacks: DLL (side 0) and port (side 1) output logged separately ---- */

static int side;
static char cblog[2][8192];
static size_t cblen[2];

static void log_reset(void)
{
    cblen[0] = cblen[1] = 0;
    cblog[0][0] = cblog[1][0] = 0;
}

static void log_add(const char *s)
{
    size_t n = strlen(s);

    if (cblen[side] + n + 1 < sizeof cblog[side]) {
        memcpy(cblog[side] + cblen[side], s, n + 1);
        cblen[side] += n;
    }
}

static long n_bookmarks, n_phoneme_cbs;

static void __cdecl cb_bookmark(void *arg, int v)
{
    char s[24];

    (void)arg;
    snprintf(s, sizeof s, "B%x;", v);
    log_add(s);
    n_bookmarks += side == 0;
}

static void __cdecl cb_phonemes(void *arg, char *p)
{
    char s[16];

    (void)arg;
    snprintf(s, sizeof s, "P%.4s;", p);
    log_add(s);
    n_phoneme_cbs += side == 0;
}

/* ---- checks ---- */

enum { F_LOAD, F_START, F_EMIT, F_FREE_SH, F_FREE_H, F_HOLD, F_UNHOLD, F_COPY, F_TEXT,
       F_FRAME, F_OPB, F_N };
static const char *fname[F_N] = {
    "ivx_load_param_tables", "op_start_utterance", "op_emit_phonemes", "seg_free_segs_held",
    "seg_free_held", "seg_hold", "seg_unhold", "seg_copy_held", "tp_text_to_segs",
    "op_next_frame", "opb_* + frames"};
static long calls[F_N], fails[F_N], nonzero[F_N];   /* nonzero: DLL return != 0 */

static void fail(int f, const char *what, size_t off, uint32_t a, uint32_t b)
{
    failures++;
    fails[f]++;
    if (max_reports-- > 0)
        printf("  FAIL %s %s call %ld: %s +0x%x: dll %08x port %08x\n", cur_lang, fname[f],
               calls[f], what, (unsigned)off, (unsigned)a, (unsigned)b);
}

/* return values, Core + pool against the shadow image, callback output */
static void check(int f, Core *d, int rd, int rp)
{
    Diff df;

    calls[f]++;
    nonzero[f] += rd != 0;
    if (rd != rp)
        fail(f, "return", 0, (uint32_t)rd, (uint32_t)rp);
    else if (compare_core_pool(&shadow.map, d, shadow.core, shadow.pool_bytes, &df))
        fail(f, df.what, df.off, df.av, df.bv);
    else if (strcmp(cblog[0], cblog[1]) != 0) {
        fail(f, "callbacks", 0, 0, 0);
        if (max_reports > 0)
            printf("    dll  %.200s\n    port %.200s\n", cblog[0], cblog[1]);
    }
}

static void begin(Core *d)
{
    depth++;
    image_take(&shadow, d);
    log_reset();
}

#define PTR(p) ((void *)(uintptr_t)map_or_same(&shadow.map, (uint32_t)(uintptr_t)(p)))

/* ---- hooks ---- */

static short __cdecl hook_load_tables(Core *d)
{
    short rd, rp;

    if (depth)
        return tr_load_tables(d);
    begin(d);
    side = 0;
    rd = tr_load_tables(d);
    side = 1;
    rp = ivx_load_param_tables(shadow.core);
    depth--;
    check(F_LOAD, d, rd, rp);
    return rd;
}

static void opb_test(Core *d);
static int opb_enabled = 1;

static short __cdecl hook_op_start(Core *d)
{
    short rd, rp;

    if (depth)
        return tr_op_start(d);
    /* opb_save of an empty list leaves list.tail.prev uninitialised in the DLL */
    if (opb_enabled && d->segs.head.next != (Seg *)&d->segs.tail)
        opb_test(d);
    begin(d);
    side = 0;
    rd = tr_op_start(d);
    side = 1;
    rp = op_start_utterance(shadow.core);
    depth--;
    check(F_START, d, rd, rp);
    return rd;
}

static long emit_buf_calls;

static void __cdecl hook_emit_phonemes(Core *d, Seg *first, Seg *end, char *buf, int n)
{
    char *pbuf = buf, *copy = NULL;
    uint32_t t;

    if (depth) {
        tr_emit_phonemes(d, first, end, buf, n);
        return;
    }
    begin(d);
    if (buf != NULL && n > 0) {
        emit_buf_calls++;
        if (map_addr(&shadow.map, (uint32_t)(uintptr_t)buf, &t)) {
            pbuf = (char *)(uintptr_t)t;
        } else {
            copy = malloc((size_t)n);
            memcpy(copy, buf, (size_t)n);
            pbuf = copy;
        }
    }
    side = 0;
    tr_emit_phonemes(d, first, end, buf, n);
    side = 1;
    op_emit_phonemes(shadow.core, PTR(first), PTR(end), pbuf, n);
    depth--;
    check(F_EMIT, d, 0, 0);
    if (copy != NULL) {
        if (memcmp(copy, buf, (size_t)n) != 0)
            fail(F_EMIT, "buffer", 0, 0, 0);
        free(copy);
    }
}

#define CORE_HOOK_V(name, tr, port, f)          \
    static void __cdecl name(Core *d)           \
    {                                           \
        if (depth) {                            \
            tr(d);                              \
            return;                             \
        }                                       \
        begin(d);                               \
        side = 0;                               \
        tr(d);                                  \
        side = 1;                               \
        port(shadow.core);                      \
        depth--;                                \
        check(f, d, 0, 0);                      \
    }
CORE_HOOK_V(hook_free_segs_held, tr_free_segs_held, seg_free_segs_held, F_FREE_SH)
CORE_HOOK_V(hook_free_held, tr_free_held, seg_free_held, F_FREE_H)
CORE_HOOK_V(hook_seg_hold, tr_seg_hold, seg_hold, F_HOLD)
CORE_HOOK_V(hook_seg_unhold, tr_seg_unhold, seg_unhold, F_UNHOLD)

static short __cdecl hook_copy_held(Core *d)
{
    short rd, rp;

    if (depth)
        return tr_copy_held(d);
    begin(d);
    side = 0;
    rd = tr_copy_held(d);
    side = 1;
    rp = seg_copy_held(shadow.core);
    depth--;
    check(F_COPY, d, rd, rp);
    return rd;
}

static short __cdecl hook_text_to_segs(Core *d, char bound, unsigned char *from, unsigned char *to)
{
    short rd, rp;

    if (depth)
        return tr_text_to_segs(d, bound, from, to);
    begin(d);
    side = 0;
    rd = tr_text_to_segs(d, bound, from, to);
    side = 1;
    rp = tp_text_to_segs(shadow.core, bound, PTR(from), PTR(to));
    depth--;
    check(F_TEXT, d, rd, rp);
    return rd;
}

/* ---- opbatch: save / init / restore / all frames, on two clones of the DLL state ---- */

static long opb_frames;

/* the replay in progress, for the failure dump: DLL (d) and port (p) sides */
static struct { OpBatch *bd, *bp; Core *c2d, *c2p; Image *cl, *sh; int frame; } replay;

static void opb_fail(const char *what, const Diff *df)
{
    if (replay.bd != NULL && replay.c2d != NULL) {
        int k;
        printf("    frame %d phinv %p | dll nodes %p+%x c2 %p cl.pool %p | port nodes %p c2 %p sh.pool %p\n",
               replay.frame, (void *)replay.c2d->phinv, (void *)replay.bd->nodes, (unsigned)replay.bd->size,
               (void *)replay.c2d, (void *)replay.cl->pool, (void *)replay.bp->nodes, (void *)replay.c2p,
               (void *)replay.sh->pool);
        for (k = 0; k < replay.c2d->ntotal; k++) {
            Track *a = &replay.c2d->tracks[k], *b = &replay.c2p->tracks[k];
            if (memcmp(a, b, sizeof *a) != 0)
                printf("    track %d dll p0 %p p1 %p p2 %p seg %p npts %d tleft %d | port p0 %p p1 %p p2 %p seg %p npts %d tleft %d\n",
                       k, (void *)a->p0, (void *)a->p1, (void *)a->p2, (void *)a->seg, a->npts, a->tleft,
                       (void *)b->p0, (void *)b->p1, (void *)b->p2, (void *)b->seg, b->npts, b->tleft);
        }
    }
    fail(F_OPB, df != NULL ? df->what : what, df != NULL ? df->off : 0,
         df != NULL ? df->av : 0, df != NULL ? df->bv : 0);
}

/* an empty free list for a Core made by opb_init (it has no pool) so that freeing the
 * restored segments cannot touch NULL links */
static void empty_free_list(Core *c)
{
    c->free_list.head.next = (Seg *)&c->free_list.tail;
    c->free_list.tail.prev = (Seg *)&c->free_list.head;
}

static void opb_test(Core *d)
{
    Image cl = {0}, sh = {0};
    OpBatch *bd = NULL, *bp = NULL;
    Core *c2d = NULL, *c2p = NULL;
    short rd, rp, *fd, *fp;
    Map m = {0};
    Diff df;

    depth++;
    calls[F_OPB]++;
    image_take(&cl, d);
    image_take(&sh, d);
    map_add(&m, cl.core, sh.core, sizeof(Core));
    map_add(&m, cl.pool, sh.pool, cl.pool_bytes);
    log_reset();

    side = 0;
    rd = ((opb_save_fn)dll_va(VA_OPB_SAVE))(cl.core, &bd);
    side = 1;
    rp = opb_save(sh.core, &bp);
    if (rd != rp || rd != 0) {
        Diff s = {"opb_save return", 0, (uint16_t)rd, (uint16_t)rp};
        opb_fail(NULL, &s);
        goto out;
    }
    replay.bd = bd; replay.bp = bp; replay.cl = &cl; replay.sh = &sh; replay.c2d = replay.c2p = NULL; replay.frame = 0;
    map_add(&m, bd, bp, sizeof(OpBatch));
    map_add(&m, bd->nodes, bp->nodes, (size_t)bd->size);
    if (compare_core_pool(&m, cl.core, sh.core, cl.pool_bytes, &df)
        || compare_words(&m, bd, bp, sizeof(OpBatch), "OpBatch", &df)
        || compare_words(&m, bd->nodes, bp->nodes, (size_t)bd->size, "batch nodes", &df)) {
        opb_fail(NULL, &df);
        goto out;
    }

    side = 0;
    rd = ((opb_init_fn)dll_va(VA_OPB_INIT))(cl.core, &c2d);
    side = 1;
    rp = opb_init(sh.core, &c2p);
    if (rd != 0 || rp != 0) {
        Diff s = {"opb_init return", 0, (uint16_t)rd, (uint16_t)rp};
        opb_fail(NULL, &s);
        goto out;
    }
    map_add(&m, c2d, c2p, sizeof(Core));
    replay.c2d = c2d; replay.c2p = c2p;
    empty_free_list(c2d);
    empty_free_list(c2p);
    if (compare_core_pool(&m, c2d, c2p, 0, &df)) {
        opb_fail(NULL, &df);
        goto out;
    }

    side = 0;
    rd = ((opb_restore_fn)dll_va(VA_OPB_RESTORE))(c2d, bd);
    side = 1;
    rp = opb_restore(c2p, bp);
    if (rd != rp) {
        Diff s = {"opb_restore return", 0, (uint16_t)rd, (uint16_t)rp};
        opb_fail(NULL, &s);
        goto out;
    }
    for (;;) {
        if (compare_core_pool(&m, c2d, c2p, 0, &df)
            || compare_words(&m, bd->nodes, bp->nodes, (size_t)bd->size, "batch nodes", &df)) {
            opb_fail(NULL, &df);
            break;
        }
        if (strcmp(cblog[0], cblog[1]) != 0) {
            opb_fail("callbacks", NULL);
            break;
        }
        log_reset();
        side = 0;
        fd = dll_next_frame(c2d);
        side = 1;
        fp = op_next_frame(c2p);
        if ((fd == NULL) != (fp == NULL) || (fd != NULL && fd - (short *)c2d != fp - (short *)c2p)) {
            Diff s = {"frame pointer", 0, (uint32_t)(uintptr_t)fd, (uint32_t)(uintptr_t)fp};
            opb_fail(NULL, &s);
            break;
        }
        if (fd == NULL)
            break;
        opb_frames++;
        replay.frame++;
    }
out:
    if (bd != NULL)
        ((opb_free_fn)dll_va(VA_OPB_FREE))(bd);
    if (bp != NULL)
        opb_free(bp);
    if (c2d != NULL)
        ((mem_free_fn)dll_va(VA_MEM_FREE))(c2d);
    if (c2p != NULL)
        mem_free(c2p);
    image_free(&cl);
    image_free(&sh);
    replay.bd = NULL;
    depth--;
}

/* ---- driving the DLL ---- */

static long frames, frames_mode1, feac_frames;
static int pass;

static void drain(Core *c)
{
    short *fd, *fp;

    for (;;) {
        if (pass == 1)
            c->feac = (unsigned char)(frames % 61 < 3);
        feac_frames += c->feac != 0;
        begin(c);
        side = 0;
        fd = dll_next_frame(c);
        side = 1;
        fp = op_next_frame(shadow.core);
        depth--;
        check(F_FRAME, c, fd == NULL ? -1 : (int)(fd - (short *)c),
              fp == NULL ? -1 : (int)(fp - (short *)shadow.core));
        if (fd == NULL)
            break;
        frames++;
        frames_mode1 += c->out_mode != 0;
    }
    c->feac = 0;
}

/* feed text; between words change the voice parameters (sets voice_dirty) */
static void speak(Core *c, const char *text, int vary)
{
    const unsigned char *p;
    int r, word = 0;

    for (p = (const unsigned char *)text; *p; p++) {
        if (p == (const unsigned char *)text || p[-1] == ' ') {
            dll_tts_ctrl(c, 0x20, (void *)(intptr_t)(0x10001 * (int)(p - (const unsigned char *)text + 1)));
            if (vary) {
                if (word % 5 == 2)
                    dll_tts_ctrl(c, 9, (void *)(intptr_t)(20 + word * 7 % 70));
                if (word % 7 == 3)
                    dll_tts_ctrl(c, 0x0a, (void *)(intptr_t)(10 + word * 13 % 80));
                if (word % 11 == 5)
                    dll_tts_ctrl(c, 8, (void *)(intptr_t)(word * 17 % 100));
                if (word % 13 == 6)
                    dll_tts_ctrl(c, 6, (void *)(intptr_t)(word % 5));
            }
            word++;
        }
        r = dll_tp_putc(c, *p);
        if (r < 0 || r > 2)
            printf("  tp_putc returned %d at offset %d\n", r, (int)(p - (const unsigned char *)text));
        drain(c);
    }
    dll_tp_putc(c, 0x17);   /* end of text, as the engine sends */
    drain(c);
}

static void test_lang(const char *bindir, const Lang *l, int npool)
{
    unsigned char *buf;
    long fail0 = failures, frames0 = frames;
    Core *c;

    cur_lang = l->file;
    c = lang_open(bindir, l, npool, &buf);
    if (c == NULL) {
        failures++;
        return;
    }
    dll_tts_ctrl(c, 0x1f, (void *)cb_bookmark);
    dll_tts_ctrl(c, 0x11, (void *)cb_phonemes);
    dll_tts_ctrl(c, 0x10, (void *)2);           /* phoneme names during synthesis */
    pass = 0;
    speak(c, l->text, 1);
    if (npool == 900) {
        pass = 1;
        dll_tts_ctrl(c, 0x1d, (void *)1);       /* out_mode 1: frame numbers and names */
        speak(c, l->text, 0);
        dll_tts_ctrl(c, 0x1d, (void *)0);
    }
    printf("%-12s pool %3d: %6ld frames: %s\n", l->file, npool, frames - frames0,
           failures == fail0 ? "ok" : "MISMATCH");
    lang_close(c, buf);
}

static int hook(uint32_t va, int n, void *fn, void **tr)
{
    *tr = trampoline(va, n);
    return *tr != NULL && patch_jump(va, fn);
}

int main(int argc, char **argv)
{
    const char *bindir = argc > 1 ? argv[1] : "bin";
    int i, ok = 1;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!dll_load(bindir))
        return 2;
    /* prologue lengths: whole instructions, none PC-relative */
    ok &= hook(VA_LOAD_TABLES, 7, (void *)hook_load_tables, (void **)&tr_load_tables);
    ok &= hook(VA_OP_START, 7, (void *)hook_op_start, (void **)&tr_op_start);
    ok &= hook(VA_EMIT_PHONEMES, 8, (void *)hook_emit_phonemes, (void **)&tr_emit_phonemes);
    ok &= hook(VA_FREE_SEGS_HELD, 5, (void *)hook_free_segs_held, (void **)&tr_free_segs_held);
    ok &= hook(VA_FREE_HELD, 10, (void *)hook_free_held, (void **)&tr_free_held);
    ok &= hook(VA_SEG_HOLD, 5, (void *)hook_seg_hold, (void **)&tr_seg_hold);
    ok &= hook(VA_SEG_UNHOLD, 10, (void *)hook_seg_unhold, (void **)&tr_seg_unhold);
    ok &= hook(VA_COPY_HELD, 5, (void *)hook_copy_held, (void **)&tr_copy_held);
    ok &= hook(VA_TEXT_TO_SEGS, 5, (void *)hook_text_to_segs, (void **)&tr_text_to_segs);
    if (!ok) {
        printf("cannot patch the DLL\n");
        return 2;
    }

    /* the engine's pool size (two passes), then a small pool that runs out of nodes */
    for (i = 0; i < nlangs; i++)
        test_lang(bindir, &langs[i], 900);
    for (i = 0; i < nlangs; i++)
        test_lang(bindir, &langs[i], argc > 2 ? atoi(argv[2]) : SMALL_POOL);

    printf("\ncalls compared, and how many returned non-zero in the DLL (frames: all):\n");
    for (i = 0; i < F_N; i++)
        printf("  %-22s %8ld  %8ld%s\n", fname[i], calls[i], nonzero[i], fails[i] ? "  FAILED" : "");
    printf("  (%ld frames, %ld with out_mode 1, %ld with feac, %ld via opb_restore; %ld bookmark "
           "and %ld phoneme callbacks; %ld buffer emits)\n",
           frames, frames_mode1, feac_frames, opb_frames, n_bookmarks, n_phoneme_cbs, emit_buf_calls);
#ifdef CW_COVERAGE
    {
        extern unsigned long op_type_count[33], op_cov[OP_COV_N];
        static const char *cov[OP_COV_N] = OP_COV_NAMES;

        printf("\nA1 types run by the port:");
        for (i = 1; i < 33; i++)
            if (op_type_count[i])
                printf(" %d:%lu", i, op_type_count[i]);
        printf("\nbranches:");
        for (i = 0; i < OP_COV_N; i++)
            printf("%s %s %lu", i ? "," : "", cov[i], op_cov[i]);
        printf("\n");
    }
#endif
    printf("\n%ld failures\n", failures);
    return failures != 0;
}
