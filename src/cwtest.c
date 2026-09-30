/* Pattern matching (cwtest.c, DLL 0x10020900-0x100216df).
 *
 * Match state: cursor = start position of the current attempt, focus = the segment the
 * rule acts on, next = the segment the next test looks at. A failed test either retries
 * the rule at the next start position (cw_retry) or, when the list is exhausted, moves
 * on to the next rule (cw_fail).
 *
 * Feature tests take two 32-bit big-endian word pairs {value, care}: a segment matches
 * when care & (feat ^ value) & mask == 0.
 */
#include "cw.h"

static int feat_miss(const Seg *s, const unsigned short *op)
{
    unsigned int v = (unsigned int)op[0] << 16 | op[1];
    unsigned int m = (unsigned int)op[2] << 16 | op[3];

    return (m & (s->feat ^ v) & s->mask) != 0;
}

/* 0x10020900: op 40 SEEK ph: search right for ph and make it the focus */
void cw_op_SEEK(Core *c)
{
    unsigned short v = *c->ip++;
    Seg *tail = SEGS_TAIL(c), *n;

    if (c->inserted) {
        n = c->focus->next;
        c->inserted = 0;
        c->next = n;
    }
    if (c->next != tail) {
        do {
            c->cursor = c->next;
            n = c->next->next;
            c->next = n;
            if ((unsigned short)c->cursor->ph == v) {
                c->focus = c->cursor;
                return;
            }
        } while (n != tail);
    }
    cw_fail(c);
}

/* 0x10020980: give up on this rule; RULE* restarts it from the head if it changed anything */
void cw_fail(Core *c)
{
    unsigned short *p;
    Seg *s;

    c->ip = c->rule_start;
    if (c->iter_mode && c->changed) {
        s = c->segs.head.next;
        c->changed = 0;
        c->cursor = s;
        c->next = s;
        c->focus = s;
        c->inserted = 0;
        return;
    }
    p = c->rule_start - 1;
    c->ip = p;
    c->ip = p + (short)*p / 2;
}

/* 0x100209e0: op 44 FOCUS ph */
void cw_op_FOCUS(Core *c)
{
    Seg *n = c->next;
    unsigned short v;

    if (n == SEGS_TAIL(c)) {
        cw_fail(c);
        return;
    }
    v = *c->ip++;
    if (v != (unsigned short)n->ph || c->inserted) {
        cw_retry(c);
        return;
    }
    c->focus = n;
    c->next = n->next;
}

/* 0x10020a50: op 48 SCAN ph: skip right to ph (left context scan) */
void cw_op_SCAN(Core *c)
{
    Seg *tail = SEGS_TAIL(c), *n;
    unsigned short v = *c->ip++;

    if (c->next != tail) {
        do {
            c->cursor = c->next;
            if (c->next == c->focus)
                c->inserted = 0;
            n = c->next->next;
            c->next = n;
            if ((unsigned short)c->cursor->ph == v)
                return;
        } while (n != tail);
    }
    cw_fail(c);
}

/* 0x10020ab0: op 4C R ph: right context */
void cw_op_R(Core *c)
{
    Seg *n = c->next;
    unsigned short v;

    if (n == SEGS_TAIL(c)) {
        cw_fail(c);
        return;
    }
    v = *c->ip++;
    if (v != (unsigned short)n->ph) {
        cw_retry(c);
        return;
    }
    if (n == c->focus)
        c->inserted = 0;
    c->next = n->next;
}

/* 0x10020b10: op 7C L ph: left context */
void cw_op_L(Core *c)
{
    Seg *n = c->next;
    unsigned short v;

    if (n == SEGS_HEAD(c)) {
        cw_retry(c);
        return;
    }
    v = *c->ip++;
    if (v != (unsigned short)n->ph) {
        cw_retry(c);
        return;
    }
    c->next = n->prev;
}

