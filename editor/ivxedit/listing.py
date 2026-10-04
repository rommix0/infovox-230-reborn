"""Rules in instruction form: one line of text per rule (spec: Rule notation, the fallback).

A line is 'RULE' or 'RULE*', then the rule's instructions, each a mnemonic and its
operands, separated by ' ; '. Every operand word is in the text, so a line determines its
rule's words: parse_rule reads the line of any rule a file loads back into that rule. It
refuses an instruction whose words would not decode as that instruction again (a wrong
operand count, an unknown opcode, a phoneme where no id goes), which only hand-built
rules have.
Tokens are separated by single spaces (a parameter test <...> is one token with spaces
inside its brackets), and an operand's kind follows from its position:

  phoneme      its name: A:  +  [  (the name ' ' is written ␠, the name ';' is written ⁏
               so that it cannot be taken for the ' ; ' separator); 'NAME+d' for a
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
import re
from dataclasses import dataclass, field

from .binfmt import pack_words
from .info import item_name, ph_name, s8
from .model import PhRef
from .rules import (FIXED, PH_OPS, REPL, REPL_NEXT, Instr, Rule, encode_rule, operand_count,
                    ph_positions, rep_carries, rep_split)

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
        name = {' ': '␠', ';': '⁏'}.get(name, name)
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
    i = 1
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
        if not (len(a) >= 1 and isinstance(a[0], int)):
            return False
        d, n = rep_split(a[0])
        return n == len(a) - 1 and d * 256 + 12 * n == a[0]
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


# --- reading lines back

class ParseError(ValueError):
    """a line that does not read; pos is the column of the problem, or None when the
    problem is the rule as a whole"""

    def __init__(self, message, pos=None):
        super().__init__(message if pos is None else 'column %d: %s' % (pos + 1, message))
        self.message, self.pos = message, pos


OPCODES = {m: op for op, m in MNEMONICS.items()}
CAPTURE_IDS = {r: op for op, r in CAPTURE.items()}
COMPARE_IDS = {s: op for op, s in COMPARE.items()}
ITEM_IDS = {item_name(i): i for i in range(0x100)}
TEST = re.compile(r'<(?:#([0-9a-f]{4})|([^ <>.]+)(\.last)?(\.hi)? '
                  r'(?:-> (\w+)|(<=|<|==|!=|>=|>) (-?[0-9]+)))>')
FEAT = re.compile(r'\[([0-9a-f]{8})/([0-9a-f]{8})\]')
RAW = re.compile(r'#([0-9a-f]{4})')
LDST = re.compile(r'([^@ ]+)(?:@(?:#([0-9a-f]{4})|(-?[0-9]+)(\.hi)?))?')
DEC = re.compile(r'-?[0-9]+')


def ph_names(phonemes):
    """{name as ph_text writes it: Phoneme}; of two phonemes with one name, the first (as
    the engine's ph_find)"""
    names = {}
    for ph in phonemes:
        names.setdefault(ph_text(PhRef(ph)), ph)
    return names


def dec(text, pos):
    """a decimal number -> int; refuses digit strings far past anything a word holds, before
    int() (Python limits int(str) to 4300 digits)"""
    if len(text.lstrip('-')) > 10:
        raise ParseError('%s... is too long a number' % text[:12], pos)
    return int(text)


def u16(text, pos):
    """a decimal operand -> its word; -32768..65535"""
    v = dec(text, pos)
    if not -0x8000 <= v <= 0xFFFF:
        raise ParseError('%s does not fit in 16 bits' % text, pos)
    return v & 0xFFFF


def _tokens(text, pos):
    """an instruction's text -> [(token, column)]: tokens are separated by single spaces,
    and a parameter test <...> is one token"""
    out, i = [], 0
    while i < len(text):
        m = TEST.match(text, i) if text[i] == '<' else None
        j = m.end() if m else (text.find(' ', i) if ' ' in text[i:] else len(text))
        if j == i:
            raise ParseError('two spaces in a row', pos + i)
        out.append((text[i:j], pos + i))
        if j < len(text) and (text[j] != ' ' or j + 1 == len(text)):
            raise ParseError('expected one space', pos + j)
        i = j + 1
    return out


def phoneme(tok, pos, names):
    """a phoneme operand: NAME, NAME+d, <end>, or #hhhh (an id outside the inventory)"""
    if RAW.fullmatch(tok):
        return int(tok[1:], 16)
    if tok == '<end>':
        return PhRef(None)
    if tok in names:
        return PhRef(names[tok])
    for k in (2, 1):
        if tok[k:k + 1] == '+' and tok[:k] in names and re.fullmatch('[1-9][0-9]*', tok[k + 1:]):
            d = dec(tok[k + 1:], pos + k + 1)
            if d > 0xFFFF:
                raise ParseError('the offset +%d does not fit in 16 bits' % d, pos + k + 1)
            return PhRef(names[tok[:k]], d)
    raise ParseError('no phoneme %r' % tok, pos)


def feat_words(tok, pos):
    """[vvvvvvvv/mmmmmmmm] -> 4 words"""
    m = FEAT.fullmatch(tok)
    if not m:
        raise ParseError('expected [vvvvvvvv/mmmmmmmm]', pos)
    v, mk = int(m.group(1), 16), int(m.group(2), 16)
    return [v >> 16, v & 0xFFFF, mk >> 16, mk & 0xFFFF]


def test_words(tok, pos):
    """a parameter test <...> -> its words"""
    m = TEST.fullmatch(tok)
    if m is None:
        raise ParseError('expected a parameter test <...>', pos)
    if m.group(1):
        return [int(m.group(1), 16)]
    name, last, hi, reg, cmp, k = m.group(2, 3, 4, 5, 6, 7)
    if name not in ITEM_IDS:
        raise ParseError('no parameter or variable %r' % name, pos)
    w = ITEM_IDS[name] << 8 | (0x80 if last else 0) | (0x40 if hi else 0)
    if reg is not None:
        if reg not in CAPTURE_IDS:
            raise ParseError('no register %r' % reg, pos)
        return [w | CAPTURE_IDS[reg]]
    return [w | COMPARE_IDS[cmp], u16(k, pos)]


def _read_pattern(toks, names):
    """the tokens between { and } -> the pattern's words, header first"""
    if toks and RAW.fullmatch(toks[0][0]):
        return [phoneme(t, p, names) for t, p in toks]
    ws, h, k = [], 0, 0
    if k < len(toks) and not FEAT.fullmatch(toks[k][0]) and not TEST.fullmatch(toks[k][0]):
        ws.append(phoneme(*toks[k], names))
        h |= 0x8000
        k += 1
    if k < len(toks) and FEAT.fullmatch(toks[k][0]):
        ws += feat_words(*toks[k])
        h |= 0x4000
        k += 1
    if k < len(toks):
        h |= 0x2000
        for t, p in toks[k:]:
            ws += test_words(t, p)
    if len(ws) + 1 > 0x7FF:
        raise ParseError('the pattern is too long', toks[0][1])
    return [h | 2 * (len(ws) + 1)] + ws


def ldst_words(toks):
    """LD/ST operands (F1@k[.hi], F1@#hhhh or vXX, then t=N) -> [spec] or [spec, index]"""
    tok, p = toks[0]
    m = LDST.fullmatch(tok)
    if not m or m.group(1) not in ITEM_IDS or len(toks) > 2:
        raise ParseError('expected F1@k or vXX', p)
    iid, t = ITEM_IDS[m.group(1)], 0
    if len(toks) == 2:
        tt, tp = toks[1]
        if not (tt.startswith('t=') and DEC.fullmatch(tt[2:]) and -128 <= dec(tt[2:], tp) <= 127
                and dec(tt[2:], tp)):
            raise ParseError('expected t=N with N in -128..127, not 0', tp)
        t = dec(tt[2:], tp) & 0xFF
    spec = t << 8 | iid
    if iid >= 0x80:
        if m.group(2) or m.group(3):
            raise ParseError('a variable has no index', p)
        return [spec]
    if m.group(2):
        return [spec, int(m.group(2), 16)]
    if m.group(3) is None:
        raise ParseError('a parameter needs an index @k', p)
    k = dec(m.group(3), p)
    if not -64 <= k <= 63:
        raise ParseError('the index %d is not in -64..63' % k, p)
    return [spec, (2 * k & 0xFF) << 8 | (0x80 if m.group(4) else 0)]


def _shaped_args(op, toks, names):
    """op's operands from toks in its shaped form, or None if toks are not in that form"""
    t = [x for x, _ in toks]
    if op in PH_OPS:
        return [phoneme(*toks[0], names)] if len(t) == 1 else None
    if op in FEAT_OPS:
        return feat_words(*toks[0]) if len(t) == 1 and FEAT.fullmatch(t[0]) else None
    if op == 0x68:
        return _read_pattern(toks[1:-1], names) if len(t) >= 2 and t[0] == '{' and t[-1] == '}' else None
    if op in (0x6C, 0x9C):
        m = re.fullmatch(r'([0-9]+),([0-9]+)', t[0]) if t else None
        if not (m and len(t) >= 3 and t[1] == '{' and t[-1] == '}'):
            return None
        lo, hi = dec(m.group(1), toks[0][1]), dec(m.group(2), toks[0][1])
        if lo > 255 or hi > 255:
            raise ParseError('a count is more than 255', toks[0][1])
        return [lo << 8 | hi] + _read_pattern(toks[2:-1], names)
    if op in (REPL, REPL_NEXT):
        if not (len(t) >= 3 and t[0] == 'del' and re.fullmatch('[0-9]+', t[1]) and t[2] == '->'):
            return None
        d, ids = dec(t[1], toks[1][1]), [phoneme(x, p, names) for x, p in toks[3:]]
        w = d * 256 + 12 * len(ids)
        if w > 0xFFFF or rep_split(w) != (d, len(ids)):
            raise ParseError('del %d with %d phonemes does not fit a REPL word' % (d, len(ids)),
                             toks[1][1])
        return [w] + ids
    if op in LDST_OPS:
        return ldst_words(toks) if t and not RAW.fullmatch(t[0]) else None
    if op == 0x188:
        m = re.fullmatch(r'(-?[0-9]+)/2\^(-?[0-9]+)', t[0]) if len(t) == 1 else None
        return [u16(m.group(1), toks[0][1]), u16(m.group(2), toks[0][1])] if m else None
    if op in (0x10, 0xC8):
        m = re.fullmatch(r'#([0-9]+)', t[0]) if len(t) == 1 else None
        return [u16(m.group(1), toks[0][1])] if m else None
    if op in FIXED and len(t) == FIXED[op] and all(DEC.fullmatch(x) for x in t):
        return [u16(x, p) for x, p in toks]
    return None


def _read_instr(text, pos, names):
    toks = _tokens(text, pos)
    if not toks:
        raise ParseError('expected an instruction', pos)
    (m, mp), rest = toks[0], toks[1:]
    if m not in OPCODES:
        raise ParseError('no instruction %r' % m, mp)
    op = OPCODES[m]
    args = _shaped_args(op, rest, names)
    if args is None:
        args = [phoneme(x, p, names) for x, p in rest]
    ins = Instr(op, args)
    check_shape(ins, mp)
    if op in (REPL, REPL_NEXT) and isinstance(args[0], int) and rep_carries(args[0]):
        ins.legacy = True   # as decode_instr marks it: the shipped files have 5 such words
    return ins


def check_shape(ins, pos):
    """refuse an instruction whose words would not read back as this instruction: the
    operand count its opcode and operands give, and phonemes only where ids go"""
    ws = [a if isinstance(a, int) else 0 for a in ins.args]
    n = operand_count(pack_words([ins.op] + ws + [0, 0]), 0)
    if n != len(ws):
        raise ParseError('%s has %d operand words here, not %d' % (MNEMONICS[ins.op], n, len(ws)), pos)
    try:
        ids = set(ph_positions(ins))
    except IndexError:
        ids = set()
    if any(isinstance(a, PhRef) and k not in ids for k, a in enumerate(ins.args)):
        raise ParseError('a phoneme where %s has no phoneme id' % MNEMONICS[ins.op], pos)


def words(rule):
    """the rule's words, each phoneme reference as (id of its Phoneme, delta): two rules with
    equal words are the same rule"""
    return encode_rule(rule, lambda ref: (id(ref.ph), ref.delta))


# where the word that fixes an instruction's operand count lies, in words after its opcode
COUNT_WORD = {0x68: 1, 0x6C: 2, 0x9C: 2, REPL: 1, REPL_NEXT: 1, 0x100: 1, 0x104: 1, 0x108: 1, 0x10C: 1}


def check_length(rule, poss=None):
    """refuse a rule longer than its length word holds, or whose instructions would not take
    the operand words they have when the rule is read back (the count of FOCUSP, RP, LP, REPL
    and LD/ST comes from a word that may be the next instruction's); returns the rule. poss: the
    text column of each instruction, for the error"""
    if 2 + 2 * sum(1 + len(i.args) for i in rule.instrs) > 0xFFFF:
        raise ParseError('the rule is longer than 65535 bytes')
    ws, starts = [], []
    for ins in rule.instrs:
        starts.append(len(ws))
        ws += [ins.op] + [a if isinstance(a, int) else 0 for a in ins.args]
    data = pack_words(ws + [0, 0])
    for i, (ins, k) in enumerate(zip(rule.instrs, starts)):
        name, pos = MNEMONICS[ins.op], poss[i] if poss else None
        if k + COUNT_WORD.get(ins.op, 0) >= len(ws):
            raise ParseError('%s lacks the word that gives its operand count' % name, pos)
        n = operand_count(data, 2 * k)
        if n != len(ins.args):
            raise ParseError('%s has %d operand words here, not %d' % (name, n, len(ins.args)), pos)
    return rule


def parse_rule(text, names):
    """a line in instruction form -> Rule; names from ph_names(); raises ParseError"""
    head, sep, body = text.partition(' ')
    if head not in ('RULE', 'RULE*'):
        raise ParseError('a line starts with RULE or RULE*', 0)
    instrs, poss, pos = [], [], len(head) + 1
    if sep:
        for part in body.split(' ; '):
            instrs.append(_read_instr(part, pos, names))
            poss.append(pos)
            pos += len(part) + 3
    return check_length(Rule(head == 'RULE*', instrs), poss)
