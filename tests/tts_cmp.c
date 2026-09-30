/* Differential test: src/ttsctrl.c (tts_ctrl and its commands, the .ivx byte swap and
 * bind), and the whole port end to end, against bin/Ivx230nt.dll (32-bit).
 *
 * 1. swap: every .ivx file is byte-swapped by the DLL (tts_ctrl 0x21) and by the port;
 *    the two files must be identical.
 * 2. commands: a DLL Core and a port Core are created side by side and get the same
 *    tts_ctrl commands: a fixed script, then a pseudo-random stream of every command with
 *    edge-case arguments (queries, out-of-range voices and UI values, step codes, binds to
 *    a body with printable A0 names, to a body tagged "XX", and to NULL). Return values
 *    and the whole Core and node pool are compared after every call; pointers must point
 *    at corresponding addresses (Core, pool, the three swapped bodies).
 * 3. text to audio: the DLL pipeline (its tts_ctrl, tp_putc, op_next_frame and synth) and
 *    the port pipeline (all port code, no DLL) speak each language's text in three
 *    configurations (the engine's 22050 Hz, and voice/rate/pitch/FD changes through
 *    tts_ctrl at every word at 16000 and 11025 Hz); every sample is compared, and the
 *    Core and pool after every char.
 *
 * Usage: tts_cmp [bindir] [wav]   ("wav" also writes the port's audio to build/wav/)
 */
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"
#include "tts.h"
#include "synth.h"

#define VA_SYNT_CTRL     0x10022cc5
#define VA_SYNT_GENERATE 0x10022fb4

typedef int(__cdecl *synt_ctrl_fn)(Synth *, short, void *);
typedef short(__cdecl *synt_gen_fn)(Synth *, void *, short);

static synt_ctrl_fn dll_synt_ctrl;
static synt_gen_fn dll_synt_generate;
static long failures, checks, cmds, cmd_count[TTS_NCMD], samples, nonzero, chars, returns[3], stops;
static int stop_mode;
static const char *cur_lang, *cur_part;

static void fail(const char *what, const Diff *d)
{
    failures++;
    if (failures > 40)
        return;
    if (d != NULL)
        printf("  FAIL %s %s: %s +0x%x: dll 0x%08x port 0x%08x\n", cur_lang, cur_part, what,
               (unsigned)d->off, (unsigned)d->av, (unsigned)d->bv);
    else
        printf("  FAIL %s %s: %s\n", cur_lang, cur_part, what);
}

static uint32_t rng;

static uint32_t rnd(uint32_t n)
{
    rng = rng * 1103515245u + 12345u;
    return (rng >> 8) % n;
}

/* ---- the two sides ---- */

enum { BODY_SHIPPED, BODY_NAMES, BODY_XX, NBODY };

typedef struct {
    Core *c;
    unsigned char *file[NBODY];   /* swapped .ivx files */
    long size;
} Side;

static Side sd, sp;   /* DLL, port */
static Map map;

static void build_map(void)
{
    int k;

    map.n = 0;
    map_add(&map, sd.c, sp.c, sizeof(Core));
    map_add(&map, sd.c->pool, sp.c->pool, (size_t)sd.c->pool_n * NODE_SIZE);
    for (k = 0; k < NBODY; k++)
        if (sd.file[k] != NULL)
            map_add(&map, sd.file[k], sp.file[k], (size_t)sd.size + 16);
}

static void compare_cores(const char *what)
{
    Diff d;

    checks++;
    memcpy(sp.c->jmpbuf, sd.c->jmpbuf, sizeof sd.c->jmpbuf);   /* only the DLL calls setjmp */
    if (compare_core_pool(&map, sd.c, sp.c, (size_t)sd.c->pool_n * NODE_SIZE, &d))
        fail(what, &d);
}

