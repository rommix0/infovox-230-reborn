"""Rules in rule notation, A → B / C _ D (spec: Rule notation; the grammar is NOTATION.md).

notation_line(rule, names) writes a rule in notation, or returns None when the notation
cannot say it. It reads its own text back with parse_notation and keeps it only if that
gives the rule's words exactly, so a rule shown in notation is that rule. Every other rule
is shown in instruction form (listing.py).

Text and instructions meet in Form, the rule's parts in reading order: the left context,
the groups (each a left-hand side, an arrow and its actions), the right context.
"""
import re
from dataclasses import dataclass, field

from .info import item_name, ph_name
from .listing import (CAPTURE, CAPTURE_IDS, ITEM_IDS, ParseError, _ldst, _Out, _read_instr,
                      _shaped, check_length, dec, ldst_words, rule_line, u16, words)
from .model import PhRef
from .rules import NEXT, REPL, REPL_NEXT, Instr, Rule, rep_split

FOCUSP, RP, LP, UNWRAP, SETF_NEXT, SETF, CPREV, LD, A_SET = (
    0x68, 0x6C, 0x9C, 0xB4, 0xC0, 0xC4, 0xCC, 0x100, 0x150)
ST_OPS = {0x104: ':=s', 0x108: ':=u', 0x10C: ':='}
COMPUTE = set(range(0x150, 0x1D0, 4)) | {LD} | set(ST_OPS)
# role -> (opcode for a phoneme, opcode for a feature bundle)
OPS = {'seek': (0x40, 0x54), 'focus': (0x44, 0x58), 'scan': (0x48, 0x5C), 'r': (0x4C, 0x60),
       'r?': (0x50, 0x64), 'l': (0x7C, 0x90), 'l?': (0x80, 0x94)}
ROLE = {op: (role, k == 1) for role, pair in OPS.items() for k, op in enumerate(pair)}
COMPARE = {0x18: '<', 0x1E: '≤', 0x24: '=', 0x2C: '≠', 0x34: '≥', 0x3C: '>'}   # value OP k
COMPARE_IDS = {s: op for op, s in COMPARE.items()}
SAFE = 'A-Za-z0-9:$%\'@!"`.,=|~\\\\+\\-?#&'   # phoneme names of these are written bare


@dataclass(eq=False)
class Elem:
    """one segment of the match: a phoneme, a feature bundle, parameter tests, or several"""
    ph: object = None        # PhRef
    feat: tuple = None       # (V, M), 32 bits each
    tests: list = None       # parameter-test words
    rep: tuple = None        # (min, opt) of a pattern; None for a plain element
    opt: bool = False        # (x): R?, L?
    search: bool = False     # … x: SCAN, SEEK

    def simple(self):
        """one phoneme or one bundle, no tests: matched by a plain or a feature opcode"""
        return (self.ph is None) != (self.feat is None) and not self.tests


@dataclass(eq=False)
class Group:
    """LHS → RHS: a REPL (ids, [] for ∅), an UNWRAP, or the focus kept; then SETFs and
    statements in order. plus: the group starts one segment after the focus (→⁺)"""
    lhs: list = field(default_factory=list)
    plus: bool = False
    repl: list = None
    unwrap: bool = False
    mods: list = field(default_factory=list)     # ('setf', (V, M)) or ('stmts', [[Instr]])
    n: int = 0                     # decoding: the number of segments on the left-hand side


@dataclass(eq=False)
class Form:
    iterative: bool = False
    label: int = None              # the NEXT operand; None: the rule has no NEXT (once)
    groups: list = field(default_factory=list)
    left: list = field(default_factory=list)     # left context, reading order
    right: list = field(default_factory=list)    # right context
    back: list = None              # left context matched after CPREV (⟵), reading order
    anchored: bool = False         # ^: the match starts at the start position, no search

    def lhs(self):
        return [e for g in self.groups for e in g.lhs]


class _NoForm(Exception):
    """a rule the notation does not write"""


# --- instructions -> Form

def _feat(ws):
    return ws[0] << 16 | ws[1], ws[2] << 16 | ws[3]


