"""Generate ivx_types.h (Ghidra import + future C port) from offset tables.

Every struct is described as (offset, type, name, comment). Gaps are filled with
`unsigned char _pad_XXX[n]` so offsets are exact; overlaps are an error.
Confidence tags follow REFERENCE.md.  Run:  python gen_types.py
Writes ivx_types.h and ivx_types_check.c (C11 offset asserts for a 32-bit build).
"""
import re, sys, os

SIZES = {'char': 1, 'unsigned char': 1, 'short': 2, 'unsigned short': 2,
         'int': 4, 'unsigned int': 4}

def tsize(t, structs):
    t = t.strip()
    if t.endswith('*') or '(*' in t:
        return 4
    if t.startswith('struct '):
        return structs[t[7:]][0]
    return SIZES[t]

# name: (size, [(off, type, field, comment)])  -- '[n]' / '[n][m]' in field name = array
S = {}

S['SegAnchor'] = (8, [
    (0x0, 'struct Seg *', 'next', ''),
    (0x4, 'struct Seg *', 'prev', ''),
])
S['SegList'] = (0x10, [
    (0x0, 'struct SegAnchor', 'head', 'head sentinel; first = head.next [CONFIRMED]'),
    (0x8, 'struct SegAnchor', 'tail', 'tail sentinel; loop ends at &tail [CONFIRMED]'),
])
S['Seg'] = (0x38, [   # pool node, core->pool, allocated n*0x38 in tts_create
    (0x00, 'struct Seg *', 'next', ''),
    (0x04, 'struct Seg *', 'prev', ''),
    (0x08, 'struct ItemBlk *', 'ext', 'overflow item blocks (pool nodes) [CONFIRMED]'),
    (0x0c, 'short', 'items_len', 'bytes of items (inline + ext) [LIKELY]'),
    (0x0e, 'short', 'ph', 'phoneme id = byte offset of record from H[8] [CONFIRMED]'),
    (0x10, 'unsigned short', 'mark_lo', 'bookmark (Core.mark_lo at creation); kept by REPL [LIKELY]'),
    (0x12, 'unsigned short', 'mark_hi', 'bookmark (Core.mark_hi) [LIKELY]'),
    (0x14, 'unsigned int', 'feat', 'feature bits (from PhRec) [CONFIRMED]'),
    (0x18, 'unsigned int', 'mask', 'feature-defined mask [CONFIRMED]'),
    (0x1c, 'short', 'word_pos', 'text position of the word (Core.word_pos) [LIKELY]'),
    (0x1e, 'short', 'dur', 'duration in frames, set by op_seg_durations [CONFIRMED]'),
    (0x20, 'unsigned short', 'items[12]', 'inline items, same format as PhRec items [CONFIRMED]'),
])
S['ItemBlk'] = (0x38, [   # also a pool node
    (0x00, 'struct ItemBlk *', 'next', ''),
    (0x04, 'void *', 'prev', 'previous block, or the owning Seg for the first block [CONFIRMED, cw_item_insert]'),
    (0x08, 'short', 'len', 'bytes used in data [LIKELY]'),
    (0x0a, 'unsigned short', 'data[23]', ''),
])
S['PhRec'] = (0x0e, [    # inventory record after swap16 (§5.4); variable length
    (0x00, 'short', 'len', 'record length in bytes'),
    (0x02, 'unsigned short', 'name', '2 chars'),
    (0x04, 'unsigned short', 'feat_hi', ''),
    (0x06, 'unsigned short', 'feat_lo', ''),
    (0x08, 'unsigned short', 'mask_hi', ''),
    (0x0a, 'unsigned short', 'mask_lo', ''),
    (0x0c, 'unsigned short', 'items[1]', 'variable length: u8 id,u8 size,data'),
])
S['IvxBody'] = (0x170, [   # .ivx body header (§5.2), after tts_ctrl 0x21 byte-swap
    (0x00, 'unsigned int', 'H[16]', 'section directory'),
    (0x40, 'char', 'tag[42]', 'build tag, e.g. SWRU981118.P2'),
    (0x6a, 'short', 'nparams2', '2*25'),
    (0x6c, 'short', 'f6c', '116'),
    (0x6e, 'short', 'ntotal2', '2*29'),
    (0x70, 'short', 'ptab[4][32]', 'per-parameter tables A0..A3, stored reversed'),
])
S['Core'] = (0xeb0, [   # tts_ctrl handle, allocated by tts_create (cmd 0)
    # -- rule interpreter (cw*.c)
    (0x000, 'short', 'q1[16]', 'ring queue 1 (PUSHQ1/POPQ1) [LIKELY]'),
    (0x020, 'short', 'q2[16]', 'ring queue 2 [LIKELY]'),
    (0x040, 'unsigned short *', 'mod_base', 'module being run (cw_run arg)'),
    (0x044, 'unsigned short *', 'rule_start', 'start of current rule body; fail jumps via its len'),
    (0x048, 'unsigned short *', 'ip', 'bytecode IP'),
    (0x04c, 'struct Seg *', 'cursor', ''),
    (0x050, 'struct Seg *', 'focus', ''),
    (0x054, 'struct Seg *', 'next', ''),
    (0x058, 'unsigned short *', 'item', 'LD/ST: found item data'),
    (0x05c, 'unsigned short *', 'item_ins', 'LD/ST: insert point when not found'),
    (0x060, 'struct Seg *', 'item_seg', 'segment/block holding item'),
    (0x064, 'struct ItemBlk *', 'item_blk', ''),
    (0x068, 'short', 'status', '0 = run, 0x90 = module done, 0x20/0x21 errors, 0x4x = out of nodes'),
    (0x06c, 'int', 'A', 'accumulator'),
    (0x070, 'short', 'B', 'operand register'),
    (0x072, 'short', 'R[4]', 'scratch registers'),
    (0x07a, 'short', 'V[10]', 'global variables; reset from V_init before M2'),
    (0x08e, 'short', 'qidx[4]', 'queue indices, zeroed by cw_run [LIKELY]'),
    (0x09a, 'unsigned char', 'iter_mode', 'RULE* iterative mode [HYPOTHESIS]'),
    (0x09b, 'unsigned char', 'changed', 'set by REPL/SETF'),
    (0x09c, 'unsigned char', 'inserted', 'REPL inserted; next scan starts after focus'),
    (0x09d, 'unsigned char', 'f09d', 'cleared by tts_c02_reset'),
    (0x0a0, 'unsigned short *', 'phinv', 'phoneme inventory (H[8])'),
    # -- segment lists (tpelem.c)
    (0x0a4, 'struct SegList', 'free_list', 'free pool nodes'),
    (0x0b4, 'struct SegList', 'segs', 'working list: the rules run on this'),
    (0x0c4, 'struct SegList', 'held', 'words held until sentence end (mode bit 8)'),
    (0x0d4, 'struct Seg *', 'held_mark', 'first node of the last seg_hold, for seg_unhold'),
    (0x0d8, 'struct SegList', 'out', 'utterance being turned into frames (op*.c)'),
    (0x0e8, 'struct Seg *', 'out_cur', ''),
    (0x0ec, 'struct IvxBody *', 'ivx', 'bound .ivx body (tts_c03_bind)'),
    (0x0f0, 'unsigned short *', 'mod[8]', 'M0..M5 = H9,H10,H12,H5,H6,H11'),
    (0x110, 'struct Seg *', 'pool', 'n*0x38 node pool'),
    (0x114, 'short', 'pool_n', ''),
    (0x116, 'short', 'count', 'counts up from -count_init at reset [HYPOTHESIS]'),
    (0x118, 'short', 'count_init', 'cleared by tts_c1c [HYPOTHESIS]'),
    (0x120, 'int', 'jmpbuf[16]', 'setjmp buffer used by tp_putc'),
    (0x160, 'void *', 'p160', 'freed by ivx_bind'),
    (0x164, 'char *(*)(void *, char *, int)', 'cb_excdict', 'cmd 0x15: exception dictionary, returns replacement transcription'),
    (0x168, 'void (*)(void *)', 'hook', 'cmd 0x18: engine callback called every hook_interval steps (SAPI: 0x10011400)'),
    (0x16c, 'short', 'hook_interval', 'cmd 0x19'),
    (0x16e, 'short', 'word_pos', 'cmd 0x1b; text offset of the current word'),
    # -- text input (tpchar.c)
    (0x170, 'char', 'text[800]', 'input text buffer'),
    (0x490, 'char *', 'text_end', 'end of buffered text'),
    (0x494, 'char *', 'text_cur', 'scan position'),
    (0x498, 'char *', 'tok_start', 'start of current token'),
    (0x49c, 'char *', 'tok_start2', ''),
    (0x4a0, 'char *', 'text_limit', '= text+0x2a0'),
    (0x4a4, 'char *', 'text_done', 'consumed up to here'),
    (0x4a8, 'unsigned char *', 'xlat_hi', 'H[14]: 2-char transliterations of 0x80..0xFF'),
    (0x4ac, 'unsigned char *', 'class_lo', 'H[13]: class codes for 0x00..0x7F'),
    (0x4b0, 'unsigned char', 'charset', 'cmd 0x04: bit0/bit1 select 7-bit national tables 0x10048508/0x10048588'),
    (0x4b2, 'unsigned short', 'class_flags[256]', 'char-class flags: 0x10 sentence punct, 0x100 speak-as-symbol, 0x200 letter, 0x400 lower, 0x800 upper'),
    (0x6b4, 'unsigned short *', 'lex_index', 'per-first-char offsets into lexicon, or NULL = binary search'),
    (0x6b8, 'unsigned char *', 'lexicon', 'H[3], or the H[13] character-name table'),
    (0x6bc, 'unsigned short', 'mark_lo', 'current bookmark (cmd 0x20 / in-text escape)'),
    (0x6be, 'unsigned short', 'mark_hi', ''),
    (0x6c0, 'unsigned short', 'mode', 'cmd 0x05 flags; bit8 = buffer words to sentence end'),
    (0x6c2, 'short', 'V_init[10]', 'cmd 0x0d; default 50; copied to V before M2'),
    (0x6d6, 'unsigned char', 'retry', 'bit0/bit1: out-of-nodes recovery state'),
    (0x6d7, 'unsigned char', 'tp_state', 'tp_putc state 0..13'),
    (0x6d8, 'unsigned char', 'esc_phon', 'cmd 0x0f: phonetic-input escape char (default 2)'),
    (0x6d9, 'unsigned char', 'f6d9', 'init 0x20'),
    (0x6da, 'unsigned char', 'esc_mark', 'cmd 0x0e: bookmark escape char (default 1)'),
    (0x6db, 'char', 'voice_no', 'cmd 0x06: voice 0..9'),
    (0x6dc, 'unsigned char', 'f6dc', 'cmd 0x05 bit 0x80'),
    (0x6dd, 'unsigned char', 'out_flags', 'cmd 0x10: bit1 phonemes cb during synthesis, bit2 no synthesis'),
    (0x6de, 'unsigned char', 'f6de', ''),
    (0x6df, 'unsigned char', 'f6df', ''),
    (0x6e0, 'unsigned char', 'flush_on_input', ''),
    (0x6e1, 'unsigned char', 'text_consumed', ''),
    (0x6e2, 'unsigned char', 'f6e2', ''),
    (0x6e3, 'unsigned char', 'pending', ''),
    # -- callbacks (ttsctrl.c)
    (0x6e4, 'void *', 'cb_arg', 'cmd 0x16: first argument of every callback'),
    (0x6e8, 'void (*)(void *, char *)', 'cb_phonemes', 'cmd 0x11: phoneme string output'),
    (0x6ec, 'void *', 'p6ec', 'cmd 0x12'),
    (0x6f0, 'void (*)(void *, int)', 'cb_status', 'cmd 0x13: status/error codes'),
    (0x6f4, 'int (*)(void *, char *, char **)', 'cb_userdict', 'cmd 0x14: user dictionary'),
    (0x6f8, 'void (*)(void *, int)', 'cb_wordpos', 'cmd 0x1a: word position'),
    (0x6fc, 'void (*)(void *, int)', 'cb_bookmark', 'cmd 0x1f: bookmark reached during synthesis'),
    # -- parameter description (ivx_bind / ivx_load_param_tables), returned by cmd 0x1d
    (0x700, 'short *', 'ptab_A1', 'body 0x0B0'),
    (0x704, 'short *', 'ptab_A2', 'body 0x0F0'),
    (0x708, 'short *', 'ptab_A0', 'body 0x070 (names)'),
    (0x70c, 'short *', 'ptab_A3', 'body 0x130'),
    (0x710, 'short', 'pdesc_n', '-1 - nparams'),
    (0x712, 'short', 'pdesc_pad', '0x2020'),
    (0x714, 'char', 'pnames[60]', '2-char parameter names'),
    (0x750, 'short', 'A1[32]', 'conversion type code (x4) per parameter, selects op_t* handler'),
    (0x790, 'short', 'A2[32]', 'smoothing rate per parameter (x16 if A1 < 0x48)'),
    (0x7d0, 'short', 'A3[32]', 'per-parameter offset'),
    (0x810, 'short', 'nparams', 'rule params DR..A0'),
    (0x812, 'short', 'ntotal', 'total params (init 0x20)'),
    # -- frame generation (opsmooth.c / opstart.c)
    (0x814, 'short', 'frames_left', 'cmd 0x17'),
    (0x816, 'short', 'frame_no', ''),
    (0x818, 'struct Track', 'tracks[32]', 'per-parameter tracks; [0] = DR/segment clock'),
    (0xc98, 'short', 'outval[32]', 'output frame; synth frame starts at outval+4'),
    (0xcd8, 'short', 'out_save0', ''),
    (0xcda, 'short', 'out_save1', ''),
    (0xcdc, 'short', 'out_mode', 'cmd 0x1d'),
    (0xcde, 'short', 'voice[32]', 'active voice parameters (offsets / scales, 64 = unity for scales)'),
    (0xd1e, 'short', 'user_voice[5][32]', 'voices 5..9 (cmd 0x07); voices 0..4 are param_defaults'),
    (0xe5e, 'short', 'voice_next[32]', 'pending voice, copied to voice[] at next frame if voice_dirty'),
    (0xe9e, 'short', 'voice_dirty', ''),
    (0xea0, 'short', 'ui_rate', 'cmd 0x09, 0..100 -> voice DR'),
    (0xea2, 'short', 'ui_pitch', 'cmd 0x0a, 0..100 -> voice F0'),
    (0xea4, 'short', 'ui_fd', 'cmd 0x08, 0..100 -> voice FD'),
    (0xea6, 'unsigned short', 'last_mark_lo', 'last bookmark reported via cb_bookmark'),
    (0xea8, 'unsigned short', 'last_mark_hi', ''),
    (0xeaa, 'unsigned char', 'synth_state', '0 idle, 1 started, 0xff busy'),
    (0xeab, 'unsigned char', 'out_active', 'frames available'),
    (0xeac, 'unsigned char', 'feac', ''),
    (0xead, 'unsigned char', 'fead', ''),
    (0xeae, 'short', 'feae', 'cmd 0x1e'),
])

