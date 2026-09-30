/* Segment lists and symbols (tpelem.c, DLL 0x1001e890-0x1001eeef).
 *
 * All segments and their overflow ItemBlks are 0x38-byte nodes from one pool
 * (Core.pool); free nodes sit on Core.free_list. The lists are doubly linked with head
 * and tail sentinel anchors inside the Core (SegList). Four lists are in use:
 * free_list, segs (the rules run on it), held (words held until the sentence ends) and
 * out (the utterance being turned into frames).
 */
#include "cw.h"

/* 0x10045334: the word-boundary symbol appended by tp_text_to_segs1 */
static const unsigned char tp_bound_sym[2] = {' ', 0};

/* 0x1001e890: write the 2-char names of first..end (exclusive) to buf, at most n-1
 * chars plus NUL; the loop stops while fewer than 3 bytes are left. With buf == NULL
 * (n forced to -1) or n < 0 each name goes to cb_phonemes instead, one call per
 * segment, through a small stack buffer. */
void op_emit_phonemes(Core *c, Seg *first, Seg *end, char *buf, int n)
{
    char tmp[4];
    char *p = buf;
    short left = (short)n;
    unsigned char ch;

    if (buf == NULL)
        left = -1;
    while (first != end && (left > 2 || left < 0)) {
        const unsigned char *name;

        if (left < 0)
            p = tmp;
        name = (const unsigned char *)(c->phinv + first->ph / 2 + 1);
        ch = name[1];
        if (ch != 0) {
            *p++ = (char)ch;
            left--;
        }
        ch = name[0];
        if (ch != 0) {
            *p++ = (char)ch;
            left--;
        }
        first = first->next;
        *p = 0;
        if (left < 0 && c->cb_phonemes != NULL)
            c->cb_phonemes(c->cb_arg, tmp);
    }
}

/* 0x1001e930 */
void seg_free_segs_held(Core *c)
{
    seg_free_range(c, c->segs.head.next, SEGS_TAIL(c));
    seg_free_range(c, c->held.head.next, (Seg *)&c->held.tail);
}

/* 0x1001e970 */
void seg_free_all(Core *c)
{
    seg_free_range(c, c->segs.head.next, SEGS_TAIL(c));
}

/* 0x1001e990 */
void seg_free_held(Core *c)
{
    seg_free_range(c, c->held.head.next, (Seg *)&c->held.tail);
}

/* 0x1001e9b0: return first..end (exclusive) and all their ItemBlks to the free list.
 * The blocks are first spliced into the list after their segment so the whole run can be
 * moved in one step; the segments keep their (now stale) ext pointers. */
void seg_free_range(Core *c, Seg *first, Seg *end)
{
    Seg *n, *b, *nb, *before, *last, *old;

    if (first == end)
        return;
    n = first;
    do {
        for (b = (Seg *)n->ext; b != NULL; b = nb) {
            nb = b->next;
            b->prev = n;
            b->next = n->next;
            n->next->prev = b;
            n->next = b;
            n = b;
        }
        n = n->next;
    } while (n != end);

    first->prev->next = end;
    before = first->prev;
    first->prev = (Seg *)&c->free_list.head;
    last = end->prev;
    old = c->free_list.head.next;
    old->prev = last;
    last->next = old;
    c->free_list.head.next = first;
    end->prev = before;
}

/* 0x1001ea20: pop a node from the free list; its fields are left as they were */
Seg *seg_alloc(Core *c)
{
    Seg *n = c->free_list.head.next;

    if (n == (Seg *)&c->free_list.tail)
        return NULL;
    c->free_list.head.next = n->next;
    n->next->prev = (Seg *)&c->free_list.head;
    return n;
}

/* 0x1001ea50: move all of segs to the end of held; held_mark = the first moved node.
 * The stores are in the DLL's order (it matters when segs is empty). */
void seg_hold(Core *c)
{
    Seg *first = c->segs.head.next;

    c->held_mark = first;
    first->prev = c->held.tail.prev;
    c->segs.tail.prev->next = c->held.tail.prev->next;
    c->held.tail.prev->next = c->segs.head.next;
    c->held.tail.prev = c->segs.tail.prev;
    c->segs.tail.prev = SEGS_HEAD(c);
    c->segs.head.next = SEGS_TAIL(c);
}

/* 0x1001eaa0: undo the last seg_hold: move held_mark..end of held back to segs */
void seg_unhold(Core *c)
{
    Seg *m = c->held_mark;

    if (m == NULL)
        return;
    c->segs.head.next = m;
    c->segs.tail.prev = c->held.tail.prev;
    m = m->prev;
    c->held.tail.prev = m;
    m->next = (Seg *)&c->held.tail;
    c->segs.head.next->prev = SEGS_HEAD(c);
    c->segs.tail.prev->next = SEGS_TAIL(c);
    c->held_mark = NULL;
}

/* copy one ItemBlk's length and data into a fresh node */
static void blk_copy(ItemBlk *dst, const ItemBlk *src)
{
    int k;

    dst->len = src->len;
    for (k = 0; k < 23; k++)
        dst->data[k] = src->data[k];
}

