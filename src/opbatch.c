/* Batch output (opbatch.c, DLL 0x10019780-0x10019f4f) [HYPOTHESIS: file name].
 *
 * Used by the SAPI engine to render an utterance separately from text processing:
 * opb_save snapshots the finished segment list (with its ItemBlks) into a private node
 * array, opb_restore installs it as Core.segs and starts frame output, and opb_init /
 * opb_copy_config give a second Core the output configuration of the first.
 * Return values are the DLL's 16-bit status: 0 = ok, -1 = failure.
 */
#include "op.h"

/* the output configuration copied by opb_init and opb_copy_config */
static void copy_config(Core *dst, const Core *src)
{
    int k;

    dst->ntotal = src->ntotal;
    dst->esc_mark = src->esc_mark;
    dst->cb_arg = src->cb_arg;
    dst->p6ec = src->p6ec;
    dst->cb_wordpos = src->cb_wordpos;
    dst->cb_bookmark = src->cb_bookmark;
    dst->phinv = src->phinv;
    dst->ptab_A1 = src->ptab_A1;
    dst->ptab_A2 = src->ptab_A2;
    dst->ptab_A0 = src->ptab_A0;
    dst->ptab_A3 = src->ptab_A3;
    for (k = 0; k < 32; k++) {
        /* pdesc_n, pdesc_pad and pnames, as 32 words */
        ((short *)&dst->pdesc_n)[k] = ((const short *)&src->pdesc_n)[k];
        dst->A1[k] = src->A1[k];
        dst->A2[k] = src->A2[k];
        dst->A3[k] = src->A3[k];
        dst->tracks[k] = src->tracks[k];   /* raw, pointers included */
        dst->voice[k] = src->voice[k];
        dst->voice_next[k] = src->voice_next[k];
        dst->outval[k] = src->outval[k];
    }
    dst->out_save0 = src->out_save0;
    dst->out_save1 = src->out_save1;
    dst->out_mode = src->out_mode;
    dst->nparams = src->nparams;
    dst->ntotal = src->ntotal;
    dst->frames_left = src->frames_left;
    dst->frame_no = src->frame_no;
    dst->voice_dirty = src->voice_dirty;
    dst->ui_rate = src->ui_rate;
    dst->ui_pitch = src->ui_pitch;
    dst->ui_fd = src->ui_fd;
    dst->last_mark_lo = src->last_mark_lo;
    dst->last_mark_hi = src->last_mark_hi;
    dst->synth_state = src->synth_state;
    dst->out_active = src->out_active;
    dst->feac = src->feac;
    dst->fead = src->fead;
}

/* 0x10019780: a new zeroed Core with src's output configuration */
short opb_init(Core *src, Core **out)
{
    Core *c = mem_calloc(1, sizeof(Core));

    if (c == NULL)
        return -1;
    copy_config(c, src);
    *out = c;
    return 0;
}

/* 0x100199e0: make b's segments Core.segs and start output. Core.out is emptied without
 * freeing (its sentinels are linked to each other), and the first restored segment's
 * prev is set to &segs.tail, not &segs.head (as in the DLL). */
short opb_restore(Core *c, OpBatch *b)
{
    const short *a1, *a2, *a3;
    short k;

    if (b->size == 0 || b->nodes == NULL)
        return -1;
    c->out.tail.prev = OUT_HEAD(c);
    c->out.head.prev = OUT_TAIL(c);
    c->out.head.next = OUT_TAIL(c);
    c->out.tail.next = OUT_HEAD(c);
    b->list.tail.prev->next = SEGS_TAIL(c);
    b->list.head.next->prev = SEGS_TAIL(c);
    c->segs.head.next = b->list.head.next;
    c->segs.tail.prev = b->list.tail.prev;
    c->out_active = 0;
    c->last_mark_lo = 0;
    c->last_mark_hi = 0;
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
    return op_start_utterance(c);
}

/* 0x10019af0: copy Core.segs (segments and ItemBlks, in list order) into one node array
 * and free the originals. Core.out is emptied without freeing its nodes. A copied first
 * ItemBlk keeps its original prev pointer (only later blocks are relinked), as in the
 * DLL. With an empty segs the DLL leaves list.tail.prev uninitialised; the port sets it
 * to the list head. */
short opb_save(Core *c, OpBatch **out)
{
    OpBatch *b;
    Seg *s, *d, *prev, *last = NULL;
    ItemBlk *src, *bd, *nb;
    int n = 0, k = 0;

    for (s = c->segs.head.next; s != SEGS_TAIL(c); s = s->next) {
        n++;
        for (src = s->ext; src != NULL; src = src->next)
            n++;
    }
    b = mem_calloc(1, sizeof(OpBatch));
    if (b == NULL)
        return -1;
    b->nodes = mem_calloc(1, n * NODE_SIZE);
    b->size = n * NODE_SIZE;
    b->list.head.next = (Seg *)&b->list.tail;
    b->list.head.prev = (Seg *)&b->list.tail;
    b->list.tail.prev = (Seg *)&b->list.head;
    b->list.tail.next = (Seg *)&b->list.head;
    prev = (Seg *)&b->list.head;
    for (s = c->segs.head.next; s != SEGS_TAIL(c); s = s->next) {
        d = (Seg *)((char *)b->nodes + k++ * NODE_SIZE);
        *d = *s;
        d->ext = NULL;
        d->prev = prev;
        d->next = prev->next;
        prev->next = d;
        last = d;
        src = s->ext;
        if (src != NULL) {
            bd = (ItemBlk *)((char *)b->nodes + k++ * NODE_SIZE);
            *bd = *src;
            d->ext = bd;
            for (src = src->next; src != NULL; src = src->next) {
                nb = (ItemBlk *)((char *)b->nodes + k++ * NODE_SIZE);
                *nb = *src;
                nb->next = NULL;
                nb->prev = bd;
                bd->next = nb;
                bd = nb;
            }
        }
        prev = d;
    }
    b->list.tail.prev = last != NULL ? last : (Seg *)&b->list.head;
    seg_free_range(c, c->segs.head.next, SEGS_TAIL(c));
    c->out.head.next = OUT_TAIL(c);
    c->out.tail.prev = OUT_HEAD(c);
    c->segs.head.next = SEGS_TAIL(c);
    c->segs.tail.prev = SEGS_HEAD(c);
    c->out_active = 0;
    c->synth_state = 0;
    *out = b;
    return 0;
}

/* 0x10019cc0 */
short opb_free(OpBatch *b)
{
    if (b != NULL) {
        if (b->nodes != NULL)
            mem_free(b->nodes);
        mem_free(b);
    }
    return 0;
}

/* 0x10019cf0 */
int opb_size(OpBatch *b)
{
    if (b == NULL)
        return -1;
    return b->size;
}

/* 0x10019d00: opb_init's copy into an existing Core */
short opb_copy_config(Core *src, Core *dst)
{
    if (dst == NULL)
        return -1;
    copy_config(dst, src);
    return 0;
}
