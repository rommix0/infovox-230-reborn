/* Segment rewriting (cwelem.c, DLL 0x100216e0-0x10021b2f). */
#include "cw.h"

/* 0x100216e0: op B0 */
void cw_op_FNEXT(Core *c)
{
    c->focus = c->focus->next;
}

/* 0x100216f0: op CC: continue matching left of the focus */
void cw_op_CPREV(Core *c)
{
    if (c->focus == c->segs.head.next) {
        cw_retry(c);
        return;
    }
    c->next = c->focus->prev;
}

/* 0x10021720: op D0 */
void cw_op_CHEAD(Core *c)
{
    c->next = c->segs.head.next;
}

/* 0x10021730: op D4 */
void cw_op_CTAIL(Core *c)
{
    c->next = c->segs.tail.prev;
}

/* splice the ItemBlks of c->next into the list after it (so they are freed with it),
 * then step past them */
static void splice_blocks(Core *c)
{
    Seg *b, *nb;

    for (b = (Seg *)c->next->ext; b != NULL; b = nb) {
        nb = b->next;
        b->prev = c->next;
        b->next = c->next->next;
        c->next->next = b;
        b->next->prev = b;
        c->next = b;
    }
    c->next = c->next->next;
}

/* 0x10021740: op B4 {n}: delete n segments from the focus on, then restart the rule */
void cw_op_UNWRAP(Core *c)
{
    unsigned char n = *(unsigned char *)c->ip;
    Seg *end, *before, *old;
    short k;

    c->ip++;
    if (n == 0) {
        c->status = CW_DEL_ZERO;
        return;
    }
    c->cursor = c->cursor->prev;
    c->next = c->focus;
    for (k = n; k != 0; k--)
        splice_blocks(c);
    end = c->next;
    before = c->focus->prev;
    c->next = end->prev;
    before->next = end;
    end->prev = before;
    c->focus->prev = (Seg *)&c->free_list.head;
    old = c->free_list.head.next;
    c->free_list.head.next = c->focus;
    old->prev = c->next;
    c->next->next = old;
    c->changed = 1;
    end = c->cursor->next;
    c->cursor = end;
    c->next = end;
    c->focus = end;
    c->ip = c->rule_start;
}

/* 0x10021800: op B8 */
void cw_op_REPL_next(Core *c)
{
    c->focus = c->focus->next;
    cw_op_REPL(c);
}

/* 0x10021820: op BC {del:8, 12*n:8} ph[n]: replace del segments from the focus by n new
 * ones (reusing the old nodes first). New segments take their feature bits from the
 * phoneme record and the bookmark of the last deleted segment. */
void cw_op_REPL(Core *c)
{
    unsigned short w = *c->ip;
    unsigned short add = w & 0xff, mark_lo = 0, mark_hi = 0;
    short del = (short)(w >> 8);
    Seg *s, *last, *old;
    unsigned short *rec;

    c->ip++;
    c->next = c->focus;
    c->cursor = c->cursor->prev;
    for (; del != 0; del--) {
        mark_lo = c->next->mark_lo;
        mark_hi = c->next->mark_hi;
        splice_blocks(c);
    }
    for (;;) {
        if (add == 0) {
            s = c->next;
            if (s != c->focus) {   /* free the unused rest */
                last = s->prev;
                c->focus->prev->next = s;
                c->next->prev = c->focus->prev;
                c->focus->prev = (Seg *)&c->free_list.head;
                old = c->free_list.head.next;
                c->free_list.head.next = c->focus;
                old->prev = last;
                last->next = old;
            }
            c->cursor = c->cursor->next;
            s = c->next->prev;
            c->focus = s;
            if (*c->ip == 0x10) {   /* followed by NEXT: continue after the new segments */
                c->changed = 1;
                c->focus = s->next;
                s = c->cursor->next;
                c->cursor = s;
                c->next = s;
                c->ip = c->rule_start;
            }
            return;
        }
        /* GUARD, not in the DLL: a count that is not a multiple of 12 (a REPL word whose
         * 12*n carried into the high byte: 23 phonemes, del 2 = 0x314 in darules.ivx M5,
         * the two quotation-mark names) wraps below 0; the DLL then reads the bytecode
         * after the rule as phoneme ids and their records far outside the .ivx data, and
         * crashes on them (0x10021942, Danish phonetic input "2 in text mode 12). Where the
         * reads survive, it fills the pool (status 0x40) and ctl_word_end restarts the
         * sentence word by word, failing the same way each time. Stop with a bad-opcode
         * status instead: the module ends and the word is dropped. */
        if (add < 12) {
            c->status = CW_BAD_OPCODE;
            return;
        }
        c->inserted = 1;
        if (c->focus == c->next) {   /* out of old nodes: insert a new one */
            s = seg_alloc(c);
            if (s == NULL) {
                c->status = CW_NO_NODES;
                return;
            }
            s->prev = c->focus->prev;
            s->next = c->focus;
            c->focus->prev = s;
            s->prev->next = s;
            c->focus = s;
        }
        add -= 12;
        s = c->focus;
        s->ext = NULL;
        s->items_len = 0;
        s->ph = (short)*c->ip++;
        s->mark_lo = mark_lo;
        s->mark_hi = mark_hi;
        rec = c->phinv + s->ph / 2;
        s->feat = (unsigned int)rec[2] << 16 | rec[3];
        s->mask = (unsigned int)rec[4] << 16 | rec[5];
        c->focus = s->next;
    }
}

/* 0x10021a00: op C0 */
void cw_op_SETF_next(Core *c)
{
    c->focus = c->focus->next;
    cw_op_SETF(c);
}

/* 0x10021a20: op C4 value,care: set features; the care bits become defined */
void cw_op_SETF(Core *c)
{
    unsigned short *op = c->ip;
    unsigned int v = (unsigned int)op[0] << 16 | op[1];
    unsigned int m = (unsigned int)op[2] << 16 | op[3];

    c->ip = op + 4;
    c->focus->feat = (~m & c->focus->feat) | v;
    c->focus->mask = (~v & c->focus->mask) | m;
    c->changed = 1;
}

/* 0x10021a80: op C8: find two equal adjacent segments with feature bits 0x10002 and mark
 * both with 0x4 (gemination) */
void cw_op_GEMIN(Core *c)
{
    Seg *tail = SEGS_TAIL(c), *s, *n;

    if (c->cursor == tail) {
        cw_fail(c);
        return;
    }
    if (c->next == tail) {
        cw_fail(c);
        return;
    }
    do {
        s = c->next;
        c->cursor = s;
        n = s->next;
        c->next = n;
        if (n == tail) {
            cw_fail(c);
            return;
        }
    } while (s->ph != n->ph || (~s->feat & 0x10002) != 0);
    c->cursor->feat |= 4;
    c->next->feat |= 4;
    c->cursor->mask |= 4;
    c->next->mask |= 4;
    c->focus = c->cursor;
    cw_op_NEXT(c);
}
