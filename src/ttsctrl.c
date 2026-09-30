/* Control interface (ttsctrl.c, DLL 0x10019f50-0x1001b04f): tts_ctrl and its commands,
 * the .ivx byte swap and bind, the class-flag table, and the two character helpers the
 * text side calls. */
#include <stdint.h>
#include <string.h>
#include "tts.h"

/* 0x10019f50: tail-dispatch through the table at 0x10048460 (commands 0x00..0x21). The
 * plain setters and getters are inlined here; each case names its DLL function. */
int tts_ctrl(void *h, int cmd, void *arg)
{
    Core *c = h;
    int a = (int)(intptr_t)arg;

    switch (cmd) {
    case TTS_CREATE:        return tts_create((Core **)h, (short)a);
    case TTS_DESTROY:       return tts_c01_destroy(c);
    case TTS_RESET:         return tts_c02_reset(c, a);
    case TTS_BIND:          return tts_c03_bind(c, arg);
    case TTS_CHARSET:       return tts_c04_charset(c, a);
    case TTS_MODE:          return tts_c05_mode(c, a);
    case TTS_SET_VOICE:     return tts_c06_set_voice(c, a);
    case TTS_STORE_VOICE:   return tts_c07_store_voice(c, a);
    case TTS_FD:            return tts_c08_fd(c, a);
    case TTS_RATE:          return tts_c09_rate(c, a);
    case TTS_PITCH:         return tts_c0a_pitch(c, a);
    case TTS_NOP_0B:
    case TTS_NOP_0C:        return tts_c0b_nop();
    case TTS_VAR_INIT:      return tts_c0d_var_init(c, a);
    case TTS_ESC_MARK:      return tts_c0e_esc_mark(c, a);
    case TTS_ESC_PHON:      return tts_c0f_esc_phon(c, a);
    case TTS_OUT_FLAGS:     return tts_c10_out_flags(c, a);
    case TTS_CB_PHONEMES:   c->cb_phonemes = (void (*)(void *, char *))arg; return 0;           /* 0x1001a6e0 */
    case TTS_P6EC:          c->p6ec = arg; return 0;                                             /* 0x1001a700 */
    case TTS_CB_STATUS:     c->cb_status = (void (*)(void *, int))arg; return 0;                 /* 0x1001a720 */
    case TTS_CB_USERDICT:   c->cb_userdict = (int (*)(void *, char *, char **))arg; return 0;    /* 0x1001a740 */
    case TTS_CB_EXCDICT:    c->cb_excdict = (char *(*)(void *, char *, int))arg; return 0;       /* 0x1001a790 */
    case TTS_CB_ARG:        c->cb_arg = arg; return 0;                                           /* 0x1001a760 */
    case TTS_FRAMES_LEFT:   return tts_c17_frames_left(c);
    case TTS_HOOK:          c->hook = (void (*)(void *))arg; return 0;                           /* 0x1001a7b0 */
    case TTS_HOOK_INTERVAL: c->hook_interval = (short)a; return 0;                               /* 0x1001a7d0 */
    case TTS_CB_WORDPOS:    c->cb_wordpos = (void (*)(void *, int))arg; return 0;                /* 0x1001a7f0 */
    case TTS_WORD_POS:      return tts_c1b_word_pos(c, a);
    case TTS_CLEAR_COUNT:   return tts_c1c_clear_count(c);
    case TTS_OUT_MODE:      return (int)(intptr_t)tts_c1d_out_mode(c, (short)a);
    case TTS_FEAE:          c->feae = (short)a; return 0;                                        /* 0x1001a880 */
    case TTS_CB_BOOKMARK:   c->cb_bookmark = (void (*)(void *, int))arg; return 0;               /* 0x1001a8a0 */
    case TTS_MARK:          return tts_c20_mark(c, a);
    case TTS_IVX_SWAP:      return tts_c21_ivx_swap(h, arg);
    }
    return -1;   /* the DLL indexes its table without a bounds check */
}

/* 0x10019f70: a Core with npool pool nodes, reset, no language bound. The five stored
 * voices start as voice 0. A failed pool allocation leaks the Core, as in the DLL. */
