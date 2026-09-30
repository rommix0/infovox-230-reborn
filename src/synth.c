/* Formant synthesizer: C port of Ivx230nt.dll 0x10022cb0-0x100250ff (tag src_synth).
 *
 * OVE III-style cascade/parallel formant synthesizer (REFERENCE.md §3). Every function
 * follows the original's control flow and integer semantics; the address of the original
 * is given above each one. The sample loops were hand-written inline asm in the DLL and
 * are reproduced bit-exactly, including two quirks:
 *   - resonator products use `imul; rcl edx,1`, whose low bit is the imul overflow flag
 *     rather than bit 31 of the product (q31_rcl below);
 *   - all intermediate sums wrap at 32 bits.
 * Table indices from frame values (amp_db[A0], res_b[F1], bw_b_ratio[B1], ...) are unchecked in
 * the original; here they are clamped to the tables (a guard, see synt_set_resonator).
 */
#include <math.h>
#include <stdint.h>
#include <string.h>
#include "synth.h"

/* floating-point constants (.rdata), values exactly as stored */
#define K_LP0     (-1256.63706144)   /* 0x10040fd0: -2pi*200 */
#define K_ONE     1.0                /* 0x10040fd8 */
#define K_INT_MAX 2147483647.0       /* 0x10040fe0 */
#define K_LP1     (-6283.1853072)    /* 0x10040fe8: -2pi*1000 */
#define K_20      20.0               /* 0x10041000 */
#define K_32767   32767.0            /* 0x10041008 */
#define K_2500    2500.0             /* 0x10041010: 50^2 */
#define K_NPI     (-3.1415926536)    /* 0x10041018 */
#define K_2PI     6.2831853072       /* 0x10041020 */
#define K_N2PI    (-6.2831853072)    /* 0x10041028 */

/* map indices (synth parameter order, REFERENCE.md §3.1) */
enum { P_F1, P_B1, P_F2, P_B2, P_F3, P_B3, P_F4, P_B4, P_FH, P_K1, P_C1, P_K2, P_C2,
       P_FN, P_BN, P_AK, P_AC, P_AH, P_AN, P_A0, P_F0 };

/* frame value of map entry i, or its default when unmapped */
static short map_get(const SynthTables *t, const short *frame, int i)
{
    const SynthMapEntry *e = &t->map[i];
    return e->frame_index == -1 ? e->def : frame[e->frame_index];
}

/* amp_db[frame value], clamped to the table (the guard in synt_set_resonator) */
static short amp_get(const SynthTables *t, const short *frame, int i)
{
    short v = map_get(t, frame, i);

    return t->amp_db[v < 0 ? 0 : v > 31 ? 31 : v];
}

/* 0x10022cb0 */
int synt_nop(void)
{
    return 0;
}

/* 0x10022cb7: unreferenced */
int synt_stub_cb7(void)
{
    return 0;
}

/* 0x10022cbe: unreferenced */
int synt_stub_cbe(void)
{
    return 0;
}

/* 0x10022cc5 */
int synt_ctrl(Synth *s, short cmd, void *arg)
{
    int v = (int)(intptr_t)arg;

    switch (cmd) {
    case 0: case 1: case 2: case 3: case 4: case 5: case 6: case 7: case 8:
        return synt_create((Synth **)s, cmd);
    case 0x09:
        s->get_frame = (short *(*)(void *))arg;
        if (arg == NULL)
            return -4;
        break;
    case 0x0a:
        s->get_frame_arg = arg;
        if (arg == NULL)
            return -5;
        break;
    case 0x0b:
        synt_set_param_list(s, (short *)arg);
        break;
    case 0x0d:
        s->map_sel = v;
        synt_select_map(s);
        synt_init_lowpass(s);
        break;
    case 0x0e:
        if (v < 2)
            v = 2;
        if (v > 1000)
            v = 1000;
        s->frame_rate = (short)v;
        break;
    case 0x0f:
        s->frame_left = 0;
        break;
    case 0x10:
        s->sample_fmt = v;
        break;
    case 0x11:
        if (s->type == 1 && v >= 16000) {
            s->half_rate = 1;
            v /= 2;
        } else {
            s->half_rate = 0;
        }
        s->fs = v;
        synt_build_tables(s->tab, v);
        synt_init_lowpass(s);
        break;
    case 0x12:
        *(short *)arg = s->produced;
        break;
    case 0x13:
        return synt_destroy(s);
    default:
        return -12;
    }
    return 0;
}

