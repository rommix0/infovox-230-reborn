/* Words and sentences (tpstr.c, DLL 0x1001eef0-0x1001fe8f).
 *
 * The lexicon (Core.lexicon, H[3] or the character-name table) holds
 * {key NUL transcription NUL} entries. Without an index they are separated by 0x7f and
 * binary searched; with Core.lex_index (per first char) the keys lack their first char,
 * both strings are padded to an even length and a 0x7f ends each first-char run.
 *
 * Out of nodes (status 0x4x) is handled by retrying: retry bit 0 means the held words were
 * dropped to make room and the current sentence is being finished without them; bit 1
 * makes the sentence restart from the start of the text buffer.
 */
#include <string.h>
#include "tp.h"

#define HELD_TAIL(c)  ((Seg *)&(c)->held.tail)

/* 0x10048dc8..0x10048de7: symbol strings */
static const unsigned char tp_end_default[] = " @.";   /* 0x10048dc8: sentence end, no punct */
static const unsigned char tp_end_mode[] = "@@.";      /* 0x10048dd0: mode 0x80, [2] = punct */
static const unsigned char tp_end_text[] = ",,@@.";    /* 0x10048dd8: end of text (0x17) */
static const unsigned char tp_ctl_name[] = "@C";       /* 0x10048de0: prefix name of ^X */
static const unsigned char tp_class_name[] = "@D";     /* 0x10048de4: class 0x800 prefix */

static size_t str_len(const unsigned char *s)
{
    size_t n = 0;

    while (s[n] != 0)
        n++;
    return n;
}

/* upper-case a transliteration char of a lower-case letter */
static unsigned char upcase(unsigned short fl, unsigned char x)
{
    if ((fl & 0x400) && (signed char)x >= 'a' && (signed char)x <= '}')
        return x & 0x5f;
    return x;
}

/* 0x1001eef0: speak ch by its name in the lexicon: printable chars as themselves (lower
 * case letters upper-cased, "<ch>%" for classes 0x2000|0x100), control chars as "^<ch+64>"
 * (or "@C" and "<ch+64>"), 8-bit chars by their 2-char transliteration (else each char of
 * it alone). Mode 2: "@D" first for class 0x800. flush (char mode): drop the output being
 * played when flush_on_input, and end the sentence after the char. */
short tp_speak_char(Core *c, unsigned char ch, char flush)
{
    unsigned char b = ch;      /* the char, after the national charset remap */
    unsigned char e = ch;      /* class index: the remapped char when remapped */
    unsigned char buf[4];
    unsigned char *p;
    unsigned short fl;
    unsigned char x;
    short st;

    if (b & 0x80) {
        if (c->charset & 1) {
            b = TP_CHARSET1[b & 0x7f];
            e = b;
        } else if (c->charset & 2) {
            b = TP_CHARSET2[b & 0x7f];
            e = b;
        }
        if (b == 0)
            return 0;
    }
    if (flush && c->flush_on_input) {
        Seg *first = c->out.head.next;

        c->out_active = 0;
        c->frames_left = 0;
        seg_free_range(c, first, OUT_TAIL(c));
    }
    if ((c->mode & 2) && (c->class_flags[e] & 0x800)) {
        TP_COV(TPC_SPEAK_CLASS800);
        st = tp_lex_word(c, tp_class_name);
        if (st != 0)
            return st;
    }
    if ((signed char)b <= 0) {
        fl = c->class_flags[e];
        if (fl & 1)
            return 0;
        p = buf;
        x = tp_xlat_char(c, e);
        buf[0] = x;
        if (x != 0) {
            buf[0] = upcase(fl, x);
            p = buf + 1;
        }
        x = tp_xlat_char(c, b & 0x7f);
        *p = x;
        if (x != 0) {
            *p = upcase(fl, x);
            p++;
        }
        if (p == buf)
            return 0;
        *p = 0;
        TP_COV(TPC_SPEAK_PAIR);
        st = tp_lex_word(c, buf);
        if (st != TP_NOT_FOUND) {
            if (st != 0)
                return st;
            goto done;
        }
        /* no name for the pair: each char alone */
        TP_COV(TPC_SPEAK_SINGLE);
        buf[2] = buf[1];
        buf[1] = 0;
        st = tp_lex_word(c, buf);
        if (st != 0)
            return st;
        buf[0] = buf[2];
        if (buf[0] == 0)
            goto done;
        st = tp_lex_word(c, buf);
    } else if ((signed char)b < 0x20 || b == 0x7f) {
        TP_COV(TPC_SPEAK_CTL);
        buf[0] = '^';
        buf[1] = b == 0x7f ? '?' : (unsigned char)(b + 0x40);
        buf[2] = 0;
        st = tp_lex_word(c, buf);
        if (st == TP_NOT_FOUND) {
            st = tp_lex_word(c, tp_ctl_name);
            if (st != 0)
                return st;
            st = tp_lex_word(c, buf + 1);
        }
    } else {
        if ((c->class_flags[e] & 0x400) && b >= 'a' && b <= '}')
            buf[0] = b & 0x5f;
        else
            buf[0] = b;
        buf[1] = 0;
        fl = c->class_flags[(signed char)b];
        if ((fl & 0x2000) && (fl & 0x100)) {
            buf[1] = '%';
            buf[2] = 0;
        }
        st = tp_lex_word(c, buf);
    }
    if (st != 0)
        return st;
done:
    if (flush)
        return tp_sentence_end(c, 0);
    return 0;
}

