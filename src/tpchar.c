/* Text input state machine (tpchar.c, DLL 0x1001b050-0x1001c4ef).
 *
 * tp_putc appends a char to Core.text and runs the state functions over the unread text
 * (text_cur..text_end). tok_start marks the token being collected. States:
 *   0 start of token          1 phonetic input (after esc_phon)   2 overlong phonetic
 *   3 word letters            4 after '-' in a word               5 hyphen token pending
 *   6 skip line break after a hyphen                              7 spell a word
 *   8 other token             9 after a separator in a number (class 0x40)
 *   10 spell a token          11 separator inside a spelled token
 *   12/13 bookmark after esc_mark (13 returns to phonetic input)
 * Char classes (Core.class_flags): 1 ignored, 2 blank, 4 line break, 0x10 sentence
 * punctuation, 0x40 number separator, 0x80 digit, 0x100 spoken as a symbol, 0x200
 * letter, 0x400 lower case.
 */
#include "tp.h"

#define HELD_TAIL(c)  ((Seg *)&(c)->held.tail)

/* text buffer guard (tp_putc): bytes kept free at the end of Core.text. Below HARD there
 * is room for the char, its 2-char transliteration in phonetic input (tp_st01_phonetic)
 * and the " ." of a full buffer (tp_st00_start). */
#define TP_GUARD_SOFT 16
#define TP_GUARD_HARD 8

#ifdef CW_COVERAGE
unsigned long tp_cov[TPC_N];
#endif

/* 0x10048998: the token spoken for a hyphen between two words, "hy\0\0" in the DLL;
 * tp_symbol_to_seg reads the byte after the 'y' as the second char of a 2-char name */
static unsigned char tp_hy[4] = {'h', 'y', 0, 0};

/* 0x1001b050: feed one char. Returns 1 when an utterance is ready (frames), 2 when the
 * synth is busy (synth_state 0xff; the char stays pending and is processed by the next
 * call), else 0. Status codes of the states go to cb_status.
 * The DLL runs setjmp(c->jmpbuf) after the utterance check; nothing in the DLL longjmps
 * to it, so its error branch (tp_reset_text, cb_status(-1), return 0) is left out. */
