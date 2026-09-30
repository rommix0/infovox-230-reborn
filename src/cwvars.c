/* Registers, variables and segment items (cwvars.c, DLL 0x10021f80-0x10022c9f).
 *
 * Items: each segment carries a list of {id:8, bytes:8} header words, each followed by
 * bytes/2 payload words, sorted by descending signed id. Storage is the Seg's 12 inline
 * words, continued in a chain of ItemBlk nodes (Seg.ext, ItemBlk.next); an item may
 * straddle two nodes. Ids >= 0x80 are single-word variables, lower ids are parameters
 * whose payload bytes are per-time-point values. Phoneme records (Core.phinv) hold the
 * defaults in the same format (ascending ids).
 *
 * The ST ops carry a spec word {tspec:8, id:8}: tspec 0 overwrites, > 0 inserts and
 * < 0 deletes a payload word; a second word {index:7, 0x80 high-byte flag} selects which.
 * These functions were ported from the disassembly (the decompiler mistyped the node
 * arithmetic); the node-boundary handling is reproduced exactly, including its limits.
 */
#include "cw.h"

/* 0x10021f80..0x10021ff0: ops 1A8..1C4 */
void cw_op_B_R0(Core *c) { c->B = c->R[0]; }
void cw_op_B_R1(Core *c) { c->B = c->R[1]; }
void cw_op_B_R2(Core *c) { c->B = c->R[2]; }
void cw_op_B_R3(Core *c) { c->B = c->R[3]; }
void cw_op_R0_A(Core *c) { c->R[0] = (short)c->A; }
void cw_op_R1_A(Core *c) { c->R[1] = (short)c->A; }
void cw_op_R2_A(Core *c) { c->R[2] = (short)c->A; }
void cw_op_R3_A(Core *c) { c->R[3] = (short)c->A; }

/* V[i] with the DLL's unchecked index (i comes from the rule bytecode) */
static short *var_slot(Core *c, short i)
{
    return (short *)((char *)c->V + 2 * (int)i);
}

/* 0x10022000: op 1C8 i: B = V[i] */
void cw_op_B_V(Core *c)
{
    short v = *var_slot(c, (short)*c->ip);

    c->ip++;
    c->B = v;
}

/* 0x10022020: op 1CC i: V[i] = A */
void cw_op_V_A(Core *c)
{
    *var_slot(c, (short)*c->ip) = (short)c->A;
    c->ip++;
}

/* index word {k:7 (bits 9..15), flags:8}: k selects a payload word; with bit 15 set,
 * k is taken relative to the last payload word */
static unsigned short *payload_word(Core *c, unsigned short *pv, unsigned short w)
{
    int k = (w >> 9) & 0x7f;

    if (w & 0x8000)
        return c->item + (k | ~0x3f) + 2;
    return pv + k;
}

/* 0x10022040: op 100 {id} [{k, flags}]: B = item id of the focus (or its default) */
void cw_op_LD(Core *c)
{
    unsigned short id = (unsigned short)(*(unsigned char *)c->ip << 8);
    unsigned short *p, *pv, *ixw;
    short d;

    c->ip++;
    c->next = c->focus;
    p = cw_find_item(c, c->focus, id);
    if (p != NULL) {
        if (*(unsigned char *)p == 0) {
            c->status = CW_EMPTY_ITEM;
            return;
        }
        pv = p + 1;
        if (id & 0x8000) {
            if (pv == NODE_END(c->item_seg))
                pv = BLK_DATA(c->item_blk);
            c->B = (short)*pv;
            return;
        }
        ixw = c->ip;
        c->item = payload_word(c, pv, *ixw);
        d = WDIFF(c->item, NODE_END(c->item_seg));
        if (d >= 0)
            c->item = BLK_DATA(c->item_blk) + d;
    } else {
        p = cw_find_default_item(c, c->next, id);
        if (p == NULL) {
            c->status = CW_NO_ITEM;
            return;
        }
        pv = p + 1;
        c->B = (short)*pv;
        if (id & 0x8000)
            return;
        ixw = c->ip;
        c->item = payload_word(c, pv, *ixw);
    }
    c->ip = ixw + 1;
    if (*(unsigned char *)ixw & 0x80)
        c->B = (short)*c->item >> 8;
    else
        c->B = *(unsigned char *)c->item;
}

