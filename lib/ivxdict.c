/* The infovox230 dictionary (ivxdict.h). The engine side is tp_word (0x1001f170,
 * src/tpstr.c): the user callback gets each word as it stands in Core.text before the
 * lexicon, the exception callback gets " " + the word's symbol names + " " when the lexicon
 * has no entry and the word is not a compound, and takes a respelling.
 */
#include <stdlib.h>
#include <string.h>
#include "infovox230.h"
#include "ivxdict.h"

struct DictEntry {
    DictEntry *next;
    int flags;                   /* IVX_DICT_* */
    size_t klen, rlen;
    unsigned char *key;          /* exact with IVX_DICT_MATCH_CASE, else case-folded */
    unsigned char *rep;
    unsigned char data[];        /* key NUL rep NUL */
};

/* ---- words and case folding ---- */

/* ASCII punctuation that ends a word in every language and charset. The rest, ' " / : @
 * [ \ ] ` { | } ~ and 0x80-0xff, is part of a word in some languages (their 7-bit RULSYS
 * alphabets) or charsets, so whether a key can match depends on the loaded language */
#define DICT_BREAK "!#$%&()*+,-.;<=>?^_"

int dict_is_word(const unsigned char *s, size_t n)
{
    size_t k;
    unsigned char b;

    for (k = 0; k < n; k++) {
        b = s[k];
        if (b <= ' ' || b == 0x7f || (b >= '0' && b <= '9') || (b < 0x80 && strchr(DICT_BREAK, b) != NULL))
            return 0;
    }
    return 1;
}

static unsigned char fold(unsigned char b)
{
    if ((b >= 'a' && b <= 'z') || (b >= 0xe0 && b <= 0xfe && b != 0xf7))
        return (unsigned char)(b - 0x20);
    if (b == 0x9a || b == 0x9c || b == 0x9e)
        return (unsigned char)(b - 0x10);
    if (b == 0xff)
        return 0x9f;
    return b;
}

static void fold_copy(unsigned char *d, const unsigned char *s, size_t n)
{
    size_t k;

    for (k = 0; k < n; k++)
        d[k] = fold(s[k]);
    d[n] = 0;
}

/* FNV-1a of the folded key */
static size_t hash(const unsigned char *s, size_t n)
{
    uint32_t h = 2166136261u;
    size_t k;

    for (k = 0; k < n; k++)
        h = (h ^ fold(s[k])) * 16777619u;
    return h;
}

/* ---- the table ---- */

static int grow(IvxDict *d)
{
    size_t n = d->nbucket ? 2 * d->nbucket : 64, k;
    DictEntry **b = calloc(n, sizeof *b), *x, *next;

    if (b == NULL)
        return IVX_E_NOMEM;
    for (k = 0; k < d->nbucket; k++)
        for (x = d->bucket[k]; x != NULL; x = next) {
            next = x->next;
            x->next = b[hash(x->key, x->klen) & (n - 1)];
            b[hash(x->key, x->klen) & (n - 1)] = x;
        }
    free(d->bucket);
    d->bucket = b;
    d->nbucket = n;
    return IVX_OK;
}

/* the link to the entry with this identity (key in stored form, the FALLBACK and
 * MATCH_CASE bits), or to the NULL at the end of its bucket */
static DictEntry **link_of(IvxDict *d, const unsigned char *key, size_t n, int id)
{
    DictEntry **p = &d->bucket[hash(key, n) & (d->nbucket - 1)];

    while (*p != NULL && !(((*p)->flags & (IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE)) == id
                           && (*p)->klen == n && memcmp((*p)->key, key, n) == 0))
        p = &(*p)->next;
    return p;
}

int dict_add(IvxDict *d, const unsigned char *word, size_t wlen, const unsigned char *rep,
             size_t rlen, int flags)
{
    unsigned char f[DICT_WORD + 1];
    const unsigned char *key = word;
    int id = flags & (IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE);
    DictEntry **p, *x;

    if (d->count >= d->nbucket && grow(d) != IVX_OK)
        return IVX_E_NOMEM;
    if (!(flags & IVX_DICT_MATCH_CASE)) {
        fold_copy(f, word, wlen);
        key = f;
    }
    x = malloc(sizeof *x + wlen + rlen + 2);
    if (x == NULL)
        return IVX_E_NOMEM;
    x->flags = flags;
    x->klen = wlen;
    x->rlen = rlen;
    x->key = x->data;
    x->rep = x->data + wlen + 1;
    memcpy(x->key, key, wlen);
    x->key[wlen] = 0;
    memcpy(x->rep, rep, rlen);
    x->rep[rlen] = 0;
    p = link_of(d, key, wlen, id);
    if (*p != NULL) {             /* same identity: replace it */
        x->next = (*p)->next;
        free(*p);
    } else {
        x->next = NULL;
        d->count++;
        if (flags & IVX_DICT_FALLBACK)
            d->nfallback++;
    }
    *p = x;
    return IVX_OK;
}