/* 0x10022e8f */
int synt_create(Synth **out, short type)
{
    Synth *s;
    int r;

    s = mem_calloc(1, sizeof(Synth));   /* 0x3c */
    if (s == NULL)
        return -2;
    s->started = 0;
    s->type = type;
    s->get_frame = NULL;
    s->get_frame_arg = NULL;
    s->map_sel = 0;
    s->f14 = 0;
    s->frame_rate = 100;
    s->fs = 11025;
    s->sample_fmt = 1;
    s->half_rate = 0;
    r = synt_alloc_state(s);
    if (r == 0) {
        synt_nop();
        *out = s;
        return 0;
    }
    *out = NULL;
    mem_free(s);
    return r;
}

/* 0x10022f62: cmd 0x0b. arg[0] < 0: bind -arg[0] codes; otherwise 25. */
int synt_set_param_list(Synth *s, short *arg)
{
    short n = arg[0];

    s->plist = arg;
    if (n < 0) {
        synt_bind_params(s, (short)-n);
    } else {
        s->f14 = 0;
        synt_bind_params(s, 25);
    }
    return 0;
}

/* 0x10022fb4: fill buf with n samples, pulling a frame from get_frame every fs/frame_rate
 * samples (twice that at half rate). A NULL frame yields a frame of silence (zero bytes).
 * Returns the number of synthesized (non-silence) samples. Called by the engine at 0x10011fbb. */
short synt_generate(Synth *s, void *buf, short n)
{
    unsigned char *p = buf;
    short left = n;
    short done = 0;
    short bps = (short)(((s->sample_fmt & 0xf) != 0) + 1);
    short k, m;
    short *frame;

    s->produced = 0;
    do {
        if (s->frame_left == 0 || s->started == 0) {
            s->frame_left = (short)(s->fs / s->frame_rate);
            if (s->half_rate != 0)
                s->frame_left = (short)(s->frame_left << 1);
            frame = s->get_frame(s->get_frame_arg);
            if (frame == NULL)
                s->frame_left = (short)-s->frame_left;
            else
                s->frame_fn(s, frame);
        }
        if (s->frame_left > 0) {
            k = s->frame_left;
            if (k > left)
                k = left;
            s->frame_left = (short)(s->frame_left - k);
            left = (short)(left - k);
            s->samples_fn(s, (short *)p, k);
            done = (short)(done + k);
            s->produced = done;
            p += k * bps;
        } else {
            k = (short)-s->frame_left;
            if (k > left)
                k = left;
            s->frame_left = (short)(s->frame_left + k);
            left = (short)(left - k);
            for (m = (short)(k * bps); m != 0; m--)
                *p++ = 0;
        }
    } while (left != 0);
    return done;
}

/* 0x10023180 */
int synt_destroy(Synth *s)
{
    synt_free_state(s);
    mem_free(s);
    return 0;
}

