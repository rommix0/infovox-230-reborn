/* Rule control flow (cwflow.c, DLL 0x10020800-0x100208ff). */
#include "cw.h"

/* 0x10020800: op 04, end of module */
void cw_op_BEGIN(Core *c)
{
    c->status = CW_DONE;
}

/* 0x10020820: any unlisted opcode */
void cw_op_invalid(Core *c)
{
    c->status = CW_BAD_OPCODE;
}

static void rule_begin(Core *c, unsigned char iter)
{
    unsigned short *p = c->ip;
    Seg *s;

    c->iter_mode = iter;
    c->changed = 0;
    c->ip = p + 1;          /* skip the rule length word */
    c->rule_start = p + 1;
    s = c->segs.head.next;
    c->inserted = 0;
    c->cursor = s;
    c->next = s;
    c->focus = s;
}

/* 0x10020830: op 08 RULE: match starting at every segment in turn */
void cw_op_RULE(Core *c)
{
    rule_begin(c, 0);
}

/* 0x10020870: op 0C RULE*: like RULE, but restart from the head after any change */
void cw_op_RULE_iter(Core *c)
{
    rule_begin(c, 1);
}

/* 0x100208b0: op 10 NEXT: rule body done, try the next start position */
void cw_op_NEXT(Core *c)
{
    c->changed = 1;
    cw_retry(c);
}

/* 0x100208d0: re-run the rule from the segment after the current start */
void cw_retry(Core *c)
{
    Seg *s;

    if (c->cursor == c->focus)
        c->inserted = 0;
    s = c->cursor->next;
    c->cursor = s;
    c->next = s;
    c->ip = c->rule_start;
}
