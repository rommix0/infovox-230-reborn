"""The checks the writer runs before saving (spec: Writer, "Checks before saving")."""
from dataclasses import dataclass

from .model import PhRef, VarItem
from .rules import REPL, REPL_NEXT, ph_positions, rep_split


@dataclass
class Problem:
    where: str        # e.g. "phoneme 'A:'", "M0 rule 12 (#32)", "lexicon entry 40 ('BORT')"
    message: str
    target: tuple = None    # what the Problems panel jumps to: ('phoneme', ph), ('rule', module, i),
                            # ('lexicon', i), or a tree node: ('header',), ('params',), ('chars',), ...
    warning: bool = False   # loads and runs, but probably not what was meant; does not stop a save

    def __str__(self):
        return '%s: %s%s' % (self.where, 'warning: ' if self.warning else '', self.message)


def errors(problems):
    """the problems that stop a save"""
    return [p for p in problems if not p.warning]


def ph_label(ph):
    name = getattr(ph, 'name', None)
    if isinstance(name, bytes):
        return 'phoneme %r' % name.lstrip(b'\0').decode('latin-1')
    return 'phoneme %r' % (name,)


def rule_label(module, i, rule):
    return '%s rule %d' % (module, i) + ('' if rule.label is None else ' (#%d)' % rule.label)


def name_ok(name):
    """a stored name the engine and the listing can use: 2 bytes, one character with a
    leading NUL or two characters, no other NUL"""
    return isinstance(name, bytes) and len(name) == 2 and name[1] != 0


def _check_phoneme(ph, out):
    """append ph's problems to out; -> its record size in bytes"""
    w, t = ph_label(ph), ('phoneme', ph)
    if not name_ok(ph.name):
        out.append(Problem(w, 'the name must be one or two characters, without NUL', t))
    if not (isinstance(ph.feat, int) and isinstance(ph.mask, int)
            and 0 <= ph.feat <= 0xFFFFFFFF and 0 <= ph.mask <= 0xFFFFFFFF):
        out.append(Problem(w, 'feat and mask must be 32-bit', t))
    ids, size = [], 12
    for it in ph.items:
        if isinstance(it, VarItem):
            size += 4
            ids.append(it.vid - 0x100)
            if not 0x80 <= it.vid <= 0xFF:
                out.append(Problem(w, 'variable id 0x%x is not in 0x80..0xFF' % it.vid, t))
            if not -0x8000 <= it.value <= 0x7FFF:
                out.append(Problem(w, 'v%02x = %d does not fit 16 bits' % (it.vid, it.value), t))
        else:
            size += 2 + 2 * len(it.points)
            ids.append(it.pid)
            if not 0 <= it.pid < 0x80:
                out.append(Problem(w, 'parameter id 0x%x is not in 0..0x7F' % it.pid, t))
            if not 1 <= len(it.points) <= 127:
                out.append(Problem(w, 'parameter %d has %d points (1..127)' % (it.pid, len(it.points)), t))
            for pt, v in it.points:
                if not (-128 <= pt <= 127 and 0 <= v <= 255):
                    out.append(Problem(w, 'point %d@%d: time must be -128..127, value 0..255' % (v, pt), t))
    if ids != sorted(set(ids)):
        out.append(Problem(w, 'items must be in ascending signed-id order, each id once', t))
    if size > 0x7FFF:
        out.append(Problem(w, 'the record is %d bytes (at most 32767)' % size, t))
    return size


def _check_instr(ins, w, t, offset, out):
    for k in ph_positions(ins):
        a = ins.args[k]
        if isinstance(a, PhRef) and a.ph is not None:
            if id(a.ph) not in offset:
                out.append(Problem(w, 'uses %s, which is not in the inventory' % ph_label(a.ph), t))
            elif not 0 <= offset[id(a.ph)] + a.delta <= 0x7FFF:
                out.append(Problem(w, 'the id of %s+%d is past 0x7FFF: ids are signed 16-bit'
                                   % (ph_label(a.ph), a.delta), t))
    if ins.op in (REPL, REPL_NEXT):
        if not ins.args or not isinstance(ins.args[0], int):
            out.append(Problem(w, 'REPL needs its count word first', t))
        elif ins.legacy:
            if rep_split(ins.args[0])[1] != len(ins.args) - 1:
                out.append(Problem(w, 'REPL word 0x%x is not for %d phonemes'
                                   % (ins.args[0], len(ins.args) - 1), t))
        elif 12 * (len(ins.args) - 1) > 0xFF:
            out.append(Problem(w, 'REPL of %d phonemes: at most 21 fit its count byte'
                               % (len(ins.args) - 1), t))
        elif ins.args[0] & 0xFF != 12 * (len(ins.args) - 1):
            out.append(Problem(w, 'REPL word 0x%x is not for %d phonemes'
                               % (ins.args[0], len(ins.args) - 1), t))
    if any(isinstance(a, int) and not 0 <= a <= 0xFFFF for a in ins.args):
        out.append(Problem(w, 'an operand does not fit 16 bits', t))


