/* Accumulator arithmetic (cwmath.c, DLL 0x10021b30-0x10021f7f).
 * A is 32-bit, B and the operands 16-bit; all arithmetic wraps as on x86. */
#include <stdint.h>
#include "cw.h"

static short operand(Core *c)
{
    return (short)*c->ip++;
}

/* 0x10021b30: op 150 A = n */
void cw_op_A_set(Core *c)
{
    c->A = operand(c);
}

/* 0x10021b50: op 154 A = B */
void cw_op_A_set_B(Core *c)
{
    c->A = c->B;
}

/* 0x10021b60: op 158 B = -B, A = -B(old) */
void cw_op_A_negB(Core *c)
{
    short b = c->B;

    c->B = (short)-b;
    c->A = -(int)b;
}

/* 0x10021b80: op 15C */
void cw_op_A_add(Core *c)
{
    c->A = (int)((uint32_t)c->A + (uint32_t)(int)operand(c));
}

/* 0x10021ba0: op 160 */
void cw_op_A_add_B(Core *c)
{
    c->A = (int)((uint32_t)c->A + (uint32_t)(int)c->B);
}

/* 0x10021bb0: op 164 */
void cw_op_A_sub(Core *c)
{
    c->A = (int)((uint32_t)c->A - (uint32_t)(int)operand(c));
}

/* 0x10021bd0: op 168 */
void cw_op_A_sub_B(Core *c)
{
    c->A = (int)((uint32_t)c->A - (uint32_t)(int)c->B);
}

/* 0x10021be0: op 16C */
void cw_op_A_abs(Core *c)
{
    if (c->A < 0)
        c->A = (int)(0u - (uint32_t)c->A);
}

/* 0x10021c00: op 170 B = n, A *= n */
void cw_op_A_mul(Core *c)
{
    short n = operand(c);

    c->B = n;
    c->A = (int)((uint32_t)c->A * (uint32_t)(int)n);
}

/* 0x10021c30: op 174 */
void cw_op_A_mul_B(Core *c)
{
    c->A = (int)((uint32_t)(int)c->B * (uint32_t)c->A);
}

/* A = (A +- d/4) / d: rounds the quotient at a quarter, away from zero */
static void div_round(Core *c, short d)
{
    int a = c->A;

    if ((a >= 0) == (d >= 0))
        a = (int)((uint32_t)a + (uint32_t)(d >> 2));
    else
        a = (int)((uint32_t)a - (uint32_t)(d >> 2));
    c->A = a;
    c->A = a / d;
}

/* 0x10021c40: op 178 B = n, A /= n */
void cw_op_A_div(Core *c)
{
    short n = operand(c);

    c->B = n;
    if (n == 0) {
        c->status = CW_DIV_ZERO;
        return;
    }
    div_round(c, n);
}

/* 0x10021cb0: op 17C */
void cw_op_A_div_B(Core *c)
{
    if (c->B == 0) {
        c->status = CW_DIV_ZERO + 1;
        return;
    }
    div_round(c, c->B);
}

/* 0x10021d10: op 180, identical to 178 apart from the status code */
void cw_op_A_div2(Core *c)
{
    short n = operand(c);

    c->B = n;
    if (n == 0) {
        c->status = CW_DIV_ZERO + 2;
        return;
    }
    div_round(c, n);
}

/* 0x10021d80: op 184 */
void cw_op_A_div2_B(Core *c)
{
    if (c->B == 0) {
        c->status = CW_DIV_ZERO + 3;
        return;
    }
    div_round(c, c->B);
}

/* The shift loops run n times with a 32-bit counter (a negative n means ~4e9 turns).
 * Every value reaches a fixed point within 64 turns, so the count is capped there. */
static unsigned shift_count(short n)
{
    uint32_t it = (uint32_t)(int)n;

    return it > 64 ? 64 : it;
}

/* A >>= n, arithmetic (the DLL also copies bit 30 into bit 31, which sar already did) */
static void sar_n(Core *c, short n)
{
    unsigned it;

    if (n == 0)
        return;
    for (it = shift_count(n); it != 0; it--) {
        c->A >>= 1;
        if (c->A & 0x40000000)
            c->A = (int)((uint32_t)c->A | 0x80000000u);
    }
}

/* 0x10021de0: op 188 m, s: B = m, A = (A*m) >> s */
void cw_op_A_mulfrac(Core *c)
{
    short m = operand(c);

    c->B = m;
    c->A = (int)((uint32_t)c->A * (uint32_t)(int)m);
    sar_n(c, operand(c));
}

/* 0x10021e30: op 18C s: A >>= s, treating bit 14 as a 16-bit sign (copied into bit 15) */
void cw_op_A_shr(Core *c)
{
    short n = operand(c);
    unsigned it;

    if (n == 0)
        return;
    for (it = shift_count(n); it != 0; it--) {
        c->A >>= 1;
        if (c->A & 0x4000)
            c->A |= 0x8000;
    }
}

/* 0x10021e60: op 190 s */
void cw_op_A_shr2(Core *c)
{
    sar_n(c, operand(c));
}

/* 0x10021ea0: op 194 s: A <<= s (count masked to 5 bits by the CPU) */
void cw_op_A_shl(Core *c)
{
    unsigned char s = *(unsigned char *)c->ip;

    c->ip++;
    c->A = (int)((uint32_t)c->A << (s & 31));
}

/* 0x10021ec0: op 198: 16-entry ring queues */
void cw_op_PUSHQ1(Core *c)
{
    c->q1[c->qidx[0]] = (short)c->A;
    c->qidx[0] = (unsigned char)(c->qidx[0] + 1) & 0xf;
}

/* 0x10021ef0: op 19C */
void cw_op_PUSHQ2(Core *c)
{
    c->q2[c->qidx[2]] = (short)c->A;
    c->qidx[2] = (unsigned char)(c->qidx[2] + 1) & 0xf;
}

/* 0x10021f20: op 1A0 B = pop q1 */
void cw_op_POPQ1(Core *c)
{
    short i = c->qidx[1];

    c->B = c->q1[i];
    c->qidx[1] = (unsigned char)(i + 1) & 0xf;
}

/* 0x10021f50: op 1A4 B = pop q2 */
void cw_op_POPQ2(Core *c)
{
    short i = c->qidx[3];

    c->B = c->q2[i];
    c->qidx[3] = (unsigned char)(i + 1) & 0xf;
}
