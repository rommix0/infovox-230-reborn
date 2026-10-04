"""The checks the writer runs before saving (spec: Writer, "Checks before saving")."""
from dataclasses import dataclass

from .model import PhRef, VarItem
from .rules import REPL, REPL_NEXT, ph_positions, rep_split


@dataclass
class Problem:
    where: str        # e.g. "phoneme 'A:'", "M0 rule 12 (#32)", "lexicon entry 40 ('BORT')"
    message: str

    def __str__(self):
        return '%s: %s' % (self.where, self.message)


def ph_label(ph):
    return 'phoneme %r' % ph.name.lstrip(b'\0').decode('latin-1')


def rule_label(module, i, rule):
    return '%s rule %d' % (module, i) + ('' if rule.label is None else ' (#%d)' % rule.label)


def _check_phoneme(ph, out):
    """append ph's problems to out; -> its record size in bytes"""
    w = ph_label(ph)
    if len(ph.name) != 2:
        out.append(Problem(w, 'the name must be 2 bytes'))
    if not (0 <= ph.feat <= 0xFFFFFFFF and 0 <= ph.mask <= 0xFFFFFFFF):
        out.append(Problem(w, 'feat and mask must be 32-bit'))
    ids, size = [], 12
    for it in ph.items:
        if isinstance(it, VarItem):
            size += 4
            ids.append(it.vid - 0x100)
            if not 0x80 <= it.vid <= 0xFF:
                out.append(Problem(w, 'variable id 0x%x is not in 0x80..0xFF' % it.vid))
            if not -0x8000 <= it.value <= 0x7FFF:
                out.append(Problem(w, 'v%02x = %d does not fit 16 bits' % (it.vid, it.value)))
        else:
            size += 2 + 2 * len(it.points)
            ids.append(it.pid)
            if not 0 <= it.pid < 0x80:
                out.append(Problem(w, 'parameter id 0x%x is not in 0..0x7F' % it.pid))
            if not 1 <= len(it.points) <= 127:
                out.append(Problem(w, 'parameter %d has %d points (1..127)' % (it.pid, len(it.points))))
            for t, v in it.points:
                if not (-128 <= t <= 127 and 0 <= v <= 255):
                    out.append(Problem(w, 'point %d@%d: time must be -128..127, value 0..255' % (v, t)))
    if ids != sorted(set(ids)):
        out.append(Problem(w, 'items must be in ascending signed-id order, each id once'))
    if size > 0x7FFF:
        out.append(Problem(w, 'the record is %d bytes (at most 32767)' % size))
    return size


def _check_instr(ins, w, present, out):
    for k in ph_positions(ins):
        a = ins.args[k]
        if isinstance(a, PhRef) and a.ph is not None and id(a.ph) not in present:
            out.append(Problem(w, 'uses %s, which is not in the inventory' % ph_label(a.ph)))
    if ins.op in (REPL, REPL_NEXT):
        if not ins.args or not isinstance(ins.args[0], int):
            out.append(Problem(w, 'REPL needs its count word first'))
        elif ins.legacy:
            if rep_split(ins.args[0])[1] != len(ins.args) - 1:
                out.append(Problem(w, 'REPL word 0x%x is not for %d phonemes'
                                   % (ins.args[0], len(ins.args) - 1)))
        elif 12 * (len(ins.args) - 1) > 0xFF:
            out.append(Problem(w, 'REPL of %d phonemes: at most 21 fit its count byte'
                               % (len(ins.args) - 1)))
        elif ins.args[0] & 0xFF != 12 * (len(ins.args) - 1):
            out.append(Problem(w, 'REPL word 0x%x is not for %d phonemes'
                               % (ins.args[0], len(ins.args) - 1)))
    if any(isinstance(a, int) and not 0 <= a <= 0xFFFF for a in ins.args):
        out.append(Problem(w, 'an operand does not fit 16 bits'))


def check(f):
    """-> list of Problem; empty when to_bytes(f) gives a file the engine can load"""
    out = []
    if not f.phonemes or not f.phonemes[0].is_defaults or any(p.is_defaults for p in f.phonemes[1:]):
        out.append(Problem('inventory', 'the defaults record must be first, and only there'))
    size = 2 + sum(_check_phoneme(ph, out) for ph in f.phonemes)
    if size > 0x8000:
        out.append(Problem('inventory', 'is %d bytes; phoneme ids are signed 16-bit offsets, '
                           'so at most 32768' % size))
    for name in ('M0', 'M1', 'M2', 'M5'):
        if name not in f.modules:
            out.append(Problem('modules', '%s is required: the engine binds or byte-swaps through it' % name))
    if 'M3' in f.modules and 'M4' not in f.modules:
        out.append(Problem('modules', 'M3 needs M4: the byte-swap runs M3 up to M4'))

    present = {id(ph) for ph in f.phonemes}
    for name, m in f.modules.items():
        for i, r in enumerate(m.rules):
            w, nwords = rule_label(name, i, r), 0
            for ins in r.instrs:
                nwords += 1 + len(ins.args)
                try:
                    _check_instr(ins, w, present, out)
                except (IndexError, TypeError):
                    out.append(Problem(w, 'malformed instruction 0x%x' % ins.op))
            if 2 + 2 * nwords > 0xFFFF:
                out.append(Problem(w, 'the rule is longer than its length word holds'))

    if f.lexicon is not None:
        prev = None
        for i, (o, t) in enumerate(f.lexicon.entries):
            w = 'lexicon entry %d (%r)' % (i, o.decode('latin-1'))
            if not o or b'\0' in o + t or b'\x7f' in o + t:
                out.append(Problem(w, 'word and transcription must be non-empty, without 0x00 or 0x7F'))
            if prev is not None and prev > o:      # duplicates are allowed: the files have them
                out.append(Problem(w, 'entries must be in ascending byte order'))
            prev = o

    for attr, n in (('class_lo', 0x80), ('xlat_hi', 0x100), ('class_hi', 0x80)):
        v = getattr(f, attr)
        if v is not None and len(v) != n:
            out.append(Problem(attr, 'must be %d bytes' % n))
    if len(f.tables) != 4 or any(len(t) != 32 or not all(0 <= x <= 0xFFFF for x in t) for t in f.tables):
        out.append(Problem('parameter tables', 'must be 4 tables of 32 16-bit words'))
    return out
