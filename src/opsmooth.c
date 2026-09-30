/* Parameter tracks -> frames (opsmooth.c, DLL 0x1001c4f0-0x1001dc3f).
 *
 * Every frame op_frame_gen advances each track to its current time point (Track.target,
 * with Track.delta = the distance still to go) and op_param_dispatch runs the parameter's
 * A1 handler, which moves delta towards 0 and writes Core.outval. Arithmetic follows the
 * DLL's 16-bit/32-bit mix exactly (divisions truncate, as MSVC's).
 */
#include "op.h"

#ifdef CW_COVERAGE
unsigned long op_type_count[33], op_cov[OP_COV_N];
#endif

/* byte-swapped 16-bit value, with the DLL's arithmetic shift of the high byte */
static unsigned short swap16(unsigned short v)
{
    return (unsigned short)((v << 8) | (unsigned short)((short)v >> 8));
}

/* 0x1001c4f0: one frame. Returns &outval[4] (out_mode 0: outval[4] carries the flags
 * outval[0], the real parameter 4 value is parked in out_save0) or &outval[3] (out_mode
 * != 0: outval[3] = frame number, outval[4] = the segment's phoneme name at its first
 * frame, else "  ", both byte-swapped). NULL when no frame is left. */
short *op_next_frame(Core *c)
{
    unsigned short name = 0x2020;
    Seg *s;
    Track *t0 = &c->tracks[0];
    short a0, m, v, d;
    int k;

    if (c->out_active == 0)
        return NULL;
    if (c->frames_left == 0) {
        c->out_active = 0;
        return NULL;
    }
    c->frames_left--;
    if (c->voice_dirty != 0) {
        OP_COV(OPC_VOICE_DIRTY);
        c->voice_dirty = 0;
        for (k = 0; k < c->ntotal; k++)
            c->voice[k] = c->voice_next[k];
        c->outval[8] = c->voice[8];
        c->outval[6] = c->voice[6];
        c->outval[10] = c->voice[10];
        c->outval[14] = c->voice[14];
        c->outval[12] = c->voice[12];
        c->outval[16] = c->voice[16];
        for (k = 0; k < c->ntotal; k++) {
            c->tracks[k].target--;
            c->tracks[k].delta--;
        }
    }
    if (c->out_mode != 0) {
        c->outval[3] = c->out_save0;
        c->outval[4] = c->out_save1;
    } else {
        c->outval[4] = c->out_save0;
    }

    /* track 0 is the segment clock (DR) */
    s = t0->seg;
    if (t0->tleft == s->dur) {
        if (c->esc_mark != 0)
            op_emit_bookmark(c, s);
        if (c->out_flags & 2)
            op_emit_phonemes(c, t0->seg, t0->seg->next, NULL, 0);
        name = c->phinv[t0->seg->ph / 2 + 1];
    }
    t0->tleft--;
    if (t0->tleft <= 0 && t0->seg != NULL) {
        t0->seg->feat ^= 1;
        s = t0->seg->next;
        t0->seg = s;
        if (s == OUT_TAIL(c))
            t0->seg = NULL;
        else
            t0->tleft = s->dur;
    }

    op_frame_gen(c);
    op_param_dispatch(c);

    /* voice[0] != 0: AC (23) and AN (24) are lowered by it, and AH (22) is raised to at
     * least their value - 12 + voice[0], max 31 */
    a0 = c->voice[0];
    if (a0 != 0) {
        OP_COV(OPC_A0_ADJUST);
        v = c->tracks[24].target - c->tracks[24].delta;
        m = c->tracks[22].target - c->tracks[22].delta;
        c->outval[24] = v;
        if (v != 0) {
            d = v - a0;
            c->outval[24] = d;
            if (d < 0)
                c->outval[24] = 0;
            d = v + a0 - 12;
            if (m < d)
                m = d;
        }
        v = c->tracks[23].target - c->tracks[23].delta;
        c->outval[23] = v;
        if (v != 0) {
            d = v - a0;
            c->outval[23] = d;
            if (d < 0)
                c->outval[23] = 0;
            d = v + a0 - 12;
            if (m < d)
                m = d;
        }
        if (m > 0x1f)
            m = 0x1f;
        c->outval[22] = m;
    }

    if (c->out_mode != 0) {
        c->out_save0 = c->outval[3];
        c->out_save1 = c->outval[4];
        c->frame_no++;
        c->outval[3] = (short)swap16((unsigned short)c->frame_no);
        c->outval[4] = (short)swap16(name);
        return &c->outval[3];
    }
    c->out_save0 = c->outval[4];
    c->outval[4] = c->outval[0];
    return &c->outval[4];
}