int tp_putc(Core *c, unsigned char ch)
{
    short st;

    if (c->ivx == NULL)
        return 0;
    if (c->synth_state != 0) {
        op_start_utterance(c);
        if (c->synth_state == 0xff)
            return 2;
        c->synth_state = 0;
        if (c->pending == 0)
            return 1;
    }
    if (c->out_active == 0 && c->out.head.next != OUT_TAIL(c))
        seg_free_range(c, c->out.head.next, OUT_TAIL(c));

    if (c->mode & 1) {
        /* char mode: every char is spoken on its own */
        if (!((signed char)ch < 0 || ch > 0x1f || (ch >= 8 && ch <= 0xd)) && ch != c->esc_mark)
            return 0;
        *c->text_end++ = (char)ch;
        TP_COV(TPC_CHAR_MODE);
        if (c->tp_state == 12)
            return tp_st0c_mark(c);
        if (ch == c->esc_mark) {
            c->tp_state = 12;
            c->tok_start = c->text;
            c->text_cur = c->text;
            c->text_end = c->text + 1;
            return tp_st0c_mark(c);
        }
        c->text_end = c->text;
        c->text_cur = c->text;
        c->mode |= 8;
        tp_speak_char(c, ch, 1);
        c->mode &= ~8;
        if (c->synth_state == 0xff)
            return 2;
        if (c->synth_state == 1) {
            c->synth_state = 0;
            return 1;
        }
        return 0;
    }

    if (!((signed char)ch < 0 || ch > 0x1f || (ch >= 8 && ch <= 0xd && ch != '\n')
          || ch == c->esc_mark || ch == c->esc_phon || ch == 0x17))
        return 0;
    if (c->text_consumed) {
        /* drop the text before text_done */
        char *d = c->text;
        int tail = (int)(c->text_end - c->text_cur);

        TP_COV(TPC_COMPACT);

        while (c->text_done < c->text_end)
            *d++ = *c->text_done++;
        /* GUARD, not in the DLL: text_cur can lag behind text_done (a token too long to
         * speak rewinds it after the sentence was consumed); then d - tail lies before the
         * buffer and the next token writes into the Core fields before Core.text (the DLL
         * does, e.g. mod[] and the segment lists). Read from the start of what was kept. */
        if (tail > d - c->text)
            tail = (int)(d - c->text);
        c->text_end = d;
        c->text_cur = d - tail;
        c->tok_start = c->text;
        c->text_done = c->text;
        c->text_consumed = 0;
    }
    if (c->pending == 0) {
        /* GUARD, not in the DLL: nothing bounds the text buffer here, so one token of about
         * 800 bytes runs past Core.text into text_limit, xlat_hi and the fields after it
         * (the DLL crashes, e.g. at 0x1001b516, or reads garbage). Close to the end a char
         * becomes a blank, which ends the token; the next token start then finds the buffer
         * full and ends the sentence (tp_st00_start), which frees the buffer. At the very
         * end chars are dropped. 0x17 always goes in, so the end of text is never lost.
         * Only a token that runs to within 16 bytes of the end is affected: one of more
         * than 100 chars, past the 128-char word limit once transliterated or near it. */
        if (c->text_end >= c->text + sizeof c->text - TP_GUARD_SOFT && ch != 0x17) {
            if (c->text_end >= c->text + sizeof c->text - TP_GUARD_HARD)
                return 0;
            ch = ' ';
        }
        *c->text_end++ = (char)ch;
    }
    while (c->text_cur < c->text_end) {
        if (c->synth_state == 0xff) {
            c->pending = 1;
            TP_COV(TPC_BUSY);
            return 2;
        }
        switch (c->tp_state) {
        case 0:  st = tp_st00_start(c); break;
        case 1:  st = tp_st01_phonetic(c); break;
        case 2:  st = tp_st02_phonetic_skip(c); break;
        case 3:  st = tp_st03_word(c); break;
        case 4:
        case 5:  st = tp_st04_hyphen(c); break;
        case 6:  st = tp_st06_skip_break(c); break;
        case 7:  st = tp_st07_spell(c); break;
        case 8:  st = tp_st08_token(c); break;
        case 9:  st = tp_st09_num_sep(c); break;
        case 10: st = tp_st0a_spell(c); break;
        case 11: st = tp_st0b_spell_sep(c); break;
        case 12:
        case 13: st = tp_st0c_mark(c); break;
        default: st = TP_BAD_STATE; break;
        }
        if (st != 0) {
            if (c->cb_status != NULL)
                c->cb_status(c->cb_arg, st);
            break;
        }
    }
    if (c->pending) {
        c->pending = 0;
        return 1;
    }
    if (c->synth_state == 0xff)
        return 2;
    if (c->synth_state == 1) {
        c->synth_state = 0;
        return 1;
    }
    return 0;
}

/* 0x1001b470: classify the char at text_cur and pick the next state. A lone CR after a
 * CR ends the sentence ('.'), and so does any CR while fewer than 40 nodes are free
 * (0x17, end of text). A full buffer gets " ." appended so the sentence ends. */
short tp_st00_start(Core *c)
{
    unsigned char *p = (unsigned char *)c->text_cur;
    unsigned char ch = *p;

    if (ch == '\r') {
        if (p[-1] == '\r' && p > (unsigned char *)c->text && c->held.head.next != HELD_TAIL(c)) {
            ch = '.';
            TP_COV(TPC_CR_DOT);
        }
        if (ch == '\r' && (short)seg_free_count(c) < 0x28 && c->text_cur > c->text
            && c->held.head.next != HELD_TAIL(c)) {
            ch = 0x17;
            TP_COV(TPC_CR_EOT);
        }
    }
    if (c->text_end >= c->text_limit && c->text_end[-2] != '.') {
        TP_COV(TPC_BUF_FULL);
        c->text_end[1] = c->text_end[-1];
        c->text_end[-1] = ' ';
        c->text_end[0] = '.';
        c->text_end += 2;
        ch = (unsigned char)*c->text_cur;
    }
    c->tok_start = c->text_cur;
    c->tok_start2 = c->text_cur;
    if (ch == c->esc_mark) {
        c->tp_state = 12;
        return 0;
    }
    /* 0x1001b55f: a patched jmp skips the demo version's nag here ("zdemo" every
     * count_init sentences); licensing, not ported */
    c->text_cur++;
    if (ch == c->esc_phon) {
        c->tok_start2++;
        c->tok_start++;
        c->tp_state = 1;
        return 0;
    }
    if (ch == 0x17)
        return tp_sentence_end(c, 0x17);
    if (tp_class(c, ch) & 1)
        return 0;
    if (tp_class(c, ch) & 4)
        ch = ' ';
    if (tp_class(c, ch) & 0x100)
        return tp_speak_char(c, ch, 0);
    if (tp_class(c, ch) & 0x10) {
        if (c->count_init != 0)
            c->count--;
        return tp_sentence_end(c, ch);
    }
    c->text_cur--;
    if (tp_class(c, ch) & 0x200) {
        c->tp_state = 3;
        return 0;
    }
    c->tp_state = 8;
    return 0;
}

