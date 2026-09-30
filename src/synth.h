/* Formant synthesizer (DLL 0x10022cb0-0x100250ff, src_synth). */
#ifndef SYNTH_H
#define SYNTH_H

#include "ivx.h"

/* synt_ctrl commands */
enum {
    SYNT_CREATE0      = 0x00,  /* 0x00..0x08: create, cmd = synth type; s is Synth ** */
    SYNT_SET_FRAMECB  = 0x09,  /* arg = short *(*)(void *) frame source */
    SYNT_SET_FRAMEARG = 0x0a,  /* arg = frame source argument */
    SYNT_SET_PARAMS   = 0x0b,  /* arg = short parameter-code list */
    SYNT_SET_MAP      = 0x0d,  /* arg = map_sel */
    SYNT_FRAME_RATE   = 0x0e,  /* arg = frames/s, 2..1000 */
    SYNT_FLUSH        = 0x0f,  /* drop the rest of the current frame */
    SYNT_SAMPLE_FMT   = 0x10,
    SYNT_SAMPLE_RATE  = 0x11,
    SYNT_GET_PRODUCED = 0x12,  /* arg = short * */
    SYNT_DESTROY      = 0x13
};

int synt_nop(void);
int synt_stub_cb7(void);
int synt_stub_cbe(void);
int synt_ctrl(Synth *s, short cmd, void *arg);
int synt_create(Synth **out, short type);
int synt_set_param_list(Synth *s, short *arg);
short synt_generate(Synth *s, void *buf, short n);
int synt_destroy(Synth *s);
int synt_alloc_state(Synth *s);
int synt_select_map(Synth *s);
int synt_init_lowpass(Synth *s);
int synt_build_tables(SynthTables *t, int fs);
void synt_bind_params(Synth *s, short n);
short synt_find_param(const short *list, short n, const SynthMapEntry *e);
int synt_free_state(Synth *s);
int synt_samples_mode1(Synth *s, short *buf, short n);
void synt_set_resonator(SynthTables *t, Resonator *r, short f, short b, char shift);
int synt_frame_mode0(Synth *s, short *frame);
int synt_frame_mode1(Synth *s, short *frame);
int synt_samples_mode0(Synth *s, short *buf, short n);
int synt_samples_mode0_16k(Synth *s, short *buf, short n);
short *synt_mk_amp_table(void);
short *synt_mk_res_b_table(int fs);
short *synt_mk_res_c_table(int fs);
short *synt_mk_bw_b_ratio(int fs);
short *synt_mk_bw_c_ratio(int fs);

#endif
