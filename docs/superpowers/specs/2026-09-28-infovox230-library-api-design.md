# infovox230: a shared-library API for the C port

Date: 2026-09-28. Status: approved in chat, awaiting written-spec review.

## Goal

Package the bit-exact C port (`src/`) as a shared library with a small public API: `infovox230.dll` on Windows, `libinfovox230.so` on Linux. `ivxsay` becomes a client of that library and can also speak through the speaker.

The library serves two kinds of caller with one model:

- **Offline rendering:** text to a WAV file or a buffer.
- **Real-time hosts:** screen-reader add-ons, speech-dispatcher modules and games. These feed text in pieces, pull audio in small blocks, cancel fast, and need index marks.

## Constraints and decisions

- **32-bit only for v1.** The pool nodes use the DLL's 32-bit layout (0x38-byte nodes with raw pointers, items straddling nodes; `src/cw.h`). The API hides every struct behind an opaque handle, so a later 64-bit rework (its own project) changes neither the API nor its callers.
- **Reentrancy.** One engine handle may be used by one thread at a time; separate handles are independent. The port keeps all its state in `Core` and `Synth`. Its only globals are test coverage counters and two constant strings (`src/contwork.c`, `src/tpchar.c`, `src/cwvars.c`) [CONFIRMED].
- **The library is audio-free.** It produces 16-bit mono PCM and events. Device output lives in `ivxsay`.
- **Pull-based streaming.** The caller feeds text and pulls samples; `ivx_synthesize` is a convenience loop on top.
- **Text is UTF-8 by default**, converted to the engine's native Windows-1252. Windows-1252 pass-through is an option.
- **Names.** The library, header and implementation file are called `infovox230`, to tell it apart from other Infovox versions. The function prefix is `ivx_`.

## Public API (`include/infovox230.h`)

All functions are declared `IVX_API` and `IVX_CALL`.

- `IVX_API` is `__declspec(dllexport)` or `__declspec(dllimport)` on Windows, and `__attribute__((visibility("default")))` elsewhere.
- `IVX_CALL` is `__cdecl` on 32-bit Windows, so MSVC clients link correctly, and empty elsewhere.

```c
typedef struct IvxEngine IvxEngine;           /* opaque */

/* lifecycle */
IvxEngine *ivx_create(int *err);               /* 900-node pool, 22050 Hz, text mode 8, charset 2 */
void       ivx_destroy(IvxEngine *e);          /* NULL is allowed */
int        ivx_load_language(IvxEngine *e, const char *path);
int        ivx_load_language_mem(IvxEngine *e, const void *ivx, size_t n);   /* copied */

/* settings */
enum {
    IVX_VOICE,           /* 0..4 built in, 5..9 stored (queued) */
    IVX_RATE,            /* 0..100 (queued) */
    IVX_PITCH,           /* 0..100 (queued) */
    IVX_FD,              /* 0..100 (queued) */
    IVX_FRAME_RATE,      /* frames/s, 2..1000; 100 = normal speed (queued) */
    IVX_SAMPLE_RATE,     /* 8000..48000 (idle only) */
    IVX_TEXT_MODE,       /* tts_ctrl 5 flags (idle only) */
    IVX_CHARSET,         /* tts_ctrl 4 flags (idle only) */
    IVX_INPUT_ENCODING,  /* IVX_ENC_UTF8 (default) or IVX_ENC_CP1252 (idle only) */
    IVX_EVENT_MASK       /* bits 1 << IVX_EV_*; default marks, errors and done (idle only) */
};
int ivx_set(IvxEngine *e, int param, int value);
int ivx_get(IvxEngine *e, int param, int *value);

/* streaming */
int  ivx_feed(IvxEngine *e, const char *text, size_t len);
int  ivx_feed_phonetic(IvxEngine *e, const char *sym, size_t len);
int  ivx_mark(IvxEngine *e, uint32_t id);
int  ivx_flush(IvxEngine *e);
int  ivx_read(IvxEngine *e, short *pcm, int max);   /* samples written, or < 0 on error */
int  ivx_next_event(IvxEngine *e, IvxEvent *ev);    /* 1 = got one, 0 = none */
void ivx_cancel(IvxEngine *e);

typedef struct {
    int      type;      /* IVX_EV_MARK, IVX_EV_PHONEME, IVX_EV_ERROR, IVX_EV_DONE */
    uint32_t value;     /* mark id / phoneme name (1-2 chars, first in low byte) / status code / 0 */
    uint64_t sample;    /* position in this engine's output, in samples */
} IvxEvent;

/* convenience: feed + flush + read until done; cb returns nonzero to cancel */
typedef int (IVX_CALL *IvxAudioCb)(void *user, const short *pcm, int n,
                                   const IvxEvent *ev, int nev);
int ivx_synthesize(IvxEngine *e, const char *text, size_t len,
                   IvxAudioCb cb, void *user);

const char *ivx_strerror(int err);
int         ivx_version(void);    /* major << 16 | minor */
```