/* 0x1001f170: one word, from..to in the text buffer. In order: the user dictionary
 * callback (1: replace the word in the text by the returned text, 2: by phonetic input
 * between esc_phon chars; the text is then read again), the lexicon, the compound path
 * (M3, split, M4) when both modules exist, the exception dictionary callback (gets the
 * word's symbols as phoneme names, returns a transcription), then M0 on the transliterated
 * upper-case word. */
short tp_word(Core *c, unsigned char *from, unsigned char *to)
{
    unsigned char buf[128];
    unsigned char *s, *d, *q, *rep;
    unsigned char b;
    short r, n, st;
    int k;

    if ((c->mode & 8) && (c->retry & 1))
        return 0;
    d = buf;
    for (s = from; s < to; s++)
        *d++ = *s;
    *d = 0;
    if (c->cb_userdict != NULL) {
        rep = s;   /* the DLL passes its copy pointer's slot, left at the end of the copy */
        r = (short)c->cb_userdict(c->cb_arg, (char *)buf, (char **)&rep);
        if (r > 0) {
            TP_COV(r == 2 ? TPC_USERDICT2 : TPC_USERDICT1);
            n = (short)((int)str_len(rep) + (int)(from - to));
            if (r == 2)
                n += 2;
            k = n;
            if (!(c->text_end + k < c->text_limit))
                return TP_COV(TPC_NO_ROOM), TP_NO_ROOM;
            q = (unsigned char *)c->text_end;
            if (n > 0)
                for (q--; q >= to; q--)
                    q[k] = *q;
            if (n < 0)
                for (; to < q; to++)
                    to[k] = *to;
            if (r == 2)
                *from++ = c->esc_phon;
            for (; *rep != 0; rep++)
                *from++ = *rep;
            if (r == 2)
                *from = c->esc_phon;
            c->text_end += k;
            c->text_cur = c->tok_start;
            return 0;
        }
    }

    d = buf;
    for (s = from; s < to; s++) {
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
                if (d - buf >= 0x80)
                    return TP_COV(TPC_TOO_LONG), TP_TOO_LONG;
                continue;
            }
        }
        *d++ = b;
        if (d - buf >= 0x80)
            return TP_COV(TPC_TOO_LONG), TP_TOO_LONG;
    }
    *d = 0;
    for (q = buf; *q != 0; q++)
        if ((signed char)*q >= 'a' && (signed char)*q <= '}')
            *q &= 0x5f;

    st = tp_lex_word(c, buf);
    if (st != TP_NOT_FOUND)
        return TP_COV(TPC_LEX_WORD), st;
    st = tp_text_to_segs(c, 1, buf, d);
    if (st != 0)
        return st;
    if (c->mod[3] != NULL && c->mod[4] != NULL) {
        st = tp_compound(c);
        if (st == TP_COMPOUND)
            return ctl_word_end(c);
        if (st != 0)
            return st;
    }
    if (c->cb_excdict != NULL) {
        /* the buffer now holds the symbol names; a NULL reply leaves them there */
        op_emit_phonemes(c, c->segs.head.next, SEGS_TAIL(c), (char *)buf, 0x2d);
        rep = (unsigned char *)c->cb_excdict(c->cb_arg, (char *)buf, 0);
        if (rep != NULL) {
            TP_COV(TPC_EXCDICT);
            if ((c->out_flags & 8) && c->cb_phonemes != NULL)
                c->cb_phonemes(c->cb_arg, (char *)rep);
            seg_free_all(c);
            st = tp_text_to_segs(c, 1, rep, rep + str_len(rep));
            if (st != 0)
                return st;
        }
    }
    TP_COV(TPC_M0);
    st = run_module(c, 0);
    if (st != 0) {
        if ((st & 0xf0) != 0x40)
            return st;
        if (c->held.head.next != HELD_TAIL(c)) {
            TP_COV(TPC_M0_RETRY);
            seg_free_segs_held(c);
            c->retry |= 1;
            tp_text_to_segs(c, 1, buf, d);
            st = run_module(c, 0);
        }
        if (st != 0) {
            seg_free_segs_held(c);
            return st;
        }
    }
    return ctl_word_end(c);
}