/* 0x10022180: op 104: store A clamped to -127..127 */
void cw_op_ST_s(Core *c)
{
    if (c->A < -0x7f)
        c->A = -0x7f;
    if (c->A > 0x7f)
        c->A = 0x7f;
    cw_op_ST(c);
}

/* 0x100221b0: op 108: store A clamped to 0..255 */
void cw_op_ST_u(Core *c)
{
    if (c->A < 0)
        c->A = 0;
    if (c->A > 0xff)
        c->A = 0xff;
    cw_op_ST(c);
}

/* 0x100221e0: op 10C {tspec, id} ...: store A into item id of the focus */
void cw_op_ST(Core *c)
{
    unsigned short id = (unsigned short)(*(unsigned char *)c->ip << 8);
    unsigned short *p;

    if (c->focus != c->cursor)
        c->inserted = 1;
    p = cw_find_item(c, c->focus, id);
    if (p != NULL) {
        cw_st_param(c, id, p);
        return;
    }
    p = cw_find_default_item(c, c->focus, id);
    if (p != NULL) {
        cw_st_var(c, id, p);
        return;
    }
    cw_st_default(c, id);
}

/* 0x10022250: the item exists on the segment */
void cw_st_param(Core *c, unsigned short id, unsigned short *val)
{
    short tspec = (short)(*c->ip & 0xff00);

    c->ip++;
    if (id & 0x8000)
        cw_st_param_rel(c, tspec, val);
    else
        cw_st_param_abs(c, tspec, val);
}

/* give an emptied ItemBlk (c->item_seg) back to the free list */
static void free_item_blk(Core *c)
{
    ItemBlk *b = (ItemBlk *)c->item_seg;
    Seg *n = c->item_seg;

    if (b->prev == c->focus)
        c->focus->ext = NULL;
    else
        ((ItemBlk *)b->prev)->next = NULL;
    n->prev = (Seg *)&c->free_list.head;
    n->next = c->free_list.head.next;
    n->next->prev = n;
    c->free_list.head.next = n;
}

/* one word was removed from the storage ending in c->item_seg */
static void shrink_storage(Core *c)
{
    if (c->item_seg == c->focus) {
        c->focus->items_len -= 2;
        return;
    }
    BLK_LEN(c->item_seg) -= 2;
    if (BLK_LEN(c->item_seg) == 0)
        free_item_blk(c);
}

/* advance c->item_seg/item_blk to the next ItemBlk */
static void next_blk(Core *c)
{
    c->item_seg = (Seg *)c->item_blk;
    c->item_blk = c->item_blk->next;
}

/* 0x10022290: single-word variable (id >= 0x80) */
void cw_st_param_rel(Core *c, short tspec, unsigned short *val)
{
    unsigned short w = *val, *dst, *src, *end;

    if ((unsigned char)w != 0) {
        if (tspec >= 0) {   /* overwrite */
            dst = val + 1;
            if (dst >= NODE_END(c->item_seg))
                dst = BLK_DATA(c->item_blk);
            *dst = (unsigned short)c->A;
            return;
        }
        /* delete the value: shift the rest of the storage down one word */
        *val = w & 0xff00;
        dst = val + 1;
        if (dst == NODE_END(c->item_seg)) {
            next_blk(c);
            dst = BLK_DATA(c->item_seg);
        }
        src = dst + 1;
        for (;;) {
            end = NODE_END(c->item_seg);
            if (src == end) {
                if (c->item_blk == NULL)
                    break;
                src = BLK_DATA(c->item_blk);
            }
            if (dst == end) {
                next_blk(c);
                dst = BLK_DATA(c->item_seg);
            }
            *dst++ = *src++;
        }
        shrink_storage(c);
        return;
    }
    if (tspec <= 0) {
        c->status = CW_ST_REL_NEW;
        return;
    }
    *val = w + 2;
    dst = val + 1;
    if (dst == NODE_END(c->item_seg))
        dst = BLK_DATA(c->item_blk);
    c->item_ins = dst;
    if (!cw_item_insert(c, 2)) {
        c->status = CW_NO_NODES;
        return;
    }
    *c->item_ins = (unsigned char)c->A;
}

