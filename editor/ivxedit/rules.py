"""RULSYS rule bytecode: instructions, rules and modules (REFERENCE §5.6).

A module is a lead word, its rules, and the closing words 0x04 0x00. A rule is RULE or
RULE* {length in bytes, counting the length word} followed by instructions. An Instr keeps
its operand words as found, except that a phoneme-id word may hold any non-int object (the
model's PhRef); encoding turns those back into ids through a callback.
"""
from dataclasses import dataclass, field

from .binfmt import IvxFormatError, pack_words, u16

RULE, RULE_ITER, NEXT, REPL_NEXT, REPL = 0x08, 0x0C, 0x10, 0xB8, 0xBC
PH_OPS = (0x40, 0x44, 0x48, 0x4C, 0x50, 0x7C, 0x80)   # SEEK FOCUS SCAN R R? L L?: one id
FIXED = {0x00: 0, 0x04: 0, 0x10: 1, 0xB0: 0, 0xB4: 1, 0xC8: 1, 0xCC: 0, 0xD0: 0, 0xD4: 0,
         0x40: 1, 0x44: 1, 0x48: 1, 0x4C: 1, 0x50: 1, 0x7C: 1, 0x80: 1,
         0x54: 4, 0x58: 4, 0x5C: 4, 0x60: 4, 0x64: 4, 0x90: 4, 0x94: 4, 0xC0: 4, 0xC4: 4,
         0x150: 1, 0x154: 0, 0x158: 0, 0x15C: 1, 0x160: 0, 0x164: 1, 0x168: 0, 0x16C: 0,
         0x170: 1, 0x174: 0, 0x178: 1, 0x17C: 0, 0x180: 1, 0x184: 0, 0x188: 2, 0x18C: 1,
         0x190: 1, 0x194: 1, 0x198: 0, 0x19C: 0, 0x1A0: 0, 0x1A4: 0, 0x1A8: 0, 0x1AC: 0,
         0x1B0: 0, 0x1B4: 0, 0x1B8: 0, 0x1BC: 0, 0x1C0: 0, 0x1C4: 0, 0x1C8: 1, 0x1CC: 1,
         0x1F0: 1, 0x1F4: 1, 0x1F8: 1}


def rep_split(w):
    """REPL word -> (del, n): w = del*256 + 12*n, where 12*n may carry into the high byte"""
    for d in range(w >> 8, -1, -1):
        r = w - d * 256
        if r % 12 == 0:
            return d, r // 12
    return w >> 8, (w & 0xFF) // 12


def rep_carries(w):
    """True for a REPL word whose 12*n carried into the high byte (the engine misreads it)"""
    return 12 * rep_split(w)[1] > 0xFF


@dataclass(eq=False)
class Instr:
    op: int
    args: list = field(default_factory=list)
    legacy: bool = False   # a REPL word with a carry, as loaded from a shipped file


@dataclass(eq=False)
class Rule:
    iterative: bool        # RULE* (0x0C) rather than RULE (0x08)
    instrs: list

    @property
    def label(self):
        """the NEXT operand: never read by the interpreter, the rule's source number"""
        if self.instrs and self.instrs[-1].op == NEXT:
            return self.instrs[-1].args[0]
        return None


@dataclass(eq=False)
class Module:
    lead: int              # first word; varies per file (a date or version stamp?)
    rules: list
    end: list = field(default_factory=lambda: [0x04, 0x00])


def operand_count(b, p):
    """number of operand words of the instruction whose opcode word is at b[p]"""
    op = u16(b, p)
    if op in FIXED:
        return FIXED[op]
    if op == 0x68:                                   # FOCUSP pattern
        return (u16(b, p + 2) & 0x0FFE) // 2
    if op in (0x6C, 0x9C):                           # RP/LP {min,opt} pattern
        return 1 + (u16(b, p + 4) & 0x0FFE) // 2
    if op in (REPL_NEXT, REPL):
        return 1 + rep_split(u16(b, p + 2))[1]
    if op in (0x100, 0x104, 0x108, 0x10C):           # LD/ST spec [index]
        return 1 if b[p + 3] & 0x80 else 2
    raise IvxFormatError('unknown opcode 0x%x at body 0x%x' % (op, p))


def ph_positions(ins):
    """indices into ins.args that hold phoneme ids"""
    op, a = ins.op, ins.args
    if op in PH_OPS:
        return [0]
    if op in (REPL_NEXT, REPL):
        return list(range(1, len(a)))
    if op == 0x68 and a[0] & 0x8000:
        return [1]
    if op in (0x6C, 0x9C) and a[1] & 0x8000:
        return [2]
    return []


def decode_instr(b, p):
    """-> (Instr, offset after it)"""
    n = operand_count(b, p)
    ins = Instr(u16(b, p), [u16(b, p + 2 + 2 * i) for i in range(n)])
    if ins.op in (REPL_NEXT, REPL) and rep_carries(ins.args[0]):
        ins.legacy = True
    return ins, p + 2 + 2 * n


def decode_module(b, start):
    """-> (Module, offset just past its closing 0x00 word)"""
    lead, p, rules = u16(b, start), start + 2, []
    while u16(b, p) in (RULE, RULE_ITER):
        end, q, instrs = p + 2 + u16(b, p + 2), p + 4, []
        while q < end:
            ins, q = decode_instr(b, q)
            instrs.append(ins)
        if q != end:
            raise IvxFormatError('rule at body 0x%x overruns its length' % p)
        rules.append(Rule(u16(b, p) == RULE_ITER, instrs))
        p = end
    closing = []
    while True:
        w = u16(b, p)
        if w not in (0x04, 0x00):
            raise IvxFormatError('unexpected word 0x%x at body 0x%x' % (w, p))
        closing.append(w)
        p += 2
        if w == 0:
            return Module(lead, rules, closing), p


def encode_instr(ins, ph_id):
    return [ins.op] + [a if isinstance(a, int) else ph_id(a) for a in ins.args]


def encode_rule(rule, ph_id):
    body = []
    for ins in rule.instrs:
        body += encode_instr(ins, ph_id)
    length = 2 + 2 * len(body)
    if length > 0xFFFF:
        raise IvxFormatError('rule is %d bytes, more than a length word holds' % length)
    return [RULE_ITER if rule.iterative else RULE, length] + body


def encode_module(m, ph_id):
    ws = [m.lead]
    for r in m.rules:
        ws += encode_rule(r, ph_id)
    return pack_words(ws + m.end)