/* 0x1001b7b0: collect letters; the word ends at the first non-letter ('-' goes to state
 * 4). A word the rules cannot take (out of nodes) or longer than 85 chars is spelled. */
short tp_st03_word(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur++;
    short st;

    if (ch == '-') {
        c->tp_state = 4;
        return 0;
    }
    if (!(tp_class(c, ch) & 0x200)) {
        c->text_cur--;
        st = tp_word(c, (unsigned char *)c->tok_start, (unsigned char *)c->text_cur);
        if ((st & 0xf0) == 0x40) {
            c->tp_state = 7;
            c->text_cur = c->tok_start;
            return 0;
        }
        c->tp_state = 0;
        return st;
    }
    if (c->text_cur - c->tok_start > 0x55) {
        c->text_cur = c->tok_start;
        c->tp_state = 7;
    }
    return 0;
}

/* 0x1001b880: states 4 and 5, the char after "word-". A letter: the first part is a word
 * of its own, then the "hy" token (state 5), and the next part starts. A line break:
 * the hyphen and the break are dropped (mode 0x10: the break becomes '\n' in the word).
 * Anything else ends the word, hyphen included. */
short tp_st04_hyphen(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur;
    short st;

    if (tp_class(c, ch) & 0x200) {
        if (c->tp_state == 5)
            goto hyphen;
        st = tp_word(c, (unsigned char *)c->tok_start, (unsigned char *)c->text_cur - 1);
        if (st != 0) {
            if ((st & 0xf0) == 0x40)
                goto spell;
            c->tp_state = 0;
            return st;
        }
        if (c->text_cur == c->tok_start) {
            c->tp_state = 0;
            return 0;
        }
        c->tp_state = 5;
        if (c->synth_state == 0xff)
            return 0;
    }
    if (c->tp_state == 5) {
hyphen:
        TP_COV(TPC_HYPHEN_WORD);
        st = tp_token_m1(c, tp_hy, tp_hy + 2);
        if ((st & 0xf0) == 0x40)
            goto spell;
        c->tp_state = 3;
        c->tok_start = c->text_cur;
        c->tok_start2 = c->text_cur;
        return st;
    }
    if (tp_class(c, ch) & 4) {
        TP_COV(TPC_HYPHEN_BREAK);
        c->text_end--;
        c->text_cur--;
        if (c->mode & 0x10) {
            *c->text_cur = '\n';
            c->tp_state = 3;
            return 0;
        }
        c->tp_state = 6;
        c->text_end--;
        return 0;
    }
    c->text_cur--;
    st = tp_word(c, (unsigned char *)c->tok_start, (unsigned char *)c->text_cur);
    if ((st & 0xf0) != 0x40) {
        c->tp_state = 0;
        return st;
    }
spell:
    c->tp_state = 7;
    c->text_cur = c->tok_start;
    return 0;
}

/* 0x1001ba40: drop blanks and line breaks after "word-<break>", then continue the word */
short tp_st06_skip_break(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur;

    if (!(tp_class(c, ch) & 4) && !(tp_class(c, ch) & 2)) {
        c->tp_state = 3;
        return 0;
    }
    c->text_end--;
    return 0;
}

/* 0x1001bad0: spell letters and hyphens one by one */
short tp_st07_spell(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur++;

    if (ch != c->esc_phon && ((tp_class(c, ch) & 0x200) || ch == '-')) {
        TP_COV(TPC_SPELL_WORD);
        return tp_speak_char(c, ch, 0);
    }
    c->tp_state = 0;
    c->text_cur--;
    return 0;
}