/* 0x100231a0 */
int synt_alloc_state(Synth *s)
{
    SynthTables *t = mem_calloc(1, sizeof(SynthTables));   /* 0x2c */
    SynthRun *run = mem_calloc(1, sizeof(SynthRun));       /* 0x24 */

    s->f06 = synth_default_f06;
    s->plist = NULL;
    s->tab = t;
    s->frame_left = 0;
    t->run = run;
    t->silent = 0;
    t->plist_n = 0;
    t->plist_buf = NULL;
    t->map = mem_calloc(1, sizeof(synth_map_mode0));      /* 0x100 */
    synt_select_map(s);
    if (s->type == 0) {
        run->coef_size = sizeof(FrameCoef0);               /* 0xa0 */
        run->coef = mem_calloc(1, sizeof(FrameCoef0));
        run->sstate = mem_calloc(1, sizeof(SynthState0));  /* 0x5c */
        synt_init_lowpass(s);
        run->coef->noise_poly = 0x1d87;
        run->coef->noise_seed = 0xfff8;
        run->sstate->noise = 0xfff8;
        run->sstate->pcount = 0x200;
    } else if (s->type == 1) {
        run->coef_size = sizeof(FrameCoef1);               /* 0x7c */
        run->coef = mem_calloc(1, sizeof(FrameCoef1));
        run->sstate = mem_calloc(1, 0x44);
        synt_init_lowpass(s);
        run->coef->noise_poly = 0x1d87;
        run->coef->noise_seed = 0xfff8;
        run->sstate->noise = 0xfff8;
        run->sstate->pcount = 0x200;
    } else if (s->type == 2) {
        return -1;   /* note: t, run and the map leak, as in the original */
    }
    synt_build_tables(t, s->fs);
    run->flags = 0x4c;
    return 0;
}

/* 0x1002335b */
int synt_select_map(Synth *s)
{
    if (s->map_sel == 0)
        memcpy(s->tab->map, synth_map_mode0, sizeof(synth_map_mode0));
    else
        memcpy(s->tab->map, synth_map_alt, sizeof(synth_map_alt));
    return 0;
}

/* 0x100233ab: select the frame/sample functions for the synth type and set the output
 * one-pole coefficient: 200 Hz (mode 0) or 1000 Hz (mode 1). */
int synt_init_lowpass(Synth *s)
{
    SynthRun *run = s->tab->run;

    if (s->type == 0) {
        s->frame_fn = synt_frame_mode0;
        s->samples_fn = s->fs == 16000 ? synt_samples_mode0_16k : synt_samples_mode0;
        run->coef->lp_coef = (int)((K_ONE - exp(K_LP0 / s->fs)) * K_INT_MAX);
    } else if (s->type == 1) {
        s->frame_fn = synt_frame_mode1;
        s->samples_fn = synt_samples_mode1;
        ((FrameCoef1 *)run->coef)->lp_coef = (int)((K_ONE - exp(K_LP1 / s->fs)) * K_INT_MAX);
    } else if (s->type == 2) {
        return -1;
    }
    return 0;
}

/* 0x100234a4 */
int synt_build_tables(SynthTables *t, int fs)
{
    if (t->amp_db != NULL)
        mem_free(t->amp_db);
    t->amp_db = synt_mk_amp_table();
    if (t->res_b != NULL)
        mem_free(t->res_b);
    t->res_b = synt_mk_res_b_table(fs);
    if (t->bw_b_ratio != NULL)
        mem_free(t->bw_b_ratio);
    t->bw_b_ratio = synt_mk_bw_b_ratio(fs);
    if (t->res_c != NULL)
        mem_free(t->res_c);
    t->res_c = synt_mk_res_c_table(fs);
    if (t->bw_c_ratio != NULL)
        mem_free(t->bw_c_ratio);
    t->bw_c_ratio = synt_mk_bw_c_ratio(fs);
    return fs;
}

/* 0x10023577: point every map entry at its code's position in s->plist (-1 if absent). */
void synt_bind_params(Synth *s, short n)
{
    SynthTables *t = s->tab;
    unsigned i;

    for (i = 0; i < 32; i++)
        t->map[i].frame_index = synt_find_param(s->plist, n, &t->map[i]);
    t->plist_n = n;
    t->plist_buf = mem_calloc(n, 2);   /* the previous buffer leaks, as in the original */
}

/* 0x100235fa: codes are big-endian char pairs starting at list[2]. The original takes a
 * pointer to a {list, n, entry} block on the caller's stack. */