/* read and swap one file on each side; k selects the variant of part 2 */
static int load_bodies(const char *bindir, const Lang *l, int nbody)
{
    char path[512];
    int k, j;
    long size;

    snprintf(path, sizeof path, "%s\\%s", bindir, l->file);
    for (k = 0; k < nbody; k++) {
        sd.file[k] = read_file(path, &sd.size);
        sp.file[k] = read_file(path, &size);
        if (sd.file[k] == NULL || sp.file[k] == NULL) {
            printf("%s: cannot read %s\n", l->file, path);
            failures++;
            return 0;
        }
        dll_tts_ctrl(NULL, TTS_IVX_SWAP, sd.file[k] + 0x200);
        tts_ctrl(NULL, TTS_IVX_SWAP, sp.file[k] + 0x200);
        cmd_count[TTS_IVX_SWAP]++;
        if (k == BODY_NAMES) {
            /* printable A0 names; one with a NUL second char, one starting with NUL */
            for (j = 0; j < 32; j++) {
                unsigned char *n = sd.file[k] + 0x200 + 0x70 + 2 * j;
                n[0] = (unsigned char)(j == 5 ? 0 : 'a' + j % 26);
                n[1] = (unsigned char)(j == 3 ? 0 : '0' + j / 26);
                memcpy(sp.file[k] + 0x200 + 0x70 + 2 * j, n, 2);
            }
        } else if (k == BODY_XX) {
            sd.file[k][0x240] = sd.file[k][0x241] = 'X';
            sp.file[k][0x240] = sp.file[k][0x241] = 'X';
        }
    }
    return 1;
}

static void free_bodies(void)
{
    int k;

    for (k = 0; k < NBODY; k++) {
        free(sd.file[k]);
        free(sp.file[k]);
        sd.file[k] = sp.file[k] = NULL;
    }
}

/* ---- 1. the byte swap ---- */

static void test_swap(const char *bindir, const Lang *l)
{
    cur_part = "swap";
    if (!load_bodies(bindir, l, 1))
        return;
    checks++;
    if (memcmp(sd.file[0], sp.file[0], (size_t)sd.size) != 0) {
        long i;

        for (i = 0; i < sd.size && sd.file[0][i] == sp.file[0][i]; i++)
            ;
        printf("  FAIL %s swap: file offset 0x%lx: dll %02x port %02x\n", l->file, i,
               sd.file[0][i], sp.file[0][i]);
        failures++;
    }
    free_bodies();
}

/* ---- 2. the commands ---- */

/* one command on both sides; body arguments (3) are indices into the side's files */
static void both(int cmd, intptr_t arg, int body_arg)
{
    void *ad = (void *)arg, *ap = (void *)arg;
    int ra, rb;
    char what[48];

    if (body_arg) {
        ad = arg < 0 ? (void *)arg : arg >= NBODY ? NULL : sd.file[arg] + 0x200;
        ap = arg < 0 ? (void *)arg : arg >= NBODY ? NULL : sp.file[arg] + 0x200;
    }
    ra = dll_tts_ctrl(sd.c, cmd, ad);
    rb = tts_ctrl(sp.c, cmd, ap);
    cmds++;
    cmd_count[cmd]++;
    snprintf(what, sizeof what, "cmd 0x%02x arg 0x%x", cmd, (unsigned)arg);
    checks++;
    if (!same_word(&map, (uint32_t)ra, (uint32_t)rb)) {
        Diff d = {"", 0, (uint32_t)ra, (uint32_t)rb};
        char w2[64];

        snprintf(w2, sizeof w2, "%s return", what);
        fail(w2, &d);
    }
    compare_cores(what);
}

static int ui_arg(void)
{
    switch (rnd(10)) {
    case 0: case 1: case 2: return -2 - (int)rnd(6);           /* step codes */
    case 3: return -1;                                           /* query */
    case 4: return 0x10000 - 2 - (int)rnd(6);                   /* a step code in 16 bits */
    case 5: return -8 - (int)rnd(100);
    case 6: return 101 + (int)rnd(200);
    default: return (int)rnd(101);
    }
}

/* the same random values in fields that only resets clear or queries read, on both sides */
static void poke(void)
{
    Core *c[2] = {sd.c, sp.c};
    uint32_t r = rnd(0x10000) << 16 | rnd(0x10000);
    int k;

    for (k = 0; k < 2; k++) {
        c[k]->fead = (unsigned char)r;
        c[k]->f6e2 = (unsigned char)(r >> 8);
        c[k]->f09d = (unsigned char)(r >> 16);
        c[k]->f6de = (unsigned char)(r >> 24);
        c[k]->f6d9 = (unsigned char)(r >> 4);
        c[k]->flush_on_input = (unsigned char)(r >> 12);
        c[k]->pending = (unsigned char)(r >> 20);
        c[k]->tp_state = (unsigned char)(r % 14);
        c[k]->frames_left = (short)(r >> 3);
        c[k]->count_init = (short)(r >> 9);
        c[k]->word_pos = (short)(r >> 13);
    }
}