`ivx_version` also serves as a header/library consistency check against `IVX_VERSION`.

**Error codes** are negative:

| Code | Meaning |
|---|---|
| `IVX_E_NOMEM` | Out of memory |
| `IVX_E_IO` | The language file can't be read |
| `IVX_E_BADFILE` | Too short, a section offset (`H[3]`, `H[5]`, `H[6]`, `H[8]`–`H[15]`) past the end, or the bind was refused ("XX" tag) |
| `IVX_E_BUSY` | An idle-only call was made while text is queued or audio is playing |
| `IVX_E_NOLANG` | Feed, read or synthesize before any language is loaded |
| `IVX_E_RANGE` | Unknown parameter or value out of range |
| `IVX_E_ARG` | NULL engine or buffer |

**Out of scope for v1:**

- user and exception lexicons (`cb_userdict`, `cb_excdict`): added in 1.1, see `2026-09-29-infovox230-dictionaries-design.md`
- the SAPI words-per-minute table (`wpm_rate_table`, `0x100453d8`): readily portable later as `IVX_SPEED_WPM`
- the `darules.dll` norm hook (the SAPI engine installs it as the exception dictionary; see the dictionaries spec)
- volume
- word-position events

## Text input

- **Encoding.**
  - **UTF-8 (default):** each code point in Windows-1252 maps to its byte; 0x80–0x9F use the cp1252 table, so `€` becomes 0x80 and `’` becomes 0x92. Other code points, and invalid or overlong sequences, become a space.
  - **CP1252:** bytes pass through unchanged.
  - A UTF-8 sequence split across two `ivx_feed` calls is carried over.
- **Control characters.** `ivx_feed` removes 0x01, 0x02 and 0x17 (the default `esc_mark`, `esc_phon` and end of text), so caller text can't trigger escapes. LF becomes CR unless a CR comes just before it (as in `ivxsay`), so a blank line ends a sentence.
- **Phonetic input.** `ivx_feed_phonetic` queues `esc_phon`, the symbols, then `esc_phon`. That is the engine's phonetic-input escape (REFERENCE §5.5.1).

## The queue and the read loop

`ivx_feed`, `ivx_feed_phonetic`, `ivx_mark`, `ivx_flush` and the queued settings append items to one ordered input queue. Nothing runs until the caller reads.

**`ivx_read(e, pcm, max)`** repeats these steps until `max` samples are written or the engine is idle:

1. If the staging block holds samples, copy them to `pcm`.
2. Otherwise, if `core->out_active`, call `synt_generate` for one **441-sample block** into the staging buffer.
3. Otherwise, if a char is pending or the queue isn't empty, process one item:
   - **Char:** `r = tp_putc(core, ch)`. On r = 2 the char stays pending and is sent again once the audio has played, up to 8 tries in all. This is the engine's protocol (`0x10012345`, REFERENCE §2.2.1).
   - **Mark:** `tts_ctrl(TTS_MARK, serial)` with the next 16-bit serial, and record `serial → id`. This is what the SAPI engine does (`0x10011304`, `0x10012a85`) [LIKELY].
   - **Setting:** voice, rate, pitch and FD go through `tts_ctrl`; the frame rate goes through `synt_ctrl 0x0e`.
   - **Flush:** `tp_putc(0x17)`, under the same pending rule.
4. Otherwise, the engine is idle. If a flush was processed since the last `DONE`, queue a `DONE` event at the current position. Return the count written, which may be 0.

The processing order equals `ivxsay`'s current `say()`/`play()` loop. So the audio **doesn't depend on `max`**, and it stays byte-identical to the DLL-verified WAVs, including the zero padding to 441 samples after each utterance. The block size is fixed at 441 for that reason.

**`ivx_synthesize`**:

1. Feeds the text and flushes.
2. Reads 441-sample blocks until `DONE`, handing each block to `cb` with the events released for it.
3. Cancels if `cb` returns nonzero.

It returns 0, or a negative error code.

## Events

The synth's frame callback is a wrapper around `op_next_frame`. It keeps `frame_pos`, the output position where the frame it's about to return starts:

- Before each call it records `frame_pos = next_frame_pos`.
- After it, it adds `fs / frame_rate`, the same integer `synt_generate` puts into `frame_left` at the same moment (`src/synth.c:179`). Frames that return NULL (silence) have the same length.

Callbacks that fire inside `op_next_frame` are stamped with `frame_pos`:

| Callback | Event |
|---|---|
| `cb_bookmark` (`op_emit_bookmark`) | `IVX_EV_MARK`, with the id looked up by serial. Values not in the map (the high-byte form) are dropped. |
| `cb_phonemes` (`op_emit_phonemes`, `out_flags` bit 1 on only when the mask asks for it) | `IVX_EV_PHONEME`. The callback gets one segment's name as a 1–2-char string (`src/tpelem.c:13`), packed with the first char in the low byte. `out_flags` bit 3, the text-side phoneme output in `tpstr.c`, stays off. |

`cb_status` becomes `IVX_EV_ERROR` with the status code, stamped with the current output position. Audio carries on as the DLL's would.

**Release rule:** `ivx_next_event` returns an event only once `ivx_read` has handed out audio up to its `sample`, and always in queue order. A player fires each event when its own playback count reaches `ev.sample`.

## Settings timing

- **Queued, applied in stream order:** voice, rate, pitch, FD, frame rate. `ivx_set` checks the range at once and returns `IVX_E_RANGE`, so the queued call can't fail. `ivx_get` returns the latest value set (queued or not).
- **Idle only (otherwise `IVX_E_BUSY`):** sample rate, text mode, charset, input encoding, event mask, and language loading. The caller can `ivx_cancel` first.

## Cancel

`ivx_cancel`:

1. empties the queue, the pending char, the staging block, the unreleased events and the mark map;
2. calls `tts_ctrl` reset 1, then reset 2, then `synt_ctrl 0x0f`, the engine's stop sequence [CONFIRMED `0x100119b1`–`0x100119e1`];
3. sets `next_frame_pos` to the output position.

Settings that were queued but not yet applied are dropped; the values that were applied remain.

## Language loading

`ivx_load_language` reads the file into a buffer padded with 16 zero bytes (`lex_lookup` can read one byte past the lexicon). It then runs swap (0x21) and bind (0x03) on the copy, and `tts_ctrl` mode and charset again. On a failed bind the old language stays bound, as in `tts_c03_bind`.

The engine owns the buffer. It's freed on the next successful load or on destroy.

## Build

**Makefile targets:**

