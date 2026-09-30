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
 *
 * .ivx files must be trusted: the load byte-swaps the file by walking its record lengths,
 * and the checks it makes (size, section offsets, the bind's tag) do not stop a corrupt
 * file from crashing or hanging the engine.
 *
 * The original Ivx230nt.dll crashes on some input that is not text (binary bytes, a
 * "word" of hundreds of letters), and its synth reads outside its tables with voices 2 and
 * 4. The port guards exactly those places, changing nothing else, and fuzzing with random
 * bytes, phonetic input and every text mode finds no crash or hang (REFERENCE.md §2.2.2).
 * Anything the caller feeds is still worth keeping to text.
 */
#ifndef INFOVOX230_H
#define INFOVOX230_H

#include <stddef.h>
#include <stdint.h>

#define IVX_VERSION 0x00010001   /* major << 16 | minor */

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
    IVX_E_BUSY    = -4,   /* idle-only call while text is queued, fed but not yet flushed,
                           * or audio is playing */
    IVX_E_NOLANG  = -5,   /* no language loaded */
    IVX_E_RANGE   = -6,   /* unknown parameter or value out of range */
    IVX_E_ARG     = -7    /* NULL engine, buffer or callback */
};

/* parameters for ivx_set / ivx_get */
enum {
    IVX_VOICE,            /* 0..4 built-in voices, 5..9 stored (copies of voice 0); queued */
    IVX_RATE,             /* 0..100; a voice parameter (breathiness-like AC/AN/AH amplitude
                             * balance), not speaking speed - it does not change duration.
                             * Use IVX_FRAME_RATE for speed. Queued. */
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
/* phonetic input: sym is a string of the language's phoneme names (the .ivx symbols, KIM
 * notation, e.g. "H[J" for Swedish "hej"), passed through as raw bytes (not UTF-8 decoded) */
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

/* Dictionary: fixes the pronunciation of single words. For each word in the text, in order:
 * an exact-case entry, a case-insensitive entry, the language's built-in lexicon, an
 * exact-case FALLBACK entry, a case-insensitive FALLBACK entry, the letter-to-sound rules.
 *
 * word: one word, 1-127 bytes after conversion to cp1252, without blanks, control chars,
 * digits or !#$%&()*+,-.;<=>?^_ (these split words in every language, so "e-mail", "R2D2"
 * and "two words" are IVX_E_RANGE). A character with no cp1252 equivalent converts to a
 * blank, so a word containing one is rejected the same way. Which other characters belong
 * to a word depends on the language and IVX_CHARSET: French splits at the apostrophe,
 * charset 2 at S/Z/Y with caron or diaeresis (0x8A 0x9A 0x8E 0x9E 0x9F). An entry whose
 * word the loaded language would split never matches. Matching ignores case (a-z, 0xE0-0xFE
 * and 0x9A 0x9C 0x9E 0xFF against their capitals) unless IVX_DICT_MATCH_CASE. word and a
 * TEXT or FALLBACK replacement are in the input encoding (IVX_INPUT_ENCODING), converted
 * when added. Replacements:
 *   IVX_DICT_TEXT      1-255 bytes of text, read by the engine in place of the word (control
 *                      chars become blanks). Its words are not looked up again: "cat" ->
 *                      "cat dog" says "cat dog".
 *   IVX_DICT_PHONETIC  1-255 bytes of phoneme names as for ivx_feed_phonetic, one phonetic
 *                      word: no blank or control char (0x01, 0x02 and 0x17 included).
 *   IVX_DICT_FALLBACK  a respelling, one word under the same rule as word, pronounced by the
 *                      letter-to-sound rules (not the lexicon); used only for words the
 *                      lexicon doesn't know and the engine doesn't split as compounds, of at
 *                      most 42 letters as the language spells them internally (the engine's
 *                      exception dictionary, tts_ctrl 0x15). Not with IVX_DICT_PHONETIC.
 *                      While FALLBACK entries exist, a word that runs out of rule nodes is
 *                      retried from its spelling minus its last letter (the engine's own
 *                      behaviour).
 *   IVX_DICT_MATCH_CASE  the entry matches only this exact case.
 * An entry is its word plus the FALLBACK and MATCH_CASE bits; adding one again replaces it.
 * Changes apply at once to every word the engine has not processed yet, including text fed
 * but not read. Entries stay across language loads and ivx_cancel. A replacement too long
 * for the engine's text buffer gives IVX_EV_ERROR 0x71. */
enum {
    IVX_DICT_TEXT       = 0,
    IVX_DICT_PHONETIC   = 1,
    IVX_DICT_FALLBACK   = 2,
    IVX_DICT_MATCH_CASE = 4
};
/* returns IVX_OK; IVX_E_ARG (e, word or replacement NULL); IVX_E_RANGE (word, replacement
 * or flags outside the rules above, including an unknown flag bit or PHONETIC with
 * FALLBACK); IVX_E_NOMEM */
IVX_API int IVX_CALL ivx_dict_add(IvxEngine *e, const char *word, const char *replacement, int flags);
/* flags: the entry's FALLBACK and MATCH_CASE bits (PHONETIC is ignored).
 * returns IVX_OK; IVX_E_ARG (e or word NULL); IVX_E_RANGE (no such entry, or word or flags
 * outside the rules above) */
IVX_API int IVX_CALL ivx_dict_remove(IvxEngine *e, const char *word, int flags);
/* removes every entry; e may be NULL */
IVX_API void IVX_CALL ivx_dict_clear(IvxEngine *e);

IVX_API const char *IVX_CALL ivx_strerror(int err);
IVX_API int IVX_CALL ivx_version(void);

#ifdef __cplusplus
}
#endif

#endif