/* index word for the parameter stores: returns k, sets *hi for the 0x80 flag.
 * Bit 6 of k selects an index relative to n (the payload word count). */
static int st_index(Core *c, int n, int *hi)
{
    unsigned short w = *c->ip++;
    int k = (w >> 9) & 0x7f;

    *hi = w & 0x80;
    if (k & 0x40)
        k = n + (k | ~0x7f) + 1;
    return k;
}

/* map a word pointer past the end of c->item_seg into the following block */
static unsigned short *wrap_ptr(Core *c, unsigned short *p, int need_blk)
{
    short d = WDIFF(p, NODE_END(c->item_seg));

    if (d >= 0 && (!need_blk || c->item_blk != NULL))
        p = BLK_DATA(c->item_blk) + d;
    return p;
}

/* 0x100223f0: parameter (id < 0x80): tspec 0 sets a byte of payload word k, > 0 inserts
 * a word at k, < 0 deletes word k */
void cw_st_param_abs(Core *c, short tspec, unsigned short *val)
{
    int hi;
    short k = (short)st_index(c, (*(unsigned char *)val >> 1) & 0x7f, &hi);
    unsigned short *p, *q, *base;
    short rem, t, len;

    if (tspec == 0) {
        if (2 * (int)k >= (int)*(unsigned char *)val || k < 0)
            return;
        p = wrap_ptr(c, val + k + 1, 0);
        if (hi) {
            *p &= 0xff;
            *p |= (unsigned short)((unsigned char)c->A << 8);
        } else {
            *(unsigned char *)p = 0;
            *p |= (unsigned char)c->A;
        }
        return;
    }
    if (tspec > 0) {
        *val += 2;
        c->item_ins = wrap_ptr(c, val + k + 1, 1);
        if (!cw_item_insert(c, 2)) {
            c->status = CW_NO_NODES;
            return;
        }
        *c->item_ins = hi ? (unsigned short)((unsigned char)c->A << 8) : (unsigned char)c->A;
        return;
    }
    /* tspec < 0: delete word k */
    if ((unsigned char)*val == 0) {
        c->status = CW_ST_ABS_DEL;
        return;
    }
    q = val + k + 1;
    *val -= 2;
    t = WDIFF(q, NODE_END(c->item_seg));
    if (t >= 0) {
        c->item_seg = (Seg *)c->item_blk;
        q = BLK_DATA(c->item_blk) + t;
        c->item_blk = c->item_blk->next;
    }
    if (c->item_seg == c->focus) {
        len = c->focus->items_len;
        base = c->focus->items;
    } else {
        len = BLK_LEN(c->item_seg);
        base = BLK_DATA(c->item_seg);
    }
    rem = (short)(len / 2 - WDIFF(q, base));
    while (rem != 0) {
        if (q != NODE_LAST(c->item_seg)) {
            *q = q[1];
            q++;
            t = rem--;
            if (t > 0)
                continue;
        }
        if (c->item_blk == NULL)
            break;
        next_blk(c);
        *q = BLK_DATA(c->item_seg)[0];
        q = BLK_DATA(c->item_seg);
        rem = (short)(BLK_LEN(c->item_seg) / 2);
    }
    shrink_storage(c);
}

/* 0x10022640: the segment has no item id but its phoneme has a default: copy the default
 * into the segment, applying the store */
void cw_st_var(Core *c, unsigned short id, unsigned short *val)
{
    short tspec = (short)(*c->ip & 0xff00);
    int n;

    c->ip++;
    n = *(unsigned char *)val + 2;
    if (tspec > 0)
        n += 2;
    if (tspec < 0)
        n -= 2;
    if (!cw_item_insert(c, (short)n)) {
        c->status = CW_NO_NODES;
        return;
    }
    *c->item_ins = (unsigned short)(n - 2 + id);
    c->item_ins++;
    if (id & 0x8000)
        cw_st_var_sub1(c, tspec);
    else
        cw_st_var_sub2(c, tspec, val + 1, (short)(n - 2));
}

/* the storage after c->item_ins continues in the next node */
static unsigned short *ins_wrap(Core *c)
{
    if (c->item_seg == c->focus)
        return BLK_DATA(c->focus->ext);
    return BLK_DATA(((ItemBlk *)c->item_seg)->next);
}

