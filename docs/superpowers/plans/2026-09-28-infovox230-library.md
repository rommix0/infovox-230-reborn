# infovox230 Library Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Package the bit-exact C port of `Ivx230nt.dll` as a shared library, `infovox230.dll` on Windows and `libinfovox230.so` on Linux. It has a pull-based streaming API. `ivxsay` becomes a client of the library that writes WAV files or plays through the speaker.

**Architecture:** `lib/infovox230.c` wraps one `Core` (driven through `tts_ctrl`/`tp_putc`) and one mode-0 synth per opaque `IvxEngine`. Text, marks, settings and flushes go into one ordered queue. `ivx_read` runs that queue through the engine's `tp_putc` resend protocol and plays each utterance in 441-sample blocks, so the audio stays byte-identical to what `tests/tts_cmp.c` verifies against the DLL. Events are stamped with the exact output sample, using a wrapper around the synth's frame callback.

**Tech Stack:** C11 and 32-bit gcc: winlibs mingw32 on Windows, gcc-multilib under WSL Ubuntu. GNU make. Python 3 for `tools/check_ivxsay.py`. winmm (`waveOut`) and ALSA for `ivxsay` playback only.

**Spec:** `docs/superpowers/specs/2026-09-28-infovox230-library-api-design.md`. Read it before starting; this plan implements it section by section.

## Global Constraints

- **32-bit builds only** (`-m32`). Keep `-msse2 -mfpmath=sse`: the synth tables are only bit-exact with SSE2 double math.
- **Names:** `infovox230.dll`, `libinfovox230.dll.a`, `infovox230.def` on Windows; `libinfovox230.so` with soname `libinfovox230.so.0` on Linux; header `include/infovox230.h`; implementation `lib/infovox230.c`. Every public function starts with `ivx_`.
- **Exports:** only the `ivx_*` functions are exported. Build with `-fvisibility=hidden`, and use `IVX_API` (`__declspec(dllexport/dllimport)` on Windows, `visibility("default")` elsewhere). `IVX_CALL` is `__cdecl` on Windows.
- **The library has no audio-device code.** Output is 16-bit mono PCM plus events.
- **One thread at a time per engine;** separate engines are independent.
- **Text input:** UTF-8 by default (`IVX_ENC_UTF8`), converted to Windows-1252. `IVX_ENC_CP1252` passes bytes through unchanged.
- **Audio blocks:** 441 samples per `synt_generate` call, never configurable. The audio must not depend on the caller's buffer size.
- **Protected files:** don't modify anything in `bin/`, or the papers (`docs/*.pdf`).
- **No git:** the project isn't a git repository and nothing is committed. Each task ends with a checkpoint step: a full `make test` run with its results written down.
- **Toolchain on Windows** (Git Bash): run `export PATH=/c/winlibs/mingw32/bin:$PATH` first, from the repo root `C:\Users\abart\Desktop\infovox_disasm`.
- **Bash heredocs mangle `\\n`, `\\x` and `\\f`.** Write files containing C escapes with an editor tool, not `cat <<EOF`.
- **Confidence tags:** tag every new fact in docs `[CONFIRMED]`, `[LIKELY]`, `[HYPOTHESIS]` or `[NOT FOUND]`, with its VA.
- **C string literals:** a `\x` escape swallows every hex digit after it, so `"v\xe4dret"` is an error. Split the literal after the escape when a hex digit follows: `"v\xe4" "dret"`.

## File map

| File | Status | Responsibility |
|---|---|---|
| `include/infovox230.h` | new | The public API (types, constants, prototypes, docs) |
| `lib/infovox230.c` | new | The engine object, queue, read loop, encoding, events, cancel |
| `tests/api_test.c` | new | The library's tests (no DLL; builds on Windows and Linux) |
| `cli/ivxsay.c` | rewrite | Command-line client of the library |
| `cli/audio.h`, `cli/audio_win.c`, `cli/audio_alsa.c` | new | Speaker output for `ivxsay` |
| `tests/tts_cmp.c` | modify | Part-3 "stops" configuration: the engine's stop sequence, DLL versus port |
| `tools/check_ivxsay.py` | modify | `-e cp1252`, and an optional path to `ivxsay` |
| `Makefile` | modify | Library, `api_test`, `ivxsay` on the library, Linux branch |
| `AGENTS.md`, `REFERENCE.md` | modify | Docs (Task 8) |

Port internals the library uses (all already exist):

| Symbol | Where | Use |
|---|---|---|
| `tts_ctrl(void *h, int cmd, void *arg)` | `src/tts.h` | `TTS_CREATE` (h = `Core **`), `TTS_DESTROY`, `TTS_RESET` (1 text, 2 output), `TTS_BIND`, `TTS_CHARSET`, `TTS_MODE`, `TTS_SET_VOICE`, `TTS_FD`, `TTS_RATE`, `TTS_PITCH` (−1 queries), `TTS_OUT_FLAGS`, `TTS_CB_PHONEMES`, `TTS_CB_STATUS`, `TTS_CB_ARG`, `TTS_CB_BOOKMARK`, `TTS_MARK`, `TTS_IVX_SWAP` (h unused) |
| `int tp_putc(Core *c, unsigned char ch)` | `src/tp.h` | Returns 0, 1 (utterance ready) or 2 (busy: send the same char again later) |
| `short *op_next_frame(Core *c)` | `src/op.h` | The frame source |
| `synt_ctrl(Synth *s, short cmd, void *arg)`, `synt_generate(Synth *s, void *buf, short n)` | `src/synth.h` | `SYNT_SET_FRAMECB`, `SYNT_SET_FRAMEARG`, `SYNT_FRAME_RATE`, `SYNT_FLUSH`, `SYNT_SAMPLE_RATE`, `SYNT_DESTROY`. `synt_ctrl((Synth *)&ptr, 0, NULL)` creates a synth. |
| `Core` fields | `tools/ghidra/ivx_types.h` | `out_active`, `esc_phon`, and the callbacks `cb_bookmark(void *, int)`, `cb_status(void *, int)`, `cb_phonemes(void *, char *)` |
| `Synth.frame_left` | `ivx_types.h` | Set to the frame length just before `get_frame` is called (`src/synth.c:179`) |

---

### Task 1: Library skeleton, language loading, settings and build

**Files:**
- Create: `include/infovox230.h`
- Create: `lib/infovox230.c`
- Create: `tests/api_test.c`
- Modify: `Makefile` (whole file shown below)

**Interfaces:**
- Consumes: the port internals in the file map.
- Produces, for all later tasks:
  - Every declaration in `include/infovox230.h`. Implemented in this task: `ivx_create`, `ivx_destroy`, `ivx_load_language`, `ivx_load_language_mem`, `ivx_set`, `ivx_get`, `ivx_strerror`, `ivx_version`.
  - In `lib/infovox230.c`:
    - the `struct IvxEngine` fields as written below;
    - `Fifo` with `fifo_at`, `fifo_push` (returns 0 or `IVX_E_NOMEM`), `fifo_pop`, `fifo_clear`;
    - `idle(const IvxEngine *)`, `frame_cb(void *)`;
    - the `QItem`/`Mark` types and the `Q_CHAR`/`Q_MARK`/`Q_SET`/`Q_FLUSH` kinds;
    - `BLOCK` (441), `NPARAMS`, `NQUEUED`.
  - In `tests/api_test.c`: `CHECK(cond, fmt, ...)`, `lang_path(code)`, `read_file(path, &size)`, `engine(code, enc)`, `failures`, `checks`, `bindir`.

- [ ] **Step 1: Write the public header** `include/infovox230.h`

