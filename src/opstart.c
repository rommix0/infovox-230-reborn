/* Utterance start: durations and track setup (opstart.c, DLL 0x1001dc40-0x1001e88f).
 *
 * Track pointers while searching (op_track_find_item/_default):
 *   segment item search:   p0 = current item header, p1 = end of this node's item
 *                          data, p2 = the next ItemBlk's data (0xa when there is none)
 *   phoneme default search: p1 = current item header, p2 = end of the record
 * Once a track is on an item, p0 (or p1 when the points come from the phoneme record,
 * p0 == NULL) walks its points.
 */
#include "op.h"

/* 0x1001dc40: empty Core.out, reset the output state and reload A1..A3 from the .ivx
 * tables (stored reversed; A2 is x16 for type codes < 0x48). Returns 0. */
short ivx_load_param_tables(Core *c)
{
    const short *a1, *a2, *a3;
    int k;

    c->out_active = 0;
    c->synth_state = 0;
    c->frames_left = 0;
    c->frame_no = 0;
    c->feac = 0;
    seg_free_range(c, c->out.head.next, OUT_TAIL(c));
    c->out_cur = OUT_TAIL(c);
    for (k = 0; k < c->ntotal; k++) {
        c->A2[k] = 0;
        c->A1[k] = 0;
        c->A3[k] = 0;
    }
    a2 = c->ptab_A2 + c->ntotal;
    a1 = c->ptab_A1 + c->ntotal;
    a3 = c->ptab_A3 + c->ntotal;
    for (k = 0; k < c->ntotal; k++) {
        c->A2[k] = *a2--;
        c->A1[k] = *a1--;
        if (c->A1[k] < 0x48)
            c->A2[k] = (short)(c->A2[k] << 4);
        c->A3[k] = *a3--;
    }
    c->last_mark_lo = 0;
    c->last_mark_hi = 0;
    return 0;
}

/* 0x1001dd40: hand the finished segs to the frame generator. Returns 0x20 when there is
 * nothing to say (no segments, or no F0 points at all), else 0. With feae set (or
 * output still running) the list stays in segs and only synth_state is updated. */
short op_start_utterance(Core *c)
{
    Seg *first;

    if (c->segs.head.next == SEGS_TAIL(c))
        return 0x20;
    if (c->feae != 0) {
        if ((c->mode & 5) != 0 || (c->retry & 2) != 0 || c->out_active != 0) {
            c->synth_state = 0xff;
            return 0;
        }
        goto started;
    }
    if (c->out_active != 0) {
        c->synth_state = 0xff;
        return 0;
    }
    seg_free_range(c, c->out.head.next, OUT_TAIL(c));
    first = c->segs.head.next;
    c->out.head.next = first;
    c->out.tail.prev = c->segs.tail.prev;
    first->prev = OUT_HEAD(c);
    c->out.tail.prev->next = OUT_TAIL(c);
    c->segs.head.next = SEGS_TAIL(c);
    c->segs.tail.prev = SEGS_HEAD(c);
    c->frames_left = op_seg_durations(c, c->out.head.next, OUT_TAIL(c));
    c->frame_no = 0;
    op_tracks_clear(c);
    op_tracks_init(c);
    op_tracks_prime(c);
    if (c->tracks[25].seg == NULL) {
        seg_free_range(c, c->out.head.next, OUT_TAIL(c));
        c->frames_left = 0;
        return 0x20;
    }
    op_mark_param(c, c->tracks[25].seg, OUT_TAIL(c), 25);
    c->out_cur = c->out.head.next;
started:
    c->synth_state = 1;
    c->out_active = 1;
    return 0;
}

/* 0x1001de90: report s's bookmark once: the high byte of mark_lo when it changes, then
 * (mark_lo low byte << 16 | mark_hi) when that changes; the mark is cleared. */