int tts_create(Core **out, short npool)
{
    Core *c = mem_calloc(1, sizeof(Core));   /* 0xeb0 */
    int v, k;

    if (c == NULL)
        return -1;
    c->pool = mem_calloc(npool, NODE_SIZE);
    if (c->pool == NULL)
        return -1;
    c->pool_n = npool;
    c->ntotal = 0x20;
    c->ivx = NULL;
    c->ptab_A0 = NULL;
    c->phinv = NULL;
    c->p160 = NULL;
    c->class_lo = NULL;
    c->lexicon = NULL;
    for (k = 0; k < 8; k++)
        c->mod[k] = NULL;
    c->cb_excdict = NULL;
    c->cb_arg = NULL;
    c->cb_userdict = NULL;
    c->cb_wordpos = NULL;
    c->cb_bookmark = NULL;
    c->p6ec = NULL;
    c->cb_phonemes = NULL;
    c->cb_status = NULL;
    c->hook = NULL;
    c->hook_interval = 0;
    c->count_init = 0;
    c->feae = 0;
    for (k = 0; k < 10; k++)
        c->V_init[k] = 50;
    for (v = 0; v < 5; v++)
        for (k = 0; k < c->ntotal; k++)
            c->user_voice[v][k] = tts_voice_defaults[k];
    tts_c02_reset(c, 0);
    *out = c;
    return 0;
}

/* 0x1001a0c0 */
int tts_c01_destroy(Core *c)
{
    mem_free(c->pool);
    mem_free(c);
    return 0;
}

/* 0x1001a0f0: what 0 = everything (lists, text options, voice 0; note that it also drops
 * the language's transliteration table xlat_hi until the next bind), 2 = output
 * (parameter tables), 1 = text input. */
int tts_c02_reset(Core *c, int what)
{
    if (what == 0) {
        core_init_lists(c);
        c->out_flags = 0;
        c->f6de = 0;
        c->flush_on_input = 0;
        c->esc_phon = 2;
        c->f6d9 = 0x20;
        c->esc_mark = 1;
        c->xlat_hi = NULL;
        c->charset = 0;
        c->mode = 8;
        c->voice_no = 0;
        c->f6dc = 0;
        tts_c06_set_voice(c, 0);
    }
    if (what == 2 || what == 0) {
        c->fead = 0;
        if (c->ivx != NULL)
            ivx_load_param_tables(c);
    }
    if (what == 1 || what == 0) {
        c->f6e2 = 0;
        c->f09d = 0;
        tp_reset_text(c);
        c->word_pos = 0;
    }
    return 0;
}

/* 0x1001a1b0: bind a swapped body, then empty the text buffer and reload the parameter
 * tables (those of the previous language when the bind fails) */
int tts_c03_bind(Core *c, IvxBody *body)
{
    short r;

    if ((intptr_t)body == -1)
        return (int)(intptr_t)c->ivx;
    r = ivx_bind(c, body);
    tp_reset_text(c);
    if (c->ivx != NULL)
        ivx_load_param_tables(c);
    return r;
}

/* 0x1001a2a0: charset bits 0/1 select the national 7-bit table used for 8-bit chars
 * (tp_char_class). Bit 4 turns the chars with class 0x1000/0x2000 (the national letters
 * of 7-bit charsets: [\]{|} ...) into letters; without it they are punctuation
 * (0x1020) or symbols (0x2100). */
int tts_c04_charset(Core *c, int flags)
{
    int ch;
    unsigned short w;

    if (flags == -1)
        return (signed char)c->charset;
    if (!(flags & 4)) {
        for (ch = 0x20; ch <= 0x7e; ch++) {
            if (c->class_flags[ch] & 0x1000)
                c->class_flags[ch] = 0x1020;
            if (c->class_flags[ch] & 0x2000)
                c->class_flags[ch] = 0x2100;
        }
    } else {
        for (ch = 0x20; ch <= 0x7e; ch++) {
            w = c->class_flags[ch] & 0x3000;
            if (w == 0)
                continue;
            if (ch >= 0x61 && ch <= 0x7d)
                w |= 0x600;
            else if (ch >= 0x41 && ch <= 0x5d)
                w |= 0xa00;
            else
                w |= 0x200;
            c->class_flags[ch] = w;
        }
    }
    c->charset = (unsigned char)flags;
    return 0;
}