static void random_cmd(int allow_bind)
{
    static const int esc[] = {-1, 0x7f, 8, 0, 1, 2, 0x1b, 0xff};
    static const int cs[] = {-1, 0, 1, 2, 3, 4, 5, 6, 7};
    int cmd;

    if (rnd(6) == 0)
        poke();
    do
        cmd = 2 + (int)rnd(TTS_IVX_SWAP - 2);   /* not create/destroy/swap */
    while (cmd == TTS_BIND && !allow_bind);
    switch (cmd) {
    case TTS_RESET:       both(cmd, rnd(3), 0); break;
    case TTS_BIND: {
        uint32_t r = rnd(10);
        both(cmd, r < 6 ? BODY_SHIPPED : r == 6 ? BODY_NAMES : r == 7 ? BODY_XX : r == 8 ? -1 : NBODY, 1);
        break;
    }
    case TTS_CHARSET:     both(cmd, cs[rnd(9)], 0); break;
    case TTS_MODE:        both(cmd, rnd(4) == 0 ? -1 : (int)rnd(0x200), 0); break;
    case TTS_SET_VOICE:   both(cmd, (int)rnd(13) - 2, 0); break;
    case TTS_STORE_VOICE: both(cmd, 3 + (int)rnd(8), 0); break;
    case TTS_FD: case TTS_RATE: case TTS_PITCH:
        both(cmd, ui_arg(), 0);
        break;
    case TTS_VAR_INIT:    both(cmd, (int)(rnd(12) << 16 | rnd(0x10000)), 0); break;
    case TTS_ESC_MARK: case TTS_ESC_PHON:
        both(cmd, rnd(3) ? esc[rnd(8)] : (int)rnd(256), 0);
        break;
    case TTS_OUT_FLAGS:   both(cmd, rnd(4) == 0 ? -1 : (int)rnd(256), 0); break;
    case TTS_CB_PHONEMES: case TTS_P6EC: case TTS_CB_STATUS: case TTS_CB_USERDICT:
    case TTS_CB_EXCDICT: case TTS_CB_ARG: case TTS_HOOK: case TTS_CB_WORDPOS: case TTS_CB_BOOKMARK:
        both(cmd, rnd(3) == 0 ? 0 : (int)(rnd(0x10000) << 16 | rnd(0x10000)), 0);   /* never called here */
        break;
    case TTS_HOOK_INTERVAL: both(cmd, (int)rnd(0x20000) - 0x10000, 0); break;
    case TTS_WORD_POS:    both(cmd, rnd(4) == 0 ? -1 : (int)rnd(0x10000) - 0x8000, 0); break;
    case TTS_OUT_MODE: case TTS_FEAE:
        both(cmd, (int)rnd(3), 0);
        break;
    case TTS_MARK:        both(cmd, (int)(rnd(0x10000) << 16 | rnd(0x10000)), 0); break;
    default:              both(cmd, 0, 0); break;   /* nops, frames_left, clear_count */
    }
}

static int create_both(int npool)
{
    int ra, rb;

    sd.c = sp.c = NULL;
    ra = dll_tts_ctrl(&sd.c, TTS_CREATE, (void *)(intptr_t)npool);
    rb = tts_ctrl(&sp.c, TTS_CREATE, (void *)(intptr_t)npool);
    cmds++;
    cmd_count[TTS_CREATE]++;
    checks++;
    if (ra != rb || sd.c == NULL || sp.c == NULL) {
        fail("create", NULL);
        return 0;
    }
    build_map();
    compare_cores("create");
    return 1;
}

static void destroy_both(void)
{
    dll_tts_ctrl(sd.c, TTS_DESTROY, NULL);
    tts_ctrl(sp.c, TTS_DESTROY, NULL);
    cmd_count[TTS_DESTROY]++;
    sd.c = sp.c = NULL;
}

