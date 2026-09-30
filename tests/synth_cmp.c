/* Differential test: src/synth.c against the original synthesizer in bin/Ivx230nt.dll.
 *
 * Loads the DLL (32-bit only), drives its synt_ctrl / synt_generate and the port with the
 * same pseudo-random frame stream, and compares the coefficient tables, the per-frame
 * coefficient blocks, the sample-loop state and every output sample.
 *
 * Build and run: see the Makefile (make test).
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "synth.h"

#define VA_SYNT_CTRL     0x10022cc5
#define VA_SYNT_GENERATE 0x10022fb4

typedef int(__cdecl *ctrl_fn)(Synth *, short, void *);
typedef short(__cdecl *gen_fn)(Synth *, void *, short);

static char *dll_base;
static ctrl_fn dll_ctrl;
static gen_fn dll_generate;
static int failures, checks;

static void *dll_va(uint32_t va)
{
    return dll_base + (va - 0x10000000u);
}

/* ---- deterministic frame source ---- */

typedef struct {
    uint32_t rng;
    int frames_left;   /* then NULL forever */
    int fh_min;        /* mode 1 uses FH-4 as an index */
    int narrow;        /* keep every value in 0..31: any slot may be bound to a B/amp param */
    short frame[32];
} Src;

static uint32_t rnd(Src *s, uint32_t n)
{
    s->rng = s->rng * 1103515245u + 12345u;
    return (s->rng >> 8) % n;
}

/* random walk within [lo, hi] with occasional jumps */
static void walk(Src *s, int i, int lo, int hi)
{
    int v = s->frame[i];
    if (s->narrow && hi > 31)
        hi = 31;
    if (rnd(s, 8) == 0)
        v = lo + (int)rnd(s, (uint32_t)(hi - lo + 1));
    else
        v += (int)rnd(s, 9) - 4;
    if (v < lo)
        v = lo;
    if (v > hi)
        v = hi;
    s->frame[i] = (short)v;
}

static short *src_next(void *arg)
{
    static const int fidx[] = {1, 3, 5, 7, 12, 14, 10, 9};   /* F1 F2 F3 F4 FN K2 K1 FH */
    static const int bidx[] = {2, 4, 6, 8, 11, 13, 15};      /* B1 B2 B3 B4 C1 C2 BN */
    static const int aidx[] = {16, 17, 18, 19, 20};           /* AK AC AH AN A0 */
    Src *s = arg;
    unsigned i;

    if (s->frames_left <= 0)
        return NULL;
    s->frames_left--;
    if (rnd(s, 20) == 0)
        return NULL;   /* a frame of silence */
    for (i = 0; i < sizeof(fidx) / sizeof(fidx[0]); i++)
        walk(s, fidx[i], fidx[i] == 9 ? s->fh_min : 0, 127);
    for (i = 0; i < sizeof(bidx) / sizeof(bidx[0]); i++)
        walk(s, bidx[i], 0, 31);
    for (i = 0; i < sizeof(aidx) / sizeof(aidx[0]); i++) {
        walk(s, aidx[i], 0, 31);
        if (rnd(s, 6) == 0)
            s->frame[aidx[i]] = 0;
    }
    walk(s, 21, 0, 300);   /* F0, including values below the 10 floor */
    s->frame[0] = (short)0x4131;   /* phoneme name slot, unused by the synth */
    return s->frame;
}

static void src_init(Src *s, uint32_t seed, int frames, int fh_min, int narrow)
{
    unsigned i;
    memset(s, 0, sizeof(*s));
    s->narrow = narrow;
    s->rng = seed;
    s->frames_left = frames;
    s->fh_min = fh_min;
    for (i = 1; i < 32; i++)
        s->frame[i] = (short)(i == 9 ? 100 : 10);
}

/* ---- comparison helpers ---- */

static int fail(const char *test, const char *what, long pos)
{
    failures++;
    printf("  FAIL %s: %s (at %ld)\n", test, what, pos);
    return 1;
}

static int cmp_mem(const char *test, const char *what, const void *a, const void *b, size_t n)
{
    const unsigned char *x = a, *y = b;
    size_t i;
    checks++;
    for (i = 0; i < n; i++)
        if (x[i] != y[i]) {
            char msg[160];
            snprintf(msg, sizeof msg, "%s byte +0x%x: dll %02x port %02x", what, (unsigned)i, x[i], y[i]);
            return fail(test, msg, (long)i);
        }
    return 0;
}

static int cmp_tables(const char *test, const SynthTables *d, const SynthTables *p)
{
    int bad = 0;
    bad |= cmp_mem(test, "amp_db", d->amp_db, p->amp_db, 32 * 2);
    bad |= cmp_mem(test, "res_b", d->res_b, p->res_b, 128 * 2);
    bad |= cmp_mem(test, "res_c", d->res_c, p->res_c, 128 * 2);
    bad |= cmp_mem(test, "bw_b_ratio", d->bw_b_ratio, p->bw_b_ratio, 32 * 2);
    bad |= cmp_mem(test, "bw_c_ratio", d->bw_c_ratio, p->bw_c_ratio, 32 * 2);
    return bad;
}