int dict_remove(IvxDict *d, const unsigned char *word, size_t wlen, int flags)
{
    unsigned char f[DICT_WORD + 1];
    const unsigned char *key = word;
    DictEntry **p, *x;

    if (d->nbucket == 0)
        return IVX_E_RANGE;
    if (!(flags & IVX_DICT_MATCH_CASE)) {
        fold_copy(f, word, wlen);
        key = f;
    }
    p = link_of(d, key, wlen, flags & (IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE));
    if (*p == NULL)
        return IVX_E_RANGE;
    x = *p;
    *p = x->next;
    d->count--;
    if (x->flags & IVX_DICT_FALLBACK)
        d->nfallback--;
    free(x);
    return IVX_OK;
}

void dict_clear(IvxDict *d)
{
    size_t k;
    DictEntry *x, *next;

    for (k = 0; k < d->nbucket; k++)
        for (x = d->bucket[k]; x != NULL; x = next) {
            next = x->next;
            free(x);
        }
    free(d->bucket);
    d->bucket = NULL;
    d->nbucket = d->count = d->nfallback = 0;
}

/* the entry for a word from the text: an exact-case one, else a case-insensitive one */
static const DictEntry *lookup(const IvxDict *d, const unsigned char *w, size_t n, int fallback)
{
    unsigned char f[DICT_WORD + 1];
    const DictEntry *x, *any = NULL;

    if (d->nbucket == 0 || n == 0 || n > DICT_WORD)
        return NULL;
    fold_copy(f, w, n);
    for (x = d->bucket[hash(f, n) & (d->nbucket - 1)]; x != NULL; x = x->next) {
        if ((x->flags & IVX_DICT_FALLBACK) != fallback || x->klen != n)
            continue;
        if (x->flags & IVX_DICT_MATCH_CASE) {
            if (memcmp(x->key, w, n) == 0)
                return x;
        } else if (memcmp(x->key, f, n) == 0) {
            any = x;
        }
    }
    return any;
}

/* ---- spans ---- */

/* the compaction tp_putc did (tpchar.c:86-105): [text_done, text_end) moved to Core.text */
static void apply_shift(IvxDict *d)
{
    int k, j = 0, cut;
    DictSpan *s;

    for (k = 0; k < d->nspan; k++) {
        s = &d->span[k];
        if (s->off + s->len <= d->shift)
            continue;
        if (s->off < d->shift) {
            cut = d->shift - s->off;
            memmove(s->rep, s->rep + cut, (size_t)(s->len - cut));
            s->len -= cut;
            s->off = 0;
        } else {
            s->off -= d->shift;
        }
        if (j != k)
            d->span[j] = *s;
        j++;
    }
    d->nspan = j;
    d->shift = 0;
}

void dict_before_putc(IvxDict *d, const Core *c)
{
    d->shift = d->nspan != 0 && c->text_consumed ? (int)(c->text_done - c->text) : 0;
}

/* a callback ran, so tp_putc got past its compaction; after the call, text_consumed still
 * set with text_done in place means it returned before it (the content check covers the
 * rare case of a new sentence ending at the same offset) */
void dict_after_putc(IvxDict *d, const Core *c)
{
    int k, j = 0, end;

    if (d->shift != 0) {
        if (!c->text_consumed || (int)(c->text_done - c->text) != d->shift)
            apply_shift(d);
        d->shift = 0;
    }
    end = (int)(c->text_end - c->text);
    for (k = 0; k < d->nspan; k++)
        if (d->span[k].off + d->span[k].len <= end) {
            if (j != k)
                d->span[j] = d->span[k];
            j++;
        }
    d->nspan = j;
}

void dict_reset(IvxDict *d)
{
    d->nspan = 0;
    d->shift = 0;
    d->word_state = 0;
}

static int in_span(const IvxDict *d, const Core *c, int pos)
{
    int k, end = (int)(c->text_end - c->text);
    const DictSpan *s;

    for (k = 0; k < d->nspan; k++) {
        s = &d->span[k];
        if (pos >= s->off && pos < s->off + s->len && s->off + s->len <= end
            && memcmp(c->text + s->off, s->rep, (size_t)s->len) == 0)
            return 1;
    }
    return 0;
}

/* the text from pos + wlen on moves by growth bytes (the word there became something
 * growth bytes longer): shift every span that starts at or after it */