short synt_find_param(const short *list, short n, const SynthMapEntry *e)
{
    int i;

    for (i = 2; i < n; i++)
        if (e->name[0] == (signed char)(list[i] >> 8) && e->name[1] == (signed char)(list[i] & 0xff))
            return (short)i;
    return -1;
}

/* 0x10023691 */
int synt_free_state(Synth *s)
{
    SynthTables *t = s->tab;

    mem_free(t->amp_db);
    mem_free(t->res_b);
    mem_free(t->bw_b_ratio);
    mem_free(t->res_c);
    mem_free(t->bw_c_ratio);
    if (t->plist_buf != NULL)
        mem_free(t->plist_buf);
    mem_free(t->map);
    mem_free(t->run->coef);
    mem_free(t->run->sstate);
    mem_free(t->run);
    mem_free(t);
    return 0;
}

/* 0x10023760: mode-1 sample function, an empty stub in the DLL */
int synt_samples_mode1(Synth *s, short *buf, short n)
{
    (void)s; (void)buf; (void)n;
    return 0;
}

/* 0x100237a0: Klatt-normalised two-pole coefficients scaled by 2^(shift-15) */
void synt_set_resonator(SynthTables *t, Resonator *r, short f, short b, char shift)
{
    int sh = 15 - shift;

    /* GUARD, not in the DLL: the frame's frequency and bandwidth indices go unchecked into
     * the 128-entry res_b/res_c and 32-entry bw tables. Voices 2 and 4 drive K1/K2 to -6
     * for a few frames in 9 of the 12 languages, so the DLL reads 12 bytes before res_b and
     * res_c: its CRT packs the tables back to back, so normally the tail of amp_db and of
     * bw_b_ratio, but 0 when res_c lands in a reused block (heap history decides). The
     * port's calloc'd tables can sit at the start of a page, and the read faults. Those
     * frames all have AC = 0, so the frication branch that uses K1/K2 is off and its state
     * cleared (synt_samples_mode0); clamping changes no audio there. */
    if (f < 0)
        f = 0;
    else if (f > 127)
        f = 127;
    if (b < 0)
        b = 0;
    else if (b > 31)
        b = 31;
    r->b = (int)t->res_b[f] * (int)t->bw_b_ratio[b] * 2 >> sh;
    r->c = (int)t->res_c[f] * (int)t->bw_c_ratio[b] * 2 >> sh;
    /* wraps near fs/2 (b << 0), as the original's 32-bit subtraction does */
    r->a = (int)((uint32_t)(0x7fffffff >> sh) - (uint32_t)r->b - (uint32_t)r->c - (uint32_t)r->b);
}