/* 0x1001a200: 0 means 8; 2 implies 1; any of 0x1f0 without 4 implies 8. 0x80 sets f6dc
 * (sentence symbol "@@."), 0x100 starts in phonetic input (tp_state 1). */
int tts_c05_mode(Core *c, int flags)
{
    if (flags == -1)
        return (short)c->mode;
    c->mode = (unsigned short)flags;
    if (c->mode == 0)
        c->mode = 8;
    if (c->mode & 2)
        c->mode |= 1;
    if ((c->mode & 0x1f0) != 0 && !(c->mode & 4))
        c->mode |= 8;
    c->f6dc = (flags & 0x80) ? 1 : 0;
    c->pending = 0;
    c->tp_state = (flags & 0x100) ? 1 : 0;
    return 0;
}

/* 0x1001a380: voice_next = voice 0..4 (built in) or 5..9 (stored); the UI values
 * (0..100) are derived back from its DR, FD and F0 */
int tts_c06_set_voice(Core *c, int voice)
{
    const short *src;
    short k, v;

    if (voice == -1)
        return c->voice_no;
    if (voice >= 0 && voice <= 4)
        src = tts_voice_defaults + voice * 29;
    else if (voice >= 5 && voice <= 9)
        src = c->user_voice[voice - 5];
    else
        return -1;
    c->voice_no = (char)voice;
    for (k = 0; k < c->ntotal; k++)
        c->voice_next[k] = src[k];
    v = c->voice_next[0];
    c->ui_rate = (short)(v < 10 ? v * 5 : v * 2 + 30);
    c->voice_dirty = 1;
    c->ui_fd = (short)((c->voice_next[4] * 2 + 300) / 6);
    c->ui_pitch = (short)(c->voice_next[25] * 2 / 5);
    return 0;
}

/* 0x1001a480: store voice_next as voice 5..9 */
int tts_c07_store_voice(Core *c, int voice)
{
    short k;

    if (voice < 5 || voice > 9)
        return -1;
    for (k = 0; k < c->ntotal; k++)
        c->user_voice[voice - 5][k] = c->voice_next[k];
    c->voice_no = (char)voice;
    return 0;
}

/* 0x1001a4e0: 0..100 (50 = neutral) -> voice FD = 3 * (v - 50) */
int tts_c08_fd(Core *c, int v)
{
    if (v == -1)
        return c->ui_fd;
    c->ui_fd = tts_ui_adjust(c->ui_fd, (short)v);
    c->voice_dirty = 1;
    c->voice_next[4] = (short)((c->ui_fd - 50) * 6 / 2);
    return c->ui_fd;
}

/* 0x1001a540: 0..100 -> voice DR: v/5 below 50, else 10 + (v - 50)/2 */
int tts_c09_rate(Core *c, int v)
{
    if (v == -1)
        return c->ui_rate;
    c->ui_rate = tts_ui_adjust(c->ui_rate, (short)v);
    c->voice_dirty = 1;
    if (c->ui_rate < 50)
        c->voice_next[0] = (short)(c->ui_rate / 5);
    else
        c->voice_next[0] = (short)((c->ui_rate - 50) / 2 + 10);
    return c->ui_rate;
}

/* 0x1001a5c0: 0..100 -> voice F0 = 5v/2 (a scale, 64 = unity) */
int tts_c0a_pitch(Core *c, int v)
{
    if (v == -1)
        return c->ui_pitch;
    c->ui_pitch = tts_ui_adjust(c->ui_pitch, (short)v);
    c->voice_dirty = 1;
    c->voice_next[25] = (short)(c->ui_pitch * 5 / 2);
    return c->ui_pitch;
}

/* 0x1001a620: commands 0x0b and 0x0c */
int tts_c0b_nop(void)
{
    return 0;
}

/* 0x1001a630: arg = index << 16 | value (no range check) */
int tts_c0d_var_init(Core *c, int arg)
{
    c->V_init[(short)(arg >> 16)] = (short)arg;
    return 0;
}

/* 0x1001a650: DEL and backspace disable the bookmark escape */
int tts_c0e_esc_mark(Core *c, int ch)
{
    if (ch == -1)
        return (signed char)c->esc_mark;
    if (ch == 0x7f || ch == 8)
        ch = 0;
    c->esc_mark = (unsigned char)ch;
    return 0;
}