void op_emit_bookmark(Core *c, Seg *s)
{
    unsigned short hi;

    if (s->mark_lo == 0 && s->mark_hi == 0)
        return;
    if (((c->last_mark_lo ^ s->mark_lo) & 0xff00) != 0) {
        hi = s->mark_lo & 0xff00;
        if (c->cb_bookmark != NULL)
            c->cb_bookmark(c->cb_arg, hi >> 8);
        c->last_mark_lo = (unsigned short)((c->last_mark_lo & 0xff) | hi);
    }
    if ((unsigned char)c->last_mark_lo != (unsigned char)s->mark_lo || c->last_mark_hi != s->mark_hi) {
        if (c->cb_bookmark != NULL)
            c->cb_bookmark(c->cb_arg, (s->mark_lo & 0xff) << 16 | (int)(short)s->mark_hi);
        c->last_mark_lo = (unsigned short)((c->last_mark_lo & 0xff00) | (s->mark_lo & 0xff));
        c->last_mark_hi = s->mark_hi;
    }
    s->mark_lo = 0;
    s->mark_hi = 0;
}

/* 0x1001df70 */
void op_tracks_clear(Core *c)
{
    int k;

    for (k = 1; k < c->ntotal; k++) {
        c->tracks[k].target = 0;
        c->tracks[k].phase = 0;
        c->tracks[k].cnt = 0;
        c->tracks[k].delta = 0;
        c->tracks[k].step = 0;
    }
}

/* the first point of the segment item at t->p0: npts, and tleft = its time + 1 */
static void first_item_point(Track *t)
{
    t->npts = op_npts(*t->p0);
    t->p0++;
    if (t->p0 >= t->p1)
        op_track_wrap(t);
    t->tleft = (short)(op_tcode(*t->p0) + 1);
}

/* 0x1001dfc0: put every track on the first segment. Segment items are sorted by
 * descending id, so the tracks are searched from ntotal-1 down, each search continuing
 * where the previous one stopped (the pointers are handed down to track i-1; track
 * ntotal-1 is searched twice, the second time from its advanced p0, as in the DLL).
 * Tracks without a segment item then search the phoneme record upwards the same way.
 * The first segment's mask/feat get the "has points" bits; track 0 gives its duration. */
void op_tracks_init(Core *c)
{
    Seg *head = c->out.head.next;
    unsigned short *p, *run, *run_end;
    unsigned int mbit, fbit, mask, feat;
    short n = (short)(c->ntotal - 1), i, k;
    Track *t = &c->tracks[n];

    t->p0 = NULL;
    c->outval[n] = 0;
    t->seg = c->out.head.next;
    p = op_track_find_item(c, c->out.head.next, n);
    run = t->p0;
    if (p != NULL) {
        t[-1].p0 = run;
        t[-1].p1 = t->p1;
        t[-1].p2 = t->p2;
        first_item_point(t);
    } else if (run != NULL) {
        t[-1].p0 = run;
        t[-1].p1 = t->p1;
        t[-1].p2 = t->p2;
        t->p0 = NULL;
    } else {
        t[-1].p0 = NULL;
    }
    for (k = n; k > 0; k--, t--) {
        c->outval[k] = 0;
        t->seg = c->out.head.next;
        if (run == NULL) {
            t[-1].p0 = t->p0;
            continue;
        }
        p = op_track_find_item(c, c->out.head.next, k);
        run = t->p0;
        t[-1].p0 = run;
        t[-1].p1 = t->p1;
        t[-1].p2 = t->p2;
        if (p != NULL)
            first_item_point(t);
        else
            t->p0 = NULL;
    }

    /* track 0: the segment duration (DR) */
    c->outval[0] = 3;
    c->tracks[0].seg = c->out.head.next;
    if (c->tracks[0].p0 != NULL) {
        p = op_track_find_item(c, c->out.head.next, 0);
        if (p != NULL) {
            p++;
            if (p >= c->tracks[0].p1)
                p = c->tracks[0].p2;
            c->tracks[0].p1 = NULL;
            op_track_find_default(c, c->out.head.next, 0);
        } else {
            c->tracks[0].p0 = NULL;
            c->tracks[0].p1 = NULL;
            p = op_track_find_default(c, c->out.head.next, 0) + 1;
        }
    } else {
        c->tracks[0].p1 = NULL;
        p = op_track_find_default(c, c->out.head.next, 0) + 1;
    }
    run_end = c->tracks[0].p2;
    run = c->tracks[0].p1;
    c->out.head.next->dur = *p & 0xff;
    c->tracks[0].tleft = c->out.head.next->dur;

    mask = feat = 0xffffffffu;
    mbit = 0;
    fbit = 1;
    for (i = 1; i < c->ntotal; i++) {
        mbit <<= 1;
        fbit <<= 1;
        if (mbit == 0 && fbit == 0)
            mbit = 1;
        t = &c->tracks[i];
        if (t->p0 != NULL)
            continue;
        if (run != NULL) {
            t->p1 = run;
            t->p2 = run_end;
            p = op_track_find_default(c, c->out.head.next, i);
            run = t->p1;
            run_end = t->p2;
            if (p != NULL) {
                t->npts = op_npts(*run);
                t->p1 = run + 1;
                t->tleft = (short)(op_tcode(run[1]) + 1);
                continue;
            }
        }
        t->p1 = NULL;
        t->npts = 0;
        t->tleft = t->seg->dur;
        mask ^= mbit;
        feat ^= fbit;
    }
    head = c->out.head.next;
    head->mask = mask;
    head->feat = feat;

    c->outval[8] = c->voice[8];
    c->outval[14] = c->voice[14];
    c->outval[6] = c->voice[6];
    c->outval[10] = c->voice[10];
    c->outval[12] = c->voice[12];
    c->outval[16] = c->voice[16];
    /* undo a previous utterance's FD offset: A3 of the parameter FD points at */
    k = (short)(c->A3[4] / 2);
    c->A3[c->ntotal - k] = c->ptab_A3[k];
}