```c
/* infovox230: the Infovox 230 text-to-speech engine (C port of Ivx230nt.dll) as a library.
 *
 * Usage: create an engine, load a language (.ivx file), queue text with ivx_feed (and
 * ivx_mark / settings), end it with ivx_flush, then pull 16-bit mono samples with
 * ivx_read until it returns 0 and take the events with ivx_next_event. Nothing is
 * synthesized until ivx_read is called; queued marks and settings take effect exactly
 * where they were queued in the text. The audio comes in blocks of 441 samples (each
 * utterance is padded with silence to a whole block) and does not depend on how many
 * samples each ivx_read asks for.
 *
 * An engine may be used by one thread at a time; separate engines are independent.
 * Errors are negative IVX_E_* codes. Design: docs/superpowers/specs/
 * 2026-09-28-infovox230-library-api-design.md
 */
#ifndef INFOVOX230_H
#define INFOVOX230_H

#include <stddef.h>
#include <stdint.h>

#define IVX_VERSION 0x00010000   /* major << 16 | minor */

#if defined(_WIN32)
#  ifdef IVX_BUILD
#    define IVX_API __declspec(dllexport)
#  else
#    define IVX_API __declspec(dllimport)
#  endif
#  define IVX_CALL __cdecl
#else
#  define IVX_API __attribute__((visibility("default")))
#  define IVX_CALL
#endif

#ifdef __cplusplus
extern "C" {
#endif

typedef struct IvxEngine IvxEngine;

/* errors */
enum {
    IVX_OK        = 0,
    IVX_E_NOMEM   = -1,   /* out of memory */
    IVX_E_IO      = -2,   /* the language file cannot be read */
    IVX_E_BADFILE = -3,   /* not a usable .ivx file (too short, bad offsets, bind refused) */
    IVX_E_BUSY    = -4,   /* idle-only call while text is queued or audio is playing */
    IVX_E_NOLANG  = -5,   /* no language loaded */
    IVX_E_RANGE   = -6,   /* unknown parameter or value out of range */
    IVX_E_ARG     = -7    /* NULL engine, buffer or callback */
};

/* parameters for ivx_set / ivx_get */
enum {
    IVX_VOICE,            /* 0..4 built-in voices, 5..9 stored (copies of voice 0); queued */
    IVX_RATE,             /* 0..100; queued */
    IVX_PITCH,            /* 0..100; queued */
    IVX_FD,               /* 0..100; queued */
    IVX_FRAME_RATE,       /* synth frames per second 2..1000, 100 = normal speed; queued */
    IVX_SAMPLE_RATE,      /* 8000..48000 Hz, default 22050; idle only */
    IVX_TEXT_MODE,        /* engine text-mode flags 0..0x1ff, default 8 (whole sentences); idle only */
    IVX_CHARSET,          /* engine charset flags 0..0x1f, default 2; idle only */
    IVX_INPUT_ENCODING,   /* IVX_ENC_UTF8 (default) or IVX_ENC_CP1252; idle only */
    IVX_EVENT_MASK        /* bits 1 << IVX_EV_*; default IVX_EVMASK_DEFAULT; idle only */
};

enum { IVX_ENC_UTF8, IVX_ENC_CP1252 };

/* events */
enum {
    IVX_EV_MARK,          /* value = the id given to ivx_mark */
    IVX_EV_PHONEME,       /* value = segment name, 1-2 chars, first char in the low byte */
    IVX_EV_ERROR,         /* value = engine status code; the audio carries on */
    IVX_EV_DONE           /* all flushed text has been spoken; value = 0 */
};
#define IVX_EVMASK_DEFAULT ((1 << IVX_EV_MARK) | (1 << IVX_EV_ERROR) | (1 << IVX_EV_DONE))
#define IVX_EVMASK_ALL     0xf

typedef struct {
    int      type;        /* IVX_EV_* */
    uint32_t value;
    uint64_t sample;      /* position in this engine's output, in samples */
} IvxEvent;

/* lifecycle */
IVX_API IvxEngine *IVX_CALL ivx_create(int *err);
IVX_API void IVX_CALL ivx_destroy(IvxEngine *e);
IVX_API int IVX_CALL ivx_load_language(IvxEngine *e, const char *path);
IVX_API int IVX_CALL ivx_load_language_mem(IvxEngine *e, const void *ivx, size_t n);

/* settings: queued ones apply in stream order, the others only while idle */
IVX_API int IVX_CALL ivx_set(IvxEngine *e, int param, int value);
IVX_API int IVX_CALL ivx_get(IvxEngine *e, int param, int *value);

/* streaming */
IVX_API int IVX_CALL ivx_feed(IvxEngine *e, const char *text, size_t len);
IVX_API int IVX_CALL ivx_feed_phonetic(IvxEngine *e, const char *sym, size_t len);
IVX_API int IVX_CALL ivx_mark(IvxEngine *e, uint32_t id);
IVX_API int IVX_CALL ivx_flush(IvxEngine *e);
IVX_API int IVX_CALL ivx_read(IvxEngine *e, short *pcm, int max);
IVX_API int IVX_CALL ivx_next_event(IvxEngine *e, IvxEvent *ev);
IVX_API void IVX_CALL ivx_cancel(IvxEngine *e);

/* convenience: feed, flush and read until done; cb gets each block with the events
 * released for it and returns nonzero to cancel */
typedef int (IVX_CALL *IvxAudioCb)(void *user, const short *pcm, int n,
                                   const IvxEvent *ev, int nev);
IVX_API int IVX_CALL ivx_synthesize(IvxEngine *e, const char *text, size_t len,
                                    IvxAudioCb cb, void *user);

IVX_API const char *IVX_CALL ivx_strerror(int err);
IVX_API int IVX_CALL ivx_version(void);

#ifdef __cplusplus
}
#endif

#endif
```

- [ ] **Step 2: Write the failing test** `tests/api_test.c` (the Task 1 version; later tasks add test functions and calls in `main`)

```c
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

int main(int argc, char **argv)
{
    if (argc > 1)
        bindir = argv[1];
    setvbuf(stdout, NULL, _IONBF, 0);
    test_lifecycle();
    test_settings();
    test_load();
    printf("api_test: %ld checks\n%ld failures\n", checks, failures);
    return failures != 0;
}
```

- [ ] **Step 3: Replace `Makefile`** with this version. The existing rules are kept; the library and `api_test` are added, plus a Linux branch.