/* 0x1001a680 */
int tts_c0f_esc_phon(Core *c, int ch)
{
    if (ch == -1)
        return (signed char)c->esc_phon;
    c->esc_phon = (unsigned char)ch;
    return 0;
}

/* 0x1001a6b0 */
int tts_c10_out_flags(Core *c, int flags)
{
    if (flags == -1)
        return (signed char)c->out_flags;
    c->out_flags = (unsigned char)flags;
    return 0;
}

/* 0x1001a780 */
int tts_c17_frames_left(Core *c)
{
    return c->frames_left;
}

/* 0x1001a810 */
int tts_c1b_word_pos(Core *c, int v)
{
    if (v == -1)
        return c->word_pos;
    c->word_pos = (short)v;
    return 0;
}

/* 0x1001a840 */
int tts_c1c_clear_count(Core *c)
{
    c->count_init = 0;
    c->count = (short)-c->count_init;
    return 0;
}

/* 0x1001a860: returns the parameter description block {-1 - nparams, "  ", names} */
short *tts_c1d_out_mode(Core *c, short mode)
{
    c->out_mode = mode;
    return &c->pdesc_n;
}

/* 0x1001a8c0: the bookmark of the following text: mark + 1 split into mark_hi (low word)
 * and the low byte of mark_lo (bits 16..23); the high byte of mark_lo is kept */
int tts_c20_mark(Core *c, int mark)
{
    mark++;
    c->mark_hi = (unsigned short)mark;
    c->mark_lo = (unsigned short)((c->mark_lo & 0xff00) | ((mark >> 16) & 0xff));
    return 0;
}

/* 0x1001a8f0: convert a big-endian body in place: the directory as dwords, the three
 * words at 0x6a, the parameter tables A1..A3 (A0 holds names), every phoneme record
 * after its length word, and the rule sections H5, H6, H9..H12 as words. The character
 * tables and the lexicon are bytes; only the per-first-char lexicon index of files
 * without a lexicon (H[13] + 0x80) is converted. */
int tts_c21_ivx_swap(void *h, IvxBody *body)
{
    unsigned char *b = (unsigned char *)body;
    unsigned short *p;
    short n, i, end, len;

    (void)h;
    swap_halves32((unsigned int *)b, 0x40);
    swap16((unsigned short *)b, 0x40);
    swap16((unsigned short *)(b + 0x6a), 6);
    n = (short)(((int)body->H[8] - 0x70) / 8);
    swap16((unsigned short *)(b + 0x70 + n * 2), n * 6);
    for (p = (unsigned short *)(b + body->H[8]); *p != 0; p += len / 2) {
        swap16(p, 2);
        len = (short)*p;
        swap16(p + 1, len - 2);
    }
    for (i = 5; i <= 12; i++) {
        if (i == 7 || i == 8 || body->H[i] == 0)
            continue;
        if (i == 6)
            end = 9;
        else if (i == 12)
            end = body->H[14] != 0 ? 14 : 13;
        else
            end = (short)(i + 1);
        swap16((unsigned short *)(b + body->H[i]), (int)(body->H[end] - body->H[i]));
    }
    if (body->H[3] == 0)
        swap16((unsigned short *)(b + body->H[13] + 0x80), 0x100);
    return 0;
}

/* 0x1001aa00: -2/-3 = +-1, -4/-5 = +-2, -6/-7 = +-5, else arg; clamped to 0..100 */
short tts_ui_adjust(short cur, short arg)
{
    short v = arg;

    if (arg == -6)
        v = (short)(cur + 5);
    else if (arg == -7)
        v = (short)(cur - 5);
    else if (arg == -4)
        v = (short)(cur + 2);
    else if (arg == -5)
        v = (short)(cur - 2);
    else if (arg == -2)
        v = (short)(cur + 1);
    else if (arg == -3)
        v = (short)(cur - 1);
    if (v < 0)
        return 0;
    if (v > 100)
        return 100;
    return v;
}

/* 0x1001aa70: empty segs, held and out (head.prev and tail.next point at themselves);
 * chain the whole pool into free_list */