/* 0x10020b60: op 50 R? ph: optional right context */
void cw_op_R_opt(Core *c)
{
    Seg *n = c->next;
    unsigned short v;

    if (n == SEGS_TAIL(c)) {
        cw_fail(c);
        return;
    }
    v = *c->ip++;
    if (v == (unsigned short)n->ph) {
        if (n == c->focus)
            c->inserted = 0;
        c->next = n->next;
    }
}

/* 0x10020bb0: op 80 L? ph: optional left context */
void cw_op_L_opt(Core *c)
{
    Seg *n = c->next;
    unsigned short v;

    if (n == SEGS_HEAD(c)) {
        cw_retry(c);
        return;
    }
    v = *c->ip++;
    if (v == (unsigned short)n->ph)
        c->next = n->prev;
}

/* 0x10020bf0: op 54 SEEKF value,care */
void cw_op_SEEKF(Core *c)
{
    unsigned short *op = c->ip;
    Seg *tail = SEGS_TAIL(c), *s, *n;

    c->ip = op + 4;
    if (c->inserted) {
        n = c->focus->next;
        c->inserted = 0;
        c->next = n;
    }
    if (c->next != tail) {
        do {
            s = c->next;
            c->cursor = s;
            n = s->next;
            c->next = n;
            if (!feat_miss(s, op)) {
                c->focus = c->cursor;
                return;
            }
        } while (n != tail);
    }
    cw_fail(c);
}

/* 0x10020c90: op 58 FOCUSF value,care */
void cw_op_FOCUSF(Core *c)
{
    unsigned short *op = c->ip;
    Seg *n;

    c->ip = op + 4;
    n = c->next;
    if (n == SEGS_TAIL(c)) {
        cw_fail(c);
        return;
    }
    if (feat_miss(n, op) || c->inserted) {
        cw_retry(c);
        return;
    }
    c->focus = n;
    c->next = n->next;
}

/* 0x10020d20: op 5C SCANF value,care */
void cw_op_SCANF(Core *c)
{
    unsigned short *op = c->ip;
    Seg *n;

    c->ip = op + 4;
    n = c->next;
    do {
        if (n == SEGS_TAIL(c)) {
            cw_fail(c);
            return;
        }
        c->cursor = c->next;
        if (c->next == c->focus)
            c->inserted = 0;
        n = c->next->next;
        c->next = n;
    } while (feat_miss(c->cursor, op));
}

/* 0x10020da0: op 60 RF value,care */
void cw_op_RF(Core *c)
{
    unsigned short *op = c->ip;
    Seg *n;

    c->ip = op + 4;
    n = c->next;
    if (n == SEGS_TAIL(c)) {
        cw_fail(c);
        return;
    }
    if (feat_miss(n, op)) {
        cw_retry(c);
        return;
    }
    if (n == c->focus)
        c->inserted = 0;
    c->next = n->next;
}

/* 0x10020e20: op 90 LF value,care */
void cw_op_LF(Core *c)
{
    unsigned short *op = c->ip;
    Seg *n;

    c->ip = op + 4;
    n = c->next;
    if (n == SEGS_HEAD(c) || feat_miss(n, op)) {
        cw_retry(c);
        return;
    }
    c->next = n->prev;
}

/* 0x10020e90: op 64 RF? value,care */
void cw_op_RF_opt(Core *c)
{
    unsigned short *op = c->ip;
    Seg *n;

    c->ip = op + 4;
    n = c->next;
    if (n == SEGS_TAIL(c)) {
        cw_fail(c);
        return;
    }
    if (!feat_miss(n, op)) {
        if (n == c->focus)
            c->inserted = 0;
        c->next = n->next;
    }
}

/* 0x10020f00: op 94 LF? value,care */
void cw_op_LF_opt(Core *c)
{
    unsigned short *op = c->ip;
    Seg *n;

    c->ip = op + 4;
    n = c->next;
    if (n == SEGS_HEAD(c)) {
        cw_retry(c);
        return;
    }
    if (!feat_miss(n, op))
        c->next = n->prev;
}