/* 0x10023846: per-frame coefficient update, mode 0 */
int synt_frame_mode0(Synth *s, short *frame)
{
    SynthTables *t = s->tab;
    SynthRun *run = t->run;
    FrameCoef0 *c = run->coef;
    int sound, period, f0;
    short fh;

    c->g_voice = amp_get(t, frame, P_A0);
    c->g_asp = amp_get(t, frame, P_AH) / 4;
    c->g_nasal = amp_get(t, frame, P_AN) / 2;
    c->fric_mix = amp_get(t, frame, P_AK);
    c->g_fric = amp_get(t, frame, P_AC) / 4;

    run->voiced = c->g_voice != 0 || c->g_nasal != 0;
    run->flags &= 0xfffb;
    if (t->silent != 0 && (c->g_asp != 0 || c->g_fric != 0))
        run->flags |= 4;   /* noise onset after silence */
    sound = c->g_voice != 0 || c->g_asp != 0 || c->g_nasal != 0 || c->g_fric != 0;
    if (sound) {
        t->silent = 0;
        run->flags &= 0xffef;
    } else {
        t->silent = 1;
        run->flags |= 0x10;
    }

    synt_set_resonator(t, &c->r_f1, map_get(t, frame, P_F1), map_get(t, frame, P_B1), 14);
    synt_set_resonator(t, &c->r_f2, map_get(t, frame, P_F2), map_get(t, frame, P_B2), 14);
    synt_set_resonator(t, &c->r_f3, map_get(t, frame, P_F3), map_get(t, frame, P_B3), 13);
    synt_set_resonator(t, &c->r_f4, map_get(t, frame, P_F4), map_get(t, frame, P_B4), 13);
    fh = map_get(t, frame, P_FH);
    synt_set_resonator(t, &c->r_fh3, fh, 29, 12);   /* +3320 Hz */
    synt_set_resonator(t, &c->r_fh2, fh, 25, 12);   /* +1570 Hz */
    synt_set_resonator(t, &c->r_fh1, fh, 10, 11);   /* +320 Hz */
    synt_set_resonator(t, &c->r_nasal, map_get(t, frame, P_FN), map_get(t, frame, P_BN), 14);
    synt_set_resonator(t, &c->r_k1, map_get(t, frame, P_K1), map_get(t, frame, P_C1), 12);
    synt_set_resonator(t, &c->r_k2, map_get(t, frame, P_K2), map_get(t, frame, P_C2), 12);

    f0 = 10;
    if (run->voiced != 0) {
        int v = map_get(t, frame, P_F0);
        if (v > 9)
            f0 = v;
    }
    period = s->fs / f0;
    c->half_period = period / 2;   /* impulses every fs/F0/2 samples: pitch = 2*F0 */
    c->samples_per_frame = s->fs / s->frame_rate;
    run->period_next = run->period_acc + period;
    run->period_acc += period;
    run->flags &= 0xfffe;
    return 0;
}

/* 0x10023fbb: per-frame coefficient update, mode 1 (three cascade formants, no F4/K2) */
int synt_frame_mode1(Synth *s, short *frame)
{
    SynthTables *t = s->tab;
    SynthRun *run = t->run;
    FrameCoef1 *c = (FrameCoef1 *)run->coef;
    short f0, period;

    c->g_voice = amp_get(t, frame, P_A0);
    c->g_asp = amp_get(t, frame, P_AH) / 4;
    c->g_nasal = amp_get(t, frame, P_AN) / 2;
    c->fric_mix = amp_get(t, frame, P_AK);
    c->g_fric = amp_get(t, frame, P_AC) / 4;

    run->voiced = c->g_voice != 0 || c->g_nasal != 0;
    run->flags &= 0xfffb;
    if (t->silent != 0 && (c->g_asp != 0 || c->g_fric != 0))
        run->flags |= 4;
    if (c->g_voice != 0 || c->g_asp != 0 || c->g_nasal != 0 || c->g_fric != 0)
        t->silent = 0;
    else
        t->silent = 1;

    synt_set_resonator(t, &c->r_f1, map_get(t, frame, P_F1), map_get(t, frame, P_B1), 14);
    synt_set_resonator(t, &c->r_f2, map_get(t, frame, P_F2), map_get(t, frame, P_B2), 13);
    synt_set_resonator(t, &c->r_f3, map_get(t, frame, P_F3), map_get(t, frame, P_B3), 12);
    synt_set_resonator(t, &c->r_fh, (short)(map_get(t, frame, P_FH) - 4), 10, 12);
    synt_set_resonator(t, &c->r_nasal, map_get(t, frame, P_FN), map_get(t, frame, P_BN), 14);
    synt_set_resonator(t, &c->r_k1, map_get(t, frame, P_K1), map_get(t, frame, P_C1), 12);

    f0 = 10;
    if (run->voiced != 0) {
        short v = map_get(t, frame, P_F0);
        if (v > 9)
            f0 = v;
    }
    period = (short)(s->fs / f0);
    c->half_period = period / 2;
    c->samples_per_frame = s->fs / s->frame_rate;
    run->period_next = run->period_acc + period;
    run->period_acc += period;
    run->flags &= 0xfffe;
    return 0;
}

/* ---- mode-0 sample loop (0x100245e0 / 0x10024940, originally inline asm) ---- */