/* 0x1001c810: advance tracks 1..ntotal-1 by one frame. A track whose time runs out
 * takes its next point (target, delta = step = the jump from the current value); when
 * the item is used up it moves to the next segment that has points. mbit/fbit are the
 * per-track "has points" flag in Seg.mask (tracks >= 32) / Seg.feat (tracks < 32). */
void op_frame_gen(Core *c)
{
    unsigned int mbit = 0, fbit = 1;
    unsigned short w;
    short i, cur, tc;
    Track *t;

    for (i = 1; i < c->ntotal; i++) {
        mbit <<= 1;
        fbit <<= 1;
        if (mbit == 0 && fbit == 0)
            mbit = 1;
        t = &c->tracks[i];
        t->tleft--;
        if (t->seg == NULL)
            continue;
        if (t->npts <= 0 && !op_track_next_seg(c, i, mbit, fbit))
            continue;
        while (t->tleft <= 0) {
            cur = t->target - t->delta;
            if (t->p0 != NULL)
                w = *t->p0++;
            else
                w = *t->p1++;
            t->target = w & 0xff;
            tc = op_tcode(w);
            t->delta = t->step = t->target - cur;
            if (--t->npts > 0) {
                if (t->p0 != NULL) {
                    if (t->p0 >= t->p1)
                        op_track_wrap(t);
                    w = *t->p0;
                } else {
                    w = *t->p1;
                }
                t->tleft = t->tleft + op_tcode(w) - tc;
            } else {
                t->seg->mask ^= mbit;
                t->seg->feat ^= fbit;
                t->tleft += t->seg->dur - tc;
                if (!op_track_next_seg(c, i, mbit, fbit))
                    break;
            }
        }
    }
}

/* first point of the item at t->p0 (segment item): npts, then tleft += its time */
static unsigned short *item_start(Track *t)
{
    t->npts = op_npts(*t->p0);
    t->p0++;
    if (t->p0 >= t->p1)
        op_track_wrap(t);
    return t->p0;
}

/* 0x1001c9d0: move track i to the next segment with points for it (skipping segments
 * whose has-points flag is clear, adding their durations to tleft). Returns 0 at the
 * end of Core.out (seg = NULL) or when the segment turns out to have no points (its flag
 * is then toggled). */
int op_track_next_seg(Core *c, short i, unsigned int mbit, unsigned int fbit)
{
    Track *t = &c->tracks[i];
    Seg *s = t->seg->next;
    unsigned short *p;

    t->seg = s;
    if (s == OUT_TAIL(c)) {
        t->seg = NULL;
        return 0;
    }
    if ((s->mask & mbit) == 0 && (s->feat & fbit) == 0) {
        OP_COV(OPC_NEXT_SKIP);
        t->tleft += s->dur;
        for (;;) {
            s = t->seg->next;
            t->seg = s;
            if (s == OUT_TAIL(c)) {
                t->seg = NULL;
                return 0;
            }
            if ((s->mask & mbit) != 0 || (s->feat & fbit) != 0)
                break;
            t->tleft += s->dur;
        }
        if (op_track_has_item(c, t->seg, i) == 1) {
            p = item_start(t);
        } else {
            op_track_has_default(c, t->seg, i);   /* result unused: flagged segs have one */
            t->npts = op_npts(*t->p1);
            p = ++t->p1;
        }
    } else if (op_track_has_item(c, s, i) == 1) {
        OP_COV(OPC_NEXT_ITEM);
        p = item_start(t);
    } else if (op_track_has_default(c, t->seg, i) == 1) {
        OP_COV(OPC_NEXT_DEFAULT);
        t->npts = op_npts(*t->p1);
        p = ++t->p1;
    } else {
        OP_COV(OPC_NEXT_NONE);
        t->tleft += t->seg->dur;
        t->seg->mask ^= mbit;
        t->seg->feat ^= fbit;
        return 0;
    }
    t->tleft += op_tcode(*p);
    return 1;
}

