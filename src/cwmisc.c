/* cwmisc.c (DLL 0x10022ca0-0x10022caf). */
#include "cw.h"

/* 0x10022ca0: ops 1F0/1F4/1F8: skip one operand word */
void cw_op_skip1(Core *c)
{
    c->ip++;
}