/* 0x100226e0: variable: write A (a delete leaves the new item empty) */
void cw_st_var_sub1(Core *c, short tspec)
{
    if (tspec < 0)
        return;
    if (c->item_ins == NODE_END(c->item_seg)) {
        if (c->item_seg == c->focus) {
            c->item_ins = BLK_DATA(c->focus->ext);
            *c->item_ins = (unsigned short)c->A;
            return;
        }
        c->item_ins = BLK_DATA(((ItemBlk *)c->item_seg)->next);
    }
    *c->item_ins = (unsigned short)c->A;
}

/* 0x10022730: parameter: copy the n bytes of default payload from src, applying the store
 * at word k */
void cw_st_var_sub2(Core *c, short tspec, unsigned short *src, short n)
{
    unsigned short w = *c->ip++;
    int hi = w & 0x80, cnt = n / 2;
    short k = (short)((w >> 9) & 0x7f), i;
    unsigned short *dst = c->item_ins;

    if (k & 0x40) {
        k = (short)((k | ~0x7f) + n / 2);
        if (tspec <= 0)
            k++;
    }
    for (i = 0; i < cnt; i++) {
        if (dst == NODE_END(c->item_seg))
            dst = ins_wrap(c);
        if (i != k) {
            *dst++ = *src++;
            continue;
        }
        if (tspec == 0) {
            if (hi) {
                *dst = *(unsigned char *)src;
                *dst |= (unsigned short)((unsigned char)c->A << 8);
            } else {
                *dst = *src & 0xff00;
                *dst |= (unsigned char)c->A;
            }
            src++;
            dst++;
        } else if (tspec < 0) {
            *dst++ = src[1];
            src += 2;
        } else {
            *dst++ = hi ? (unsigned short)((unsigned char)c->A << 8) : (unsigned char)c->A;
        }
    }
}

/* 0x10022860: neither the segment nor its phoneme has item id: create a one-word item */
void cw_st_default(Core *c, unsigned short id)
{
    unsigned short *p = c->ip;
    short tspec = (short)(*p & 0xff00);

    c->ip = p + 1;
    if (tspec < 0) {
        if ((short)id >= 0)
            c->ip = p + 2;
        return;
    }
    if (tspec == 0 && (short)id >= 0) {
        c->ip = p + 2;
        return;
    }
    if (!cw_item_insert(c, 4)) {
        c->status = CW_NO_NODES;
        return;
    }
    *c->item_ins = id | 2;
    c->item_ins++;
    if (c->item_ins == NODE_END(c->item_seg))
        c->item_ins = ins_wrap(c);
    if (id & 0x8000) {
        *c->item_ins = (unsigned short)c->A;
        return;
    }
    p = c->ip++;
    if (*(unsigned char *)p & 0x80)
        *c->item_ins = (unsigned short)((unsigned char)c->A << 8);
    else
        *c->item_ins = (unsigned char)c->A;
}

/* 0x10022940: open nbytes at c->item_ins in the focus's item storage, shifting the rest
 * up and appending an ItemBlk when the last node is full. Returns 0 if out of nodes. */
#ifdef CW_COVERAGE
unsigned long cw_insert_count[4];   /* inline, inline+new block, block, block+new block */
#define INSERT_CASE(i) (cw_insert_count[i]++)
#else
#define INSERT_CASE(i) ((void)0)
#endif