static void test_commands(const char *bindir, const Lang *l, int li)
{
    static const int pools[] = {1, 2, 60, 1000};
    int i;

    cur_part = "commands";
    if (!load_bodies(bindir, l, NBODY))
        return;
    for (i = 0; i < 4; i++)
        if (create_both(pools[i]))
            destroy_both();
    if (!create_both(900)) {
        free_bodies();
        return;
    }
    /* the engine's own sequence first (REFERENCE §2.2), then the edge cases */
    both(TTS_CHARSET, 2, 0);
    both(TTS_SET_VOICE, 4, 0);          /* ntotal 32 before a bind: 3 words past voice 4 */
    both(TTS_BIND, BODY_SHIPPED, 1);
    both(TTS_CLEAR_COUNT, 0, 0);
    both(TTS_HOOK_INTERVAL, 500, 0);
    both(TTS_MODE, -1, 0);
    both(TTS_MODE, 0, 0);
    both(TTS_MODE, 0x1f0, 0);
    both(TTS_MODE, 0x182, 0);
    both(TTS_CHARSET, 4, 0);
    both(TTS_CHARSET, 3, 0);
    both(TTS_BIND, BODY_NAMES, 1);
    both(TTS_BIND, BODY_XX, 1);
    both(TTS_BIND, NBODY, 1);           /* NULL */
    both(TTS_BIND, -1, 1);
    both(TTS_OUT_MODE, 1, 0);
    both(TTS_MARK, -1, 0);
    both(TTS_MARK, 0x7fffffff, 0);
    both(TTS_ESC_MARK, 0x7f, 0);
    for (i = -8; i <= 9; i++)
        both(TTS_SET_VOICE, i, 0);
    both(TTS_RESET, 0, 0);
    both(TTS_BIND, BODY_SHIPPED, 1);
    rng = 0x1234567u + (uint32_t)li * 7919u;
    for (i = 0; i < 4000; i++)
        random_cmd(1);
    destroy_both();
    /* commands before any bind */
    if (create_both(900)) {
        for (i = 0; i < 300; i++)
            random_cmd(0);
        destroy_both();
    }
    free_bodies();
}

/* ---- 3. text to audio ---- */

typedef struct {
    const char *name;
    int mode;
    int fs;
    int voices;   /* change the voice through tts_ctrl at every word */
    int stops;    /* the engine's stop sequence at random points mid-utterance */
} Config;

static const Config configs[] = {
    {"engine 22050", 8, 22050, 0, 0},
    {"voices 16000", 4, 16000, 1, 0},
    {"voices 11025", 12, 11025, 1, 0},
    {"stops 22050", 8, 22050, 0, 1},
};
#define NCONFIGS (int)(sizeof configs / sizeof configs[0])

static Synth *syd, *syp;
static FILE *wav;
static long wav_samples;

static void wav_header(FILE *f, int fs, long n)
{
    uint32_t v32;
    uint16_t v16;

#define W32(x) (v32 = (uint32_t)(x), fwrite(&v32, 4, 1, f))
#define W16(x) (v16 = (uint16_t)(x), fwrite(&v16, 2, 1, f))
    fseek(f, 0, SEEK_SET);
    fwrite("RIFF", 1, 4, f); W32(36 + 2 * n); fwrite("WAVEfmt ", 1, 8, f);
    W32(16); W16(1); W16(1); W32(fs); W32(2 * fs); W16(2); W16(16);
    fwrite("data", 1, 4, f); W32(2 * n);
#undef W32
#undef W16
}

/* each side through its own tts_ctrl */
static void ctrl_both(int cmd, int arg)
{
    int ra = dll_tts_ctrl(sd.c, cmd, (void *)(intptr_t)arg);
    int rb = tts_ctrl(sp.c, cmd, (void *)(intptr_t)arg);

    cmds++;
    cmd_count[cmd]++;
    checks++;
    if (ra != rb) {
        Diff d = {"", (size_t)cmd, (uint32_t)ra, (uint32_t)rb};
        fail("tts_ctrl in text", &d);
    }
}