void core_init_lists(Core *c)
{
    Seg *s;
    short i;

    c->segs.tail.prev = SEGS_HEAD(c);
    c->segs.head.prev = SEGS_HEAD(c);
    c->segs.head.next = SEGS_TAIL(c);
    c->segs.tail.next = SEGS_TAIL(c);
    c->held.head.next = (Seg *)&c->held.tail;
    c->held.tail.next = (Seg *)&c->held.tail;
    c->held.tail.prev = (Seg *)&c->held.head;
    c->held.head.prev = (Seg *)&c->held.head;
    c->free_list.head.prev = (Seg *)&c->free_list.head;
    c->free_list.tail.prev = c->pool + c->pool_n - 1;
    c->out.head.next = OUT_TAIL(c);
    c->out.tail.prev = OUT_HEAD(c);
    c->out.head.prev = OUT_HEAD(c);
    c->free_list.head.next = c->pool;
    c->free_list.tail.next = (Seg *)&c->free_list.tail;
    c->out.tail.next = OUT_TAIL(c);
    for (i = 0, s = c->pool; i < c->pool_n; i++, s++) {
        s->next = s + 1;
        s->prev = s - 1;
        s->ext = NULL;
    }
    c->free_list.head.next->prev = (Seg *)&c->free_list.head;
    c->free_list.tail.prev->next = (Seg *)&c->free_list.tail;
}

/* 0x1001ab40: point the Core at a swapped body (REFERENCE §5.2). A "XX" build tag is
 * refused. Clears the exception dictionary callback. Parameter names: A0's own when its
 * second entry is two printable chars (no shipped file), else the built-in F1..ZZ. */
short ivx_bind(Core *c, IvxBody *body)
{
    unsigned char *b = (unsigned char *)body;
    unsigned short w;
    char *d;
    short n;
    int k;

    if (body == NULL || (body->tag[0] == 'X' && body->tag[1] == 'X'))
        return -1;
    if (c->p160 != NULL) {
        mem_free(c->p160);
        c->p160 = NULL;
    }
    c->cb_excdict = NULL;
    c->ivx = body;
    c->nparams = (short)(body->nparams2 / 2);
    c->ntotal = (short)(body->ntotal2 / 2);
    c->ptab_A0 = (short *)(b + 0x70);
    n = (short)(((int)body->H[8] - 0x70) / 8);
    c->pdesc_n = (short)(-1 - c->nparams);
    c->pdesc_pad = 0x2020;
    d = c->pnames;
    w = (unsigned short)c->ptab_A0[1];
    if ((w & 0xff) > 0x20 && (w & 0xff00) > 0x2000) {
        for (k = c->nparams - 1; k != 0; k--, d += 2)
            strncpy(d, (const char *)(c->ptab_A0 + k), 2);
    } else {
        for (k = 0; k < 24; k++, d += 2)
            strncpy(d, tts_param_names[5 + k], 2);
    }
    c->ptab_A1 = c->ptab_A0 + n;
    c->ptab_A2 = c->ptab_A0 + 2 * n;
    c->ptab_A3 = c->ptab_A0 + 3 * n;
    c->phinv = (unsigned short *)(b + body->H[8]);
    for (k = 0; k < 8; k++)
        c->mod[k] = NULL;
    c->mod[0] = (unsigned short *)(b + body->H[9]);
    c->mod[1] = (unsigned short *)(b + body->H[10]);
    c->mod[2] = (unsigned short *)(b + body->H[12]);
    if (body->H[6] != 0)
        c->mod[4] = (unsigned short *)(b + body->H[6]);
    if (body->H[5] != 0)
        c->mod[3] = (unsigned short *)(b + body->H[5]);
    if (body->H[11] != 0)
        c->mod[5] = (unsigned short *)(b + body->H[11]);
    if (body->H[14] != 0) {
        c->xlat_hi = b + body->H[14];
        c->class_lo = b + body->H[14];
    } else {
        c->xlat_hi = NULL;
        c->class_lo = b + body->H[13];
    }
    if (body->H[3] != 0) {
        c->lex_index = NULL;
        c->lexicon = b + body->H[3];
    } else {
        /* no lexicon: H[13] + 0x80 is a per-first-char index into the character-name
         * table at H[13] + 0x180 */
        c->lex_index = (unsigned short *)(b + body->H[13] + 0x80);
        c->lexicon = b + body->H[13] + 0x180;
    }
    ivx_build_class_flags(c);
    return 0;
}