int cw_item_insert(Core *c, short nbytes)
{
    Seg *f = c->focus;
    unsigned short *stop = c->item_ins - 1, *src, *dst, *srclim, *dstlim;
    ItemBlk *b, *nb, *srcblk, *dstblk;
    short room;

    if (f->ext == NULL) {
        room = (short)(0x18 - f->items_len);
        src = f->items + f->items_len / 2 - 1;
        INSERT_CASE(nbytes > room);
        if (nbytes > room) {
            nb = (ItemBlk *)seg_alloc(c);
            if (nb == NULL)
                return 0;
            f->ext = nb;
            nb->prev = f;
            nb->next = NULL;
            nb->len = (short)(nbytes - room);
            dst = (unsigned short *)((char *)nb + 8) + (nbytes - room) / 2;
            dstlim = (unsigned short *)((char *)nb + 8);
            f->items_len = 0x18;
            if (c->item_ins == NODE_END(f))
                c->item_ins = BLK_DATA(f->ext);
        } else {
            f->items_len = (short)(f->items_len + nbytes);
            dstlim = (unsigned short *)f;
            dst = src + nbytes / 2;
        }
        while (src != stop) {
            *dst = *src;
            src--;
            dst--;
            if (dst == dstlim)
                dst = NODE_LAST(c->focus);
        }
        return 1;
    }

    for (b = f->ext; b->next != NULL; b = b->next)
        ;
    srcblk = b;
    srclim = (unsigned short *)((char *)b + 8);
    src = srclim + b->len / 2;
    room = (short)(0x2e - b->len);
    INSERT_CASE(2 + (nbytes > room));
    if (nbytes > room) {
        nb = (ItemBlk *)seg_alloc(c);
        if (nb == NULL)
            return 0;
        b->next = nb;
        nb->prev = b;
        nb->len = (short)(nbytes - room);
        nb->next = NULL;
        dst = (unsigned short *)((char *)nb + 8) + (nbytes - room) / 2;
        dstlim = (unsigned short *)((char *)nb + 8);
        dstblk = nb;
        b->len = 0x2e;
        if (c->item_ins == NODE_END(b))
            c->item_ins = BLK_DATA(b->next);
    } else {
        b->len = (short)(b->len + nbytes);
        dstlim = srclim;
        dstblk = b;
        dst = src + nbytes / 2;
    }
    while (src != stop) {
        if (src == srclim) {
            srcblk = (ItemBlk *)srcblk->prev;
            srclim = (unsigned short *)((char *)srcblk + 8);
            src = NODE_LAST(srcblk);
        }
        if (dst == dstlim) {
            dstblk = (ItemBlk *)dstblk->prev;
            dstlim = (unsigned short *)((char *)dstblk + 8);
            dst = NODE_LAST(dstblk);
        }
        *dst = *src;
        src--;
        dst--;
    }
    return 1;
}

/* 0x10022b20: find item id on segment s (descending ids). Sets c->item_seg/item_blk to the
 * node holding the stop position; on success c->item = last payload word, else
 * c->item_ins = where the item would go. Returns the header or NULL. */
unsigned short *cw_find_item(Core *c, Seg *s, unsigned short id)
{
    unsigned short *p = s->items, *end, key;
    ItemBlk *b = s->ext, *nx;
    short d;

    c->item_seg = s;
    c->item_blk = b;
    if (s->items_len == 0) {
        c->item_ins = p;
        return NULL;
    }
    end = s->items + s->items_len / 2;
    for (; p < end; p += ((*p >> 1) & 0x7f) + 1) {
        key = *p & 0xff00;
        if ((short)key < (short)id) {
            c->item_ins = p;
            return NULL;
        }
        if (key == id) {
            c->item = p + ((*(unsigned char *)p >> 1) & 0x7f);
            return p;
        }
    }
    for (; b != NULL; b = nx) {
        c->item_seg = (Seg *)b;
        nx = b->next;
        c->item_blk = nx;
        d = WDIFF(p, end);
        end = b->data + b->len / 2;
        for (p = b->data + d; p < end; p += ((*p >> 1) & 0x7f) + 1) {
            key = *p & 0xff00;
            if ((short)key < (short)id) {
                c->item_ins = p;
                return NULL;
            }
            if (key == id) {
                c->item = p + ((*(unsigned char *)p >> 1) & 0x7f);
                return p;
            }
        }
    }
    c->item_ins = p;
    return NULL;
}

/* 0x10022c20: find item id in the phoneme record of s (ascending ids) */
unsigned short *cw_find_default_item(Core *c, Seg *s, unsigned short id)
{
    unsigned short *rec = c->phinv + s->ph / 2;
    unsigned short *end = rec + (short)rec[0] / 2, *p, key;

    if (rec[0] == 0xc)
        return NULL;
    for (p = rec + 6; p < end; p += ((*p >> 1) & 0x7f) + 1) {
        key = *p & 0xff00;
        if (key == id) {
            c->item = p + ((*(unsigned char *)p >> 1) & 0x7f);
            return p;
        }
        if ((short)key > (short)id)
            return NULL;
    }
    return NULL;
}