```make
# C port of Ivx230nt.dll and the infovox230 library.
# Windows: needs the 32-bit winlibs gcc (C:\winlibs\mingw32\bin) on PATH:
#   export PATH=/c/winlibs/mingw32/bin:$PATH ; make test
# Linux (gcc-multilib): make test builds build/linux/ and runs the library tests.
# SSE2 double math matches the DLL's x87 tables (MSVC runs x87 at 53-bit precision).

CC      = gcc
CFLAGS  = -std=c11 -O2 -Wall -Wextra -m32 -msse2 -mfpmath=sse -Isrc -Itools/ghidra
LDLIBS  = -lm

SRC     = src/synth.c src/synth_data.c src/mem.c \
          src/contwork.c src/cwflow.c src/cwtest.c src/cwelem.c src/cwmath.c src/cwvars.c \
          src/cwmisc.c src/tpelem.c src/opsmooth.c src/opstart.c src/opbatch.c src/op_data.c \
          src/ttsctrl.c src/tpchar.c src/tpstr.c src/tp_data.c
OBJ     = $(patsubst src/%.c,build/%.o,$(SRC))
HDR     = src/ivx.h src/synth.h src/cw.h src/op.h src/tp.h src/tts.h tools/ghidra/ivx_types.h
TESTS   = build/synth_cmp.exe build/cw_cmp.exe build/op_cmp.exe build/tp_cmp.exe build/tts_cmp.exe

# the infovox230 shared library (include/infovox230.h) and its clients
ifeq ($(OS),Windows_NT)
B       = build
EXE     = .exe
LIB     = $(B)/infovox230.dll
LIBLD   = -shared -static-libgcc -Wl,--out-implib,$(B)/libinfovox230.dll.a \
          -Wl,--output-def,$(B)/infovox230.def
PIC     =
RPATH   =
else
B       = build/linux
EXE     =
LIB     = $(B)/libinfovox230.so
LIBLD   = -shared -Wl,-soname,libinfovox230.so.0
PIC     = -fPIC
RPATH   = -Wl,-rpath,'$$ORIGIN'
endif
LIBOBJ    = $(patsubst src/%.c,$(B)/lib/%.o,$(SRC)) $(B)/lib/infovox230.o
LIBCFLAGS = $(CFLAGS) $(PIC) -fvisibility=hidden -DIVX_BUILD -Iinclude
CLIFLAGS  = -std=gnu11 -O2 -Wall -Wextra -m32 -Iinclude

ifeq ($(OS),Windows_NT)
all: $(TESTS) $(LIB) $(B)/api_test$(EXE) build/ivxsay.exe
else
all: $(LIB) $(B)/api_test$(EXE)
endif

$(B)/lib/%.o: src/%.c $(HDR)
	@mkdir -p $(B)/lib
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(B)/lib/infovox230.o: lib/infovox230.c include/infovox230.h $(HDR)
	@mkdir -p $(B)/lib
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(LIB): $(LIBOBJ)
	$(CC) $(CFLAGS) $(LIBLD) $^ -o $@ $(LDLIBS)

$(B)/api_test$(EXE): tests/api_test.c include/infovox230.h $(LIB)
	$(CC) $(CLIFLAGS) tests/api_test.c -o $@ -L$(B) -linfovox230 $(RPATH)

# text to WAV with the port only (no DLL)
build/ivxsay.exe: cli/ivxsay.c $(OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

src/synth_data.c: tools/gen_synth_data.py bin/Ivx230nt.dll
	python tools/gen_synth_data.py

src/op_data.c: tools/gen_op_data.py bin/Ivx230nt.dll
	python tools/gen_op_data.py

src/tp_data.c: tools/gen_tp_data.py bin/Ivx230nt.dll
	python tools/gen_tp_data.py

build/%.o: src/%.c $(HDR)
	@mkdir -p build
	$(CC) $(CFLAGS) -c $< -o $@

build/%.exe: tests/%.c $(OBJ)
	@mkdir -p build
	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)

# the interpreter test counts opcode executions (CW_COVERAGE build of the port)
build/cov/%.o: src/%.c $(HDR)
	@mkdir -p build/cov
	$(CC) $(CFLAGS) -DCW_COVERAGE -DCW_STEP_LIMIT=20000000 -c $< -o $@

build/cw_cmp.exe: tests/cw_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

build/op_cmp.exe: tests/op_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

build/tp_cmp.exe: tests/tp_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

build/tts_cmp.exe: tests/tts_cmp.c tests/harness.c tests/harness.h $(patsubst src/%.c,build/cov/%.o,$(SRC))
	$(CC) $(CFLAGS) -Itests -DCW_COVERAGE $(filter %.c %.o,$^) -o $@ $(LDLIBS)

ifeq ($(OS),Windows_NT)
test: all
	build/synth_cmp.exe bin/Ivx230nt.dll
	build/cw_cmp.exe bin
	build/op_cmp.exe bin
	build/tp_cmp.exe bin
	build/tts_cmp.exe bin wav
	build/api_test.exe bin
	python tools/check_ivxsay.py
else
test: all
	$(B)/api_test bin
endif

clean:
	rm -f build/*.o build/cov/*.o $(TESTS) build/ivxsay.exe $(B)/lib/*.o $(LIB) \
	      $(B)/api_test$(EXE) build/libinfovox230.dll.a build/infovox230.def

.PHONY: all test clean
```

- [ ] **Step 4: Run the test build and confirm it fails**

Run: `export PATH=/c/winlibs/mingw32/bin:$PATH; make build/api_test.exe`

Expected: FAIL. `make` stops with "No rule to make target 'lib/infovox230.c'".

- [ ] **Step 5: Write the Task 1 part of `lib/infovox230.c`**

```c
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
    uint32_t u8cp;              /* UTF-8 decoder: code point so far */
    int u8need;                 /* continuation bytes still expected */
    unsigned char u8lo, u8hi;   /* allowed range of the next continuation byte */
    unsigned char last_in;      /* previous input char (LF after CR stays LF) */
    int flushed;                /* a flush went in since the last DONE */
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

/* nothing queued but settings, no char pending, no audio playing or staged */
static int idle(const IvxEngine *e)
{
    return e->qtext == 0 && !e->have_pending && !e->core->out_active
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

    if (e == NULL || ivx == NULL)
        return IVX_E_ARG;
    if (!idle(e))
        return IVX_E_BUSY;
    if (n < 0x200 + sizeof(IvxBody))
        return IVX_E_BADFILE;
    for (k = 0; k < sizeof offsets / sizeof offsets[0]; k++)
        if (be32(h + 4 * offsets[k]) > n - 0x200)
            return IVX_E_BADFILE;
    b = malloc(n + 16);
    if (b == NULL)
        return IVX_E_NOMEM;
    memcpy(b, ivx, n);
    memset(b + n, 0, 16);   /* lex_lookup can read one byte past the lexicon */
    tts_ctrl(NULL, TTS_IVX_SWAP, b + 0x200);
    tts_ctrl(e->core, TTS_CHARSET, (void *)(intptr_t)e->val[IVX_CHARSET]);
    if (tts_ctrl(e->core, TTS_BIND, b + 0x200) != 0) {   /* the old language stays bound */
        free(b);
        return IVX_E_BADFILE;
    }
    free(e->ivx);
    e->ivx = b;
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
        e->u8need = 0;
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
```

- [ ] **Step 6: Build and run the test**

Run: `make build/api_test.exe && build/api_test.exe bin`

Expected: the build has no warnings, and the output ends with `0 failures`.

- [ ] **Step 7: Check the exports**

Run: `cat build/infovox230.def`

Expected: after the `EXPORTS` line, only `ivx_*` names appear. At this stage they are the eight functions Step 5 implements: create, destroy, get, load_language, load_language_mem, set, strerror, version. There are no `tts_*`, `synt_*` or `op_*` names.

- [ ] **Step 8: Checkpoint**

Run: `make test`

Expected: every test prints `0 failures`, and `api_test` prints `0 failures`. The existing `ivxsay` check still passes, with 12 of 12 identical.

---

### Task 2: Text queue, read loop, flush/DONE, synthesize, cancel and UTF-8

**Files:**
- Modify: `lib/infovox230.c` (append the code below after `ivx_get`)
- Modify: `tests/api_test.c` (add helpers and tests before `main`, and calls in `main`)

**Interfaces:**
- Consumes: from Task 1, the `IvxEngine` fields, `Fifo` functions, `idle`, `QItem`, `Mark` and `BLOCK`.
- Produces:
  - `ivx_feed`, `ivx_feed_phonetic`, `ivx_mark` (queue side: serials and the `marks` fifo), `ivx_flush`, `ivx_read`, `ivx_next_event`, `ivx_synthesize`, `ivx_cancel`.
  - Internal: `push_event(IvxEngine *, int type, uint32_t value, uint64_t sample)`, used by Task 3.
  - Test helpers: `Out` (`pcm`, `n`, `ev`, `nev`, `base`), `out_free`, `read_some`, `drain`, `speak(e, text, mark0)`, `render(code, enc, text, mark0, chunk, Out *)`, `same_audio`, `same_events`, and the texts `SW`, `SW_UTF8`, `GE`.

- [ ] **Step 1: Write the failing tests.** Add these helpers and tests to `tests/api_test.c`, just above `int main`.

