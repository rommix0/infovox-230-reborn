/* Rule-bytecode interpreter (DLL 0x1001fe90-0x10022caf: contwork.c, cwflow.c, cwtest.c,
 * cwelem.c, cwmath.c, cwvars.c, cwmisc.c) and the segment lists (tpelem.c).
 *
 * The interpreter works directly on the 0x38-byte pool nodes: a Seg holds 12 inline item
 * words at +0x20 and continues into ItemBlk nodes (data at +0xa, 23 words). Items are
 * {id:8, bytes:8} header words followed by bytes/2 payload words and may straddle nodes.
 * This layout is only valid for the 32-bit build (pointer size 4), like the DLL itself.
 */
#ifndef CW_H
#define CW_H

#include <stdint.h>
#include "ivx.h"

typedef struct Seg Seg;
typedef struct ItemBlk ItemBlk;
typedef struct SegList SegList;
typedef struct Core Core;

/* status codes (Core.status) */
enum {
    CW_RUNNING    = 0x00,
    CW_BAD_OPCODE = 0x10,
    CW_NO_ITEM    = 0x20,  /* LD: no item and no default */
    CW_EMPTY_ITEM = 0x21,  /* LD: item has no payload */
    CW_ST_REL_NEW = 0x22,  /* ST (rel): delete of an absent item */
    CW_ST_ABS_DEL = 0x23,  /* ST (abs): delete from an empty item */
    CW_DIV_ZERO   = 0x30,  /* 0x30..0x33: the four divide ops */
    CW_NO_NODES   = 0x40,  /* pool exhausted */
    CW_PTEST_OP   = 0x50,  /* bad parameter-test op */
    CW_DEL_ZERO   = 0x60,  /* DEL with count 0 */
    CW_DONE       = 0x90   /* op 04: end of module (reported as 0) */
};

/* node geometry */
#define NODE_SIZE     0x38
#define NODE_END(n)   ((unsigned short *)((char *)(n) + NODE_SIZE))
#define NODE_LAST(n)  ((unsigned short *)((char *)(n) + NODE_SIZE - 2))
#define BLK_DATA(b)   ((unsigned short *)((uintptr_t)(b) + 0xa))
#define BLK_LEN(b)    (*(short *)((char *)(b) + 8))
/* block owning a data pointer; BLK_DATA(NULL) (no next block) maps back to NULL. Done in
 * integers so the compiler cannot assume the result is non-NULL. */
#define DATA_BLK(p)   ((ItemBlk *)((uintptr_t)(p) - 0xa))
/* word distance a - b (both pointers into item storage), as the DLL computes it */
#define WDIFF(a, b)   ((short)(((char *)(a) - (char *)(b)) >> 1))
#define SEGS_HEAD(c)  ((Seg *)&(c)->segs.head)
#define SEGS_TAIL(c)  ((Seg *)&(c)->segs.tail)

/* tpelem.c */
typedef struct PhRec PhRec;
void op_emit_phonemes(Core *c, Seg *first, Seg *end, char *buf, int n);
void seg_free_segs_held(Core *c);
void seg_free_all(Core *c);
void seg_free_held(Core *c);
void seg_free_range(Core *c, Seg *first, Seg *end);
Seg *seg_alloc(Core *c);
void seg_hold(Core *c);
void seg_unhold(Core *c);
short seg_copy_held(Core *c);
short tp_text_to_segs(Core *c, char bound, unsigned char *from, unsigned char *to);
short tp_text_to_segs1(Core *c, char bound, unsigned char *from, unsigned char *to);
short tp_symbol_to_seg(Core *c, const unsigned char *p);
PhRec *ph_find(Core *c, short name);
int seg_free_count(Core *c);

/* contwork.c */
short run_module(Core *c, short idx);
short cw_run(Core *c, unsigned short *code);
void cw_step(Core *c);

/* cwflow.c */
void cw_op_BEGIN(Core *c);
void cw_op_invalid(Core *c);
void cw_op_RULE(Core *c);
void cw_op_RULE_iter(Core *c);
void cw_op_NEXT(Core *c);
void cw_retry(Core *c);