static int cmp_maps(const char *test, const SynthTables *d, const SynthTables *p)
{
    int i;
    checks++;
    for (i = 0; i < 32; i++) {
        const SynthMapEntry *a = &d->map[i], *b = &p->map[i];
        if (strncmp(a->name, b->name, 2) != 0 || a->frame_index != b->frame_index || a->def != b->def)
            return fail(test, "map entry", i);
    }
    return 0;
}

static int cmp_state(const char *test, const Synth *d, const Synth *p, long pos)
{
    const SynthRun *dr = d->tab->run, *pr = p->tab->run;
    int bad = 0;

    checks++;
    if (d->started != p->started || d->frame_left != p->frame_left || d->produced != p->produced)
        bad |= fail(test, "Synth started/frame_left/produced", pos);
    if (dr->flags != pr->flags || dr->voiced != pr->voiced || dr->period_acc != pr->period_acc
        || dr->period_next != pr->period_next || d->tab->silent != p->tab->silent)
        bad |= fail(test, "SynthRun flags/voiced/period", pos);
    bad |= cmp_mem(test, "coef", dr->coef, pr->coef, (size_t)dr->coef_size);
    if (d->type == 0)
        bad |= cmp_mem(test, "sstate", dr->sstate, pr->sstate, sizeof(SynthState0));
    return bad;
}

/* ---- one test case ---- */

typedef struct {
    const char *name;
    short type;
    int fs;            /* 0 = leave the default */
    int frame_rate;    /* 0 = leave the default */
    int map_sel;
    int sample_fmt;    /* -1 = leave the default */
    const short *plist;
    int frames;
    uint32_t seed;
} Case;