/* 0x1001f570: M3 marks a compound in the word's symbols with '=' (prefix) and/or '-'
 * (suffix); the stem is looked up in the lexicon and the segments rebuilt as prefix
 * symbols + stem transcription + suffix symbols, then M4 runs. The word's segments are
 * held (and a copy worked on) while M3 runs. Returns 0 (not a compound: go on with the
 * word), 0xf0 (done) or a status. */
short tp_compound(Core *c)
{
    unsigned char buf[0x34];
    unsigned char *eq, *hy, *stem, *d, *s;
    short st;

    seg_hold(c);
    if (seg_copy_held(c) != 0) {
        TP_COV(TPC_CMP_RETRY);
        if (c->held_mark == c->held.head.next) {
            seg_free_segs_held(c);
            return CW_NO_NODES;
        }
        seg_unhold(c);
        seg_free_held(c);
        c->retry |= 1;
        seg_hold(c);
        if (seg_copy_held(c) != 0) {
            seg_free_segs_held(c);
            return CW_NO_NODES;
        }
    }
    st = run_module(c, 3);
    if (st != 0) {
        if ((st & 0xf0) != 0x40)
            return st;
        TP_COV(TPC_CMP_RETRY);
        if (c->held_mark == c->held.head.next) {
            seg_free_segs_held(c);
            return CW_NO_NODES;
        }
        seg_unhold(c);
        seg_free_held(c);
        c->retry |= 1;
        seg_hold(c);
        seg_copy_held(c);
        st = run_module(c, 3);
        if (st != 0) {
            seg_unhold(c);
            return st;
        }
    }
    op_emit_phonemes(c, c->segs.head.next, SEGS_TAIL(c), (char *)buf, 0x2d);
    seg_free_all(c);
    seg_unhold(c);
    eq = (unsigned char *)strchr((char *)buf, '=');
    hy = (unsigned char *)strchr((char *)buf, '-');
    if (eq == NULL) {
        if (hy == NULL)
            return 0;
        /* stem-suffix */
        TP_COV(TPC_CMP_HY);
        *hy = 0;
        stem = lex_lookup(c, buf + 1);
        if (stem == NULL)
            return 0;
        *hy = '-';
    } else if (hy == NULL) {
        /* prefix=stem: the last char is dropped from the stem */
        TP_COV(TPC_CMP_EQ);
        eq[str_len(eq) - 1] = 0;
        stem = lex_lookup(c, eq + 1);
        if (stem == NULL)
            return 0;
    } else {
        /* prefix=stem-suffix: try prefix+stem as one word, else the stem alone */
        TP_COV(TPC_CMP_BOTH);
        *eq = 0;
        *hy = 0;
        d = buf + str_len(buf);
        s = eq + 1;
        do
            *d++ = *s;
        while (*s++ != 0);
        stem = lex_lookup(c, buf + 1);
        if (stem == NULL) {
            stem = lex_lookup(c, eq);
            if (stem == NULL)
                return 0;
            *eq = '=';
            *hy = '-';
        } else {
            eq = NULL;
            *hy = '-';
        }
    }
    seg_free_all(c);
    if (eq != NULL) {
        st = tp_text_to_segs(c, 0, buf, eq + 1);
        if (st != 0)
            return st;
    }
    st = tp_text_to_segs(c, 1, stem, stem + str_len(stem));
    if (st != 0)
        return st;
    if (hy != NULL) {
        st = tp_text_to_segs(c, 0, hy, hy + str_len(hy));
        if (st != 0)
            return st;
    }
    st = run_module(c, 4);
    if (st == 0)
        TP_COV(TPC_CMP_DONE);
    return st != 0 ? st : TP_COMPOUND;
}