static void shift_spans(IvxDict *d, int pos, int wlen, int growth)
{
    int k;

    for (k = 0; k < d->nspan; k++)
        if (d->span[k].off >= pos + wlen)
            d->span[k].off += growth;
}

/* the word at pos (wlen bytes) is about to become rep: later spans move with the text.
 * Called before tp_word's own room check, so a replacement the engine still refuses with
 * 0x71 (no room) has already recorded a span and shifted later ones. With more than
 * DICT_SPANS replacements live in one sentence, the oldest span here is evicted, so an
 * out-of-nodes re-read could look those words up again. in_span's content check (the bytes
 * must still be there) backstops both cases. */
static void add_span(IvxDict *d, int pos, int wlen, const unsigned char *rep, int rlen)
{
    shift_spans(d, pos, wlen, rlen - wlen);
    if (d->nspan == DICT_SPANS) {
        memmove(d->span, d->span + 1, (DICT_SPANS - 1) * sizeof *d->span);
        d->nspan--;
    }
    d->span[d->nspan].off = pos;
    d->span[d->nspan].len = rlen;
    memcpy(d->span[d->nspan].rep, rep, (size_t)rlen);
    d->nspan++;
}

/* ---- the callbacks ---- */

/* tp_word's transliteration (tpstr.c:201-230): the charset table, tp_xlat_char, then
 * upper case. Returns the length, or -1 at 128 chars or more */
static int translit(Core *c, const unsigned char *s, size_t n, unsigned char *out)
{
    unsigned char *d = out, *q, b;

    for (; n != 0; s++, n--) {
        b = *s;
        if (b & 0x80) {
            if (c->charset & 1)
                b = TP_CHARSET1[b & 0x7f];
            else if (c->charset & 2)
                b = TP_CHARSET2[b & 0x7f];
            if (b == 0)
                continue;
            if ((signed char)b < 0) {
                *d = tp_xlat_char(c, b);
                if (*d != 0)
                    d++;
                *d = tp_xlat_char(c, b & 0x7f);
                if (*d != 0)
                    d++;
                if (d - out >= 0x80)
                    return -1;
                continue;
            }
        }
        *d++ = b;
        if (d - out >= 0x80)
            return -1;
    }
    *d = 0;
    for (q = out; *q != 0; q++)
        if ((signed char)*q >= 'a' && (signed char)*q <= '}')
            *q &= 0x5f;
    return (int)(d - out);
}

/* cb_userdict: called with from == Core.tok_start (tpchar.c:242,270,310) */
int dict_user(IvxDict *d, Core *c, const char *word, char **out)
{
    size_t n = strlen(word);
    int pos;
    const DictEntry *x;

    if (d->shift != 0)
        apply_shift(d);
    d->word_state = 0;
    if (n == 0 || n > DICT_WORD)
        return 0;
    memcpy(d->word, word, n + 1);
    d->word_len = (int)n;
    pos = (int)(c->tok_start - c->text);
    if (in_span(d, c, pos)) {
        d->word_state = 2;
        return 0;
    }
    d->word_state = 1;
    x = lookup(d, d->word, n, 0);
    if (x == NULL)
        return 0;
    memcpy(d->reply, x->rep, x->rlen + 1);
    *out = d->reply;
    if (x->flags & IVX_DICT_PHONETIC) {
        /* tp_word wraps the reply in esc_phon bytes: the text grows by rlen + 2 - wlen
         * (tpstr.c:176-178). No span is needed since phonetic input is never looked up. */
        shift_spans(d, pos, (int)n, (int)x->rlen + 2 - (int)n);
        return 2;
    }
    add_span(d, pos, (int)n, x->rep, (int)x->rlen);
    return 1;
}

/* cb_excdict: the key is " " + the word's symbol names + " " (op_emit_phonemes into a
 * 0x2d buffer, so at most 44 chars); the reply is a respelling in the same form */
char *dict_exc(IvxDict *d, Core *c, const char *key)
{
    unsigned char t[DICT_WORD + 4];
    int state = d->word_state, n;
    const DictEntry *x;

    d->word_state = 0;
    if (state != 1)
        return NULL;
    n = translit(c, d->word, (size_t)d->word_len, t + 1);
    if (n < 0)
        return NULL;
    t[0] = ' ';
    t[n + 1] = ' ';
    t[n + 2] = 0;
    if (strcmp((const char *)t, key) != 0)
        return NULL;
    x = lookup(d, d->word, (size_t)d->word_len, IVX_DICT_FALLBACK);
    if (x == NULL || translit(c, x->rep, x->rlen, (unsigned char *)d->reply) <= 0)
        return NULL;
    return d->reply;
}
