"""Rules in instruction form: one line of text per rule (spec: Rule notation, the fallback).

A line is 'RULE' or 'RULE*', then the rule's instructions, each a mnemonic and its
operands, separated by ' ; '. Every operand word is in the text, so a line determines its
rule's words; the parser that reads lines back comes with the rule notation (step 3).
Tokens are separated by single spaces, and an operand's kind follows from its position:

  phoneme      its name: A:  +  [  ;  (the name ' ' is written ␠); 'NAME+d' for a
               reference d bytes into a record; <end> for the inventory's terminator
  raw word     #hhhh: a phoneme id that is not in the inventory, or any word of an
               instruction whose operands do not have their opcode's shape
  features     [vvvvvvvv/mmmmmmmm] (V and M in hex)
  pattern      { NAME [v/m] <test> ... }, each part present when its header bit is;
               tests <F1 < 50> (compare), <F1.last.hi -> R0> (capture), <#hhhh> (a word
               the engine rejects); { #hhhh ... } when the header is not what the parts
               give
  RP, LP       MIN,OPT then the pattern
  REPL, REPL+  del D -> NAME NAME ...
  LD, ST...    F1@k (payload word k; .hi for the high byte), vXX; t=N for a nonzero
               spec high byte; F1@#hhhh for an index word with other bits
  A*=frac      M/2^S
  NEXT, GEMIN  #N (the rule's source number)
  others       the operand as a signed decimal

Each line comes with spans (start, end, kind, obj) for highlighting and jumps: kind is
'op', 'ph' (obj = the PhRef), 'num', 'feat', 'param' or 'raw'.
"""
from dataclasses import dataclass, field

from .info import item_name, ph_name, s8
from .model import PhRef
from .rules import FIXED, PH_OPS, REPL, REPL_NEXT, rep_split

MNEMONICS = {
    0x00: 'END', 0x04: 'BEGIN', 0x10: 'NEXT', 0x40: 'SEEK', 0x44: 'FOCUS', 0x48: 'SCAN',
    0x4C: 'R', 0x50: 'R?', 0x7C: 'L', 0x80: 'L?', 0x54: 'SEEKF', 0x58: 'FOCUSF',
    0x5C: 'SCANF', 0x60: 'RF', 0x64: 'RF?', 0x90: 'LF', 0x94: 'LF?', 0x68: 'FOCUSP',
    0x6C: 'RP', 0x9C: 'LP', 0xB0: 'FNEXT', 0xB4: 'UNWRAP', 0xB8: 'REPL+', 0xBC: 'REPL',
    0xC0: 'SETF+', 0xC4: 'SETF', 0xC8: 'GEMIN', 0xCC: 'CPREV', 0xD0: 'CHEAD', 0xD4: 'CTAIL',
    0x100: 'LD', 0x104: 'STs', 0x108: 'STu', 0x10C: 'ST',
    0x150: 'A=', 0x154: 'A=B', 0x158: 'A=B=-B', 0x15C: 'A+=', 0x160: 'A+=B', 0x164: 'A-=',
    0x168: 'A-=B', 0x16C: 'A=|A|', 0x170: 'A*=', 0x174: 'A*=B', 0x178: 'A/=', 0x17C: 'A/=B',
    0x180: 'A/=*', 0x184: 'A/=B*', 0x188: 'A*=frac', 0x18C: 'A>>=w', 0x190: 'A>>=',
    0x194: 'A<<=', 0x198: 'PUSHQ1', 0x19C: 'PUSHQ2', 0x1A0: 'B=POPQ1', 0x1A4: 'B=POPQ2',
    0x1A8: 'B=R0', 0x1AC: 'B=R1', 0x1B0: 'B=R2', 0x1B4: 'B=R3', 0x1B8: 'R0=A',
    0x1BC: 'R1=A', 0x1C0: 'R2=A', 0x1C4: 'R3=A', 0x1C8: 'B=V', 0x1CC: 'V=A',
    0x1F0: 'OP1F0', 0x1F4: 'OP1F4', 0x1F8: 'OP1F8'}
FEAT_OPS = (0x54, 0x58, 0x5C, 0x60, 0x64, 0x90, 0x94, 0xC0, 0xC4)
LDST_OPS = (0x100, 0x104, 0x108, 0x10C)
CAPTURE = {0: 'R0', 1: 'V0', 2: 'V1', 3: 'V2', 4: 'V3', 5: 'V4', 6: 'R1', 7: 'V5', 8: 'V6',
           9: 'V7', 10: 'V8', 11: 'V9', 12: 'R2', 18: 'R3'}
COMPARE = {0x18: '<', 0x1E: '<=', 0x24: '==', 0x2C: '!=', 0x34: '>=', 0x3C: '>'}  # value OP k


@dataclass
class Line:
    text: str
    spans: list = field(default_factory=list)   # (start, end, kind, obj)


class _Out:
    def __init__(self):
        self.parts, self.spans, self.pos = [], [], 0

    def put(self, s, kind=None, obj=None):
        if kind:
            self.spans.append((self.pos, self.pos + len(s), kind, obj))
        self.parts.append(s)
        self.pos += len(s)

    def tok(self, s, kind=None, obj=None):
        """a space, then the token"""
        self.put(' ')
        self.put(s, kind, obj)

    def line(self):
        return Line(''.join(self.parts), self.spans)


def ph_text(ref):
    """a phoneme operand (PhRef) as text"""
    if ref.ph is None:
        name = '<end>'
    else:
        name = ph_name(ref.ph.name)
        name = '␠' if name == ' ' else name
    return name + ('+%d' % ref.delta if ref.delta else '')