/* 0x1001eb00: append copies of held_mark..end of held (with their ItemBlks) to segs.
 * Out of nodes: free segs and return 1 (nodes already popped for the failed copy are
 * lost, as in the DLL). Seg.dur is not copied. */
short seg_copy_held(Core *c)
{
    Seg *s = c->held_mark;
    Seg *n;
    ItemBlk *src, *b, *nb;
    int k;

    if (s == (Seg *)&c->held.tail)
        return 0;
    do {
        n = seg_alloc(c);
        if (n == NULL)
            goto fail;
        n->ext = NULL;
        n->items_len = s->items_len;
        n->ph = s->ph;
        n->mark_lo = s->mark_lo;
        n->mark_hi = s->mark_hi;
        n->feat = s->feat;
        n->mask = s->mask;
        n->word_pos = s->word_pos;
        for (k = 0; k < 12; k++)
            n->items[k] = s->items[k];
        src = s->ext;
        if (src != NULL) {
            b = (ItemBlk *)seg_alloc(c);
            if (b == NULL)
                goto fail;
            n->ext = b;
            b->next = NULL;
            b->prev = n;
            blk_copy(b, src);
            for (src = src->next; src != NULL; src = src->next) {
                nb = (ItemBlk *)seg_alloc(c);
                if (nb == NULL)
                    goto fail;
                b->next = nb;
                nb->next = NULL;
                nb->prev = b;
                blk_copy(nb, src);
                b = nb;
            }
        }
        n->next = SEGS_TAIL(c);
        n->prev = c->segs.tail.prev;
        n->prev->next = n;
        n->next->prev = n;
        s = s->next;
    } while (s != (Seg *)&c->held.tail);
    return 0;
fail:
    seg_free_all(c);
    return 1;
}

/* 0x1001ecb0: out of nodes with words held: set retry bit 0 and try once more (the
 * caller has freed nothing yet; tp_text_to_segs1 appends to segs); if that fails too,
 * free segs and held. Returns 0 or 0x40. */
short tp_text_to_segs(Core *c, char bound, unsigned char *from, unsigned char *to)
{
    short st = tp_text_to_segs1(c, bound, from, to);

    if (st != 0 && c->held.head.next != (Seg *)&c->held.tail) {
        c->retry |= 1;
        st = tp_text_to_segs1(c, bound, from, to);
        if (st != 0)
            seg_free_segs_held(c);
    }
    return st;
}

/* 0x1001ed20: append a segment per symbol of from..to, with a boundary symbol before
 * and after when bound != 0. 0x40 = out of nodes. */
short tp_text_to_segs1(Core *c, char bound, unsigned char *from, unsigned char *to)
{
    short n;

    if (bound != 0 && tp_symbol_to_seg(c, tp_bound_sym) < 0)
        return CW_NO_NODES;
    for (; from < to; from += n) {
        n = tp_symbol_to_seg(c, from);
        if (n < 0)
            return CW_NO_NODES;
    }
    if (bound != 0 && tp_symbol_to_seg(c, tp_bound_sym) < 0)
        return CW_NO_NODES;
    return 0;
}

/* 0x1001eda0: append a segment for the inventory symbol at p, trying the 2-char name
 * first, then the 1-char one. Returns the chars used (1 or 2; 1 also when p[0] is not
 * a symbol, which is skipped), or -1 when out of nodes. The new segment's items, dur and
 * stale fields are not initialised beyond what is set here. */
short tp_symbol_to_seg(Core *c, const unsigned char *p)
{
    PhRec *r;
    Seg *s;
    unsigned short name = (unsigned short)((signed char)p[0] * 256 + p[1]);

    r = ph_find(c, (short)name);
    if (r == NULL) {
        name = p[0];
        r = ph_find(c, (short)name);
        if (r == NULL)
            return 1;
    }
    s = seg_alloc(c);
    if (s == NULL)
        return -1;
    s->next = SEGS_TAIL(c);
    s->prev = c->segs.tail.prev;
    c->segs.tail.prev = s;
    s->prev->next = s;
    s->ext = NULL;
    s->items_len = 0;
    s->ph = (short)((((char *)r - (char *)c->phinv) >> 1) << 1);
    s->mark_lo = c->mark_lo;
    s->mark_hi = c->mark_hi;
    s->word_pos = c->word_pos;
    s->feat = (unsigned int)r->feat_hi << 16 | r->feat_lo;
    s->mask = (unsigned int)r->mask_hi << 16 | r->mask_lo;
    return (name & 0xff00) != 0 ? 2 : 1;
}

/* 0x1001ee90: inventory record with the given 2-char name (len 0 ends the chain) */
PhRec *ph_find(Core *c, short name)
{
    PhRec *r = (PhRec *)c->phinv;

    while (r->len != 0) {
        if ((short)r->name == name)
            return r;
        r = (PhRec *)((unsigned short *)r + r->len / 2);
    }
    return NULL;
}

/* 0x1001eed0 */
int seg_free_count(Core *c)
{
    Seg *s;
    int n = 0;

    for (s = c->free_list.head.next; s != (Seg *)&c->free_list.tail; s = s->next)
        n++;
    return n;
}