def _elem_of(ins):
    """a match instruction -> (role, Elem)"""
    op, a = ins.op, ins.args
    if op in ROLE:
        role, isfeat = ROLE[op]
        if isfeat and len(a) == 4:
            e = Elem(feat=_feat(a))
        elif not isfeat and len(a) == 1 and isinstance(a[0], PhRef):
            e = Elem(ph=a[0])
        else:
            raise _NoForm
        if role.endswith('?'):
            e.opt, role = True, role[0]
        return role, e
    if op == FOCUSP:
        return 'focus', _pattern_elem(a, (1, 0))
    if op in (RP, LP) and a and isinstance(a[0], int):
        return ('r' if op == RP else 'l'), _pattern_elem(a[1:], (a[0] >> 8, a[0] & 0xFF))
    raise _NoForm


def _pattern_elem(ws, rep):
    if not ws or not isinstance(ws[0], int):
        raise _NoForm
    h, e, i = ws[0], Elem(rep=rep), 1
    if h & 0x1001 or (h & 0x0FFE) // 2 != len(ws):
        raise _NoForm
    if h & 0x8000:
        if i >= len(ws) or not isinstance(ws[i], PhRef):
            raise _NoForm
        e.ph, i = ws[i], i + 1
    if h & 0x4000:
        if i + 4 > len(ws) or not all(isinstance(w, int) for w in ws[i:i + 4]):
            raise _NoForm
        e.feat, i = _feat(ws[i:i + 4]), i + 4
    if h & 0x2000:
        e.tests = list(ws[i:])
        if not e.tests or not all(isinstance(w, int) for w in e.tests):
            raise _NoForm
    elif i != len(ws):
        raise _NoForm
    return e


def _groups(acts):
    """the actions -> [Group] with each group's LHS length in .n"""
    groups, k = [], 0
    while k < len(acts):
        op, a = acts[k].op, acts[k].args
        if op in (REPL, REPL_NEXT) and (op == REPL_NEXT or not groups):
            if not a or not isinstance(a[0], int) or not all(isinstance(x, PhRef) for x in a[1:]):
                raise _NoForm
            d, n = rep_split(a[0])
            if n != len(a) - 1 or d * 256 + 12 * n != a[0] or 12 * n > 0xFF:
                raise _NoForm             # 12*n carries into the high byte: the engine misreads it
            g = Group(plus=op == REPL_NEXT, repl=list(a[1:]))
            g.n = d
        elif op == UNWRAP and not groups and len(acts) == 1:
            if len(a) != 1 or not 1 <= a[0] <= 0xFF:
                raise _NoForm
            g = Group(unwrap=True)
            g.n = a[0]
        elif op == SETF_NEXT and len(a) == 4:
            g = Group(plus=True, mods=[('setf', _feat(a))])
            g.n = 1
        elif not groups and (op == SETF or op in COMPUTE):
            g = Group()
            g.n = 1
            groups.append(g)
            continue
        elif groups and op == SETF and len(a) == 4:
            groups[-1].mods.append(('setf', _feat(a)))
            k += 1
            continue
        elif groups and op in COMPUTE:
            fused = op == A_SET and k + 1 < len(acts) and acts[k + 1].op in ST_OPS
            stmt = acts[k:k + 2] if fused else acts[k:k + 1]
            mods = groups[-1].mods
            if mods and mods[-1][0] == 'stmts':
                mods[-1][1].append(stmt)
            else:
                mods.append(('stmts', [stmt]))
            k += len(stmt)
            continue
        else:
            raise _NoForm
        groups.append(g)
        k += 1
    if not groups:
        raise _NoForm
    return groups