| Platform | Outputs |
|---|---|
| Windows (`$(OS) == Windows_NT`) | `build/infovox230.dll`, `build/libinfovox230.dll.a` (`-Wl,--out-implib`), `build/infovox230.def` (`-Wl,--output-def`) |
| Linux | `build/libinfovox230.so` (`-m32 -fPIC -shared -Wl,-soname,libinfovox230.so.0`) |

- Both use `-fvisibility=hidden`, so only the `ivx_*` symbols are exported, and keep `-msse2 -mfpmath=sse`.
- `ivxsay` and `api_test` link against the shared library.
- The existing tests keep linking the object files.

## `ivxsay` (`cli/`)

- `cli/ivxsay.c` includes only `infovox230.h`. The options are as today, plus:
  - `-e utf8|cp1252` (default utf8);
  - `-E`, which prints events to stderr as `time_s type value`;
  - **no `-o`, which plays through the speaker.**
- `cli/audio.h` declares `audio_open(fs)`, `audio_write(pcm, n)` and `audio_close(drain)`.
  - `cli/audio_win.c` uses `waveOut` (winmm) with 4 buffers of 100 ms each, waiting on `WHDR_DONE` through an event.
  - `cli/audio_alsa.c` uses ALSA's `default` device (S16_LE, mono), `snd_pcm_writei` and `snd_pcm_drain`, linked with `-lasound`. It needs `libasound2-dev:i386`.
- Ctrl+C sets a flag. The loop then calls `ivx_cancel` and `audio_close(0)`, which drops the buffered audio.
- Turning `-l sw` into `bin/swrules.ivx` stays in `ivxsay`.

## Testing

1. **`tools/check_ivxsay.py`** runs `ivxsay -e cp1252` through the shared library and still requires byte-identical WAVs to `build/wav/<lang>_0.wav` in all 12 languages. That proves the read loop equals the DLL-verified pipeline.
2. **`tests/api_test.c`** links only the library:
   - **Buffer size:** `max` = 1, 7, 441 and 4096 give identical audio and events; `ivx_synthesize` equals feed + flush + read.
   - **Marks:**
     - they arrive in order, with non-decreasing samples, and are never released before their audio;
     - text with marks gives the same audio as the same text without them [HYPOTHESIS that `TTS_MARK` only stamps segments; if it's wrong, the finding is documented and the check changes];
     - several engines run interleaved equal the same engines run one after another.
   - **Encoding:** UTF-8 `é`, `€` and `’` give the same audio as their cp1252 bytes. Code points outside cp1252, and invalid, overlong or split sequences, behave as specified. Embedded 0x01, 0x02 and 0x17 are removed.
   - **Errors:** `BUSY`, `NOLANG`, `IO`, `BADFILE` (a short file, the "XX" tag), `RANGE`, `ARG`.
   - **Cancel:** at random points, then new text. The result must be non-empty audio and exactly one `DONE`, with no event from before the cancel.
3. **`tests/tts_cmp.c`** gains a part-3 configuration that runs the stop sequence (reset 1, reset 2, `synt 0x0f`) at random points mid-utterance, DLL versus port, and compares the Core, pool and samples. That proves cancel is faithful to the original.
4. **Linux (WSL Ubuntu):** build `libinfovox230.so`, `api_test` and `ivxsay`, run `api_test`, and compare the 12 WAVs byte for byte with the Windows ones. This needs `gcc-multilib` and `libasound2-dev:i386`, installed with sudo inside WSL after asking. Speaker playback under WSL is a smoke test only.

`make test` runs 1–3 on Windows. On Linux it runs 2 and a WAV comparison, since the DLL tests need Windows.

## Documentation

- **AGENTS.md:**
  - layout rows for `include/`, `lib/` and `cli/`;
  - the `docs/` ground rule: the papers stay untouched, and new design docs go in `docs/superpowers/specs/`;
  - the next steps.
- **REFERENCE.md:** a new §2.2.2 on the library, with the engine's stop and bookmark findings and their confidence tags.
