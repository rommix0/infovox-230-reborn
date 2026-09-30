/* Differential test: the text side (src/tpchar.c, tpstr.c, the ttsctrl.c char helpers and
 * ctl_word_end) against bin/Ivx230nt.dll, and with it the whole port from text to frames.
 *
 * Each run binds a language in the DLL, copies the Core and pool into an image (harness.c)
 * and then feeds the same text to both: the DLL's tp_putc on its Core, the port's tp_putc
 * on the image. Everything below tp_putc runs as DLL or as port respectively (rule
 * modules, op_start_utterance, frames). After every char the return values, every dword
 * of the Core and the pool (pointers through the map) and the callback output must match;
 * the frames are drained after every char, DLL and port side by side.
 * The runs cover the engine's settings (mode 8, charset 2) and the other modes (word at a
 * time, char mode, mode 0x80/0x10, no synthesis), all callbacks (user and exception
 * dictionaries rewrite some words; the word-position callback makes the synth "busy" now
 * and then), in-text bookmarks and phonetic input, and pools small enough to run out of
 * nodes.
 */
#include <stdarg.h>
#include <windows.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"
#include "tp.h"

static long failures, putcs, frames, returns[3], runs;
static int max_reports = 20;
static int side;            /* 0 = DLL, 1 = port: callbacks log per side */
static char cblog[2][16384];
static size_t cblen[2];
static long n_cb[8];        /* status wordpos bookmark phonemes userdict userdict>0 excdict excdict!=NULL */

static void log_add(const char *fmt, ...)
{
    char s[600];
    va_list ap;
    size_t n;

    va_start(ap, fmt);
    vsnprintf(s, sizeof s, fmt, ap);
    va_end(ap);
    n = strlen(s);
    if (cblen[side] + n + 1 < sizeof cblog[side]) {
        memcpy(cblog[side] + cblen[side], s, n + 1);
        cblen[side] += n;
    }
}

static void log_reset(void)
{
    cblen[0] = cblen[1] = 0;
    cblog[0][0] = cblog[1][0] = 0;
}

/* ---- callbacks (cb_arg is the Core of the side that calls) ---- */

static int busy_every;      /* > 0: every n-th word position callback sets synth_state 0xff */
static long wordpos_n[2];

static void __cdecl cb_status(void *arg, int st)
{
    (void)arg;
    n_cb[0] += side == 0;
    log_add("S%d ", (short)st);   /* the DLL passes junk in the high half */
}

static void __cdecl cb_wordpos(void *arg, int pos)
{
    n_cb[1] += side == 0;
    log_add("W%d ", (short)pos);
    wordpos_n[side]++;
    if (busy_every > 0 && wordpos_n[side] % busy_every == busy_every / 2)
        ((Core *)arg)->synth_state = 0xff;
}

static void __cdecl cb_bookmark(void *arg, int v)
{
    (void)arg;
    n_cb[2] += side == 0;
    log_add("B%x ", v);
}

static void __cdecl cb_phonemes(void *arg, char *s)
{
    (void)arg;
    n_cb[3] += side == 0;
    log_add("P%s ", s);
}

/* user dictionary: 5-letter capitalised words become another word (the replacement is
 * read again), 9-letter words become phonetic input (their letters as symbols), and
 * words of 13+ letters get a replacement that never fits the text buffer */
static char ud_out[2][800];

static int __cdecl cb_userdict(void *arg, char *word, char **out)
{
    size_t n = strlen(word), i;

    (void)arg;
    n_cb[4] += side == 0;
    log_add("U%s ", word);
    if (n == 5 && word[0] >= 'A' && word[0] <= 'Z') {
        strcpy(ud_out[side], "Xylophonic");
        *out = ud_out[side];
        n_cb[5] += side == 0;
        return 1;
    }
    if (n == 9) {
        for (i = 0; i < n; i++)
            ud_out[side][2 * i] = (char)(word[i] >= 'a' && word[i] <= 'z' ? word[i] - 32 : word[i]),
            ud_out[side][2 * i + 1] = ' ';
        ud_out[side][2 * n - 1] = 0;
        *out = ud_out[side];
        n_cb[5] += side == 0;
        return 2;
    }
    if (n >= 13) {
        memset(ud_out[side], 'a', 700);
        ud_out[side][700] = 0;
        *out = ud_out[side];
        n_cb[5] += side == 0;
        return 1;
    }
    return 0;
}