def decode(rule):
    """Rule -> Form; raises _NoForm for a rule the notation does not write"""
    ins = list(rule.instrs)
    if not all(_shaped(i) for i in ins):
        raise _NoForm                     # operands not in their opcode's shape (hand-built)
    form = Form(iterative=rule.iterative)
    if ins and ins[-1].op == NEXT:
        if len(ins[-1].args) != 1 or not isinstance(ins[-1].args[0], int):
            raise _NoForm
        form.label = ins.pop().args[0]
    fwd, back, k = [], None, 0
    while k < len(ins) and (ins[k].op in ROLE or ins[k].op in (FOCUSP, RP, LP, CPREV)):
        if ins[k].op == CPREV:
            if back is not None or ins[k].args:
                raise _NoForm
            back = []
        else:
            role, e = _elem_of(ins[k])
            if (role == 'l') != (back is not None):
                raise _NoForm
            (fwd if back is None else back).append((role, e))
        k += 1
    focus = [i for i, (role, _) in enumerate(fwd) if role in ('seek', 'focus')]
    if len(focus) != 1:
        raise _NoForm
    form.anchored = fwd[0][0] not in ('seek', 'scan')
    for i, (role, e) in enumerate(fwd):
        e.search = i > 0 and role in ('seek', 'scan')
    if back is not None:
        form.back = [e for _, e in reversed(back)]
    form.groups = _groups(ins[k:])
    elems = [e for _, e in fwd]
    start = focus[0] + (1 if form.groups[0].plus else 0)
    for g in form.groups:
        if start + g.n > len(elems):
            raise _NoForm
        g.lhs = elems[start:start + g.n]
        start += g.n
    form.left = elems[:focus[0] + (1 if form.groups[0].plus else 0)]
    form.right = elems[start:]
    return form


# --- Form -> text

def ph_word(ref):
    """a phoneme operand as notation writes it; _NoForm for one it does not write"""
    if not isinstance(ref, PhRef) or ref.ph is None or ref.delta:
        raise _NoForm
    name = ph_name(ref.ph.name)
    if name == ' ':
        return '␠'
    if re.fullmatch('[%s]+' % SAFE, name) and not re.match(r'#[0-9]', name):
        return name
    if not name or '›' in name:
        raise _NoForm
    return '‹%s›' % name


def bundle(v, m):
    """[+b20 −b24] (V ⊆ M), or [V=hhhhhhhh M=hhhhhhhh]"""
    if v & ~m:
        return '[V=%08x M=%08x]' % (v, m)
    return '[%s]' % ' '.join(('+' if v >> b & 1 else '−') + 'b%d' % b
                             for b in range(32) if m >> b & 1)


def _tests(ws):
    out, i = [], 0
    while i < len(ws):
        w = ws[i]
        name = item_name(w >> 8) + ('.last' if w & 0x80 else '') + ('.hi' if w & 0x40 else '')
        if w & 0x3F in CAPTURE:
            out.append('<%s → %s>' % (name, CAPTURE[w & 0x3F]))
            i += 1
        elif w & 0x3F in COMPARE and i + 1 < len(ws):
            k = ws[i + 1]
            out.append('<%s %s %d>' % (name, COMPARE[w & 0x3F], k - 0x10000 if k & 0x8000 else k))
            i += 2
        else:
            raise _NoForm
    return out


def _rep(e):
    """the repetition suffix; nothing for a pattern that matches once and needs the pattern
    form anyway (several parts, tests, or none)"""
    if e.rep is None or (e.rep == (1, 0) and not e.simple()):
        return ''
    lo, hi = e.rep
    if (lo, hi) == (0, 127):
        return '*'
    return '{%d}' % lo if hi == 0 else '{%d,%d}' % (lo, hi)


def _elem(o, e, lead=True):
    """an element, with a space before it if lead"""
    if e.search:
        o.tok('…', 'op')
    if lead:
        o.put(' ')
    if e.opt:
        o.put('(')
    if e.ph is not None:
        o.put(ph_word(e.ph), 'ph', e.ph)
    if e.feat is not None:
        o.put(bundle(*e.feat), 'feat')
    if e.ph is None and e.feat is None and not e.tests:
        o.put('·', 'op')
    for t in _tests(e.tests or []):
        o.put(t, 'param')
    if _rep(e):
        o.put(_rep(e), 'num')
    if e.opt:
        o.put(')')


def _stmt(group):
    """X := n, X := A, B = X, or an accumulator instruction in instruction form"""
    a, b = group[0], group[-1]
    if b.op in ST_OPS or a.op == LD:
        o = _Out()
        _ldst(o, b.args)
        target = o.line().text[1:]
        if a.op == LD:
            return 'B = ' + target
        value = 'A' if len(group) == 1 else str(a.args[0] - 0x10000 if a.args[0] & 0x8000 else a.args[0])
        return '%s %s %s' % (target, ST_OPS[b.op], value)
    return rule_line(Rule(False, [a])).text[len('RULE '):]