def _word(o, w):
    """any operand word: a phoneme reference, or raw"""
    if isinstance(w, PhRef):
        o.tok(ph_text(w), 'ph', w)
    elif isinstance(w, int):
        o.tok('#%04x' % w, 'raw')
    else:
        o.tok('#?%r' % (w,), 'raw')


def _ints(ws):
    return all(isinstance(w, int) and 0 <= w <= 0xFFFF for w in ws)


def _feat(ws):
    return '[%04x%04x/%04x%04x]' % tuple(ws)


def _s16(v):
    return v - 0x10000 if v & 0x8000 else v


def _tests(ws):
    """parameter-test words -> token texts, or None if they do not split into tests"""
    out, i = [], 0
    while i < len(ws):
        w = ws[i]
        name = item_name(w >> 8) + ('.last' if w & 0x80 else '') + ('.hi' if w & 0x40 else '')
        op = w & 0x3F
        if op in CAPTURE:
            out.append('<%s -> %s>' % (name, CAPTURE[op]))
            i += 1
        elif op in COMPARE and i + 1 < len(ws):
            out.append('<%s %s %d>' % (name, COMPARE[op], _s16(ws[i + 1])))
            i += 2
        else:
            out.append('<#%04x>' % w)
            i += 1
    return out


def _pattern(o, ws):
    """a pattern: header word, then [phoneme] [V M] [tests]"""
    o.tok('{')
    h = ws[0] if ws else None
    parts, i = [], 1
    ok = isinstance(h, int) and not h & 0x1001 and (h & 0x0FFE) // 2 == len(ws)
    if ok and h & 0x8000:
        ok = i < len(ws) and isinstance(ws[i], PhRef)
        i += 1
    if ok and h & 0x4000:
        ok = i + 4 <= len(ws) and _ints(ws[i:i + 4])
        i += 4
    tests = None
    if ok:
        rest = ws[i:]
        if h & 0x2000:
            ok = bool(rest) and _ints(rest)
            tests = _tests(rest) if ok else None
        else:
            ok = not rest
    if not ok:
        for w in ws:
            _word(o, w)
    else:
        i = 1
        if h & 0x8000:
            _word(o, ws[1])
            i = 2
        if h & 0x4000:
            o.tok(_feat(ws[i:i + 4]), 'feat')
        for t in tests or []:
            o.tok(t, 'param')
    o.tok('}')


def _ldst(o, ws):
    spec = ws[0]
    pid = spec & 0xFF
    o.tok(item_name(pid) + _index(ws[1] if len(ws) > 1 else None), 'param')
    if spec >> 8:
        o.tok('t=%d' % s8(spec >> 8), 'num')


def _index(w):
    if w is None:
        return ''
    if w & 0x017F:
        return '@#%04x' % w
    return '@%d' % (s8(w >> 8) // 2) + ('.hi' if w & 0x80 else '')


def _shaped(ins):
    """True if ins has its opcode's operand shape, so it can be written with typed operands"""
    op, a = ins.op, ins.args
    phs = [k for k in range(len(a)) if not isinstance(a[k], int)]
    if not all(isinstance(x, PhRef) for x in a if not isinstance(x, int)):
        return False
    if not all(0 <= x <= 0xFFFF for x in a if isinstance(x, int)):
        return False
    if op in FIXED:
        return len(a) == FIXED[op] and (phs == [] or (op in PH_OPS and phs == [0]))
    if op == 0x68:
        return len(a) >= 1 and isinstance(a[0], int)
    if op in (0x6C, 0x9C):
        return len(a) >= 2 and isinstance(a[0], int) and isinstance(a[1], int)
    if op in (REPL, REPL_NEXT):
        return len(a) >= 1 and isinstance(a[0], int) and rep_split(a[0])[1] == len(a) - 1
    if op in LDST_OPS:
        return not phs and len(a) == (1 if a and a[0] & 0x80 else 2)
    return False


def _instr(o, ins):
    op, a = ins.op, ins.args
    o.put(MNEMONICS.get(op, '?%x' % op), 'op')
    if not _shaped(ins):
        for w in a:
            _word(o, w)
    elif op in PH_OPS:
        _word(o, a[0])
    elif op in FEAT_OPS:
        o.tok(_feat(a), 'feat')
    elif op == 0x68:
        _pattern(o, a)
    elif op in (0x6C, 0x9C):
        o.tok('%d,%d' % (a[0] >> 8, a[0] & 0xFF), 'num')
        _pattern(o, a[1:])
    elif op in (REPL, REPL_NEXT):
        d = rep_split(a[0])[0]
        o.tok('del')
        o.tok(str(d), 'num')
        o.tok('->')
        for w in a[1:]:
            _word(o, w)
    elif op in LDST_OPS:
        _ldst(o, a)
    elif op == 0x188:
        o.tok('%d/2^%d' % (_s16(a[0]), _s16(a[1])), 'num')
    elif op in (0x10, 0xC8):
        o.tok('#%d' % a[0], 'num')
    else:
        for w in a:
            o.tok(str(_s16(w)), 'num')


def rule_line(rule):
    """a Rule -> Line"""
    o = _Out()
    o.put('RULE*' if rule.iterative else 'RULE', 'op')
    for k, ins in enumerate(rule.instrs):
        o.put(' ;' if k else '')
        o.put(' ')
        _instr(o, ins)
    return o.line()