/* pull audio while either side has an utterance going */
static int audio_blocks(long max)
{
    static short bd[512], bp[512];
    short na, nb;
    long guard = 0;
    int i;

    while ((sd.c->out_active || sp.c->out_active) && max-- != 0) {
        checks++;
        if (sd.c->out_active != sp.c->out_active || ++guard > 200000) {
            Diff d = {"", 0, sd.c->out_active, sp.c->out_active};
            fail("out_active", &d);
            return 0;
        }
        memset(bd, 0x55, sizeof bd);
        memset(bp, 0x55, sizeof bp);
        na = dll_synt_generate(syd, bd, 441);
        nb = synt_generate(syp, bp, 441);
        if (na != nb || memcmp(bd, bp, sizeof bd) != 0) {
            for (i = 0; i < 441 && bd[i] == bp[i]; i++)
                ;
            {
                Diff d = {"", (size_t)(samples + i), (uint16_t)bd[i], (uint16_t)bp[i]};
                fail("sample", &d);
            }
            return 0;
        }
        for (i = 0; i < 441; i++)
            nonzero += bd[i] != 0;
        samples += 441;
        if (wav != NULL) {
            fwrite(bp, 2, 441, wav);
            wav_samples += 441;
        }
    }
    return 1;
}

static int audio(void)
{
    return audio_blocks(-1);
}

/* the engine's stop (0x100119b1-0x100119e1) on both sides: reset 1, reset 2, drop the frame */
static int stop_both(long blocks)
{
    if (!audio_blocks(blocks))
        return 0;
    ctrl_both(TTS_RESET, 1);
    ctrl_both(TTS_RESET, 2);
    dll_synt_ctrl(syd, SYNT_FLUSH, NULL);
    synt_ctrl(syp, SYNT_FLUSH, NULL);
    stops++;
    compare_cores("stop");
    checks++;
    if (sd.c->out_active || sp.c->out_active) {
        Diff d = {"", 0, sd.c->out_active, sp.c->out_active};
        fail("out_active after stop", &d);
        return 0;
    }
    return 1;
}

static int feed(unsigned char ch)
{
    int ra, rb, tries;

    /* the engine's protocol (0x10012345): after 1 or 2 the audio plays to the end of the
     * utterance; after 2 (synth busy) the same char is sent again */
    for (tries = 0; tries < 8; tries++) {
        ra = dll_tp_putc(sd.c, ch);
        rb = tp_putc(sp.c, ch);
        chars++;
        checks++;
        if (ra >= 0 && ra <= 2)
            returns[ra]++;
        if (ra != rb) {
            Diff d = {"", 0, (uint32_t)ra, (uint32_t)rb};
            fail("tp_putc", &d);
            return 0;
        }
        compare_cores("tp_putc");
        if (stop_mode && sd.c->out_active && rnd(2) == 0)
            return stop_both((long)rnd(30));   /* the char goes with the rest of the text */
        if (!audio())
            return 0;
        compare_cores("after audio");
        if (ra != 2)
            return 1;
    }
    fail("tp_putc still busy after 8 tries", NULL);
    return 0;
}

static void voice_change(int w)
{
    switch (w % 8) {
    case 0: ctrl_both(TTS_RATE, 20 + w * 7 % 70); break;
    case 1: ctrl_both(TTS_PITCH, 10 + w * 13 % 80); break;
    case 2: ctrl_both(TTS_FD, w * 17 % 101); break;
    case 3: ctrl_both(TTS_SET_VOICE, w % 10); break;
    case 4: ctrl_both(TTS_STORE_VOICE, 5 + w % 5); break;
    case 5: ctrl_both(TTS_RATE, -6 - w % 2); break;
    case 6: ctrl_both(TTS_PITCH, -2 - w % 6); break;
    default: break;
    }
}