/* 0x1001cbb0: run the A1 handler of every parameter still moving (delta != 0).
 * outval[0] = 2, or 3 when an amplitude handler starts a new point. feac forces
 * delta = 1 first. */
void op_param_dispatch(Core *c)
{
    Track *t;
    short i;

    c->outval[0] = 2;
    for (i = 1; i < c->ntotal; i++) {
        t = &c->tracks[i];
        if (t->delta == 0)
            continue;
        if (c->feac != 0)
            t->delta = 1;
#ifdef CW_COVERAGE
        if (c->A1[i] / 4 >= 1 && c->A1[i] / 4 <= 32)
            op_type_count[c->A1[i] / 4]++;
#endif
        switch (c->A1[i] / 4) {
        case 1: op_t01_amp_rates(c, t, i); break;
        case 2: op_t02_formant_rates(c, t, i); break;
        case 3: op_t03_f1_rate(c, t, i); break;
        case 4: case 5: case 6: case 7: case 10: case 16: op_t04_smooth(c, t, i); break;
        case 8: op_t08_amp_decay(c, t, i); break;
        case 9: op_t09_amp_decay(c, t, i); break;
        case 11: case 32: op_t0b_cosine(c, t, i); break;
        case 12: op_t0c_linear(c, t, i); break;
        case 13: op_t0d_amp_rates(c, t, i); break;
        case 14: op_t0e_formant_rates(c, t, i); break;
        case 15: op_t0f_f1_rate(c, t, i); break;
        case 18: case 19: case 20: case 21: case 28: op_t12_approach(c, t, i); break;
        case 22: op_t16_amp_approach(c, t, i); break;
        case 23: op_t17_amp_approach(c, t, i); break;
        case 24: op_t18_set_rates4(c, t, i); break;
        case 25: op_t19_set_rate(c, t, i); break;
        case 26: op_t1a_set_rates(c, t, i); break;
        default: break;
        }
    }
}

/* 0x1001cd70..0x1001cdf0: meta-parameters, value + voice -> A2 rates of other params */
void op_t01_amp_rates(Core *c, Track *t, short i)
{
    short v;

    t->delta = 0;
    v = c->voice[i] + t->target;
    c->A2[21] = v;
    c->A2[22] = v;
    c->A2[23] = v;
    c->A2[24] = v;
}

void op_t02_formant_rates(Core *c, Track *t, short i)
{
    short v;

    t->delta = 0;
    v = c->voice[i] + t->target;
    c->A2[7] = v;
    c->A2[9] = v;
    c->A2[11] = v;
    c->A2[13] = v;
}

void op_t03_f1_rate(Core *c, Track *t, short i)
{
    t->delta = 0;
    c->A2[5] = c->voice[i] + t->target;
}

/* value * voice / 64 */
static short scaled(Core *c, Track *t, short i)
{
    return (short)(c->voice[i] * t->target / 64);
}

/* 0x1001ce20..0x1001cee0: the same, value scaled by voice/64 and clamped */
void op_t0d_amp_rates(Core *c, Track *t, short i)
{
    short v;

    t->delta = 0;
    v = scaled(c, t, i);
    if (v <= 0)
        v = 0;
    else if (v > 0xf0)
        v = 0xf0;
    c->A2[21] = v;
    c->A2[22] = v;
    c->A2[23] = v;
    c->A2[24] = v;
}

void op_t0e_formant_rates(Core *c, Track *t, short i)
{
    short v;

    t->delta = 0;
    v = scaled(c, t, i);
    if (v <= 0)
        v = 4;
    else if (v > 0xff)
        v = 0xff;
    c->A2[7] = v;
    c->A2[9] = v;
    c->A2[11] = v;
    c->A2[13] = v;
}

void op_t0f_f1_rate(Core *c, Track *t, short i)
{
    short v;

    t->delta = 0;
    v = scaled(c, t, i);
    if (v <= 0)
        v = 4;
    else if (v > 0xff)
        v = 0xff;
    c->A2[5] = v;
}