/* 0x1001bb50: phonetic input: symbols up to a blank, sentence punctuation, esc_phon or
 * an ignored char become segments directly. 8-bit chars are replaced by their 2-char
 * transliteration in the buffer. */
short tp_st01_phonetic(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur++;
    unsigned char x;
    char *p;
    short st;

    if (!(tp_class(c, ch) & 2) && !(tp_class(c, ch) & 0x10) && ch != c->esc_phon
        && !(tp_class(c, ch) & 1)) {
        if (ch & 0x80) {
            TP_COV(TPC_PHON_8BIT);
            c->text_cur--;
            c->text_end--;
            for (p = c->text_cur; p < c->text_end; p++)
                p[0] = p[1];
            if (c->charset & 1)
                ch = TP_CHARSET1[ch & 0x7f];
            else if (c->charset & 2)
                ch = TP_CHARSET2[ch & 0x7f];
            x = tp_xlat_char(c, ch);
            if (x != 0) {
                for (p = c->text_end; p > c->text_cur; p--)
                    p[0] = p[-1];
                *c->text_cur++ = (char)x;
                c->text_end++;
            }
            x = tp_xlat_char(c, ch & 0x7f);
            if (x != 0) {
                for (p = c->text_end; p > c->text_cur; p--)
                    p[0] = p[-1];
                *c->text_cur++ = (char)x;
                c->text_end++;
            }
        }
        if (c->text_cur - c->tok_start + 1 > 0x55) {
            TP_COV(TPC_PHON_LONG);
            c->text_cur = c->tok_start;
            c->tp_state = 2;
            return tp_speak_char(c, c->esc_phon, 0);
        }
        return 0;
    }

    if (!(c->mode & 8) || !(c->retry & 1)) {
        TP_COV(TPC_PHON_SEGS);
        st = tp_text_to_segs(c, 1, (unsigned char *)c->tok_start, (unsigned char *)c->text_cur - 1);
        if (st == 0)
            st = ctl_word_end(c);
    } else {
        st = 0;
    }
    if ((st & 0xf0) == 0x40) {
        c->tp_state = 2;
        c->text_cur = c->tok_start;
        return 0;
    }
    if (ch == c->esc_phon) {
        c->tp_state = 0;
        return st;
    }
    if (ch == c->esc_mark) {
        c->tp_state = 13;
        c->text_cur--;
        c->tok_start = c->text_cur;
        c->tok_start2 = c->text_cur;
        return st;
    }
    if (tp_class(c, ch) & 0x10)
        return tp_sentence_end(c, ch);
    if (!(tp_class(c, ch) & 2) && !(tp_class(c, ch) & 1)) {
        c->tp_state = 0;
        c->text_cur--;
        return st;
    }
    c->tok_start = c->text_cur;
    c->tok_start2 = c->text_cur;
    return st;
}

/* 0x1001beb0: phonetic input that could not be used: speak its chars up to esc_phon */
short tp_st02_phonetic_skip(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur++;

    if (ch == c->esc_phon) {
        c->tp_state = 0;
        return tp_speak_char(c, ch, 0);
    }
    if (tp_class(c, ch) & 0x10) {
        c->tp_state = 0;
        c->text_cur--;
        return 0;
    }
    if (!(tp_class(c, ch) & 1))
        return tp_speak_char(c, ch, 0);
    return 0;
}

/* 0x1001bf80: collect a non-word token (digits, symbols) up to a letter, a spoken
 * symbol, a blank, an ignored char, sentence punctuation or esc_phon; a number
 * separator goes to state 9. Out of nodes or longer than 85 chars: spell it. */
short tp_st08_token(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur++;
    short st;

    if (tp_class(c, ch) & 0x40) {
        c->tp_state = 9;
        return 0;
    }
    if (!(tp_class(c, ch) & 0x200) && !(tp_class(c, ch) & 0x100) && !(tp_class(c, ch) & 2)
        && !(tp_class(c, ch) & 1) && !(tp_class(c, ch) & 0x10) && ch != c->esc_phon) {
        if (c->text_cur - c->tok_start > 0x55) {
            c->text_cur = c->tok_start;
            c->tp_state = 10;
        }
        return 0;
    }
    c->text_cur--;
    st = tp_token_m1(c, (unsigned char *)c->tok_start, (unsigned char *)c->text_cur);
    if ((st & 0xf0) != 0x40) {
        c->tp_state = 0;
        return st;
    }
    c->tp_state = 10;
    c->text_cur = c->tok_start;
    return 0;
}