def write(form):
    """Form -> Line; raises _NoForm"""
    o = _Out()
    head = ('*' if form.iterative else '') + ('' if form.label is None else '#%d' % form.label)
    if head:
        o.put(head, 'num')
        o.put(' ')
    for gi, g in enumerate(form.groups):
        if gi:
            o.tok(';', 'op')
            o.put(' ')
        for k, e in enumerate(g.lhs):
            search, e.search = e.search, False
            _elem(o, e, lead=k > 0)
            e.search = search
        if not g.lhs:
            o.put('∅', 'op')
        o.tok('→⁺' if g.plus else '→', 'op')
        if g.unwrap:
            o.tok('∅', 'op')
            o.tok('↺', 'op')
        if g.repl is not None:
            for ref in g.repl:
                o.tok(ph_word(ref), 'ph', ref)
            if not g.repl:
                o.tok('∅', 'op')
        for kind, val in g.mods:
            if kind == 'setf':
                o.tok(bundle(*val), 'feat')
            else:
                o.tok('{ %s }' % '; '.join(_stmt(s) for s in val), 'param')
    lhs = form.lhs()
    if form.left or form.right or form.back is not None or form.anchored or (lhs and lhs[0].search):
        o.tok('/', 'op')
        if form.back is not None:
            for e in form.back:
                _elem(o, e)
            o.tok('⟵', 'op')
        if form.anchored:
            o.tok('^', 'op')
        for e in form.left:
            _elem(o, e)
        if lhs and lhs[0].search:
            o.tok('…', 'op')
        o.tok('_', 'op')
        for e in form.right:
            _elem(o, e)
    if form.label is None:
        o.tok('once', 'op')
    return o.line()


# --- text -> Form

_LEX = re.compile(r'''
    (?P<ws>\s+)
  | (?P<esc>‹[^›]*›)
  | (?P<space>␠)
  | (?P<any>·)
  | (?P<bundle>\[[^\]]*\])
  | (?P<test><[^\s<>.]+(?:\.last)?(?:\.hi)?\ (?:→\ \w+|[<≤=≠≥>]\ -?[0-9]+)>)
  | (?P<rep>\{[0-9]+(?:,[0-9]+)?\}|\*)
  | (?P<block>\{[^{}]*\})
  | (?P<arrow>→⁺|→)
  | (?P<once>once(?!\S))
  | (?P<sym>[()∅↺/_…^⟵;])
  | (?P<name>[%s]+)
''' % SAFE, re.X)
_HEAD = re.compile(r'\*?#[0-9]+(?!\S)|\*(?!\S)')
_NTEST = re.compile(r'<([^\s<>.]+)(\.last)?(\.hi)? (?:→ (\w+)|([<≤=≠≥>]) (-?[0-9]+))>')
ATOM = ('name', 'esc', 'space', 'any', 'bundle', 'test')


@dataclass
class Tok:
    kind: str
    text: str
    pos: int
    attached: bool      # no space before it


def lex(text):
    out, i, gap = [], 0, True
    m = _HEAD.match(text)
    if m:
        out.append(Tok('head', m.group(), 0, False))
        i = m.end()
    while i < len(text):
        m = _LEX.match(text, i)
        if not m:
            raise ParseError('unexpected %r' % text[i], i)
        if m.lastgroup == 'ws':
            gap = True
        else:
            out.append(Tok(m.lastgroup, m.group(), i, not gap))
            gap = False
        i = m.end()
    return out