```c
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

static void render(const char *code, int enc, const char *text, uint32_t mark0, int chunk, Out *o)
{
    IvxEngine *e = engine(code, enc);

    memset(o, 0, sizeof *o);
    speak(e, text, mark0);
    CHECK(ivx_flush(e) == 0, "ivx_flush");
    drain(e, chunk, o);
    ivx_destroy(e);
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

static void test_interleave(void)
{
    Out sa, sb, ia, ib;
    IvxEngine *a, *b;
    short buf[300];
    int da = 0, db = 0;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &sa);
    render("ge", IVX_ENC_CP1252, GE, 0, 441, &sb);
    a = engine("sw", IVX_ENC_CP1252);
    b = engine("ge", IVX_ENC_CP1252);
    memset(&ia, 0, sizeof ia);
    memset(&ib, 0, sizeof ib);
    speak(a, SW, 0);
    speak(b, GE, 0);
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

static void test_settings_effect(void)
{
    Out def, fast, frames, ph;
    IvxEngine *e;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &def);
    render_set(IVX_RATE, 90, &fast);
    render_set(IVX_FRAME_RATE, 200, &frames);
    CHECK(fast.n != def.n, "rate 90 did not change the audio (%ld)", fast.n);
    CHECK(frames.n < def.n * 3 / 4, "frame rate 200: %ld samples vs %ld", frames.n, def.n);
    /* phonetic input is accepted and ends in DONE */
    e = engine("sw", IVX_ENC_CP1252);
    memset(&ph, 0, sizeof ph);
    CHECK(ivx_feed_phonetic(e, "h e j", 5) == 0 && ivx_flush(e) == 0, "phonetic feed");
    drain(e, 441, &ph);
    CHECK(count_events(&ph, IVX_EV_DONE) == 1, "phonetic input: DONE");
    ivx_destroy(e);
    out_free(&def);
    out_free(&fast);
    out_free(&frames);
    out_free(&ph);
}
```

Add these calls in `main` after `test_load();`:

```c
    test_stream_errors();
    test_chunks();
    test_synthesize();
    test_encoding();
    test_busy();
    test_interleave();
    test_load_failure();
    test_settings_effect();
```

- [ ] **Step 2: Run the test build and confirm it fails**

Run: `make build/api_test.exe`

Expected: FAIL at link time, with undefined references to `ivx_feed`, `ivx_read`, `ivx_flush`, `ivx_mark`, `ivx_next_event`, `ivx_cancel`, `ivx_synthesize` and `ivx_feed_phonetic`.

- [ ] **Step 3: Implement.** Append this to `lib/infovox230.c`:

```c
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

static int put_cp(IvxEngine *e, uint32_t cp)
{
    int k;

    if (cp < 0x80 || (cp >= 0xa0 && cp <= 0xff))
        return put_byte(e, (unsigned char)cp);
    for (k = 0; k < 32; k++)
        if (cp1252_hi[k] == cp)
            return put_byte(e, (unsigned char)(0x80 + k));
    return put_byte(e, ' ');
}

/* UTF-8 (Unicode table 3-7): a bad or unfinished sequence becomes one space, and the byte
 * that ended it starts afresh */
static int put_utf8(IvxEngine *e, unsigned char b)
{
    int r;

    if (e->u8need != 0) {
        if (b >= e->u8lo && b <= e->u8hi) {
            e->u8cp = e->u8cp << 6 | (b & 0x3f);
            e->u8lo = 0x80;
            e->u8hi = 0xbf;
            return --e->u8need == 0 ? put_cp(e, e->u8cp) : IVX_OK;
        }
        e->u8need = 0;
        if ((r = put_byte(e, ' ')) != 0)
            return r;
    }
    e->u8lo = 0x80;
    e->u8hi = 0xbf;
    if (b < 0x80)
        return put_byte(e, b);
    if (b >= 0xc2 && b <= 0xdf) {
        e->u8need = 1;
        e->u8cp = b & 0x1f;
    } else if (b >= 0xe0 && b <= 0xef) {
        e->u8need = 2;
        e->u8cp = b & 0x0f;
        if (b == 0xe0)
            e->u8lo = 0xa0;
        if (b == 0xed)
            e->u8hi = 0x9f;
    } else if (b >= 0xf0 && b <= 0xf4) {
        e->u8need = 3;
        e->u8cp = b & 0x07;
        if (b == 0xf0)
            e->u8lo = 0x90;
        if (b == 0xf4)
            e->u8hi = 0x8f;
    } else {
        return put_byte(e, ' ');
    }
    return IVX_OK;
}

/* an unfinished UTF-8 sequence becomes a space */
static int end_utf8(IvxEngine *e)
{
    if (e->u8need == 0)
        return IVX_OK;
    e->u8need = 0;
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
    r = tp_putc(e->core, e->pending_ch);
    if (r == 2 && ++e->tries < 8)
        return;
    e->have_pending = 0;
    if (e->pending_flush)
        e->flushed = 1;
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
    e->u8need = 0;
    e->last_in = 0;
    e->stage_pos = e->stage_len = 0;
    for (p = 0; p < NQUEUED; p++) {
        e->qcount[p] = 0;
        e->val[p] = e->applied[p];
    }
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
    int n, nev, r;

    if (e == NULL || cb == NULL || (text == NULL && len != 0))
        return IVX_E_ARG;
    if (e->ivx == NULL)
        return IVX_E_NOLANG;
    dones = e->dones;
    if ((r = ivx_feed(e, text, len)) != 0 || (r = ivx_flush(e)) != 0)
        return r;
    do {
        n = ivx_read(e, buf, BLOCK);
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
    } while (e->dones == dones);
    return IVX_OK;
}
```

- [ ] **Step 4: Build and run the test**

Run: `make build/api_test.exe && build/api_test.exe bin`

Expected: no warnings, and the output ends with `0 failures`. If "encoding: LF becomes CR" fails, check that `put_byte` uses the raw previous byte. If `test_chunks` fails, compare the `ivx_read` step order against the old `cli/ivxsay.c` `say()`/`play()` loop: play while `out_active`, then the next char.

- [ ] **Step 5: Checkpoint**

Run: `make test`

Expected: all tests print `0 failures`, including `api_test`. `ivxsay` still gives 12 of 12 identical.

---

### Task 3: Marks, phoneme and error events

**Files:**
- Modify: `lib/infovox230.c`: add three callback prototypes above `ivx_create`, append the callbacks at the end of the file (after `push_event`, which they call), and register them in `ivx_create`.
- Modify: `tests/api_test.c`: add `test_marks` and `test_phonemes`.

**Interfaces:**
- Consumes: `push_event`, `Mark`, the `marks` fifo, `frame_pos` and `gen_pos` from Tasks 1–2.
- Produces: `IVX_EV_MARK`, `IVX_EV_PHONEME` and `IVX_EV_ERROR` events, as the spec's Events section describes.

- [ ] **Step 1: Write the failing tests.** Add these above `int main` in `tests/api_test.c`:

```c
/* ---- Task 3: events ---- */

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
```

Add these calls in `main` after `test_settings_effect();`:

```c
    test_marks();
    test_phonemes();
```

- [ ] **Step 2: Run the test and confirm it fails**

Run: `make build/api_test.exe && build/api_test.exe bin`

Expected: FAIL. With no bookmark callback, every mark is released at DONE, so the output shows "only 1 distinct mark positions". It also shows "0 phoneme events".

- [ ] **Step 3: Implement.** Add these prototypes to `lib/infovox230.c`, just above `IvxEngine *ivx_create`:

```c
static void cb_bookmark(void *arg, int v);
static void cb_phonemes(void *arg, char *name);
static void cb_status(void *arg, int st);
```

Append the callbacks at the end of `lib/infovox230.c`:

```c
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
```

In `ivx_create`, replace the line `tts_ctrl(e->core, TTS_CB_ARG, e);` with:

```c
    tts_ctrl(e->core, TTS_CB_ARG, e);
    tts_ctrl(e->core, TTS_CB_BOOKMARK, (void *)cb_bookmark);
    tts_ctrl(e->core, TTS_CB_PHONEMES, (void *)cb_phonemes);
    tts_ctrl(e->core, TTS_CB_STATUS, (void *)cb_status);
```

- [ ] **Step 4: Run the test**

Run: `make build/api_test.exe && build/api_test.exe bin`

Expected: `0 failures`. If "marks changed the audio" fails, the mark hypothesis is wrong. Stop and report it to the user with the first differing sample; don't loosen the check.

