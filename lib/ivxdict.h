/* The infovox230 dictionary: entries, the engine's two dictionary callbacks and the text
 * replacements' spans. Design: docs/superpowers/specs/2026-09-29-infovox230-dictionaries-design.md
 *
 * Everything here is cp1252; lib/infovox230.c converts the caller's text and installs the
 * callbacks (tts_ctrl 0x14, 0x15) while entries exist.
 */
#ifndef IVXDICT_H
#define IVXDICT_H

#include <stddef.h>
#include "tts.h"

#define DICT_WORD  127   /* longest key or fallback respelling */
#define DICT_REP   255   /* longest text or phonetic replacement */
#define DICT_SPANS 64    /* text replacements tracked in Core.text at once */

typedef struct DictEntry DictEntry;

/* a text replacement in Core.text: its words are not looked up again */
typedef struct {
    int off, len;
    unsigned char rep[DICT_REP];
} DictSpan;

typedef struct {
    DictEntry **bucket;          /* by case-folded key */
    size_t nbucket, count, nfallback;
    DictSpan span[DICT_SPANS];
    int nspan;
    int shift;                   /* compaction tp_putc may do: Core.text_done - Core.text */
    char reply[DICT_REP + 1];    /* what a callback returns */
    unsigned char word[DICT_WORD + 1];   /* the word of the last user callback */
    int word_len, word_state;    /* 0 none, 1 looked up, 2 inside a span (final) */
} IvxDict;

/* word: 1..DICT_WORD bytes passing dict_is_word; rep: checked by the caller
 * (lib/infovox230.c). IVX_OK, IVX_E_NOMEM */
int dict_add(IvxDict *d, const unsigned char *word, size_t wlen, const unsigned char *rep,
             size_t rlen, int flags);
/* IVX_OK, IVX_E_RANGE (no such entry) */
int dict_remove(IvxDict *d, const unsigned char *word, size_t wlen, int flags);
void dict_clear(IvxDict *d);
/* 0 if a byte ends a word in every language: a blank, a control char, a digit or
 * !#$%&()*+,-.;<=>?^_ */
int dict_is_word(const unsigned char *s, size_t n);

/* the engine callbacks, and the calls around every tp_putc */
int dict_user(IvxDict *d, Core *c, const char *word, char **out);
char *dict_exc(IvxDict *d, Core *c, const char *key);
void dict_before_putc(IvxDict *d, const Core *c);
void dict_after_putc(IvxDict *d, const Core *c);
/* the Core's text was dropped (cancel, language load) */
void dict_reset(IvxDict *d);

#endif