/* exception dictionary: gets the word's symbol names; some get them back as the
 * transcription, some an empty one */
static char ex_out[2][128];

static char *__cdecl cb_excdict(void *arg, char *phon, int x)
{
    size_t n = strlen(phon);

    (void)arg;
    n_cb[6] += side == 0;
    log_add("E%s,%d ", phon, x);
    if (n % 7 == 3 || n % 11 == 5) {
        n_cb[7] += side == 0;
        snprintf(ex_out[side], sizeof ex_out[side], "%s", n % 7 == 3 ? phon : "");
        return ex_out[side];
    }
    return NULL;
}

/* ---- runs ---- */

typedef struct {
    const char *name;
    int npool;
    int mode;           /* tts_ctrl 5 */
    int out_flags;      /* tts_ctrl 0x10 */
    int charset;        /* tts_ctrl 4 */
    int dicts;          /* user and exception dictionary callbacks */
    int busy;           /* busy_every */
    int flush;          /* Core.flush_on_input (nothing in the DLL sets it) */
    int count_init;     /* Core.count_init (likewise) */
    int extra;          /* feed the stress text too */
    int maxlen;         /* > 0: only the first maxlen chars of the language text */
} Run;

static const Run run_list[] = {
    {"engine",      900, 8,    2, 2, 1, 29, 0, 3, 1, 0},
    {"word-now",    900, 4,    9, 2, 1, 0,  0, 0, 1, 0},
    {"mode 0x90",   900, 0x90, 2, 2, 0, 0,  0, 0, 1, 0},
    {"char mode",   900, 2,    3, 2, 0, 0,  1, 0, 0, 90},
    {"no synth",    900, 8,    5, 2, 1, 0,  0, 0, 1, 0},
    {"no synth 4",  900, 4,    4, 2, 0, 0,  0, 0, 1, 0},
    {"charset 0",   900, 8,    0, 0, 0, 0,  0, 0, 0, 0},
    {"charset 1",   900, 8,    0, 1, 0, 0,  0, 0, 0, 0},
    {"hold+now",    900, 12,   2, 2, 1, 0,  0, 0, 1, 0},
    /* out of nodes (mode 4 alone does not run out at these sizes) */
    {"pool 80",     80,  8,    2, 2, 1, 0,  0, 0, 1, 0},
    {"pool 120",    120, 8,    2, 2, 1, 0,  0, 0, 1, 0},
    {"pool 80 h+n", 80,  12,   2, 2, 1, 0,  0, 0, 1, 0},
};
#define NRUNS ((int)(sizeof run_list / sizeof run_list[0]))

/* esc_mark = 0x01, esc_phon = 0x02 (the defaults) */
static const char stress_text[] =
    " \x01" "ABC" "Marked text. \x01\xe9\x80\xff" "High mark. Phonetic \x02" "A B C" "\x02 input, "
    "\x02" "A B\x01" "XYZ" "C \xe4\xf6\x02 and "
    "\x02" "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
    "\x02 skip. co-operation well-known re-\r\nentry x-ray, go- on. "
    "Numbers 3.14 and 1,000,000 or 12:30, 1.2.3 then 7. "
    "Tokens #1 $5 50% a&b c/d <x> [y] {z} ~ ^ | _ @ ... "
    "Ctl \x0b\x0c\x7f\t chars. "
    "Veryveryveryveryveryveryveryveryveryveryveryveryveryveryveryveryveryveryveryveryveryverylongword ok. "
    "\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4"
    "\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4"
    "\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4\xe4 too long. "
    "1234567890123456789012345678901234567890123456789012345678901234567890123456789012345678901234567890 "
    "digits. ================================================================================================ "
    "Line one\r\rLine two\r\r"
    "UPPER lower MiXeD a b c Remarkable. M\xfc" "ller na\xef" "ve \xc6" "sop. Question? Exclamation! Semicolon; colon: "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "aa bb cc dd ee ff gg hh ii jj kk ll mm nn oo pp qq rr ss tt uu vv ww xx yy zz "
    "end";