/* 0x1001ada0: class codes (signed bytes) -> class flags. 0x00..0x7f come from H[13]
 * (= H[14] + 0x180), 0x80..0xff from H[14] + 0x100, or from the built-in table for files
 * without H[14]. Codes -6..5 select a flag word (jump table 0x1001aeb4); any other code
 * is a letter, with ASCII case by position. Then TAB and SP are blanks, '.' and 0x17
 * (end of text) sentence ends, LF, FF and CR line breaks. */
void ivx_build_class_flags(Core *c)
{
    static const unsigned short code_flags[12] = {
        0x008, 0x001, 0x020, 0x040, 0x010, 0x080, 0x100, 0x200, 0x600, 0xa00, 0x1020, 0x2100
    };
    const unsigned char *p = c->class_lo;
    signed char code;
    int ch;

    if (c->xlat_hi != NULL)
        p += 0x180;
    for (ch = 0; ch < 0x100; ch++) {
        if (ch == 0x80)
            p = c->xlat_hi != NULL ? c->class_lo + 0x100 : TP_CLASS80;
        code = (signed char)*p++;
        if (code >= -6 && code <= 5)
            c->class_flags[ch] = code_flags[code + 6];
        else if (ch >= 0x61 && ch <= 0x7d)
            c->class_flags[ch] = 0x600;
        else if (ch >= 0x41 && ch <= 0x5d)
            c->class_flags[ch] = 0xa00;
        else
            c->class_flags[ch] = 0x200;
    }
    c->class_flags['\t'] |= 2;
    c->class_flags[' '] |= 2;
    c->class_flags['.'] |= 0x10;
    c->class_flags[0x17] |= 0x10;
    c->class_flags['\n'] |= 4;
    c->class_flags['\r'] |= 4;
    c->class_flags['\f'] |= 4;
}

/* 0x1001aef0: one char of the 2-char transliteration of an 8-bit char: ch with bit 7 set
 * gives the first char of ch, ch without it the second char of ch|0x80. The language's
 * table (Core.xlat_hi, H[14]) wins; else built-in tables chosen by the .ivx tag ("SP"/"IT",
 * "FR" patches four entries). 0 = no char. */
unsigned char tp_xlat_char(Core *c, unsigned char ch)
{
    unsigned short i;
    short tag;

    if (ch & 0x80)
        i = (unsigned short)((ch & 0x7f) << 1);
    else
        i = (unsigned short)(ch * 2 + 1);
    if (c->xlat_hi != NULL)
        return c->xlat_hi[i];
    if (i < 0x60) {
        tag = *(short *)c->ivx->tag;
        if (tag == 0x5053 || tag == 0x5449)       /* "SP", "IT" */
            return TP_XLAT[0x80 + i];
        if (tag == 0x5246) {                      /* "FR" */
            if (i == 2 || i == 0x34)
                return ':';
            if (i == 3)
                return 'u';
            if (i == 0x35)
                return 'U';
        }
        return TP_XLAT[0x20 + i];
    }
    if (i == 0xc2)
        return '~';
    if (i >= 0xe0)
        return TP_XLAT[i];
    return 0;
}

/* 0x1001afa0: class flags of a char >= 0x80, through the national charset table */
unsigned short tp_char_class(Core *c, unsigned int ch)
{
    if (c->charset & 1)
        ch = TP_CHARSET1[ch & 0x7f];
    else if (c->charset & 2)
        ch = TP_CHARSET2[ch & 0x7f];
    return c->class_flags[ch & 0xff];
}

/* 0x1001aff0: swap the bytes of each word; n bytes, rounded up to even */
void swap16(unsigned short *p, int n)
{
    unsigned int k;

    for (k = (unsigned int)(n + 1) & ~1u; k != 0; k -= 2, p++)
        *p = (unsigned short)(*p >> 8 | *p << 8);
}

/* 0x1001b020: swap the 16-bit halves of each dword; n bytes, rounded up to 4 */
void swap_halves32(unsigned int *p, int n)
{
    unsigned int k;

    for (k = (unsigned int)(n + 3) & ~3u; k != 0; k -= 4, p++)
        *p = *p >> 16 | *p << 16;
}