/* the lexicon's key compare: unsigned bytes, -1/0/1 */
static int lex_cmp(const unsigned char *a, const unsigned char *b)
{
    for (;;) {
        if (a[0] != b[0])
            return a[0] < b[0] ? -1 : 1;
        if (a[0] == 0)
            return 0;
        if (a[1] != b[1])
            return a[1] < b[1] ? -1 : 1;
        a += 2;
        b += 2;
        if (a[-1] == 0)
            return 0;
    }
}

/* 0x1001f890: the transcription of word, or NULL */
unsigned char *lex_lookup(Core *c, const unsigned char *word)
{
    unsigned char *lo, *hi, *mid, *p;
    int r;

    if (word == NULL || word[0] == 0)
        return NULL;
    if (c->lex_index != NULL) {
        p = c->lexicon + c->lex_index[(signed char)word[0]];
        if (*p == 0x7f)
            return NULL;
        for (;;) {
            r = lex_cmp(p, word + 1);
            if (r > 0)
                return NULL;
            do
                p += 2;
            while (p[-1] != 0);
            if (r == 0)
                return p;
            do
                p += 2;
            while (p[-1] != 0);
            if (*p == 0x7f)
                return NULL;
        }
    }
    p = c->lexicon;
    lo = p + 4;
    hi = p + ((((((unsigned)p[0] << 8) + p[1]) << 8) + p[2]) << 8) + p[3];   /* BE size */
    while (lo != hi) {
        mid = lo + (hi - lo) / 2;
        while (mid[-1] != 0x7f && mid > lo)
            mid--;
        r = lex_cmp(mid, word);
        if (r == 0) {
            while (*mid++ != 0)
                ;
            return mid;
        }
        if (r > 0) {
            hi = mid;
        } else {
            while (*mid++ != 0x7f)
                ;
            lo = mid;
        }
    }
    return NULL;
}

/* 0x1001fa10: a non-word token (digits, symbols) through M1. 8-bit chars are remapped
 * to the national charset in place. */
short tp_token_m1(Core *c, unsigned char *from, unsigned char *to)
{
    unsigned char *p;
    short st;

    if ((c->mode & 8) && (c->retry & 1))
        return 0;
    for (p = from; p < to; p++)
        if ((signed char)*p < 0) {
            if (c->charset & 1)
                *p = TP_CHARSET1[*p & 0x7f];
            else if (c->charset & 2)
                *p = TP_CHARSET2[*p & 0x7f];
        }
    st = tp_text_to_segs(c, 1, from, to);
    if (st != 0)
        return st;
    TP_COV(TPC_M1);
    st = run_module(c, 1);
    if (st != 0) {
        if ((st & 0xf0) != 0x40)
            return st;
        if (c->held.head.next != HELD_TAIL(c)) {
            TP_COV(TPC_M1_RETRY);
            seg_free_held(c);
            c->retry |= 1;
            tp_text_to_segs(c, 1, from, to);
            st = run_module(c, 1);
        }
        if (st != 0) {
            seg_free_segs_held(c);
            return st;
        }
    }
    return ctl_word_end(c);
}

