/* Interpreter entry and dispatch (contwork.c, DLL 0x1001fe90-0x100207ff), and
 * ctl_word_end (0x1001fef0), which finishes a word for the text side (tp.h). */
#include "tp.h"

#ifdef CW_COVERAGE
unsigned long cw_op_count[0x80];   /* executions per opcode/4, for the tests */
#endif

/* 0x1001fe90: run rule module idx (M0..M5 = c->mod[0..5]). M2 starts from V_init. */
short run_module(Core *c, short idx)
{
    short st;
    int i;

    if (idx == 2)
        for (i = 0; i < 10; i++)
            c->V[i] = c->V_init[i];
    st = cw_run(c, c->mod[idx]);
    if (st != 0)
        seg_free_all(c);
    return st;
}

/* 0x10048dcc: the word-end symbol */
static unsigned char word_end_sym[3] = "@.";

/* 0x1001fef0: a word's segments are ready in segs. Stamp them with the bookmark and the
 * word's text position (cb_wordpos), then either hold them until the sentence ends
 * (mode 8; seg_hold) or, in mode 4 or while restarting a sentence (retry bit 1), append
 * "@.", run M5 and M2 and start the utterance at once (in mode 8 on a copy of the held
 * words, so the sentence end still sees them). Out of nodes: drop the held words and
 * set retry bit 0. */