S['Track'] = (0x24, [   # Core.tracks[i]: one per parameter (op*.c)
    (0x00, 'unsigned short *', 'p0', 'current item in segment items (op_track_find_item)'),
    (0x04, 'unsigned short *', 'p1', 'end of item area / current inventory item (op_track_find_default)'),
    (0x08, 'unsigned short *', 'p2', 'next ItemBlk data / end of inventory record'),
    (0x0c, 'struct Seg *', 'seg', 'segment in Core.out being read'),
    (0x10, 'short', 'target', 'target value of current point'),
    (0x12, 'short', 'delta', 'remaining distance to target; 0 = settled'),
    (0x14, 'short', 'step', ''),
    (0x16, 'short', 'span', 'cosine interp: value span'),
    (0x18, 'short', 'phase_inc', 'cosine interp: 0x4000 / frames'),
    (0x1a, 'short', 'phase', 'interp phase / smoothing state'),
    (0x1c, 'short', 'npts', 'points left in current item'),
    (0x1e, 'short', 'cnt', 'frames left in current interpolation'),
    (0x20, 'short', 'tleft', 'frames until next point'),
])
S['OpBatch'] = (0x18, [   # opb_save / opb_restore snapshot of the segment list
    (0x00, 'struct SegList', 'list', ''),
    (0x10, 'struct Seg *', 'nodes', 'copied nodes'),
    (0x14, 'int', 'size', 'bytes'),
])