/* 0x1001fb20: a word from the lexicon only; 0x73 = not found */
short tp_lex_word(Core *c, const unsigned char *word)
{
    unsigned char *t;
    short st;

    if ((c->mode & 8) && (c->retry & 1))
        return 0;
    t = lex_lookup(c, word);
    if (t == NULL)
        return TP_NOT_FOUND;
    st = tp_text_to_segs(c, 1, t, t + str_len(t));
    if (st != 0)
        return st;
    return ctl_word_end(c);
}

/* restart the sentence from the start of the text buffer after running out of nodes */
static short restart(Core *c)
{
    TP_COV(TPC_SENT_RESTART);
    seg_free_segs_held(c);
    c->retry |= 2;
    c->text_cur = c->text;
    return 0;
}

/* 0x1001fb90: end of sentence (mode 8): the held words become segs, a punctuation
 * symbol is appended, M5 and M2 run and the utterance starts. punct 0x17 = end of text,
 * 0 = none (" @."). */
short tp_sentence_end(Core *c, unsigned char punct)
{
    unsigned char buf[8];
    const unsigned char *src;
    Seg *s;
    short st;
    int k;

    if (!(c->mode & 8)) {
        c->text_consumed = 1;
        c->text_done = c->text_cur;
        return 0;
    }
    if (c->retry & 2) {
        TP_COV(TPC_SENT_SKIP);
        c->retry &= ~2;
        c->text_done = c->text_cur;
        c->text_consumed = 1;
        return 0;
    }
    if (c->retry & 1) {
        TP_COV(TPC_SENT_SKIP);
        c->text_cur = c->text;
        c->retry = (unsigned char)((c->retry & ~1) | 2);
        return 0;
    }
    TP_COV(TPC_SENT_END);
    seg_free_all(c);
    s = c->held.head.next;
    c->segs.head.next = s;
    s->prev = SEGS_HEAD(c);
    s = c->held.tail.prev;
    c->segs.tail.prev = s;
    s->next = SEGS_TAIL(c);
    c->held.head.next = HELD_TAIL(c);
    c->held.tail.prev = (Seg *)&c->held.head;

    if (punct == 0x17) {
        src = tp_end_text;
    } else if (punct != 0 && c->f6dc) {
        for (k = 0; k < 4; k++)
            buf[k] = tp_end_mode[k];
        buf[2] = punct;
        src = NULL;
    } else if (punct != 0) {
        buf[0] = punct;
        buf[1] = 0;
        src = NULL;
    } else {
        src = tp_end_default;
    }
    if (src != NULL) {
        k = 0;
        do
            buf[k] = src[k];
        while (src[k++] != 0);
    }
    if (!(c->out_flags & 4)) {
        st = tp_text_to_segs(c, 0, buf, buf + str_len(buf));
        if (st != 0)
            return restart(c);
    }
    if (c->mod[5] != NULL) {
        st = run_module(c, 5);
        if (st != 0) {
            if ((st & 0xf0) != 0x40)
                return st;
            return restart(c);
        }
    }
    if (c->out_flags & 5)
        op_emit_phonemes(c, c->segs.head.next, SEGS_TAIL(c), NULL, 0);
    if (c->out_flags & 4) {
        TP_COV(TPC_NOSYNTH);
        seg_free_segs_held(c);
        c->text_end = c->text;
        c->text_cur = c->text;
        c->tok_start = c->text;
        c->tok_start2 = c->text;
        c->text_done = c->text;
        return 0;
    }
    st = run_module(c, 2);
    if (st != 0) {
        if ((st & 0xf0) != 0x40)
            return st;
        return restart(c);
    }
    c->segs.tail.prev->mark_lo = c->mark_lo;
    c->segs.tail.prev->mark_hi = c->mark_hi;
    c->retry = 0;
    c->text_done = c->text_cur;
    c->text_consumed = 1;
    return op_start_utterance(c);
}