static void test_audio(const char *bindir, const Lang *l, const Config *cf, int write_wav)
{
    const unsigned char *p, *t = (const unsigned char *)l->text;
    long s0 = samples;
    int w = 0, ok = 1;
    long fail0 = failures;

    cur_part = cf->name;
    stop_mode = cf->stops;
    if (stop_mode)
        rng = 0x5eed5u + (uint32_t)(l - langs) * 31u;
    if (!load_bodies(bindir, l, 1))
        return;
    sd.c = sp.c = NULL;
    dll_tts_ctrl(&sd.c, TTS_CREATE, (void *)900);
    tts_ctrl(&sp.c, TTS_CREATE, (void *)900);
    build_map();
    ctrl_both(TTS_CHARSET, 2);
    dll_tts_ctrl(sd.c, TTS_BIND, sd.file[0] + 0x200);
    tts_ctrl(sp.c, TTS_BIND, sp.file[0] + 0x200);
    ctrl_both(TTS_MODE, cf->mode);
    compare_cores("setup");

    syd = syp = NULL;
    dll_synt_ctrl((Synth *)&syd, 0, NULL);
    synt_ctrl((Synth *)&syp, 0, NULL);
    dll_synt_ctrl(syd, SYNT_SET_FRAMECB, (void *)dll_next_frame);
    synt_ctrl(syp, SYNT_SET_FRAMECB, (void *)op_next_frame);
    dll_synt_ctrl(syd, SYNT_SET_FRAMEARG, sd.c);
    synt_ctrl(syp, SYNT_SET_FRAMEARG, sp.c);
    dll_synt_ctrl(syd, SYNT_SAMPLE_RATE, (void *)(intptr_t)cf->fs);
    synt_ctrl(syp, SYNT_SAMPLE_RATE, (void *)(intptr_t)cf->fs);

    wav = NULL;
    if (write_wav) {
        char path[256], name[64];

        snprintf(name, sizeof name, "%s", l->file);
        *strchr(name, '.') = 0;
        CreateDirectoryA("build\\wav", NULL);
        snprintf(path, sizeof path, "build\\wav\\%s_%d.wav", name, (int)(cf - configs));
        wav = fopen(path, "wb");
        wav_samples = 0;
        if (wav != NULL)
            wav_header(wav, cf->fs, 0);
    }

    for (p = t; *p && ok; p++) {
        if (p == t || p[-1] == ' ') {
            ctrl_both(TTS_MARK, 0x10001 * (int)(p - t + 1));
            if (cf->voices)
                voice_change(w);
            w++;
        }
        ok = feed(*p);
    }
    if (ok)
        feed(0x17);   /* end of text, as the engine sends */
    if (ok) {
        /* a reset after use relinks a pool whose nodes carry old links and ext pointers */
        ctrl_both(TTS_RESET, 2);
        compare_cores("reset 2 after text");
        ctrl_both(TTS_RESET, 0);
        compare_cores("reset after text");
    }

    if (wav != NULL) {
        wav_header(wav, cf->fs, wav_samples);
        fclose(wav);
        wav = NULL;
    }
    printf("%-12s %-12s %8ld samples: %s\n", l->file, cf->name, samples - s0,
           failures == fail0 ? "ok" : "MISMATCH");
    dll_synt_ctrl(syd, SYNT_DESTROY, NULL);
    synt_ctrl(syp, SYNT_DESTROY, NULL);
    destroy_both();
    free_bodies();
}

int main(int argc, char **argv)
{
    const char *bindir = argc > 1 ? argv[1] : "bin";
    int write_wav = argc > 2 && strcmp(argv[2], "wav") == 0;
    int i, k;

    setvbuf(stdout, NULL, _IONBF, 0);
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);   /* crash = exit at once */
    if (!dll_load(bindir))
        return 2;
    dll_synt_ctrl = (synt_ctrl_fn)dll_va(VA_SYNT_CTRL);
    dll_synt_generate = (synt_gen_fn)dll_va(VA_SYNT_GENERATE);

    for (i = 0; i < nlangs; i++) {
        cur_lang = langs[i].file;
        test_swap(bindir, &langs[i]);
        test_commands(bindir, &langs[i], i);
    }
    printf("swap and commands: %ld commands compared (per command:", cmds);
    for (k = 0; k < TTS_NCMD; k++)
        printf(" %lx:%ld", (unsigned long)k, cmd_count[k]);
    printf(")\n\n");

    for (k = 0; k < NCONFIGS; k++)
        for (i = 0; i < nlangs; i++) {
            cur_lang = langs[i].file;
            test_audio(bindir, &langs[i], &configs[k], write_wav);
        }

    printf("\n%ld tts_ctrl commands, %ld tp_putc calls (returned 0/1/2: %ld/%ld/%ld), %ld stops, "
           "%ld samples compared (%ld non-zero), %ld checks\n", cmds, chars, returns[0], returns[1],
           returns[2], stops, samples, nonzero, checks);
    printf("\n%ld failures\n", failures);
    return failures != 0;
}