/* skip a pattern: its header word carries the pattern length in bits 1..11 (words) */
static unsigned short *pattern_end(unsigned short *hdr)
{
    return hdr + ((*hdr >> 1) & 0x7ff);
}

/* 0x10020f70: op 68 FOCUSP pattern */
void cw_op_FOCUSP(Core *c)
{
    unsigned short *pat;

    if (c->next == SEGS_TAIL(c)) {
        cw_fail(c);
        return;
    }
    pat = c->ip;
    if (cw_match_pattern(c) || c->inserted) {
        cw_retry(c);
        return;
    }
    c->focus = c->next;
    c->next = c->next->next;
    c->ip = pattern_end(pat);
}

/* 0x10020ff0: op 6C RP {opt:8, min:8} pattern: min..min+opt segments to the right */
void cw_op_RP(Core *c)
{
    unsigned short *op = c->ip, *pat = op + 1;
    unsigned short k;

    c->ip = pat;
    for (k = ((unsigned char *)op)[1]; k-- != 0;) {
        if (c->next == SEGS_TAIL(c)) {
            cw_fail(c);
            return;
        }
        if (cw_match_pattern(c)) {
            cw_retry(c);
            return;
        }
        c->ip = pat;
        if (c->next == c->focus)
            c->inserted = 0;
        c->next = c->next->next;
    }
    for (k = ((unsigned char *)op)[0]; k-- != 0;) {
        if (c->next == SEGS_TAIL(c) || cw_match_pattern(c))
            break;
        c->ip = pat;
        if (c->next == c->focus)
            c->inserted = 0;
        c->next = c->next->next;
    }
    c->ip = pattern_end(pat);
}

/* 0x100210e0: op 9C LP {opt:8, min:8} pattern: the same to the left */
void cw_op_LP(Core *c)
{
    unsigned short *op = c->ip, *pat = op + 1;
    unsigned short k;

    c->ip = pat;
    for (k = ((unsigned char *)op)[1]; k-- != 0;) {
        if (c->next == SEGS_HEAD(c) || cw_match_pattern(c)) {
            cw_retry(c);
            return;
        }
        c->ip = pat;
        c->next = c->next->prev;
    }
    for (k = ((unsigned char *)op)[0]; k-- != 0;) {
        if (c->next == SEGS_HEAD(c) || cw_match_pattern(c))
            break;
        c->ip = pat;
        c->next = c->next->prev;
    }
    c->ip = pattern_end(pat);
}

/* 0x100211a0: test c->next against the pattern at c->ip; 0 = match.
 * Header: bit 15 phoneme word follows, bit 14 feature {value, care} follows,
 * bit 13 parameter tests follow up to the pattern end (see cw_ptest_a). */
short cw_match_pattern(Core *c)
{
    unsigned short *hdr = c->ip, *end, *pv;
    unsigned short h = *hdr, w, id;
    short v;

    c->ip = hdr + 1;
    if (h & 0x8000) {
        v = (short)*c->ip++;
        if (c->next->ph != v)
            return 1;
    }
    if (h & 0x4000) {
        unsigned short *op = c->ip;
        c->ip = op + 4;
        if (feat_miss(c->next, op))
            return 1;
    }
    if (!(h & 0x2000))
        return 0;
    end = pattern_end(hdr);
    while (c->ip < end) {
        w = *c->ip;
        id = w & 0xff00;
        pv = cw_ptest_b(c, id);
        if (pv == NULL)
            pv = cw_ptest_c(c, id);
        if (pv == NULL || ((unsigned char *)pv)[-2] == 0)
            return 1;
        if (id & 0x8000) {
            v = (short)*pv;
        } else {
            if (*c->ip & 0x80)
                pv = c->item;          /* last payload word instead of the first */
            if (*c->ip & 0x40)
                v = (short)*pv >> 8;   /* signed high byte */
            else
                v = *(unsigned char *)pv;
        }
        if (cw_ptest_a(c, v))
            return 1;
    }
    return 0;
}