def _check_inventory(f, out):
    """-> {id(phoneme): its offset from H[8]}"""
    if not f.phonemes or not f.phonemes[0].is_defaults or any(p.is_defaults for p in f.phonemes[1:]):
        out.append(Problem('inventory', 'the defaults record must be first, and only there', ('phonemes',)))
    offset, size, first = {}, 0, {}
    for k, ph in enumerate(f.phonemes):
        offset[id(ph)] = size
        try:
            size += _check_phoneme(ph, out)
        except (TypeError, ValueError, AttributeError):
            out.append(Problem(ph_label(ph), 'malformed record', ('phoneme', ph)))
        name = getattr(ph, 'name', None)
        if name_ok(name):                       # the defaults record's name counts too
            if name in first:
                out.append(Problem(ph_label(ph), 'the name is also phoneme %d\'s; the engine finds '
                                   'the first one by name' % first[name], ('phoneme', ph), True))
            else:
                first[name] = k
    if size + 2 > 0x8000:
        out.append(Problem('inventory', 'is %d bytes; phoneme ids are signed 16-bit offsets, '
                           'so at most 32768' % (size + 2), ('phonemes',)))
    return offset


def check(f):
    """-> list of Problem (errors and warnings); no errors when to_bytes(f) gives a file the
    engine can load. Never raises on a model the editor can build."""
    out = []
    if not isinstance(f.header, bytes) or len(f.header) != 0x200:
        out.append(Problem('header', 'must be 0x200 bytes', ('header',)))
    if not isinstance(f.head, bytes) or len(f.head) != 0x30:
        out.append(Problem('header', 'body 0x40..0x70 must be 0x30 bytes', ('header',)))
    elif f.head[:2] == b'XX':
        out.append(Problem('header', "a build tag starting 'XX' is refused by the engine (ivx_bind)",
                           ('header',)))
    offset = _check_inventory(f, out)
    for name in ('M0', 'M1', 'M2', 'M5'):
        if name not in f.modules:
            out.append(Problem('modules', '%s is required: the engine binds or byte-swaps through it' % name,
                               ('rules',)))
    if 'M3' in f.modules and 'M4' not in f.modules:
        out.append(Problem('modules', 'M3 needs M4: the byte-swap runs M3 up to M4', ('rules',)))

    for name, m in f.modules.items():
        for i, r in enumerate(m.rules):
            w, t, nwords = rule_label(name, i, r), ('rule', name, i), 0
            for ins in r.instrs:
                nwords += 1 + len(ins.args)
                try:
                    _check_instr(ins, w, t, offset, out)
                except (IndexError, TypeError):
                    out.append(Problem(w, 'malformed instruction 0x%x' % ins.op
                                       if isinstance(ins.op, int) else 'malformed instruction', t))
            if 2 + 2 * nwords > 0xFFFF:
                out.append(Problem(w, 'the rule is longer than its length word holds', t))

    if f.lexicon is not None:
        prev = None
        for i, e in enumerate(f.lexicon.entries):
            t = ('lexicon', i)
            if not (isinstance(e, tuple) and len(e) == 2 and all(isinstance(x, bytes) for x in e)):
                out.append(Problem('lexicon entry %d' % i, 'must be two byte strings', t))
                continue
            o, tr = e
            w = 'lexicon entry %d (%r)' % (i, o.decode('latin-1'))
            if not o or b'\0' in o + tr or b'\x7f' in o + tr:
                out.append(Problem(w, 'word and transcription must be non-empty, without 0x00 or 0x7F', t))
            if prev is not None and prev > o:      # duplicates are allowed: the files have them
                out.append(Problem(w, 'entries must be in ascending byte order', t))
            prev = o

    for attr, n in (('class_lo', 0x80), ('xlat_hi', 0x100), ('class_hi', 0x80)):
        v = getattr(f, attr)
        if v is not None and (not isinstance(v, bytes) or len(v) != n):
            out.append(Problem(attr, 'must be %d bytes' % n, ('chars',)))
    if len(f.tables) != 4 or any(len(t) != 32 or not all(isinstance(x, int) and 0 <= x <= 0xFFFF for x in t)
                                 for t in f.tables):
        out.append(Problem('parameter tables', 'must be 4 tables of 32 16-bit words', ('params',)))
    return out