/* 0x1001cf40: types 4-7, 10, 16. Rate-limited approach: phase is a speed (0x10..0x7f,
 * reset /256 or negated when delta changes sign); each frame delta shrinks by
 * A2*phase/16*delta/2048 (rounded away from zero). Then the type's output mapping. */
void op_t04_smooth(Core *c, Track *t, short i)
{
    short delta = t->delta, v;
    int x;

    if (t->phase < 0) {
        if (delta >= 0)
            t->phase = t->phase / 256;
        t->phase = -t->phase;
    } else if (delta < 0) {
        t->phase = t->phase / 256;
    }
    if (t->phase > 0x7f)
        t->phase = 0x7f;
    else if (t->phase == 0)
        t->phase = 0x10;
    x = c->A2[i] * t->phase / 16 * delta;
    x += x < 0 ? -0x800 : 0x800;
    t->phase = (short)(x / 128);
    t->delta -= (short)(x / 2048);
    if (t->delta == 0)
        t->phase = 0;
    v = t->target - t->delta;
    if (v < 0)
        v = 0;
    switch (c->A1[i] / 4) {
    case 4: case 5: op_out_freq(c, v, i); return;
    case 6: op_out_div16(c, v, i); return;
    case 7: op_out_amp2(c, v, i); return;
    case 10: op_out_amp(c, v, i); return;
    case 16: op_out_offset(c, v, i); return;
    default: t->delta = 0; return;
    }
}

/* amplitude decay shared by types 8 and 9: delta' = A2*(delta+1)/128, output target -
 * delta' (a negative difference outputs 0 and keeps it as the new delta), + voice,
 * clamped 0..31; a 0 output stays 0 */
static void amp_decay(Core *c, Track *t, short i, short q)
{
    short target = t->target, out;

    if (target - q < 0) {
        q = target - q;
        out = 0;
    } else {
        out = target - q;
    }
    t->delta = q;
    if (q == 0)
        out = target;
    c->outval[i] = out;
    if (out != 0) {
        out += c->voice[i];
        if (out <= 0)
            out = 0;
        if (out > 0x1f)
            out = 0x1f;
        c->outval[i] = out;
    }
}

/* 0x1001d0e0: type 8 (16-bit product); outval[0] = 3 on a fresh point */
void op_t08_amp_decay(Core *c, Track *t, short i)
{
    if (t->target == t->delta)
        c->outval[0] = 3;
    amp_decay(c, t, i, (short)((short)(c->A2[i] * (short)(t->delta + 1)) / 128));
}

/* 0x1001d180: type 9 (32-bit product) */
void op_t09_amp_decay(Core *c, Track *t, short i)
{
    amp_decay(c, t, i, (short)(c->A2[i] * (t->delta + 1) / 128));
}

/* F0-style output: optional remap, voice scaling around 100, + A3, halved, 15..255 */
static void out_f0(Core *c, short v, short i)
{
    short o;

    if (c->A1[i] == 0x80)
        v = v < 0x103 ? OP_VALMAP[v] : OP_VALMAP[0x103];
    v = (short)(c->voice[i] * (v - 100) / 64);
    o = (short)((c->A3[i] + v + 100) / 2);
    c->outval[i] = o;
    if (o < 0xf)
        c->outval[i] = 0xf;
    else if (o > 0xff)
        c->outval[i] = 0xff;
}

/* 0x1001d210: types 11, 32 (F0). Cosine interpolation from the current point to the
 * next over tleft frames; with no next point the target is held (delta stays 0x100 so
 * the handler keeps running). */
void op_t0b_cosine(Core *c, Track *t, short i)
{
    unsigned short *p;

    if (--t->cnt <= 0) {
        if (t->npts == 0) {
            t->delta = 0x100;
            out_f0(c, t->target, i);
            return;
        }
        p = t->p0;
        t->cnt = t->tleft;
        if (p == NULL)
            p = t->p1;
        t->delta = 0x100;
        t->phase = 0;
        t->span = (short)((*p & 0xff) - t->target);
        t->phase_inc = (short)(0x4000 / t->tleft);
    } else {
        t->phase += t->phase_inc;
    }
    out_f0(c, (short)(OP_COSINE[t->phase / 128] * t->span / 0x7f00 + t->target), i);
}

/* 0x1001d400: type 12. Linear interpolation in 1/128 steps; output (voice-scaled
 * around 100, + A3 + 100) / 2, 15..255. */