class _Reader:
    def __init__(self, text, names):
        self.toks, self.k, self.names, self.end = lex(text), 0, names, len(text)

    def peek(self, kind=None, text=None, at=0):
        k = self.k + at
        if k < len(self.toks):
            t = self.toks[k]
            if (kind is None or t.kind == kind) and (text is None or t.text == text):
                return t
        return None

    def take(self, kind=None, text=None, what=None):
        t = self.peek(kind, text)
        if t is None:
            raise ParseError('expected %s' % (what or text or kind), self.pos())
        self.k += 1
        return t

    def pos(self):
        return self.toks[self.k].pos if self.k < len(self.toks) else self.end

    def sym(self, s):
        """take symbol s if it comes next"""
        if self.peek('sym', s):
            self.k += 1
            return True
        return False

    def phoneme(self, t):
        name = {'space': ' ', 'esc': t.text[1:-1], 'name': t.text}[t.kind]
        key = {' ': '␠', ';': '⁏'}.get(name, name)
        if key not in self.names:
            raise ParseError('no phoneme %r' % name, t.pos)
        return PhRef(self.names[key])

    def at_elem(self):
        t = self.peek()
        return t is not None and (t.kind in ATOM or (t.kind == 'sym' and t.text in ('(', '…')
                                                     and self.peek('sym', '_', 1) is None))

    def elem(self):
        """[…] atom, or (atom)"""
        e = Elem(search=self.sym('…'))
        if self.sym('('):
            self.atom(e)
            t = self.take('sym', ')')
            if not t.attached:
                raise ParseError('no space before )', t.pos)
            if not e.simple() or e.rep is not None:
                raise ParseError('(x) holds one phoneme or one bundle', t.pos)
            e.opt = True
        else:
            self.atom(e)
        return e

    def atom(self, e):
        """name, bundle, tests and repetition, written together without spaces, in that order"""
        t, stage = self.take(what='a phoneme, ·, [features] or <test>'), 0
        while True:
            if t.kind in ('name', 'esc', 'space') and stage == 0:
                e.ph, stage = self.phoneme(t), 1
            elif t.kind == 'any' and stage == 0:
                stage = 2
            elif t.kind == 'bundle' and stage <= 1:
                e.feat, stage = read_bundle(t), 2
            elif t.kind == 'test' and stage <= 3:
                e.tests, stage = (e.tests or []) + read_test(t), 3
            elif t.kind == 'rep' and stage >= 1:
                e.rep, stage = read_rep(t), 4
            else:
                raise ParseError('unexpected %r in an element' % t.text, t.pos)
            nxt = self.peek()
            if nxt is None or not nxt.attached or nxt.kind not in ATOM + ('rep',):
                break
            t = self.take()
        if e.rep is None and not e.simple():
            e.rep = (1, 0)


def read_bundle(t):
    body = t.text[1:-1]
    m = re.fullmatch(r'V=([0-9a-f]{8}) M=([0-9a-f]{8})', body)
    if m:
        v, mk = int(m.group(1), 16), int(m.group(2), 16)
        if not v & ~mk:
            raise ParseError('V is inside M: write the bits as [+bN −bN]', t.pos)
        return v, mk
    v = mk = 0
    for part in body.split(' ') if body else []:
        m = re.fullmatch(r'([+−])b([0-9]+)', part)
        bit = dec(m.group(2), t.pos) if m else 99
        if bit > 31 or mk >> bit & 1:
            raise ParseError('expected +bN or −bN, N in 0..31, each bit once', t.pos)
        mk |= 1 << bit
        v |= (m.group(1) == '+') << bit
    return v, mk


def read_test(t):
    name, last, hi, reg, cmp, k = _NTEST.fullmatch(t.text).groups()
    if name not in ITEM_IDS:
        raise ParseError('no parameter or variable %r' % name, t.pos)
    w = ITEM_IDS[name] << 8 | (0x80 if last else 0) | (0x40 if hi else 0)
    if reg is not None:
        if reg not in CAPTURE_IDS:
            raise ParseError('no register %r' % reg, t.pos)
        return [w | CAPTURE_IDS[reg]]
    return [w | COMPARE_IDS[cmp], u16(k, t.pos)]


def read_rep(t):
    if t.text == '*':
        return 0, 127
    lo, _, hi = t.text[1:-1].partition(',')
    lo, hi = dec(lo, t.pos), dec(hi or '0', t.pos)
    if lo > 255 or hi > 255:
        raise ParseError('a count is more than 255', t.pos)
    return lo, hi