/* imul r/m32; rcl edx,1 -- about (a*b)>>31, but bit 0 is imul's overflow flag */
static int32_t q31_rcl(int32_t a, int32_t b)
{
    int64_t p = (int64_t)a * b;
    return (int32_t)(((uint32_t)(p >> 32) << 1) | (uint32_t)(p != (int32_t)p));
}

/* two-pole resonator y = a*x + c*y2 + 2b*y1, output << shift; st = {y2, y1} */
static int32_t resonate(const Resonator *r, int32_t *st, int32_t x, int shift)
{
    uint32_t acc, t;

    acc = (uint32_t)q31_rcl(x, r->a);
    acc += (uint32_t)q31_rcl(st[0], r->c);
    t = (uint32_t)q31_rcl(st[1], r->b);
    st[0] = st[1];
    acc += t + t;
    st[1] = (int32_t)(acc << shift);
    return st[1];
}

/* fric_shift: noise gain pre-shift (2, or 1 at 16 kHz); out_gain: 4, or 3 at 16 kHz */
static int samples_mode0(Synth *s, short *buf, short n, int fric_shift, int out_gain)
{
    SynthRun *run = s->tab->run;
    SynthState0 *st = run->sstate;
    const FrameCoef0 *c = run->coef;
    short i;

    for (i = 0; i < n; i++) {
        int32_t imp = 0, noise, cnt, x, y, d, o;
        uint32_t out = 0;
        uint16_t nz;

        /* impulse source */
        cnt = st->pcount + 1;
        if (cnt >= c->half_period) {
            imp = 0x7fff;
            cnt = 0;
        }
        st->pcount = cnt;

        /* 16-bit Galois LFSR noise */
        nz = (uint16_t)(st->noise << 1);
        if ((int16_t)nz < 0)
            nz ^= (uint16_t)c->noise_poly;
        st->noise = nz;
        noise = (int16_t)nz >> 2;

        /* voice branch: impulse*A0 + noise*AH through the 7-resonator cascade */
        if ((c->g_voice | c->g_asp) != 0) {
            x = (int32_t)(((uint32_t)imp * (uint32_t)c->g_voice
                           + (uint32_t)noise * (uint32_t)c->g_asp) << 1) >> 4;
            y = resonate(&c->r_f1, &st->casc[0], x, 1);
            y = resonate(&c->r_fh1, &st->casc[2], y, 4);
            y = resonate(&c->r_fh2, &st->casc[4], y, 3);
            y = resonate(&c->r_f2, &st->casc[6], y, 1);
            y = resonate(&c->r_fh3, &st->casc[8], y, 3);
            y = resonate(&c->r_f4, &st->casc[10], y, 2);
            y = resonate(&c->r_f3, &st->casc[12], y, 2);
            out = (uint32_t)y;
        } else {
            memset(st->casc, 0, sizeof(st->casc));
        }

        /* nasal branch: impulse*AN through FN, in parallel */
        if (c->g_nasal != 0) {
            x = (int32_t)(((uint32_t)imp * (uint32_t)c->g_nasal) << 1) >> 4;
            out += (uint32_t)resonate(&c->r_nasal, st->nasal, x, 1);
        } else {
            memset(st->nasal, 0, sizeof(st->nasal));
        }

        /* fricative branch: noise*AC through K2 then K1; AK mixes K1 back out */
        if (c->g_fric != 0) {
            int32_t y1, y2;
            x = (int32_t)(((uint32_t)noise * (uint32_t)c->g_fric) << fric_shift) >> 4;
            y1 = resonate(&c->r_k2, &st->fric[0], x, 3);
            out += (uint32_t)y1;
            y2 = resonate(&c->r_k1, &st->fric[2], y1, 3);
            out += (((uint32_t)(y2 >> 16) * (uint32_t)c->fric_mix) << 1) - (uint32_t)y2;
        } else {
            memset(st->fric, 0, sizeof(st->fric));
        }

        /* output one-pole low-pass, clip, 16-bit */
        d = (int32_t)(out * (uint32_t)out_gain - (uint32_t)st->lp);
        st->lp = (int32_t)((uint32_t)(int32_t)(((int64_t)d * c->lp_coef) >> 31) + (uint32_t)st->lp);
        o = (int32_t)((uint32_t)st->lp << 1);
        if (o <= -0x8000000)
            o = -0x8000000;
        else if (o > 0x7ffffff)
            o = 0x7ffffff;
        buf[i] = (short)(((uint32_t)o << 4) >> 16);
    }
    s->started = 2;
    return i;
}