/* 0x10021300: parameter test op (low 6 bits of the test word): 0..18 capture v into a
 * register, 0x18..0x3c compare the following word k with v. Returns 0 = pass. */
short cw_ptest_a(Core *c, short v)
{
    unsigned char op = *(unsigned char *)c->ip & 0x3f;
    short k;

    c->ip++;
    switch (op) {
    case 0:  c->R[0] = v; return 0;
    case 1:  c->V[0] = v; return 0;
    case 2:  c->V[1] = v; return 0;
    case 3:  c->V[2] = v; return 0;
    case 4:  c->V[3] = v; return 0;
    case 5:  c->V[4] = v; return 0;
    case 6:  c->R[1] = v; return 0;
    case 7:  c->V[5] = v; return 0;
    case 8:  c->V[6] = v; return 0;
    case 9:  c->V[7] = v; return 0;
    case 10: c->V[8] = v; return 0;
    case 11: c->V[9] = v; return 0;
    case 12: c->R[2] = v; return 0;
    case 18: c->R[3] = v; return 0;
    case 0x18: k = (short)*c->ip++; return !(k > v);
    case 0x1e: k = (short)*c->ip++; return !(k >= v);
    case 0x24: k = (short)*c->ip++; return !(k == v);
    case 0x2c: k = (short)*c->ip++; return !(k != v);
    case 0x34: k = (short)*c->ip++; return !(k <= v);
    case 0x3c: k = (short)*c->ip++; return !(k < v);
    default:
        c->status = CW_PTEST_OP;
        return 1;
    }
}

/* found item header p in a node whose storage ends at end and continues in cont:
 * set c->item to its last payload word and return its first payload word */
static unsigned short *item_found(Core *c, unsigned short *p, unsigned short *end, void *cont)
{
    unsigned short *pv = p + 1;
    short d;

    c->item = p + ((*(unsigned char *)p >> 1) & 0x7f);
    if (pv >= end)
        pv = BLK_DATA(cont);
    d = WDIFF(c->item, end);
    if (d >= 0)
        c->item = BLK_DATA(cont) + d;
    return pv;
}

/* 0x10021540: item id of c->next (items are sorted by descending signed id) */
unsigned short *cw_ptest_b(Core *c, unsigned short id)
{
    Seg *s = c->next;
    ItemBlk *b;
    unsigned short *p, *end, key;
    short d;

    if (s->items_len == 0)
        return NULL;
    p = s->items;
    end = s->items + s->items_len / 2;
    for (; p < end; p += ((*p >> 1) & 0x7f) + 1) {
        key = *p & 0xff00;
        if (key == id)
            return item_found(c, p, end, s->ext);
        if ((short)key < (short)id)
            return NULL;
    }
    for (b = s->ext; b != NULL; b = b->next) {
        d = WDIFF(p, end);
        p = b->data + d;
        end = b->data + b->len / 2;
        for (; p < end; p += ((*p >> 1) & 0x7f) + 1) {
            key = *p & 0xff00;
            if (key == id)
                return item_found(c, p, end, b->next);
            if ((short)key < (short)id)
                return NULL;
        }
    }
    return NULL;
}

/* 0x10021660: default item id from the phoneme record of c->next (ascending ids) */
unsigned short *cw_ptest_c(Core *c, unsigned short id)
{
    unsigned short *rec = c->phinv + c->next->ph / 2;
    unsigned short *end = rec + (short)rec[0] / 2, *p, key;

    if (rec[0] == 0xc)
        return NULL;
    for (p = rec + 6; p < end; p += ((*p >> 1) & 0x7f) + 1) {
        key = *p & 0xff00;
        if (key == id) {
            if ((unsigned char)*p == 0)
                return NULL;
            c->item = p + ((*p >> 1) & 0x7f);
            return p + 1;
        }
        if ((short)key > (short)id)
            return NULL;
    }
    return NULL;
}