def read_block(t, names):
    """{ statement; ... } -> [[Instr]]"""
    out, pos = [], t.pos
    for s in t.text[1:-1].split(';'):
        s, pos = s.strip(), pos + 1
        m = re.fullmatch(r'(.+?) (:=s|:=u|:=) (A|-?[0-9]+)', s)
        if m:
            target = ldst_words([(x, pos) for x in m.group(1).split(' ')])
            st = {v: k for k, v in ST_OPS.items()}[m.group(2)]
            pre = [] if m.group(3) == 'A' else [Instr(A_SET, [u16(m.group(3), pos)])]
            out.append(pre + [Instr(st, target)])
        elif s.startswith('B = '):
            out.append([Instr(LD, ldst_words([(x, pos) for x in s[4:].split(' ')]))])
        elif s:
            ins = _read_instr(s, pos, names)
            if ins.op not in COMPUTE or ins.op == LD or ins.op in ST_OPS:
                raise ParseError('%r is not a statement' % s, pos)
            out.append([ins])
        else:
            raise ParseError('an empty statement', pos)
        pos += len(s)
    return out


def _group(r, first):
    g = Group()
    if not r.sym('∅'):
        while not r.peek('arrow'):
            if not r.at_elem():
                raise ParseError('expected →', r.pos())
            g.lhs.append(r.elem())
    arrow = r.take('arrow')
    g.plus = arrow.text == '→⁺'
    if not first and not g.plus:
        raise ParseError('after the first group, each acts after the one before: →⁺', arrow.pos)
    if r.sym('∅'):
        if first and not g.plus and r.sym('↺'):
            g.unwrap = True
            return g
        g.repl = []
    elif r.peek('name') or r.peek('esc') or r.peek('space'):
        g.repl = []
        while r.peek('name') or r.peek('esc') or r.peek('space'):
            g.repl.append(r.phoneme(r.take()))
    while r.peek('bundle') or r.peek('block'):
        t = r.take()
        g.mods.append(('setf', read_bundle(t)) if t.kind == 'bundle' else ('stmts', read_block(t, r.names)))
    if g.repl is None and not g.mods:
        raise ParseError('expected phonemes, ∅, [features] or { statements }', r.pos())
    if g.repl is None and g.plus and g.mods[0][0] != 'setf':
        raise ParseError('→⁺ without phonemes starts with [features] (SETF+)', r.pos())
    return g


def read(text, names):
    """notation text -> Form; raises ParseError"""
    r = _Reader(text, names)
    form = Form()
    t = r.peek('head')
    if t:
        r.k += 1
        form.iterative = t.text.startswith('*')
        if '#' in t.text:
            form.label = dec(t.text.lstrip('*#'), t.pos)
            if form.label > 0xFFFF:
                raise ParseError('a label is at most 65535', t.pos)
    form.groups.append(_group(r, True))
    while r.sym(';'):
        form.groups.append(_group(r, False))
    if r.sym('/'):
        _env(r, form)
    if r.peek('once'):
        if form.label is not None:
            raise ParseError('a rule with a label #N is not once', r.pos())
        r.k += 1
    elif form.label is None:
        raise ParseError('expected once: a rule without a label #N', r.pos())
    if r.peek() is not None:
        raise ParseError('unexpected %r' % r.peek().text, r.pos())
    return form


def _env(r, form):
    elems = []
    while not r.sym('_'):
        if r.peek('sym', '⟵'):
            if form.back is not None or form.anchored:
                raise ParseError('⟵ comes once, before ^', r.pos())
            r.k += 1
            form.back, elems = elems, []
        elif r.peek('sym', '^'):
            if form.anchored or elems:
                raise ParseError('^ comes before the first element of the match', r.pos())
            r.k += 1
            form.anchored = True
        elif r.peek('sym', '…') and r.peek('sym', '_', 1):
            lhs = form.lhs()
            if not lhs or form.groups[0].plus:
                raise ParseError('… _ searches for the focus, the first element on the left-hand side',
                                 r.pos())
            r.k += 1
            lhs[0].search = True
        elif r.at_elem():
            elems.append(r.elem())
        else:
            raise ParseError('expected _', r.pos())
    if form.back is not None and elems:
        raise ParseError('⟵ is not used with other left context', r.pos())
    form.left = elems
    while r.at_elem():
        form.right.append(r.elem())


# --- Form -> instructions

def _feat_words(f):
    v, m = f
    return [v >> 16, v & 0xFFFF, m >> 16, m & 0xFFFF]