/* 0x1001e3c0: tracks with no points on the first segment move to the first later
 * segment that has some (toggling the has-points bits of those skipped). */
void op_tracks_prime(Core *c)
{
    unsigned int mbit = 0, fbit = 1;
    unsigned short *p;
    Track *t;
    Seg *s;
    short i;

    for (i = 1; i < c->ntotal; i++) {
        mbit <<= 1;
        fbit <<= 1;
        if (mbit == 0 && fbit == 0)
            mbit = 1;
        t = &c->tracks[i];
        if (t->npts > 0)
            continue;
        t->p0 = NULL;
        s = t->seg->next;
        t->seg = s;
        while (s != OUT_TAIL(c)) {
            p = op_track_find_item(c, s, i);
            t->p0 = p;
            if (p != NULL) {
                t->npts = op_npts(*p);
                t->p0 = p + 1;
                if (t->p0 >= t->p1)
                    op_track_wrap(t);
                t->tleft += op_tcode(*t->p0) + 1;
                break;
            }
            t->p1 = NULL;
            p = op_track_find_default(c, t->seg, i);
            t->p1 = p;
            if (p != NULL) {
                t->npts = op_npts(*p);
                t->p1 = p + 1;
                t->tleft += op_tcode(p[1]) + 1;
                break;
            }
            s = t->seg;
            t->tleft += s->dur;
            s->mask ^= mbit;
            t->seg->feat ^= fbit;
            s = t->seg->next;
            t->seg = s;
        }
        if (t->seg == OUT_TAIL(c))
            t->seg = NULL;
    }
}

/* the DR (track 0) duration word of segment s: its own item, else the phoneme default;
 * the item header may be the last word of the inline area (then the value is in the
 * first ItemBlk) */
static const unsigned short dur_none;   /* 0: the duration of a segment with no DR at all */

static const unsigned short *dur_point(Core *c, Seg *s)
{
    unsigned short *p;

    c->tracks[0].p0 = NULL;
    p = op_track_find_item(c, s, 0);
    if (p != NULL) {
        p++;
        if (p >= c->tracks[0].p1)
            p = c->tracks[0].p2;
        return p;
    }
    c->tracks[0].p1 = NULL;
    p = op_track_find_default(c, s, 0);
    /* GUARD, not in the DLL: a segment with neither its own DR item nor a phoneme
     * default makes the DLL read address 2 and crash (0x1001e56e); it gets duration 0 */
    if (p == NULL)
        return &dur_none;
    return p + 1;
}

/* 0x1001e510: s->dur = DR value (frames) of each segment; mask = feat = all ones.
 * Returns the total (16-bit). */