- [ ] **Step 5: Checkpoint**

Run: `make test`

Expected: all tests print `0 failures`, and `ivxsay` gives 12 of 12.

---

### Task 4: Cancel, proven against the DLL and tested in the library

**Files:**
- Modify: `tests/tts_cmp.c`: add a `stops` field to `Config`, a fourth configuration, `audio_blocks`, `stop_both`, and the stop branch in `feed`.
- Modify: `tests/api_test.c`: add `test_cancel`.

**Interfaces:**
- Consumes: `ivx_cancel` (Task 2); in `tts_cmp`: `ctrl_both`, `compare_cores`, `dll_synt_ctrl`, `rnd`.
- Produces: a DLL-verified stop sequence and the library's cancel checks.

- [ ] **Step 1: Write the DLL comparison for the stop sequence.** Edit `tests/tts_cmp.c`.

  Replace the `Config` struct and the table:

```c
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
```

  Add `stops` to the statistics line near the top: change `static long failures, checks, cmds, cmd_count[TTS_NCMD], samples, nonzero, chars, returns[3];` to

```c
static long failures, checks, cmds, cmd_count[TTS_NCMD], samples, nonzero, chars, returns[3], stops;
static int stop_mode;
```

  Rename `static int audio(void)` to `static int audio_blocks(long max)`. Change its loop condition from `while (sd.c->out_active || sp.c->out_active) {` to `while ((sd.c->out_active || sp.c->out_active) && max-- != 0) {`. Then add after the function:

```c
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
```

  `ctrl_both` is defined after `feed` in the current file. Move the `ctrl_both` function above `audio_blocks`, unchanged, so `stop_both` can call it.

  In `feed`, replace

```c
        compare_cores("tp_putc");
        if (!audio())
            return 0;
```

  with

```c
        compare_cores("tp_putc");
        if (stop_mode && sd.c->out_active && rnd(2) == 0)
            return stop_both((long)rnd(30));   /* the char goes with the rest of the text */
        if (!audio())
            return 0;
```

  In `test_audio`, just after `cur_part = cf->name;`, add:

```c
    stop_mode = cf->stops;
    if (stop_mode)
        rng = 0x5eed5u + (uint32_t)(l - langs) * 31u;
```

  In `main`, change the final statistics `printf` so it also prints the stops:

```c
    printf("\n%ld tts_ctrl commands, %ld tp_putc calls (returned 0/1/2: %ld/%ld/%ld), %ld stops, "
           "%ld samples compared (%ld non-zero), %ld checks\n", cmds, chars, returns[0], returns[1],
           returns[2], stops, samples, nonzero, checks);
```

- [ ] **Step 2: Run the DLL comparison**

Run: `make build/tts_cmp.exe && build/tts_cmp.exe bin`

Expected: 12 new lines ending `stops 22050 ... samples: ok`, a summary with more than 50 stops, and `0 failures`. A mismatch here means the port's reset 1, reset 2 or `SYNT_FLUSH` differs from the DLL's. Fix the port (`src/`), not the test.

- [ ] **Step 3: Write the failing library test.** Add this above `int main` in `tests/api_test.c`:

```c
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
```

Add `test_cancel();` in `main` after `test_phonemes();`.

- [ ] **Step 4: Run the library test**

Run: `make build/api_test.exe && build/api_test.exe bin`

Expected: `0 failures`. `ivx_cancel` already exists from Task 2, so this test is expected to pass at once. If it fails, the likely causes are `next_frame_pos` or `gen_pos` not being reset to `delivered` in `ivx_cancel`, or `flushed` not being cleared.

- [ ] **Step 5: Checkpoint**

Run: `make test`

Expected: all tests print `0 failures`. The `tts_cmp` summary shows the stops, and `ivxsay` gives 12 of 12.

---

### Task 5: `ivxsay` on the library (WAV output)

**Files:**
- Rewrite: `cli/ivxsay.c`
- Modify: `tools/check_ivxsay.py`
- Modify: `Makefile`: the `ivxsay` rule and `all`

**Interfaces:**
- Consumes: only `include/infovox230.h`.
- Produces: `build/ivxsay.exe` linked against `infovox230.dll`, with an `emit(const short *, int)` output function that Task 6 extends.

- [ ] **Step 1: Make the check use the new option first** (it fails until `ivxsay` supports `-e`). In `tools/check_ivxsay.py`:
  - replace `say = os.path.abspath(os.path.join('build', 'ivxsay.exe'))` with `say = os.path.abspath(sys.argv[1] if len(sys.argv) > 1 else os.path.join('build', 'ivxsay.exe'))`;
  - replace `subprocess.run([say, '-l', os.path.join('bin', name), '-i', text, '-o', wav],` with `subprocess.run([say, '-l', os.path.join('bin', name), '-e', 'cp1252', '-i', text, '-o', wav],`;
  - add `Usage: python tools/check_ivxsay.py [path/to/ivxsay]` to the docstring.

- [ ] **Step 2: Run it and confirm it fails**

Run: `python tools/check_ivxsay.py`

Expected: FAIL with `CalledProcessError`, because the old `ivxsay` rejects `-e` (usage, exit 2).

- [ ] **Step 3: Rewrite `cli/ivxsay.c`**