/* 0x100245e0 */
int synt_samples_mode0(Synth *s, short *buf, short n)
{
    return samples_mode0(s, buf, n, 2, 4);
}

/* 0x10024940: fs == 16000 variant */
int synt_samples_mode0_16k(Synth *s, short *buf, short n)
{
    return samples_mode0(s, buf, n, 1, 3);
}

/* ---- coefficient tables (the only floating point in the DLL) ---- */

/* 0x10024ca0: amp[i] = 32767*10^((i-31)/20), amp[0] = 0 */
short *synt_mk_amp_table(void)
{
    short *tab = mem_calloc(32, 2);
    double v;
    int i;

    if (tab != NULL) {
        for (i = 0; i < 32; i++) {
            v = i != 0 ? pow(10.0, (double)(i - 31) / K_20) : 0.0;
            tab[i] = (short)(int)(K_32767 * v);
        }
    }
    return tab;
}

/* default bandwidth B(F) = sqrt((F/20)^2 + 50^2) Hz */
static double default_bw(double f)
{
    return sqrt(pow(f / K_20, 2.0) + K_2500);
}

/* 0x10024d34: 32767 * exp(-pi B/fs) * cos(2pi F/fs) */
short *synt_mk_res_b_table(int fs)
{
    short *tab = mem_calloc(128, 2);
    double f, b, e, co;
    int k;

    if (tab == NULL)
        return NULL;
    for (k = 0; k < 128; k++) {
        f = freq_index_hz[k];
        b = default_bw(f) / fs;
        f = f / fs;
        e = exp(K_NPI * b);
        co = cos(K_2PI * f);
        tab[k] = (short)(int)(co * e * K_32767);
    }
    return tab;
}

/* 0x10024e2f: -32767 * exp(-2pi B/fs) */
short *synt_mk_res_c_table(int fs)
{
    short *tab = mem_calloc(128, 2);
    double b;
    int k;

    if (tab == NULL)
        return NULL;
    for (k = 0; k < 128; k++) {
        b = default_bw(freq_index_hz[k]) / fs;
        tab[k] = (short)(int)(-exp(K_N2PI * b) * K_32767);
    }
    return tab;
}

/* 0x10024efc: 32767 * exp(-pi (B0+dB)/fs) / exp(-pi B0/fs), B0 = B(1000 Hz) */
short *synt_mk_bw_b_ratio(int fs)
{
    short *tab = mem_calloc(32, 2);
    double b0, e0, e1;
    int i;

    if (tab == NULL)
        return NULL;
    b0 = default_bw(1000.0);
    for (i = 0; i < 32; i++) {
        e0 = exp(K_NPI * b0 / fs);
        e1 = exp(((double)bw_delta_hz[i] + b0) * K_NPI / fs);
        tab[i] = (short)(int)(e1 / e0 * K_32767);
    }
    return tab;
}

/* 0x10024fe5: the same ratio for the -r^2 term */
short *synt_mk_bw_c_ratio(int fs)
{
    short *tab = mem_calloc(32, 2);
    double b0, e0, e1;
    int i;

    if (tab == NULL)
        return NULL;
    b0 = default_bw(1000.0);
    for (i = 0; i < 32; i++) {
        e0 = -exp(K_N2PI * b0 / fs);
        e1 = -exp(((double)bw_delta_hz[i] + b0) * K_N2PI / fs);
        tab[i] = (short)(int)(e1 / e0 * K_32767);
    }
    return tab;
}