void op_t0c_linear(Core *c, Track *t, short i)
{
    unsigned short *p;
    short v;

    if (--t->cnt <= 0) {
        t->phase = (short)(t->target << 7);
        if (t->npts == 0) {
            t->delta = 0x100;
        } else {
            p = t->p0;
            t->cnt = t->tleft;
            if (p == NULL)
                p = t->p1;
            t->delta = 0x100;
            t->phase_inc = (short)((short)(((*p & 0xff) - t->target) << 7) / t->tleft);
        }
    } else {
        t->phase += t->phase_inc;
    }
    v = (short)((short)(c->voice[i] * (short)(t->phase / 128 - 100)) / 64);
    v = (short)(v + (short)(c->A3[i] + 100));
    if (v < 0x1e)
        v = 0x1e;
    else if (v > 0x1ff)
        v = 0x1ff;
    c->outval[i] = v / 2;
}

/* 0x1001d4f0: quarter-tone index: v/4 + voice + A3, 0..127 */
void op_out_freq(Core *c, short v, short i)
{
    short o = (short)(v / 4 + (short)(c->voice[i] + c->A3[i]));

    c->outval[i] = o;
    if (o <= 0)
        c->outval[i] = 0;
    else if (o > 0x7f)
        c->outval[i] = 0x7f;
}

/* 0x1001d550: (voice + v)/16, >= 0 */
void op_out_div16(Core *c, short v, short i)
{
    short o = (short)((c->voice[i] + v) / 16);

    c->outval[i] = o;
    if (o < 0)
        c->outval[i] = 0;
}

/* 0x1001d590: voice + v/8, 0..31 */
void op_out_amp(Core *c, short v, short i)
{
    short o = (short)(c->voice[i] + (short)(v / 8));

    c->outval[i] = o;
    if (o < 0)
        c->outval[i] = 0;
    else if (o >= 0x20)
        c->outval[i] = 0x1f;
}

/* 0x1001d5e0: sets A3 of parameter ntotal - A3[i]/2 (FD: the formant offset) */
void op_out_offset(Core *c, short v, short i)
{
    short k = (short)(c->ntotal - (short)(c->A3[i] / 2));

    c->A3[k] = (short)(c->voice[i] + v - 100);
}

/* 0x1001d620: (v + voice)/8, 0..31 */
void op_out_amp2(Core *c, short v, short i)
{
    short o = (short)(v + c->voice[i]);

    if (o < 0) {
        c->outval[i] = 0;
        return;
    }
    o = o / 8;
    c->outval[i] = o;
    if (o >= 0x20)
        c->outval[i] = 0x1f;
}

/* first-order approach shared by types 18-23, 28: delta -= step*rate[A2]/128 (at least
 * 1 in step's direction), stopping at 0 */
static void approach(Core *c, Track *t, short i)
{
    short step = t->step, q;

    if (step == 0) {
        t->delta = 0;
        return;
    }
    q = (short)(OP_RATE[c->A2[i]] * step / 128);
    if (step > 0) {
        if (q == 0)
            q = 1;
        t->delta -= q;
        if (t->delta < 0)
            t->delta = 0;
    } else {
        if (q == 0)
            q = -1;
        t->delta -= q;
        if (t->delta > 0)
            t->delta = 0;
    }
}

/* 0x1001d670: types 18-21, 28 */
void op_t12_approach(Core *c, Track *t, short i)
{
    short v;

    approach(c, t, i);
    v = t->target - t->delta;
    switch (c->A1[i] / 4) {
    case 18: case 19: op_out_freq(c, v, i); return;
    case 20: op_out_amp(c, v, i); return;
    case 21: op_out_offset(c, v, i); return;
    case 28: op_out_amp2(c, v, i); return;
    default: t->delta = 0; return;
    }
}

/* amplitude output of types 22/23: + voice, 0..31; 0 stays 0 */
static void amp_out(Core *c, short i)
{
    short o = c->outval[i];

    if (o == 0)
        return;
    o = (short)(c->voice[i] + o);
    c->outval[i] = o;
    if (o < 0)
        c->outval[i] = 0;
    else if (o > 0x1f)
        c->outval[i] = 0x1f;
}