short ctl_word_end(Core *c)
{
    Seg *s = c->segs.head.next;
    short st;

    c->held_mark = NULL;
    c->word_pos = (short)(c->text_cur - c->tok_start - 1);
    if (c->cb_wordpos != NULL)
        c->cb_wordpos(c->cb_arg, c->word_pos);
    for (; s != SEGS_TAIL(c); s = s->next) {
        s->mark_lo = c->mark_lo;
        s->mark_hi = c->mark_hi;
        s->word_pos = c->word_pos;
    }
    if ((c->mode & 8) && c->retry == 0) {
        TP_COV(TPC_WORD_HOLD);
        seg_hold(c);
    }
    if (!(c->mode & 4) && !(c->retry & 2)) {
        seg_free_all(c);
        return 0;
    }
    TP_COV(TPC_WORD_NOW);
    if (c->held_mark != NULL && seg_copy_held(c) != 0) {
        TP_COV(TPC_WORD_RETRY);
        seg_unhold(c);
        seg_free_held(c);
        c->retry |= 1;
    }
    st = tp_text_to_segs(c, 0, word_end_sym, word_end_sym + 2);
    if (st != 0) {
        if (c->held.head.next == (Seg *)&c->held.tail) {
            seg_free_all(c);
            return st;
        }
        TP_COV(TPC_WORD_RETRY);
        seg_free_held(c);
        c->retry |= 1;
        if (tp_text_to_segs(c, 0, word_end_sym, word_end_sym + 2) != 0) {
            seg_free_all(c);
            return st;
        }
    }
    if (c->mod[5] != NULL) {
        st = run_module(c, 5);
        if (st != 0) {
            if ((st & 0xf0) != 0x40)
                return st;
            seg_free_segs_held(c);
            c->retry |= 2;
            c->text_cur = c->text;
            return 0;
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
        if (c->held_mark != NULL) {
            TP_COV(TPC_WORD_RETRY);
            seg_unhold(c);
            c->retry |= 1;
            seg_free_held(c);
            st = run_module(c, 2);
        }
        if (st != 0)
            return st;
    }
    if (!(c->mode & 8)) {
        c->text_consumed = 1;
        c->text_done = c->text_cur;
    }
    c->segs.tail.prev->mark_lo = c->mark_lo;
    c->segs.tail.prev->mark_hi = c->mark_hi;
    return op_start_utterance(c);
}

/* 0x100201b0: code[0] is the module header; the first rule's length word is code[2]. */
short cw_run(Core *c, unsigned short *code)
{
    int n = 0;
#ifdef CW_STEP_LIMIT
    unsigned long steps = 0;   /* test builds only: report a runaway module instead of hanging */
#endif

    c->mod_base = code;
    c->ip = code + 1;
    c->rule_start = code + 3;
    c->qidx[0] = 0;
    c->qidx[2] = 0;
    c->qidx[1] = 0;
    c->qidx[3] = 0;
    c->status = 0;
    if (c->hook != NULL)
        n = c->hook_interval;
    do {
#ifdef CW_STEP_LIMIT
        if (++steps > CW_STEP_LIMIT) {
            c->status = 0x7fff;
            break;
        }
#endif
        cw_step(c);
        if (n != 0 && --n == 0) {
            c->hook(c->cb_arg);
            n = c->hook_interval;
        }
    } while (c->status == 0);
    if (c->status == CW_DONE)
        c->status = 0;
    return c->status;
}

/* 0x10020240: opcodes are multiples of 4; op 0 is a no-op, anything unlisted is invalid */
void cw_step(Core *c)
{
    short op = (short)*c->ip++;

#ifdef CW_COVERAGE
    cw_op_count[((unsigned short)op >> 2) & 0x7f]++;
#endif
    switch (op) {
    case 0x000: return;
    case 0x004: cw_op_BEGIN(c); return;
    case 0x008: cw_op_RULE(c); return;
    case 0x00c: cw_op_RULE_iter(c); return;
    case 0x010: cw_op_NEXT(c); return;
    case 0x040: cw_op_SEEK(c); return;
    case 0x044: cw_op_FOCUS(c); return;
    case 0x048: cw_op_SCAN(c); return;
    case 0x04c: cw_op_R(c); return;
    case 0x050: cw_op_R_opt(c); return;
    case 0x054: cw_op_SEEKF(c); return;
    case 0x058: cw_op_FOCUSF(c); return;
    case 0x05c: cw_op_SCANF(c); return;
    case 0x060: cw_op_RF(c); return;
    case 0x064: cw_op_RF_opt(c); return;
    case 0x068: cw_op_FOCUSP(c); return;
    case 0x06c: cw_op_RP(c); return;
    case 0x07c: cw_op_L(c); return;
    case 0x080: cw_op_L_opt(c); return;
    case 0x090: cw_op_LF(c); return;
    case 0x094: cw_op_LF_opt(c); return;
    case 0x09c: cw_op_LP(c); return;
    case 0x0b0: cw_op_FNEXT(c); return;
    case 0x0b4: cw_op_UNWRAP(c); return;
    case 0x0b8: cw_op_REPL_next(c); return;
    case 0x0bc: cw_op_REPL(c); return;
    case 0x0c0: cw_op_SETF_next(c); return;
    case 0x0c4: cw_op_SETF(c); return;
    case 0x0c8: cw_op_GEMIN(c); return;
    case 0x0cc: cw_op_CPREV(c); return;
    case 0x0d0: cw_op_CHEAD(c); return;
    case 0x0d4: cw_op_CTAIL(c); return;
    case 0x100: cw_op_LD(c); return;
    case 0x104: cw_op_ST_s(c); return;
    case 0x108: cw_op_ST_u(c); return;
    case 0x10c: cw_op_ST(c); return;
    case 0x150: cw_op_A_set(c); return;
    case 0x154: cw_op_A_set_B(c); return;
    case 0x158: cw_op_A_negB(c); return;
    case 0x15c: cw_op_A_add(c); return;
    case 0x160: cw_op_A_add_B(c); return;
    case 0x164: cw_op_A_sub(c); return;
    case 0x168: cw_op_A_sub_B(c); return;
    case 0x16c: cw_op_A_abs(c); return;
    case 0x170: cw_op_A_mul(c); return;
    case 0x174: cw_op_A_mul_B(c); return;
    case 0x178: cw_op_A_div(c); return;
    case 0x17c: cw_op_A_div_B(c); return;
    case 0x180: cw_op_A_div2(c); return;
    case 0x184: cw_op_A_div2_B(c); return;
    case 0x188: cw_op_A_mulfrac(c); return;
    case 0x18c: cw_op_A_shr(c); return;
    case 0x190: cw_op_A_shr2(c); return;
    case 0x194: cw_op_A_shl(c); return;
    case 0x198: cw_op_PUSHQ1(c); return;
    case 0x19c: cw_op_PUSHQ2(c); return;
    case 0x1a0: cw_op_POPQ1(c); return;
    case 0x1a4: cw_op_POPQ2(c); return;
    case 0x1a8: cw_op_B_R0(c); return;
    case 0x1ac: cw_op_B_R1(c); return;
    case 0x1b0: cw_op_B_R2(c); return;
    case 0x1b4: cw_op_B_R3(c); return;
    case 0x1b8: cw_op_R0_A(c); return;
    case 0x1bc: cw_op_R1_A(c); return;
    case 0x1c0: cw_op_R2_A(c); return;
    case 0x1c4: cw_op_R3_A(c); return;
    case 0x1c8: cw_op_B_V(c); return;
    case 0x1cc: cw_op_V_A(c); return;
    case 0x1f0: case 0x1f4: case 0x1f8: cw_op_skip1(c); return;
    default: cw_op_invalid(c); return;
    }
}