/* 0x1001c120: a separator continues the token only when a digit follows */
short tp_st09_num_sep(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur;
    short st;

    c->tok_start2 = c->text_cur;
    if (!(tp_class(c, ch) & 0x80)) {
        c->text_cur--;
        st = tp_token_m1(c, (unsigned char *)c->tok_start, (unsigned char *)c->text_cur);
        if ((st & 0xf0) != 0x40) {
            c->tp_state = 0;
            return st;
        }
        c->tp_state = 10;
        c->text_cur = c->tok_start;
        return 0;
    }
    TP_COV(TPC_NUM_SEP);
    c->text_cur++;
    c->tp_state = 8;
    if (c->text_cur - c->tok_start > 0x55) {
        c->text_cur = c->tok_start;
        c->tp_state = 10;
    }
    return 0;
}

/* 0x1001c1e0: spell a token char by char */
short tp_st0a_spell(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur++;

    if (tp_class(c, ch) & 0x40) {
        c->tp_state = 11;
        return 0;
    }
    if (!(tp_class(c, ch) & 0x200) && !(tp_class(c, ch) & 0x100) && !(tp_class(c, ch) & 2)
        && !(tp_class(c, ch) & 1) && !(tp_class(c, ch) & 0x10) && ch != c->esc_phon) {
        TP_COV(TPC_SPELL_TOKEN);
        return tp_speak_char(c, ch, 0);
    }
    c->tp_state = 0;
    c->text_cur--;
    return 0;
}

/* 0x1001c330: a separator inside a spelled token is spoken when a digit follows */
short tp_st0b_spell_sep(Core *c)
{
    unsigned char ch = (unsigned char)*c->text_cur++;
    short st;

    if (!(tp_class(c, ch) & 0x80)) {
        c->tp_state = 0;
        c->text_cur -= 2;
        return 0;
    }
    st = tp_speak_char(c, (unsigned char)c->text_cur[-2], 0);
    if (st == 0) {
        st = tp_speak_char(c, ch, 0);
        c->tp_state = 10;
    }
    return st;
}

/* 0x1001c3c0: states 12/13: esc_mark and 3 bytes b1 b2 b3 set the bookmark:
 * mark_lo = (mark_lo & 0xff00) | (signed)b1, mark_hi = b2 << 8 | (signed)b3 (the sign
 * extensions are the DLL's). Returns 1 on an inconsistent position. */
short tp_st0c_mark(Core *c)
{
    char *p = c->text_cur;
    int n;

    c->text_cur = p + 1;
    n = (int)(c->text_cur - c->tok_start);
    if (n < 0)
        return 1;
    if (n <= 3)
        return 0;
    if (n != 4)
        return 1;
    TP_COV(TPC_MARK);
    c->mark_lo &= 0xff00;
    c->mark_lo = (unsigned short)((short)(signed char)p[-2] | c->mark_lo);
    c->mark_hi = (unsigned short)((unsigned char)p[-1] << 8 | (short)(signed char)p[0]);
    if (c->tp_state == 13) {
        c->tok_start = c->text_cur;
        c->tok_start2 = c->text_cur;
        c->tp_state = 1;
        return 0;
    }
    c->tp_state = 0;
    return 0;
}

/* 0x1001c460 */
void tp_reset_text(Core *c)
{
    c->text_end = c->text;
    c->text_cur = c->text;
    c->tok_start = c->text;
    c->tok_start2 = c->text;
    c->text_done = c->text;
    c->text_limit = c->text + 0x2a0;
    c->mark_lo = 0;
    c->mark_hi = 0;
    c->retry = 0;
    c->f6df = 0;
    c->tp_state = 0;
    c->text_consumed = 0;
    c->pending = 0;
    c->count = (short)-c->count_init;
    seg_free_all(c);
    seg_free_held(c);
}