static void run_case(const Case *tc)
{
    Synth *d = NULL, *p = NULL;
    Src ds, ps;
    static unsigned char dbuf[2 * 32768 + 64], pbuf[sizeof dbuf];
    long total = 0, nonzero = 0;
    int peak = 0;
    int bad = 0, calls = 0, fail0 = failures;
    int rd, rp;

    printf("%s\n", tc->name);
    rd = dll_ctrl((Synth *)&d, tc->type, NULL);
    rp = synt_ctrl((Synth *)&p, tc->type, NULL);
    if (rd != rp || d == NULL || p == NULL) {
        fail(tc->name, "create", 0);
        return;
    }
    src_init(&ds, tc->seed, tc->frames, tc->type == 1 ? 4 : 0, tc->plist != NULL);
    src_init(&ps, tc->seed, tc->frames, tc->type == 1 ? 4 : 0, tc->plist != NULL);

#define BOTH(cmd, argd, argp)                                           \
    do {                                                                \
        int a_ = dll_ctrl(d, cmd, argd), b_ = synt_ctrl(p, cmd, argp);  \
        checks++;                                                       \
        if (a_ != b_)                                                   \
            fail(tc->name, "synt_ctrl result " #cmd, a_);               \
    } while (0)

    BOTH(SYNT_SET_FRAMECB, (void *)src_next, (void *)src_next);
    BOTH(SYNT_SET_FRAMEARG, &ds, &ps);
    if (tc->fs)
        BOTH(SYNT_SAMPLE_RATE, (void *)(intptr_t)tc->fs, (void *)(intptr_t)tc->fs);
    if (tc->frame_rate)
        BOTH(SYNT_FRAME_RATE, (void *)(intptr_t)tc->frame_rate, (void *)(intptr_t)tc->frame_rate);
    if (tc->map_sel)
        BOTH(SYNT_SET_MAP, (void *)(intptr_t)tc->map_sel, (void *)(intptr_t)tc->map_sel);
    if (tc->sample_fmt >= 0)
        BOTH(SYNT_SAMPLE_FMT, (void *)(intptr_t)tc->sample_fmt, (void *)(intptr_t)tc->sample_fmt);
    if (tc->plist)
        BOTH(SYNT_SET_PARAMS, (void *)tc->plist, (void *)tc->plist);

    bad |= cmp_tables(tc->name, d->tab, p->tab);
    bad |= cmp_maps(tc->name, d->tab, p->tab);
    checks++;
    if (d->fs != p->fs || d->half_rate != p->half_rate || d->frame_rate != p->frame_rate)
        bad |= fail(tc->name, "fs/half_rate/frame_rate", 0);

    while (ds.frames_left > 0 && !bad) {
        short n = (short)(1 + rnd(&ds, 700));
        short gd, gp, prod_d = 0, prod_p = 0;
        rnd(&ps, 700);   /* keep both streams in step */
        memset(dbuf, 0xab, sizeof dbuf);
        memset(pbuf, 0xab, sizeof pbuf);
        gd = dll_generate(d, dbuf, n);
        gp = synt_generate(p, pbuf, n);
        calls++;
        checks++;
        if (gd != gp)
            bad |= fail(tc->name, "synt_generate result", total);
        bad |= cmp_mem(tc->name, "samples", dbuf, pbuf, sizeof dbuf);
        bad |= cmp_state(tc->name, d, p, total);
        if (tc->type == 0 && tc->sample_fmt != 0) {
            short *smp = (short *)dbuf;
            int j;
            for (j = 0; j < n; j++) {
                int v = smp[j] < 0 ? -smp[j] : smp[j];
                nonzero += v != 0;
                if (v > peak)
                    peak = v;
            }
        }
        dll_ctrl(d, SYNT_GET_PRODUCED, &prod_d);
        synt_ctrl(p, SYNT_GET_PRODUCED, &prod_p);
        checks++;
        if (prod_d != prod_p)
            bad |= fail(tc->name, "cmd 0x12", total);
        if (calls % 7 == 0) {
            dll_ctrl(d, SYNT_FLUSH, NULL);
            synt_ctrl(p, SYNT_FLUSH, NULL);
        }
        total += n;
    }
    BOTH(SYNT_DESTROY, NULL, NULL);
    printf("  %s: %ld samples in %d calls (%ld non-zero, peak %d)\n",
           failures == fail0 ? "ok" : "MISMATCH", total, calls, nonzero, peak);
#undef BOTH
}

/* parameter-code list for cmd 0x0b: {-n, 0, codes...}, big-endian char pairs */
#define C2(a, b) (short)(((a) << 8) | (b))
static const short plist_neg[] = {
    -26, 0,
    C2('A','0'), C2('F','0'), C2('F','1'), C2('B','1'), C2('F','2'), C2('B','2'), C2('F','3'),
    C2('B','3'), C2('F','4'), C2('B','4'), C2('F','H'), C2('K','1'), C2('C','1'), C2('K','2'),
    C2('C','2'), C2('F','N'), C2('B','N'), C2('A','K'), C2('A','C'), C2('A','H'), C2('A','N'),
    C2('R','G'), C2('N','A'), C2('X','X'),
};
static const short plist_pos[] = {
    3, 0,
    C2('F','1'), C2('B','1'), C2('F','2'), C2('B','2'), C2('F','3'), C2('B','3'), C2('F','4'),
    C2('B','4'), C2('F','H'), C2('K','1'), C2('C','1'), C2('K','2'), C2('C','2'), C2('F','N'),
    C2('B','N'), C2('A','K'), C2('A','C'), C2('A','H'), C2('A','N'), C2('A','0'), C2('F','0'),
    C2('Q','Q'), C2('Q','Q'),
};

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "bin\\Ivx230nt.dll";
    static const Case cases[] = {
        {"mode0 default (11025 Hz, 100 fr/s)", 0, 0, 0, 0, -1, NULL, 400, 1},
        {"mode0 22050 Hz (engine setting)", 0, 22050, 0, 0, -1, NULL, 400, 2},
        {"mode0 16000 Hz (16k loop)", 0, 16000, 0, 0, -1, NULL, 400, 3},
        {"mode0 8000 Hz, 37 fr/s", 0, 8000, 37, 0, -1, NULL, 200, 4},
        {"mode0 44100 Hz, 1000 fr/s", 0, 44100, 1000, 0, -1, NULL, 2000, 5},
        {"mode0 11025 Hz, 2 fr/s", 0, 11025, 2, 0, -1, NULL, 12, 6},
        {"mode0 alt map (map_sel 1)", 0, 22050, 0, 1, -1, NULL, 300, 7},
        {"mode0 8-bit sample_fmt 0", 0, 11025, 0, 0, 0, NULL, 200, 8},
        {"mode0 param list (negative count)", 0, 11025, 0, 0, -1, plist_neg, 300, 9},
        {"mode0 param list (default count 25)", 0, 16000, 0, 0, -1, plist_pos, 300, 10},
        {"mode1 11025 Hz", 1, 11025, 0, 0, -1, NULL, 300, 11},
        {"mode1 22050 Hz (half rate)", 1, 22050, 0, 0, -1, NULL, 300, 12},
    };
    HMODULE h;
    unsigned i;

    h = LoadLibraryA(path);
    if (h == NULL) {
        printf("cannot load %s (error %lu)\n", path, GetLastError());
        return 2;
    }
    dll_base = (char *)h;
    dll_ctrl = (ctrl_fn)dll_va(VA_SYNT_CTRL);
    dll_generate = (gen_fn)dll_va(VA_SYNT_GENERATE);
    printf("loaded %s at %p\n", path, (void *)h);

    for (i = 0; i < sizeof(cases) / sizeof(cases[0]); i++)
        run_case(&cases[i]);

    printf("\n%d checks, %d failures\n", checks, failures);
    return failures != 0;
}