def _pattern_words(e):
    ws, h = [], 0
    if e.ph is not None:
        ws.append(e.ph)
        h |= 0x8000
    if e.feat is not None:
        ws += _feat_words(e.feat)
        h |= 0x4000
    if e.tests:
        ws += e.tests
        h |= 0x2000
    if len(ws) >= 0x7FF:
        raise ParseError('the pattern is too long')
    return [h | 2 * (len(ws) + 1)] + ws


def _match(role, e):
    """an element in its role ('seek', 'focus', 'scan', 'r', 'l') -> Instr"""
    if role in ('seek', 'scan') and not (e.simple() and e.rep is None and not e.opt):
        raise ParseError('a search (the first element, or after …) is for one phoneme or one bundle')
    if role == 'focus' and (e.opt or e.rep not in (None, (1, 0))):
        raise ParseError('the focus is one segment: no (x), {n} or *')
    if e.rep is None:
        op = OPS[role + ('?' if e.opt else '')][e.ph is None]
        return Instr(op, [e.ph] if e.ph is not None else _feat_words(e.feat))
    if role == 'focus':
        return Instr(FOCUSP, _pattern_words(e))
    return Instr(RP if role == 'r' else LP, [e.rep[0] << 8 | e.rep[1]] + _pattern_words(e))


def encode(form):
    """Form -> Rule; raises ParseError"""
    g0 = form.groups[0]
    for g in form.groups:
        if g.repl is None and not g.unwrap and len(g.lhs) != 1:
            raise ParseError('a group that keeps its segment has one element on the left')
        if g.unwrap and not g.lhs:
            raise ParseError('↺ deletes the left-hand side: not ∅')
        if g.repl is not None and (len(g.lhs) > 255 or len(g.repl) > 21):
            raise ParseError('a REPL deletes at most 255 and inserts at most 21 segments')
    if g0.plus and (not form.left or form.back is not None):
        raise ParseError('→⁺ acts after the focus, the last element of the left context (not with ⟵)')
    lhs = form.lhs()
    seq = form.left + lhs + form.right
    f = len(form.left) - (1 if g0.plus else 0)
    if f >= len(seq):
        raise ParseError('no focus: ∅ on the left needs right context')
    for e in lhs:
        if e.opt or e.rep not in (None, (1, 0)) or (e.search and e is not seq[f]):
            raise ParseError('the left-hand side is single segments: no (x), {n}, * or …')
    if seq[0].search:
        raise ParseError('the first element is searched for already: no …')
    instrs = []
    for i, e in enumerate(seq):
        search = e.search or (i == 0 and not form.anchored)
        role = ('seek' if search else 'focus') if i == f else ('scan' if search else 'r')
        instrs.append(_match(role, e))
    if form.back is not None:
        instrs.append(Instr(CPREV, []))
        for e in reversed(form.back):
            if e.search:
                raise ParseError('no … before ⟵')
            instrs.append(_match('l', e))
    for g in form.groups:
        mods = list(g.mods)
        if g.repl is not None:
            instrs.append(Instr(REPL_NEXT if g.plus else REPL, [len(g.lhs) * 256 + 12 * len(g.repl)] + g.repl))
        elif g.unwrap:
            instrs.append(Instr(UNWRAP, [len(g.lhs)]))
        elif g.plus:
            instrs.append(Instr(SETF_NEXT, _feat_words(mods.pop(0)[1])))
        for kind, val in mods:
            if kind == 'setf':
                instrs.append(Instr(SETF, _feat_words(val)))
            else:
                instrs += [Instr(i.op, list(i.args)) for s in val for i in s]
    if form.label is not None:
        instrs.append(Instr(NEXT, [form.label]))
    return check_length(Rule(form.iterative, instrs))


# --- the API

def parse_notation(text, names):
    """a rule in notation -> Rule; names from listing.ph_names(); raises ParseError"""
    return encode(read(text, names))


def notation_line(rule, names):
    """the rule in notation as a listing.Line, or None if the notation cannot say it"""
    try:
        line = write(decode(rule))
        again = parse_notation(line.text, names)
    except (_NoForm, ParseError):
        return None
    return line if words(again) == words(rule) else None