short op_seg_durations(Core *c, Seg *first, Seg *end)
{
    short total = 0;
    Seg *s;

    for (s = first; s != end; s = s->next) {
        s->dur = *dur_point(c, s) & 0xff;
        total += s->dur;
        s->mask = 0xffffffffu;
        s->feat = 0xffffffffu;
    }
    return total;
}

/* 0x1001e5a0: for the segments after first up to end: toggle feat bit i when the segment
 * has no item i and its phoneme record has no default for it (both searches stop early
 * like op_track_find_*). */
void op_mark_param(Core *c, Seg *first, Seg *end, unsigned char i)
{
    unsigned int bit = 1u << (i & 0x1f);
    short key = (short)(i << 8);
    unsigned short *p, *lim, *b, *rec;
    short id;
    Seg *s;

    for (s = first->next; s != end; s = s->next) {
        b = BLK_DATA(s->ext);
        p = s->items;
        lim = s->items + s->items_len / 2;
        for (;;) {
            while (p < lim) {
                id = (short)(*p & 0xff00);
                if (id == key)
                    goto next;       /* has its own item */
                if (id < key)
                    goto dflt;
                p += op_npts(*p) + 1;
            }
            if (DATA_BLK(b) == NULL)
                goto dflt;
            p = b + (short)WDIFF(p, lim);
            lim = b + BLK_LEN(DATA_BLK(b)) / 2;
            b = BLK_DATA(DATA_BLK(b)->next);
        }
dflt:
        rec = c->phinv + s->ph / 2;
        lim = rec + (short)*rec / 2;
        for (p = rec + 6; p < lim; p += op_npts(*p) + 1) {
            id = (short)(*p & 0xff00);
            if (id == key)
                goto next;           /* has a default */
            if (id > key)
                break;
        }
        OP_COV(OPC_MARK_TOGGLE);
        s->feat ^= bit;
next:;
    }
}

/* 0x1001e720: find parameter i's item on s, continuing from tracks[i].p0 when that is
 * set (see op_tracks_init). A lower id stops the search with p0 left on it. */
unsigned short *op_track_find_item(Core *c, Seg *s, short i)
{
    Track *t = &c->tracks[i];
    short key = (short)((i & 0xff) << 8);
    unsigned short *b;
    short id;

    if (t->p0 == NULL) {
        t->p2 = BLK_DATA(s->ext);
        t->p1 = s->items + s->items_len / 2;
        t->p0 = s->items;
    }
    for (;;) {
        while (t->p0 < t->p1) {
            id = (short)(*t->p0 & 0xff00);
            if (id == key)
                return t->p0;
            if (id < key)
                return NULL;
            t->p0 += op_npts(*t->p0) + 1;
        }
        b = t->p2;
        if (DATA_BLK(b) == NULL) {
            t->p0 = NULL;
            return NULL;
        }
        OP_COV(OPC_BLOCK_STEP);
        t->p0 = b + (short)WDIFF(t->p0, t->p1);
        t->p1 = b + BLK_LEN(DATA_BLK(b)) / 2;
        t->p2 = BLK_DATA(DATA_BLK(b)->next);
    }
}

/* 0x1001e7e0: find parameter i in s's phoneme record, continuing from tracks[i].p1 when
 * set. A higher id stops the search with p1 left on it; the end of the record clears
 * p1. */
unsigned short *op_track_find_default(Core *c, Seg *s, short i)
{
    Track *t = &c->tracks[i];
    short key = (short)((i & 0xff) << 8);
    unsigned short *rec, *end;
    short id;

    if (t->p1 == NULL) {
        rec = c->phinv + s->ph / 2;
        t->p1 = rec;
        t->p2 = rec + (short)*rec / 2;
        t->p1 = rec + 6;
    }
    end = t->p2;
    if (t->p1 >= end) {
        t->p1 = NULL;
        return NULL;
    }
    do {
        id = (short)(*t->p1 & 0xff00);
        if (id == key)
            return t->p1;
        if (id > key)
            return NULL;
        t->p1 += op_npts(*t->p1) + 1;
    } while (t->p1 < end);
    t->p1 = NULL;
    return NULL;
}