# ---- synthesizer ----
S['Resonator'] = (0xc, [
    (0x0, 'int', 'c', 'Tc[F]*Tcr[B] (-r^2)'),
    (0x4, 'int', 'b', 'Tb[F]*Tbr[B] (r cos); used as 2b'),
    (0x8, 'int', 'a', '1 - 2b - c (gain)'),
])
S['SynthMapEntry'] = (8, [
    (0x0, 'const char *', 'name', '2-char code, table 0x10049518 [CONFIRMED]'),
    (0x4, 'short', 'frame_index', 'index into the frame vector; -1 = use def'),
    (0x6, 'short', 'def', ''),
])
S['FrameCoef0'] = (0xa0, [   # mode-0 frame coefficients (SynthRun.coef)
    (0x00, 'int', 'samples_per_frame', 'fs / frame_rate (not read by the sample loop)'),
    (0x04, 'int', 'half_period', '(fs/F0)/2: impulse spacing in samples [CONFIRMED]'),
    (0x08, 'int', 'noise_seed', 'init 0xfff8; never read [CONFIRMED]'),
    (0x0c, 'int', 'noise_poly', 'LFSR xor mask, init 0x1d87 [CONFIRMED]'),
    (0x10, 'int', 'g_voice', 'amp[A0]'),
    (0x14, 'int', 'g_asp', 'amp[AH]/4'),
    (0x18, 'struct Resonator', 'r_f1', 'F1/B1 shift 14'),
    (0x24, 'struct Resonator', 'r_fh1', 'FH/dB#10 shift 11'),
    (0x30, 'struct Resonator', 'r_fh2', 'FH/dB#25 shift 12'),
    (0x3c, 'struct Resonator', 'r_f2', 'F2/B2 shift 14'),
    (0x48, 'struct Resonator', 'r_fh3', 'FH/dB#29 shift 12'),
    (0x54, 'struct Resonator', 'r_f4', 'F4/B4 shift 13'),
    (0x60, 'struct Resonator', 'r_f3', 'F3/B3 shift 13'),
    (0x6c, 'int', 'g_nasal', 'amp[AN]/2'),
    (0x70, 'struct Resonator', 'r_nasal', 'FN/BN'),
    (0x7c, 'int', 'g_fric', 'amp[AC]/4'),
    (0x80, 'struct Resonator', 'r_k2', 'K2/C2'),
    (0x8c, 'struct Resonator', 'r_k1', 'K1/C1'),
    (0x98, 'int', 'fric_mix', 'amp[AK]'),
    (0x9c, 'int', 'lp_coef', 'output one-pole, (1-exp(-2pi 200/fs))*(2^31-1)'),
])
S['FrameCoef1'] = (0x7c, [   # mode-1 frame coefficients; its sample function is an empty stub
    (0x00, 'int', 'samples_per_frame', ''),
    (0x04, 'int', 'half_period', ''),
    (0x08, 'int', 'noise_seed', ''),
    (0x0c, 'int', 'noise_poly', ''),
    (0x10, 'int', 'g_voice', 'amp[A0]'),
    (0x14, 'int', 'g_asp', 'amp[AH]/4'),
    (0x18, 'struct Resonator', 'r_f1', 'F1/B1 shift 14'),
    (0x24, 'struct Resonator', 'r_fh', 'FH-4/dB#10 shift 12'),
    (0x30, 'struct Resonator', 'r_f3', 'F3/B3 shift 12'),
    (0x3c, 'struct Resonator', 'r_f2', 'F2/B2 shift 13'),
    (0x48, 'int', 'g_nasal', 'amp[AN]/2'),
    (0x4c, 'struct Resonator', 'r_nasal', 'FN/BN shift 14'),
    (0x58, 'int', 'g_fric', 'amp[AC]/4'),
    (0x68, 'struct Resonator', 'r_k1', 'K1/C1 shift 12 (no K2, no F4)'),
    (0x74, 'int', 'fric_mix', 'amp[AK]'),
    (0x78, 'int', 'lp_coef', 'one-pole, 1000 Hz'),
])
S['SynthState0'] = (0x5c, [   # mode-0 sample-loop state (SynthRun.sstate; 0x44 bytes in mode 1)
    (0x00, 'int', 'pcount', 'samples since last impulse, init 0x200'),
    (0x04, 'unsigned short', 'noise', '16-bit Galois LFSR, init 0xfff8'),
    (0x08, 'int', 'casc[14]', '{y2,y1} x F1 FH1 FH2 F2 FH3 F4 F3'),
    (0x40, 'int', 'nasal[2]', ''),
    (0x48, 'int', 'fric[4]', 'K2, K1'),
    (0x58, 'int', 'lp', 'output low-pass state'),
])
S['SynthRun'] = (0x24, [
    (0x00, 'short', 'f00', ''),
    (0x02, 'unsigned short', 'flags', 'init 0x4c; 1=frame pending, 4=noise onset, 0x10=silent'),
    (0x04, 'struct FrameCoef0 *', 'coef', 'FrameCoef0 (mode 0) / FrameCoef1 (mode 1)'),
    (0x08, 'int', 'coef_size', ''),
    (0x0c, 'struct SynthState0 *', 'sstate', '0x5c (mode 0) / 0x44 (mode 1) bytes'),
    (0x10, 'int', 'f10', ''),
    (0x14, 'int', 'f14', ''),
    (0x18, 'int', 'period_next', 'period_acc + period (never read)'),
    (0x1c, 'int', 'period_acc', 'running sum of fs/F0 (never read)'),
    (0x20, 'int', 'voiced', 'A0 or AN non-zero'),
])
S['SynthTables'] = (0x2c, [   # synth->tab; first arg of synt_set_resonator
    (0x00, 'struct SynthRun *', 'run', ''),
    (0x04, 'short', 'silent', 'previous frame had all source gains 0'),
    (0x06, 'short', 'plist_n', 'synt_bind_params count'),
    (0x08, 'short *', 'plist_buf', 'calloc(plist_n, 2) by synt_bind_params; never used otherwise'),
    (0x0c, 'struct SynthMapEntry *', 'map', '32 entries, copied from 0x10048ed0/0x10048fd0'),
    (0x10, 'short *', 'amp_db', '32: 32767*10^((i-31)/20)'),
    (0x14, 'short *', 'res_b', '128: r*cos(theta)'),
    (0x18, 'short *', 'bw_b_ratio', '32'),
    (0x1c, 'short *', 'res_c', '128: -r^2'),
    (0x20, 'short *', 'bw_c_ratio', '32'),
    (0x24, 'int', 'f24', ''),
    (0x28, 'int', 'f28', ''),
])
S['Synth'] = (0x3c, [   # synt_ctrl handle, allocated by synt_create
    (0x00, 'int', 'started', '0 at create; set to 2 by the sample loop'),
    (0x04, 'short', 'type', 'synt_ctrl create cmd: 0 = mode 0, 1 = mode 1, 2 = unsupported'),
    (0x06, 'short', 'f06', 'init from 0x10048ec8 (512); never read in synth'),
    (0x08, 'short *(*)(void *)', 'get_frame', 'cmd 0x09: frame source, engine 0x10011410 -> op_next_frame'),
    (0x0c, 'void *', 'get_frame_arg', 'cmd 0x0a'),
    (0x10, 'int', 'map_sel', 'cmd 0x0d: 0 -> map 0x10048ed0, else 0x10048fd0'),
    (0x14, 'int', 'f14', 'cleared by synt_set_param_list'),
    (0x18, 'short *', 'plist', 'cmd 0x0b: {n or -n, ?, code, code, ...}'),
    (0x1c, 'short', 'frame_rate', 'frames/s, cmd 0x0e (2..1000), default 100'),
    (0x20, 'int', 'fs', 'cmd 0x11, default 11025 (engine sets 22050)'),
    (0x24, 'int', 'sample_fmt', 'cmd 0x10, default 1; &0xf != 0 -> 2 bytes/sample'),
    (0x28, 'struct SynthTables *', 'tab', ''),
    (0x2c, 'int (*)(struct Synth *, short *)', 'frame_fn', 'synt_frame_mode0/1'),
    (0x30, 'int (*)(struct Synth *, short *, short)', 'samples_fn', 'synt_samples_mode0[_16k] / mode1'),
    (0x34, 'short', 'frame_left', 'samples left in frame; <0 = silence (no frame); cmd 0x0f clears'),
    (0x36, 'short', 'half_rate', 'mode 1 and fs >= 16000'),
    (0x38, 'short', 'produced', 'samples synthesized by the last synt_generate; cmd 0x12'),
])