static Core *da;            /* DLL side */
static Image img;           /* port side: img.core */
static const char *cur_lang, *cur_run;
static int cur_off;
static int run_failed;

static void fail(const char *what, const Diff *d)
{
    failures++;
    run_failed = 1;
    if (max_reports-- > 0) {
        printf("FAIL %s [%s] offset %d: %s", cur_lang, cur_run, cur_off, what);
        if (d != NULL)
            printf(": %s +0x%x: dll %08x port %08x", d->what, (unsigned)d->off, d->av, d->bv);
        printf("\n");
        if (strcmp(cblog[0], cblog[1]) != 0)
            printf("  dll  cb: %.300s\n  port cb: %.300s\n", cblog[0], cblog[1]);
    }
}

/* Small pools reach two DLL bugs on out-of-nodes paths: a crash (0x1001e56e,
 * op_seg_durations reads through a NULL default) and a livelock (a word that does not fit
 * even alone makes tp_putc restart the sentence forever; the port does the same). A
 * vectored handler turns an access violation, and a watchdog thread a call that makes no
 * progress for WATCHDOG_S seconds, into a jump out of the run. On the DLL side the run is
 * abandoned (reported, not a failure); on the port side it fails. */
#define VA_KNOWN_CRASH 0x1001e56e
#define WATCHDOG_S     15
static void *run_jmp[5];
static volatile int in_run;
static volatile long progress;
static HANDLE main_thread;
static uint32_t crash_eip;
static int crash_side, crash_hang;
static long dll_crashes, dll_hangs;

static void crash_recover(void)
{
    __builtin_longjmp(run_jmp, 1);
}