```c
/* ivxsay: text to speech with the infovox230 library (include/infovox230.h).
 *
 * The text comes from the arguments, a file (-i) or stdin, as UTF-8 (or Windows-1252
 * with -e cp1252), and goes to a WAV file (-o). tools/check_ivxsay.py requires the WAV
 * files to be byte-identical to the DLL-verified audio of tests/tts_cmp.c.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "infovox230.h"

#define BLOCK 441

static FILE *out;
static long nsamples;
static volatile sig_atomic_t interrupted;

static void usage(void)
{
    fprintf(stderr,
        "usage: ivxsay -l LANG [options] [text ...]\n"
        "  -l LANG   .ivx file, or a language code (sw, no, da, fi, ic, ge, du, fr, it, sp, bl, am)\n"
        "            for bin/<code>rules.ivx\n"
        "  -o FILE   output WAV (default out.wav)\n"
        "  -i FILE   read the text from FILE (default: the arguments, else stdin)\n"
        "  -e ENC    text encoding: utf8 (default) or cp1252\n"
        "  -s HZ     sample rate 8000..48000 (default 22050)\n"
        "  -v N      voice 0..9\n"
        "  -r N      rate 0..100\n"
        "  -p N      pitch 0..100\n"
        "  -f N      FD 0..100\n"
        "  -m N      text mode flags (default 8 = whole sentences)\n"
        "  -F N      synth frames per second 2..1000 (default 100)\n"
        "  -E        print events (marks, phonemes, errors, done) to stderr\n");
    exit(2);
}

static unsigned char *read_all(FILE *f, long *size)
{
    size_t cap = 4096, n = 0, k;
    unsigned char *b = malloc(cap);

    while (b != NULL && (k = fread(b + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap)
            b = realloc(b, cap *= 2);
    }
    if (b == NULL) {
        fprintf(stderr, "ivxsay: out of memory\n");
        exit(1);
    }
    *size = (long)n;
    return b;
}

static void wav_header(int fs, long n)
{
    unsigned char h[44];
    uint32_t v[] = {36 + 2 * (uint32_t)n, 16, 1 | 1 << 16, (uint32_t)fs, 2 * (uint32_t)fs,
                    2 | 16 << 16, 2 * (uint32_t)n};

    memcpy(h, "RIFF", 4);
    memcpy(h + 4, &v[0], 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    memcpy(h + 16, &v[1], 20);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &v[6], 4);
    fseek(out, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, out);
}

static void emit(const short *pcm, int n)
{
    fwrite(pcm, 2, (size_t)n, out);
    nsamples += n;
}

static void on_sigint(int sig)
{
    (void)sig;
    interrupted = 1;
}

static void print_event(const IvxEvent *ev, int fs)
{
    static const char *const names[] = {"mark", "phoneme", "error", "done"};

    fprintf(stderr, "%10.3f %-7s ", (double)ev->sample / fs,
            ev->type >= 0 && ev->type < 4 ? names[ev->type] : "?");
    if (ev->type == IVX_EV_PHONEME)
        fprintf(stderr, "%c%c\n", (int)(ev->value & 0xff), ev->value >> 8 ? (int)(ev->value >> 8) : ' ');
    else
        fprintf(stderr, "%lu\n", (unsigned long)ev->value);
}

static int set(IvxEngine *e, int param, int value, const char *what)
{
    int r = ivx_set(e, param, value);

    if (r != 0)
        fprintf(stderr, "ivxsay: %s %d: %s\n", what, value, ivx_strerror(r));
    return r;
}

static int opt_int(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc)
        usage();
    return atoi(argv[++*i]);
}

int main(int argc, char **argv)
{
    const char *lang = NULL, *outpath = "out.wav", *inpath = NULL;
    int fs = 22050, voice = -1, rate = -1, pitch = -1, fd = -1, mode = 8, fps = 0;
    int enc = IVX_ENC_UTF8, events = 0, err, r, n, i;
    unsigned char *text = NULL;
    long tlen = 0, k;
    char path[512];
    short buf[BLOCK];
    IvxEngine *e;
    IvxEvent ev;
    FILE *f;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != 0; i++) {
        switch (argv[i][1]) {
        case 'l': if (i + 1 >= argc) usage(); lang = argv[++i]; break;
        case 'o': if (i + 1 >= argc) usage(); outpath = argv[++i]; break;
        case 'i': if (i + 1 >= argc) usage(); inpath = argv[++i]; break;
        case 'e':
            if (i + 1 >= argc) usage();
            i++;
            if (strcmp(argv[i], "utf8") == 0) enc = IVX_ENC_UTF8;
            else if (strcmp(argv[i], "cp1252") == 0) enc = IVX_ENC_CP1252;
            else usage();
            break;
        case 's': fs = opt_int(argc, argv, &i); break;
        case 'v': voice = opt_int(argc, argv, &i); break;
        case 'r': rate = opt_int(argc, argv, &i); break;
        case 'p': pitch = opt_int(argc, argv, &i); break;
        case 'f': fd = opt_int(argc, argv, &i); break;
        case 'm': mode = opt_int(argc, argv, &i); break;
        case 'F': fps = opt_int(argc, argv, &i); break;
        case 'E': events = 1; break;
        default: usage();
        }
    }
    if (lang == NULL)
        usage();

    e = ivx_create(&err);
    if (e == NULL) {
        fprintf(stderr, "ivxsay: %s\n", ivx_strerror(err));
        return 1;
    }
    if (set(e, IVX_SAMPLE_RATE, fs, "sample rate") || set(e, IVX_TEXT_MODE, mode, "text mode")
        || set(e, IVX_INPUT_ENCODING, enc, "encoding")
        || (events && set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL, "event mask")))
        return 1;
    r = ivx_load_language(e, lang);
    if (r == IVX_E_IO && strlen(lang) == 2) {
        snprintf(path, sizeof path, "bin/%srules.ivx", lang);
        r = ivx_load_language(e, path);
    }
    if (r != 0) {
        fprintf(stderr, "ivxsay: %s: %s\n", lang, ivx_strerror(r));
        return 1;
    }
    if ((voice >= 0 && set(e, IVX_VOICE, voice, "voice")) || (rate >= 0 && set(e, IVX_RATE, rate, "rate"))
        || (pitch >= 0 && set(e, IVX_PITCH, pitch, "pitch")) || (fd >= 0 && set(e, IVX_FD, fd, "FD"))
        || (fps && set(e, IVX_FRAME_RATE, fps, "frame rate")))
        return 1;

    if (inpath != NULL) {
        f = fopen(inpath, "rb");
        if (f == NULL) {
            fprintf(stderr, "ivxsay: cannot read %s\n", inpath);
            return 1;
        }
        text = read_all(f, &tlen);
        fclose(f);
    } else if (i < argc) {
        for (k = i; k < argc; k++)
            tlen += (long)strlen(argv[k]) + 1;
        text = calloc(1, (size_t)tlen + 1);
        for (tlen = 0; i < argc; i++)
            tlen += sprintf((char *)text + tlen, "%s%s", tlen ? " " : "", argv[i]);
    } else {
        text = read_all(stdin, &tlen);
    }

    out = fopen(outpath, "wb");
    if (out == NULL) {
        fprintf(stderr, "ivxsay: cannot write %s\n", outpath);
        return 1;
    }
    wav_header(fs, 0);
    signal(SIGINT, on_sigint);
    if ((r = ivx_feed(e, (const char *)text, (size_t)tlen)) != 0 || (r = ivx_flush(e)) != 0) {
        fprintf(stderr, "ivxsay: %s\n", ivx_strerror(r));
        return 1;
    }
    for (;;) {
        n = interrupted ? 0 : ivx_read(e, buf, BLOCK);
        if (n < 0) {
            fprintf(stderr, "ivxsay: %s\n", ivx_strerror(n));
            return 1;
        }
        if (n > 0)
            emit(buf, n);
        while (ivx_next_event(e, &ev) == 1)
            if (events)
                print_event(&ev, fs);
        if (n == 0)
            break;
    }
    if (interrupted)
        ivx_cancel(e);
    wav_header(fs, nsamples);
    fclose(out);
    printf("%s: %ld samples, %.2f s at %d Hz\n", outpath, nsamples, (double)nsamples / fs, fs);
    ivx_destroy(e);
    free(text);
    return 0;
}
```

- [ ] **Step 4: Link `ivxsay` against the library.** In the `Makefile`:
  - Replace the rule

    ```make
    # text to WAV with the port only (no DLL)
    build/ivxsay.exe: cli/ivxsay.c $(OBJ)
    	@mkdir -p build
    	$(CC) $(CFLAGS) $^ -o $@ $(LDLIBS)
    ```

    with

    ```make
    # text to speech through the library
    $(B)/ivxsay$(EXE): cli/ivxsay.c include/infovox230.h $(LIB)
    	$(CC) $(CLIFLAGS) cli/ivxsay.c -o $@ -L$(B) -linfovox230 $(RPATH)
    ```

  - Change the Windows `all:` line to `all: $(TESTS) $(LIB) $(B)/api_test$(EXE) $(B)/ivxsay$(EXE)`.
  - Change the Linux `all:` line to `all: $(LIB) $(B)/api_test$(EXE) $(B)/ivxsay$(EXE)`.
  - Change `build/ivxsay.exe` in `clean` to `$(B)/ivxsay$(EXE)`.

- [ ] **Step 5: Build and run the check**

Run: `rm -f build/ivxsay.exe && make build/ivxsay.exe && python tools/check_ivxsay.py`

Expected: `ivxsay: 12 of 12 languages identical to the DLL-verified audio` and `0 failures`. If any file is DIFFERENT, compare the call order with the old `cli/ivxsay.c`: create, charset 2, bind, mode, then the synth at `fs`. Keep the charset/mode deferral in `ivx_set`/`ivx_load_language_mem`. Report to the user before changing the order again.

- [ ] **Step 6: Exercise the options and errors by hand**

Run each command:

```bash
build/ivxsay.exe -l sw -o build/t1.wav "God morgon."
```

```bash
build/ivxsay.exe -l sw -e utf8 -E -o build/t2.wav "Hej d$(printf '\xc3\xa5')."
```

```bash
build/ivxsay.exe -l xx -o build/t3.wav "a"
```

```bash
build/ivxsay.exe -l sw -s 99 -o build/t4.wav "a"
```

```bash
build/ivxsay.exe -l sw -r 101 -o build/t5.wav "a"
```

Expected:
- The first prints samples with a non-zero count, and exit 0.
- The second prints event lines to stderr (phonemes, then `done`), with exit 0.
- The third prints `ivxsay: xx: cannot read the language file`, exit 1.
- The fourth prints `ivxsay: sample rate 99: unknown parameter or value out of range`, exit 1.
- The fifth prints the same message for `rate 101`, exit 1.