ORDER = ['SegAnchor', 'SegList', 'Seg', 'ItemBlk', 'PhRec', 'IvxBody', 'Track', 'Core', 'OpBatch',
         'Resonator', 'SynthMapEntry', 'FrameCoef0', 'FrameCoef1', 'SynthState0', 'SynthRun',
         'SynthTables', 'Synth']

def decl(t, name):
    m = re.match(r'(\w+)((?:\[\d+\])*)$', name)
    base, dims = m.group(1), m.group(2)
    if '(*)' in t:
        return t.replace('(*)', '(*%s%s)' % (base, dims))
    return '%s %s%s' % (t, base, dims)

def fsize(t, name):
    n = 1
    for d in re.findall(r'\[(\d+)\]', name):
        n *= int(d)
    return tsize(t, S) * n

def main():
    out = ['/* Generated by tools/ghidra/gen_types.py -- do not edit by hand. */',
           '/* Infovox 230 (Ivx230nt.dll) data structures, 32-bit layout. */', '']
    chk = ['/* Generated by gen_types.py: compile with a 32-bit C11 compiler to verify offsets. */',
           '#include <stddef.h>', '#include "ivx_types.h"', '']
    for n in ORDER:
        out.append('struct %s;' % n)
    out.append('')
    for n in ORDER:
        size, fields = S[n]
        out.append('struct %s {  /* size 0x%x */' % (n, size))
        pos = 0
        for off, t, name, com in fields:
            if off < pos:
                sys.exit('%s.%s overlaps (0x%x < 0x%x)' % (n, name, off, pos))
            if off > pos:
                out.append('    unsigned char _pad_%03x[%d];' % (pos, off - pos))
            line = '    %s;' % decl(t, name)
            out.append('%-48s/* +0x%03x %s */' % (line, off, com) if com else '%-48s/* +0x%03x */' % (line, off))
            chk.append('_Static_assert(offsetof(struct %s, %s) == 0x%x, "%s.%s");'
                       % (n, re.match(r'\w+', name).group(0), off, n, name))
            pos = off + fsize(t, name)
        if pos > size:
            sys.exit('%s overflows: 0x%x > 0x%x' % (n, pos, size))
        if pos < size:
            out.append('    unsigned char _pad_%03x[%d];' % (pos, size - pos))
        out.append('};')
        out.append('')
        chk.append('_Static_assert(sizeof(struct %s) == 0x%x, "sizeof %s");' % (n, size, n))
    here = os.path.dirname(os.path.abspath(__file__))
    open(os.path.join(here, 'ivx_types.h'), 'w', newline='\n').write('\n'.join(out) + '\n')
    open(os.path.join(here, 'ivx_types_check.c'), 'w', newline='\n').write('\n'.join(chk) + '\n')
    print('wrote ivx_types.h, ivx_types_check.c (%d structs)' % len(ORDER))

main()