static LONG CALLBACK on_exception(EXCEPTION_POINTERS *e)
{
    if (in_run && e->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        in_run = 0;
        crash_eip = (uint32_t)e->ContextRecord->Eip;
        crash_side = side;
        crash_hang = 0;
        e->ContextRecord->Eip = (DWORD)(uintptr_t)crash_recover;
        return EXCEPTION_CONTINUE_EXECUTION;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static DWORD WINAPI watchdog(void *arg)
{
    long last = -1;
    int idle = 0;
    CONTEXT ctx;

    (void)arg;
    for (;;) {
        Sleep(1000);
        if (!in_run || progress != last) {
            last = progress;
            idle = 0;
            continue;
        }
        if (++idle < WATCHDOG_S)
            continue;
        SuspendThread(main_thread);
        ctx.ContextFlags = CONTEXT_CONTROL;
        if (in_run && GetThreadContext(main_thread, &ctx)) {
            in_run = 0;
            crash_eip = (uint32_t)ctx.Eip;
            crash_side = side;
            crash_hang = 1;
            ctx.Eip = (DWORD)(uintptr_t)crash_recover;
            SetThreadContext(main_thread, &ctx);
        }
        ResumeThread(main_thread);
        idle = 0;
    }
    return 0;
}

static void compare_state(const char *what)
{
    Diff d;
    Core *b = img.core;

    memcpy(b->jmpbuf, da->jmpbuf, sizeof da->jmpbuf);   /* only the DLL calls setjmp */
    if (compare_core_pool(&img.map, da, b, img.pool_bytes, &d))
        fail(what, &d);
    else if (cblen[0] != cblen[1] || memcmp(cblog[0], cblog[1], cblen[0]) != 0)
        fail(what, NULL);
    log_reset();
}

static void drain(void)
{
    short *fa, *fb;
    int oa, ob;
    Diff d;

    for (;;) {
        side = 0;
        fa = dll_next_frame(da);
        side = 1;
        fb = op_next_frame(img.core);
        oa = fa == NULL ? -1 : (int)(fa - (short *)da);
        ob = fb == NULL ? -1 : (int)(fb - (short *)img.core);
        if (oa != ob) {
            Diff s = {"frame pointer", 0, (uint32_t)oa, (uint32_t)ob};
            fail("frame", &s);
            return;
        }
        if (fa == NULL)
            break;
        frames++;
        progress++;
        if (compare_words(&img.map, da->outval, img.core->outval, sizeof da->outval, "outval", &d)) {
            fail("frame", &d);
            return;
        }
    }
    compare_state("after frames");
}

static void feed(unsigned char ch)
{
    int ra, rb, tries;

    for (tries = 0; tries < 2 && !run_failed; tries++) {
        side = 0;
        ra = dll_tp_putc(da, ch);
        side = 1;
        rb = tp_putc(img.core, ch);
        putcs++;
        progress++;
        if (ra != rb) {
            Diff s = {"return", 0, (uint32_t)ra, (uint32_t)rb};
            fail("tp_putc", &s);
            return;
        }
        if (ra >= 0 && ra <= 2)
            returns[ra]++;
        compare_state("tp_putc");
        if (run_failed)
            return;
        drain();
        if (ra != 2)
            break;
        /* busy: the synth "finishes", the char is sent again */
        da->synth_state = 0;
        img.core->synth_state = 0;
    }
}

static void both_ctrl(int cmd, intptr_t arg)
{
    dll_tts_ctrl(da, cmd, (void *)arg);
    dll_tts_ctrl(img.core, cmd, (void *)arg);   /* plain setters, safe on the image */
}

static void speak(const char *text, int maxlen)
{
    const unsigned char *p, *t = (const unsigned char *)text;

    for (p = t; *p && !run_failed && (maxlen <= 0 || p - t < maxlen); p++) {
        cur_off = (int)(p - t);
        if (p == t || p[-1] == ' ')
            both_ctrl(0x20, 0x10001 * (int)(p - t + 1));
        feed(*p);
    }
}

static void test_run(const char *bindir, const Lang *l, const Run *r)
{
    unsigned char *buf;
    long fail0 = failures, frames0 = frames;

    cur_lang = l->file;
    cur_run = r->name;
    cur_off = -1;
    run_failed = 0;
    da = lang_open(bindir, l, r->npool, &buf);
    if (da == NULL) {
        failures++;
        return;
    }
    dll_tts_ctrl(da, 4, (void *)(intptr_t)r->charset);
    dll_tts_ctrl(da, 5, (void *)(intptr_t)r->mode);
    dll_tts_ctrl(da, 0x10, (void *)(intptr_t)r->out_flags);
    dll_tts_ctrl(da, 0x16, da);
    dll_tts_ctrl(da, 0x13, (void *)cb_status);
    dll_tts_ctrl(da, 0x1a, (void *)cb_wordpos);
    dll_tts_ctrl(da, 0x1f, (void *)cb_bookmark);
    dll_tts_ctrl(da, 0x11, (void *)cb_phonemes);
    if (r->dicts) {
        dll_tts_ctrl(da, 0x14, (void *)cb_userdict);
        dll_tts_ctrl(da, 0x15, (void *)cb_excdict);
    }
    da->flush_on_input = (unsigned char)r->flush;
    da->count_init = (short)r->count_init;
    busy_every = r->busy;
    wordpos_n[0] = wordpos_n[1] = 0;
    log_reset();
    image_take(&img, da);
    runs++;

    side = 0;
    if (__builtin_setjmp(run_jmp) == 0) {
        in_run = 1;
        speak(l->text, r->maxlen);
        if (r->extra)
            speak(stress_text, 0);
        cur_off = -2;
        if (!run_failed)
            feed(0x17);   /* end of text, as the engine sends */
        in_run = 0;
    } else {
        /* crashed or hung: leave this run's memory alone */
        if (crash_side == 0 && crash_hang) {
            dll_hangs++;
            printf("%-12s %-11s pool %3d: the DLL did not return in %d s at offset %d (eip "
                   "0x%x, out-of-nodes restart loop); run abandoned\n", l->file, r->name,
                   r->npool, WATCHDOG_S, cur_off, (unsigned)(crash_eip - (uint32_t)(uintptr_t)dll_base + 0x10000000u));
        } else if (crash_side == 0 && crash_eip == (uint32_t)(uintptr_t)dll_va(VA_KNOWN_CRASH)) {
            dll_crashes++;
            printf("%-12s %-11s pool %3d: the DLL crashed at 0x%x (its NULL read in "
                   "op_seg_durations) at offset %d; run abandoned\n",
                   l->file, r->name, r->npool, VA_KNOWN_CRASH, cur_off);
        } else {
            Diff d = {"eip, side", 0, crash_eip, (uint32_t)crash_side};
            fail(crash_hang ? "no progress" : "access violation", &d);
        }
        return;
    }

    printf("%-12s %-11s pool %3d: %6ld frames: %s\n", l->file, r->name, r->npool, frames - frames0,
           failures == fail0 ? "ok" : "MISMATCH");
    image_free(&img);
    lang_close(da, buf);
}

int main(int argc, char **argv)
{
    const char *bindir = argc > 1 ? argv[1] : "bin";
    const char *sel = argc > 2 ? argv[2] : NULL;   /* run numbers, e.g. "0,8" */
    int i, k;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!dll_load(bindir))
        return 2;
    AddVectoredExceptionHandler(1, on_exception);
    DuplicateHandle(GetCurrentProcess(), GetCurrentThread(), GetCurrentProcess(), &main_thread,
                    0, FALSE, DUPLICATE_SAME_ACCESS);
    CreateThread(NULL, 0, watchdog, NULL, 0, NULL);
    if (sel == NULL) {
        for (k = 0; k < NRUNS; k++)
            for (i = 0; i < nlangs; i++)
                test_run(bindir, &langs[i], &run_list[k]);
    } else {
        for (; *sel; sel += *sel == ',') {
            Run r;

            k = (int)strtol(sel, (char **)&sel, 10);
            if (k < 0 || k >= NRUNS)
                continue;
            r = run_list[k];
            if (argc > 3)
                r.npool = atoi(argv[3]);   /* pool size override, for exploring */
            for (i = 0; i < nlangs; i++)
                test_run(bindir, &langs[i], &r);
        }
    }

    printf("\n%ld runs (abandoned: %ld DLL crashes, %ld DLL livelocks), %ld tp_putc calls "
           "compared (returned 0/1/2: %ld/%ld/%ld), %ld frames\n",
           runs, dll_crashes, dll_hangs, putcs, returns[0], returns[1], returns[2], frames);
    printf("DLL callbacks: %ld status, %ld word position, %ld bookmark, %ld phonemes, "
           "%ld user dictionary (%ld rewrites), %ld exception dictionary (%ld replies)\n",
           n_cb[0], n_cb[1], n_cb[2], n_cb[3], n_cb[4], n_cb[5], n_cb[6], n_cb[7]);
#ifdef CW_COVERAGE
    {
        static const char *cov[TPC_N] = TP_COV_NAMES;

        printf("port branches:");
        for (i = 0; i < TPC_N; i++)
            printf("%s %s %lu", i ? "," : "", cov[i], tp_cov[i]);
        printf("\n");
    }
#endif
    printf("\n%ld failures\n", failures);
    return failures != 0;
}