/* cwtest.c */
void cw_op_SEEK(Core *c);
void cw_fail(Core *c);
void cw_op_FOCUS(Core *c);
void cw_op_SCAN(Core *c);
void cw_op_R(Core *c);
void cw_op_L(Core *c);
void cw_op_R_opt(Core *c);
void cw_op_L_opt(Core *c);
void cw_op_SEEKF(Core *c);
void cw_op_FOCUSF(Core *c);
void cw_op_SCANF(Core *c);
void cw_op_RF(Core *c);
void cw_op_LF(Core *c);
void cw_op_RF_opt(Core *c);
void cw_op_LF_opt(Core *c);
void cw_op_FOCUSP(Core *c);
void cw_op_RP(Core *c);
void cw_op_LP(Core *c);
short cw_match_pattern(Core *c);
short cw_ptest_a(Core *c, short v);
unsigned short *cw_ptest_b(Core *c, unsigned short id);
unsigned short *cw_ptest_c(Core *c, unsigned short id);

/* cwelem.c */
void cw_op_FNEXT(Core *c);
void cw_op_CPREV(Core *c);
void cw_op_CHEAD(Core *c);
void cw_op_CTAIL(Core *c);
void cw_op_UNWRAP(Core *c);
void cw_op_REPL_next(Core *c);
void cw_op_REPL(Core *c);
void cw_op_SETF_next(Core *c);
void cw_op_SETF(Core *c);
void cw_op_GEMIN(Core *c);

/* cwmath.c */
void cw_op_A_set(Core *c);
void cw_op_A_set_B(Core *c);
void cw_op_A_negB(Core *c);
void cw_op_A_add(Core *c);
void cw_op_A_add_B(Core *c);
void cw_op_A_sub(Core *c);
void cw_op_A_sub_B(Core *c);
void cw_op_A_abs(Core *c);
void cw_op_A_mul(Core *c);
void cw_op_A_mul_B(Core *c);
void cw_op_A_div(Core *c);
void cw_op_A_div_B(Core *c);
void cw_op_A_div2(Core *c);
void cw_op_A_div2_B(Core *c);
void cw_op_A_mulfrac(Core *c);
void cw_op_A_shr(Core *c);
void cw_op_A_shr2(Core *c);
void cw_op_A_shl(Core *c);
void cw_op_PUSHQ1(Core *c);
void cw_op_PUSHQ2(Core *c);
void cw_op_POPQ1(Core *c);
void cw_op_POPQ2(Core *c);

/* cwvars.c */
void cw_op_B_R0(Core *c);
void cw_op_B_R1(Core *c);
void cw_op_B_R2(Core *c);
void cw_op_B_R3(Core *c);
void cw_op_R0_A(Core *c);
void cw_op_R1_A(Core *c);
void cw_op_R2_A(Core *c);
void cw_op_R3_A(Core *c);
void cw_op_B_V(Core *c);
void cw_op_V_A(Core *c);
void cw_op_LD(Core *c);
void cw_op_ST_s(Core *c);
void cw_op_ST_u(Core *c);
void cw_op_ST(Core *c);
void cw_st_param(Core *c, unsigned short id, unsigned short *val);
void cw_st_param_rel(Core *c, short tspec, unsigned short *val);
void cw_st_param_abs(Core *c, short tspec, unsigned short *val);
void cw_st_var(Core *c, unsigned short id, unsigned short *val);
void cw_st_var_sub1(Core *c, short tspec);
void cw_st_var_sub2(Core *c, short tspec, unsigned short *src, short n);
void cw_st_default(Core *c, unsigned short id);
int cw_item_insert(Core *c, short nbytes);
unsigned short *cw_find_item(Core *c, Seg *s, unsigned short id);
unsigned short *cw_find_default_item(Core *c, Seg *s, unsigned short id);

/* cwmisc.c */
void cw_op_skip1(Core *c);

#endif