/* 0x1001d790: type 22; outval[0] = 3 on a fresh point */
void op_t16_amp_approach(Core *c, Track *t, short i)
{
    if (t->target == t->delta)
        c->outval[0] = 3;
    approach(c, t, i);
    c->outval[i] = t->delta == 0 ? t->target : (short)(t->target - t->delta);
    amp_out(c, i);
}

/* 0x1001d8a0: type 23 */
void op_t17_amp_approach(Core *c, Track *t, short i)
{
    approach(c, t, i);
    c->outval[i] = t->delta == 0 ? t->target : (short)(t->target - t->delta);
    amp_out(c, i);
}

/* index of the parameter a type 24-26 meta-parameter points at: ntotal - A3[i]/2 */
static short target_param(Core *c, short i)
{
    return (short)(c->ntotal - (short)(c->A3[i] / 2));
}

/* 0x1001d990: type 24: A2 of params k, k-1, k-2, k-3 = value */
void op_t18_set_rates4(Core *c, Track *t, short i)
{
    short k;

    t->delta = 0;
    k = target_param(c, i);
    c->A2[k] = t->target;
    c->A2[(short)(k - 1)] = t->target;
    c->A2[(short)(k - 2)] = t->target;
    c->A2[(short)(k - 3)] = t->target;
}

/* 0x1001da00: type 26: A2 of params k, k-2, k-4, k-6 = value, and each one's step =
 * its current delta (restarting its approach from where it is) */
void op_t1a_set_rates(Core *c, Track *t, short i)
{
    short k;
    int j;

    t->delta = 0;
    k = target_param(c, i);
    for (j = 0; j <= 6; j += 2) {
        c->A2[(short)(k - j)] = t->target;
        c->tracks[k - j].step = c->tracks[k - j].delta;
    }
}

/* 0x1001daa0: type 25: A2 of param k = value, step = delta */
void op_t19_set_rate(Core *c, Track *t, short i)
{
    short k;

    t->delta = 0;
    k = target_param(c, i);
    c->A2[k] = t->target;
    c->tracks[k].step = c->tracks[k].delta;
}

/* 0x1001db00: find parameter i's item on segment s (items sorted by descending signed
 * id; a lower id ends the search). Sets p0 (header, or NULL), p1 = end of the node's
 * item data, p2 = the next block's data. Returns 1 if found. */
int op_track_has_item(Core *c, Seg *s, short i)
{
    Track *t = &c->tracks[i];
    short key = (short)((i & 0xff) << 8);
    unsigned short *b;
    short id;

    t->p2 = BLK_DATA(s->ext);
    t->p1 = s->items + s->items_len / 2;
    t->p0 = s->items;
    for (;;) {
        while (t->p0 < t->p1) {
            id = (short)(*t->p0 & 0xff00);
            if (id == key)
                return 1;
            if (id < key) {
                t->p0 = NULL;
                return 0;
            }
            t->p0 += op_npts(*t->p0) + 1;
        }
        b = t->p2;
        if (DATA_BLK(b) == NULL)
            break;
        OP_COV(OPC_BLOCK_STEP);
        t->p0 = b + (short)WDIFF(t->p0, t->p1);
        t->p1 = b + BLK_LEN(DATA_BLK(b)) / 2;
        t->p2 = BLK_DATA(DATA_BLK(b)->next);
    }
    t->p0 = NULL;
    return 0;
}

/* 0x1001dbb0: find parameter i in s's phoneme record (ascending ids; a higher id ends
 * the search). p1 = the item (or where the search stopped), p2 = record end. */
int op_track_has_default(Core *c, Seg *s, short i)
{
    Track *t = &c->tracks[i];
    short key = (short)((i & 0xff) << 8);
    unsigned short *rec = c->phinv + s->ph / 2;
    unsigned short *end;
    short id;

    t->p1 = rec;
    end = rec + (short)*rec / 2;
    t->p1 = rec + 6;
    t->p2 = end;
    if (t->p1 >= end)
        return 0;
    do {
        id = (short)(*t->p1 & 0xff00);
        if (id == key)
            return 1;
        if (id > key)
            return 0;
        t->p1 += op_npts(*t->p1) + 1;
    } while (t->p1 < end);
    return 0;
}