- [ ] **Step 7: Checkpoint**

Run: `make test`

Expected: all tests print `0 failures`, and `ivxsay` gives 12 of 12.

---

### Task 6: Speaker output for `ivxsay`

**Files:**
- Create: `cli/audio.h`, `cli/audio_win.c`, `cli/audio_alsa.c`
- Modify: `cli/ivxsay.c`: speaker when there's no `-o`, and cancel on Ctrl+C.
- Modify: `Makefile`: the audio sources and libraries.

**Interfaces:**
- Consumes: `emit()` and the `interrupted` flag in `cli/ivxsay.c` (Task 5).
- Produces: `int audio_open(int fs)` (0 or −1), `int audio_write(const short *pcm, int n)` (0 or −1), `void audio_close(int drain)`.

- [ ] **Step 1: Write `cli/audio.h`**

```c
/* Speaker output for ivxsay: 16-bit mono PCM to the default device.
 * cli/audio_win.c (waveOut) on Windows, cli/audio_alsa.c (ALSA "default") on Linux. */
#ifndef AUDIO_H
#define AUDIO_H

int audio_open(int fs);                     /* 0, or -1 if the device cannot be opened */
int audio_write(const short *pcm, int n);   /* blocks while the device buffers are full */
void audio_close(int drain);                /* drain: play what is buffered; else drop it */

#endif
```

- [ ] **Step 2: Write `cli/audio_win.c`**

```c
/* audio.h on Windows: waveOut with 4 buffers of 100 ms, waiting on WHDR_DONE through an
 * event */
#include <windows.h>
#include <mmsystem.h>
#include <stdlib.h>
#include <string.h>
#include "audio.h"

#define NBUF 4

static HWAVEOUT wo;
static HANDLE done_ev;
static WAVEHDR hdr[NBUF];
static short *bufs[NBUF];
static int cur, fill, bufsamples;

int audio_open(int fs)
{
    WAVEFORMATEX f;
    int k;

    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = (DWORD)fs;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = 2 * (DWORD)fs;
    done_ev = CreateEvent(NULL, FALSE, FALSE, NULL);
    if (done_ev == NULL)
        return -1;
    if (waveOutOpen(&wo, WAVE_MAPPER, &f, (DWORD_PTR)done_ev, 0, CALLBACK_EVENT) != MMSYSERR_NOERROR) {
        CloseHandle(done_ev);
        return -1;
    }
    bufsamples = fs / 10;
    for (k = 0; k < NBUF; k++) {
        bufs[k] = malloc((size_t)bufsamples * sizeof(short));
        memset(&hdr[k], 0, sizeof hdr[k]);
        hdr[k].lpData = (LPSTR)bufs[k];
        hdr[k].dwBufferLength = (DWORD)bufsamples * sizeof(short);
        waveOutPrepareHeader(wo, &hdr[k], sizeof hdr[k]);
        hdr[k].dwFlags |= WHDR_DONE;   /* free */
    }
    cur = fill = 0;
    return 0;
}

static void submit(void)
{
    hdr[cur].dwBufferLength = (DWORD)fill * sizeof(short);
    hdr[cur].dwFlags &= ~WHDR_DONE;
    waveOutWrite(wo, &hdr[cur], sizeof hdr[cur]);
    cur = (cur + 1) % NBUF;
    fill = 0;
}

int audio_write(const short *pcm, int n)
{
    int k;

    while (n > 0) {
        while (fill == 0 && !(hdr[cur].dwFlags & WHDR_DONE))
            WaitForSingleObject(done_ev, INFINITE);
        k = bufsamples - fill;
        if (k > n)
            k = n;
        memcpy(bufs[cur] + fill, pcm, (size_t)k * sizeof(short));
        fill += k;
        pcm += k;
        n -= k;
        if (fill == bufsamples)
            submit();
    }
    return 0;
}

void audio_close(int drain)
{
    int k;

    if (drain && fill > 0)
        submit();
    if (drain) {
        for (k = 0; k < NBUF; k++)
            while (!(hdr[k].dwFlags & WHDR_DONE))
                WaitForSingleObject(done_ev, INFINITE);
    } else {
        waveOutReset(wo);
    }
    for (k = 0; k < NBUF; k++) {
        waveOutUnprepareHeader(wo, &hdr[k], sizeof hdr[k]);
        free(bufs[k]);
    }
    waveOutClose(wo);
    CloseHandle(done_ev);
}
```

- [ ] **Step 3: Write `cli/audio_alsa.c`**

```c
/* audio.h on Linux: ALSA's "default" device (PipeWire and PulseAudio provide it too) */
#include <alsa/asoundlib.h>
#include "audio.h"

static snd_pcm_t *pcm_dev;

int audio_open(int fs)
{
    if (snd_pcm_open(&pcm_dev, "default", SND_PCM_STREAM_PLAYBACK, 0) < 0)
        return -1;
    if (snd_pcm_set_params(pcm_dev, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED, 1,
                           (unsigned)fs, 1, 200000) < 0) {
        snd_pcm_close(pcm_dev);
        return -1;
    }
    return 0;
}

int audio_write(const short *pcm, int n)
{
    snd_pcm_sframes_t k;

    while (n > 0) {
        k = snd_pcm_writei(pcm_dev, pcm, (snd_pcm_uframes_t)n);
        if (k < 0) {
            if (snd_pcm_recover(pcm_dev, (int)k, 1) < 0)
                return -1;
            continue;
        }
        pcm += k;
        n -= (int)k;
    }
    return 0;
}

void audio_close(int drain)
{
    if (drain)
        snd_pcm_drain(pcm_dev);
    else
        snd_pcm_drop(pcm_dev);
    snd_pcm_close(pcm_dev);
}
```

- [ ] **Step 4: Switch `ivxsay` to the speaker when there's no `-o`.** Edit `cli/ivxsay.c`:
  - Add `#include "audio.h"` after `#include "infovox230.h"`.
  - Add `static int speaker;` after `static long nsamples;`.
  - Change `const char *lang = NULL, *outpath = "out.wav", *inpath = NULL;` to `const char *lang = NULL, *outpath = NULL, *inpath = NULL;`.
  - In `usage()`, change the `-o` line to `"  -o FILE   write a WAV file instead of playing through the speaker\n"`.
  - Replace `emit` with:

```c
static void emit(const short *pcm, int n)
{
    if (speaker)
        audio_write(pcm, n);
    else
        fwrite(pcm, 2, (size_t)n, out);
    nsamples += n;
}
```

  - Replace the block from `out = fopen(outpath, "wb");` through `wav_header(fs, 0);` with:

```c
    if (outpath == NULL) {
        speaker = 1;
        if (audio_open(fs) != 0) {
            fprintf(stderr, "ivxsay: cannot open the audio device\n");
            return 1;
        }
    } else {
        out = fopen(outpath, "wb");
        if (out == NULL) {
            fprintf(stderr, "ivxsay: cannot write %s\n", outpath);
            return 1;
        }
        wav_header(fs, 0);
    }
```

  - Replace the block from `if (interrupted)` through the `printf(...)` line with:

```c
    if (interrupted)
        ivx_cancel(e);
    if (speaker) {
        audio_close(!interrupted);
    } else {
        wav_header(fs, nsamples);
        fclose(out);
        printf("%s: %ld samples, %.2f s at %d Hz\n", outpath, nsamples, (double)nsamples / fs, fs);
    }
```

  - Update the file comment: "...and goes to the speaker, or to a WAV file with -o."

- [ ] **Step 5: Add the audio sources to the `Makefile`.**
  - In the Windows block (after `RPATH   =`) add `AUDIO   = cli/audio_win.c` and `AUDIOLIBS = -lwinmm`.
  - In the Linux block (after `RPATH   = ...`) add `AUDIO   = cli/audio_alsa.c` and `AUDIOLIBS = -lasound`.
  - Replace the `ivxsay` rule with:

```make
$(B)/ivxsay$(EXE): cli/ivxsay.c cli/audio.h $(AUDIO) include/infovox230.h $(LIB)
	$(CC) $(CLIFLAGS) cli/ivxsay.c $(AUDIO) -o $@ -L$(B) -linfovox230 $(RPATH) $(AUDIOLIBS)
```

- [ ] **Step 6: Build, check the WAV output is unchanged, and play**

Run: `make build/ivxsay.exe && python tools/check_ivxsay.py`

Expected: 12 of 12 identical, `0 failures`.

Run: `time build/ivxsay.exe -l sw "God morgon. Det här är ett test."`

Expected: exit 0, and a wall-clock time about equal to the audio length (1–3 s), which shows the output is paced by the device. Ask the user to listen and confirm the speech is clear, with nothing cut off at the start or end. Then run it with a long text and press Ctrl+C; it must stop within about 0.1 s and exit 0.

- [ ] **Step 7: Checkpoint**

Run: `make test`

Expected: all tests print `0 failures`, and `ivxsay` gives 12 of 12.

---

### Task 7: Linux build under WSL

**Files:**
- Modify: `Makefile`: the Linux `test` target runs the WAV comparison.

**Interfaces:**
- Consumes: everything above. `build/wav/*.wav` must first be written by a Windows `make test`, which Task 6's checkpoint did.
- Produces: `build/linux/libinfovox230.so`, `build/linux/api_test` and `build/linux/ivxsay`, tested.

- [ ] **Step 1: Ask the user before installing packages.** The command runs as root inside WSL Ubuntu:

```bash
wsl -d Ubuntu -u root -- bash -c "dpkg --add-architecture i386 && apt-get update && apt-get install -y make gcc-multilib python3 libasound2-dev:i386"
```

Run it only after the user says yes.

- [ ] **Step 2: Add the WAV comparison to the Linux test target.** In the `Makefile`'s `else` branch of `test`, replace

```make
test: all
	$(B)/api_test bin
```

with

```make
test: all
	$(B)/api_test bin
	python3 tools/check_ivxsay.py $(B)/ivxsay
```

- [ ] **Step 3: Build and test on Linux**

Run:

```bash
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Users/abart/Desktop/infovox_disasm && make all && make test"
```

Expected: no warnings; `api_test` prints `0 failures`; `check_ivxsay` prints 12 of 12 identical to the Windows-generated `build/wav/*_0.wav`. If any language is DIFFERENT, the likely cause is glibc's `pow`/`exp`/`cos` differing from mingw's in the synth table builders (`synt_mk_*` in `src/synth.c`). **Stop and report to the user** with the DIFFERENT list; don't change the port without a decision.

- [ ] **Step 4: Check the exported symbols**

Run:

```bash
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Users/abart/Desktop/infovox_disasm && nm -D --defined-only build/linux/libinfovox230.so | grep -v ' ivx_' | grep -v -E ' (_init|_fini|_edata|_end|__bss_start)$'"
```

Expected: no output.

- [ ] **Step 5: Smoke-test the ALSA path**

Run:

```bash
wsl -d Ubuntu -- bash -lc "cd /mnt/c/Users/abart/Desktop/infovox_disasm && build/linux/ivxsay -l sw 'God morgon.'; echo exit \$?"
```

Expected: either audible speech through WSLg and `exit 0`, or `ivxsay: cannot open the audio device` and `exit 1`. Both are acceptable here; report which happened.

- [ ] **Step 6: Checkpoint**

Run the Windows `make test` again. Expected: unchanged, `0 failures` everywhere. The Linux build lives in `build/linux/` and must not disturb the Windows objects.

---

### Task 8: Documentation

**Files:**
- Modify: `AGENTS.md`
- Modify: `REFERENCE.md`

**Interfaces:**
- Consumes: the results printed by the Task 4, 6 and 7 checkpoints (sample and stop counts, the Linux outcome).

- [ ] **Step 1: Update the `AGENTS.md` layout table.**
  - Add rows after the `src/` row:
    - `include/infovox230.h`: the public API of the `infovox230` library (opaque engine, pull-based streaming, events; UTF-8 text by default). Spec: `docs/superpowers/specs/2026-09-28-infovox230-library-api-design.md`.
    - `lib/infovox230.c`: the library over `src/`: the input queue, the read loop with the `tp_putc` resend protocol, 441-sample blocks, the UTF-8 → cp1252 conversion, events stamped with exact sample positions, and cancel. Built as `build/infovox230.dll` (plus `libinfovox230.dll.a` and `infovox230.def`), or `build/linux/libinfovox230.so`.
  - Replace the `cli/ivxsay.c` row: `ivxsay` is a client of the library, playing through the speaker (`cli/audio_win.c` waveOut, `cli/audio_alsa.c` ALSA) or writing a WAV with `-o`. Mention `-e utf8|cp1252` and `-E`, and that its WAV output is byte-identical to the DLL's in all 12 languages (`tools/check_ivxsay.py`).
  - In the `tests/` row, add `api_test.c`: the library's own test, with no DLL, run on Windows and Linux. Also add the new `tts_cmp` "stops 22050" configuration: the engine's stop sequence, DLL versus port.
  - In the `Makefile` row: `make test` also builds the library, `api_test` and `ivxsay`; on Linux (`gcc-multilib`, `libasound2-dev:i386`) it builds `build/linux/` and runs `api_test` plus the WAV comparison.
  - In "Suggested next steps": replace the `cli/ivxsay.c` sentence with the library status. The open items are the `wpm_rate_table` as `IVX_SPEED_WPM`, user lexicons, volume, the `darules.dll` hook, and the 64-bit pool rework.

- [ ] **Step 2: Add `REFERENCE.md` §2.2.2** after §2.2.1, titled `### 2.2.2 The infovox230 library [CONFIRMED bit-exact]`. It covers:
  - **Stop sequence** [CONFIRMED `0x100119b1`–`0x100119e1`, also `0x10011c67`]: `tts_ctrl` reset 1, reset 2, then `synt_ctrl 0x0f`. Proven DLL versus port by `tts_cmp` "stops 22050" (give the stop count from the Task 4 output).
  - **Bookmarks** [LIKELY, `0x10011304`, `0x10012a85`, callback `0x10010fe0`]:
    - The SAPI engine keeps a 16-bit serial at `eng+0x108` and passes it to `TTS_MARK` inside its text loop; `tts_c20_mark` stores serial + 1.
    - `op_emit_bookmark` reports `(mark_lo & 0xff) << 16 | (short)mark_hi`, so the serial is its low 16 bits.
    - A zero mark is never reported.
    - Marks with no segments between them share one segment's serial.
  - **Marks don't change the audio** [CONFIRMED]:
    - `tts_cmp`'s reference WAVs have `TTS_MARK` at every word, and `ivxsay` (none) matches them byte for byte.
    - `api_test` compares the same text with and without marks.
  - **Engine init** [CONFIRMED addresses; effect NOT FOUND]: the engine also sets `feae` = 1 (`0x10010703`), `hook_interval` = 500 (`0x100105d7`) and a user-dictionary callback. The library, like `tts_cmp` part 3, leaves `feae` at 0; whether `feae` = 1 changes the audio is open.
  - **The library in brief:** the queue and read loop, 441-sample blocks, event stamping through `Synth.frame_left` in the frame callback, the release rule, the UTF-8 rules, idle-only settings, and cancel.
  - **Tests:** `api_test` (the checks count from its output), `check_ivxsay` 12/12, and the Linux result from Task 7 (identical, or the reported difference).
  - Update the §2.2 row for `ttsctrl.c` if it mentions `ivxsay` as the only driver.

- [ ] **Step 3: Final checkpoint**

Run: `make clean && make test`

Expected: a clean build with no warnings, and every test printing `0 failures`. Report the final numbers to the user.
