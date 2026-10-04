"""Edits of the voice data (spec: GUI, step 4). Each function checks its input and returns
an Edit, which replaces one attribute of a model object with a new value, or None when
the value is already there. Values are replaced, never changed in place, so undo (putting
the old value back) is exact."""
import bisect
import re
import string
import struct
from dataclasses import dataclass

from .checks import ph_label, rule_label
from .model import PNAMES, ParamItem, Phoneme, VarItem
from .xref import phoneme_uses

# header text fields: offset, size (the text is NUL-terminated, so size - 1 characters fit)
HEADER_TEXT = {'version': (0x80, 0x40), 'copyright': (0xC0, 0x40), 'lexicon name': (0x10, 0x0C)}
PARAM_COLUMNS = ('A1', 'A2', 'A3')
A1_TYPES = 33                       # A1/4 = 0..32 select a handler (REFERENCE §5.3)
NUMBER = re.compile(r'\s*([+-]?)(0[xX][0-9a-fA-F]{1,8}|[0-9]{1,10})\s*$')
POINT = re.compile(r'\s*([0-9]{1,3})\s*@\s*([+-]?[0-9]{1,3})\s*$')


class EditError(ValueError):
    """the input cannot be stored; str() says why"""


@dataclass(eq=False)
class Edit:
    obj: object       # the model object whose attribute changes
    attr: str
    value: object     # the new value
    text: str         # what the Edit menu calls it: "Rename phoneme 'A:' to 'A2'"
    target: tuple     # what the window shows after undo or redo, as Problem.target
    old: object = None

    def apply(self):
        self.old = getattr(self.obj, self.attr)
        setattr(self.obj, self.attr, self.value)

    def revert(self):
        setattr(self.obj, self.attr, self.old)


def _edit(obj, attr, value, text, target):
    """an Edit, or None when the value is already there"""
    return None if getattr(obj, attr) == value else Edit(obj, attr, value, text, target)


def number(text, lo, hi, what):
    """text -> int in lo..hi (decimal, or hex with 0x), else EditError"""
    m = NUMBER.match(text)
    if not m:
        raise EditError('%s: %r is not a number' % (what, text))
    digits = m.group(2)
    v = int(digits[2:], 16) if digits[:2] in ('0x', '0X') else int(digits)
    v = -v if m.group(1) == '-' else v
    if not lo <= v <= hi:
        raise EditError('%s must be %d..%d' % (what, lo, hi))
    return v


def latin1(text, what):
    try:
        return text.encode('latin-1')
    except UnicodeEncodeError:
        raise EditError('%s: only Latin-1 characters can be stored' % what) from None


# --- header
def set_header_text(f, field, text):
    off, size = HEADER_TEXT[field]
    b = latin1(text, field)
    if b'\0' in b or len(b) > size - 1:
        raise EditError('%s: at most %d characters, without NUL' % (field, size - 1))
    if f.header[off:off + size].split(b'\0')[0] == b:     # the text is there; bytes after its NUL stay
        return None
    header = f.header[:off] + b + bytes(size - len(b)) + f.header[off + size:]
    return _edit(f, 'header', header, 'Set the %s' % field, ('header',))


def set_header_flags(f, text):
    v = number(text, 0, 0xFFFFFFFF, 'flags')
    return _edit(f, 'header', struct.pack('>I', v) + f.header[4:], 'Set the flags', ('header',))


# --- parameter tables
def set_param(f, i, column, text):
    """column 'A1' (the type, A1/4: '18' or '18: approach → freq'), 'A2' (rate) or 'A3'
    (signed offset) of parameter i"""
    k = 1 + PARAM_COLUMNS.index(column)
    if column == 'A1':
        m = re.match(r'\s*([0-9]{1,3})\s*(:.*)?$', text)      # 'N' or 'N: handler name'
        v = number(m.group(1) if m else text, 0, A1_TYPES - 1, 'A1 type') * 4
    elif column == 'A2':
        v = number(text, 0, 255, 'A2 rate')
    else:
        v = number(text, -0x8000, 0x7FFF, 'A3 offset') & 0xFFFF
    tables = [list(t) for t in f.tables]
    tables[k][f.ntotal - i] = v
    return _edit(f, 'tables', tables, 'Set %s of %s' % (column, PNAMES[i]), ('params',))


# --- characters
def set_class(f, ch, text):
    v = number(text, -128, 127, 'class code') & 0xFF
    if ch < 0x80:
        attr, k = 'class_lo', ch
    elif f.class_hi is not None:
        attr, k = 'class_hi', ch - 0x80
    else:
        raise EditError("this file has no table for 0x80-0xFF: the engine's built-in one is used")
    old = getattr(f, attr)
    return _edit(f, attr, old[:k] + bytes([v]) + old[k + 1:], 'Set the class of 0x%02x' % ch, ('chars',))


def set_xlat(f, ch, text):
    if ch < 0x80 or f.xlat_hi is None:
        raise EditError('only 0x80-0xFF have transliterations, in files with H[14]')
    b = latin1(text, 'transliteration')
    if b'\0' in b or len(b) > 2:
        raise EditError('a transliteration is at most 2 characters')
    k = 2 * (ch - 0x80)
    value = f.xlat_hi[:k] + b.ljust(2, b'\0') + f.xlat_hi[k + 2:]
    return _edit(f, 'xlat_hi', value, 'Set the transliteration of 0x%02x' % ch, ('chars',))


# --- phonemes
def stored_name(text):
    """a phoneme name as typed -> its 2 stored bytes (one character gets a leading NUL)"""
    b = latin1(text, 'name')
    if not 1 <= len(b) <= 2 or b'\0' in b:
        raise EditError('a phoneme name is one or two characters')
    return b.rjust(2, b'\0')


def rename_phoneme(ph, text):
    return _edit(ph, 'name', stored_name(text), 'Rename %s to %r' % (ph_label(ph), text), ('phoneme', ph))


def set_bit(ph, which, bit, on):
    """which: 'feat' or 'mask'"""
    v = getattr(ph, which)
    v = v | 1 << bit if on else v & ~(1 << bit)
    return _edit(ph, which, v, '%s %s bit %d of %s' % ('Set' if on else 'Clear', which, bit, ph_label(ph)),
                 ('phoneme', ph))


def copy_item(it):
    return VarItem(it.vid, it.value) if isinstance(it, VarItem) else ParamItem(it.pid, list(it.points))


def _items(ph, items, text):
    return Edit(ph, 'items', items, '%s of %s' % (text, ph_label(ph)), ('phoneme', ph))


def set_cell(ph, row, k, text):
    """item `row` of ph: a variable's value (k = 0), or point k of a parameter as
    'value@time' (k = the number of points appends one; empty text removes point k)"""
    items = [copy_item(it) for it in ph.items]
    it = items[row]
    if isinstance(it, VarItem):
        if k != 0:
            raise EditError('a variable has one value')
        v = number(text, -0x8000, 0x7FFF, 'a variable')
        if v == it.value:
            return None
        it.value = v
        return _items(ph, items, 'Set v%02x' % it.vid)
    name = PNAMES[it.pid] if it.pid < len(PNAMES) else 'p%d' % it.pid
    if not text.strip():
        if k >= len(it.points):
            return None
        if len(it.points) == 1:
            raise EditError('a parameter needs a point: remove the item instead')
        del it.points[k]
        return _items(ph, items, 'Remove point %d of %s' % (k, name))
    m = POINT.match(text)
    if not m:
        raise EditError("a point is 'value@time', e.g. 165@2")
    v, t = int(m.group(1)), int(m.group(2))
    if not (0 <= v <= 255 and -128 <= t <= 127):
        raise EditError('a point is value 0..255 @ time -128..127')
    if k < len(it.points):
        if it.points[k] == (t, v):
            return None
        it.points[k] = (t, v)
    elif k == len(it.points) and k < 127:
        it.points.append((t, v))
    else:
        raise EditError('points are added after the last one, at most 127')
    return _items(ph, items, 'Set point %d of %s' % (k, name))


def _key(iid):
    """items are stored in ascending signed-id order: variables (0x80..0xFF) first"""
    return iid - 0x100 if iid >= 0x80 else iid


def add_item(ph, iid):
    """a new item: a parameter (iid < 0x80) with one point 0@-1, or a variable = 0"""
    ids = [it.vid if isinstance(it, VarItem) else it.pid for it in ph.items]
    if iid in ids:
        raise EditError('the phoneme already has this item')
    new = VarItem(iid, 0) if iid >= 0x80 else ParamItem(iid, [(-1, 0)])
    items = [copy_item(it) for it in ph.items]
    items.insert(bisect.bisect([_key(i) for i in ids], _key(iid)), new)
    return _items(ph, items, 'Add an item')


def remove_item(ph, row):
    items = [copy_item(it) for k, it in enumerate(ph.items) if k != row]
    return _items(ph, items, 'Remove an item')


def free_name(f):
    """a two-character name no phoneme has: Z0, Z1, …"""
    used = {ph.name for ph in f.phonemes}
    for a in string.ascii_uppercase[::-1]:
        for b in string.digits:
            if (a + b).encode() not in used:
                return a + b
    raise EditError('no free name')


def add_phoneme(f, index, text, model=None):
    """a new phoneme named `text` at f.phonemes[index]: empty, or a copy of `model`"""
    if not 1 <= index <= len(f.phonemes):
        raise EditError('a phoneme goes after the defaults record')
    if model is None:
        ph = Phoneme(stored_name(text), 0, 0, [])
    else:
        ph = Phoneme(stored_name(text), model.feat, model.mask, [copy_item(it) for it in model.items])
    phonemes = f.phonemes[:index] + [ph] + f.phonemes[index:]
    what = 'Add' if model is None else 'Copy %s to' % ph_label(model)
    return Edit(f, 'phonemes', phonemes, '%s phoneme %r' % (what, text), ('phoneme', ph))


def delete_phoneme(f, ph):
    """refused for the defaults record and for a phoneme the rules use"""
    if ph.is_defaults:
        raise EditError('the defaults record cannot be deleted')
    uses = phoneme_uses(f).get(ph, [])
    if uses:
        labels = [rule_label(m, i, f.modules[m].rules[i]) for m, i in uses]
        raise EditError('%s is used by %d rule%s: %s%s' % (
            ph_label(ph), len(uses), '' if len(uses) == 1 else 's', ', '.join(labels[:10]),
            ', …' if len(labels) > 10 else ''))
    phonemes = [p for p in f.phonemes if p is not ph]
    return Edit(f, 'phonemes', phonemes, 'Delete %s' % ph_label(ph), ('phonemes',))


# --- lexicon (H[3]): entries stay in the order the engine's binary search needs
def _entry(word, trans):
    o, t = latin1(word, 'headword'), latin1(trans, 'transcription')
    if not o or b'\0' in o + t or b'\x7f' in o + t:
        raise EditError('a headword is not empty; neither text may hold 0x00 or 0x7F')
    return o, t


def _placed(entries, entry):
    """entries with entry inserted after any equal headwords -> (entries, its index)"""
    k = bisect.bisect_right([o for o, _ in entries], entry[0])
    return entries[:k] + [entry] + entries[k:], k


def set_entry(lex, i, word, trans):
    """-> (Edit, the entry's new index); (None, i) when nothing changes"""
    entry = _entry(word, trans)
    if lex.entries[i] == entry:
        return None, i
    if lex.entries[i][0] == entry[0]:
        entries, k = lex.entries[:i] + [entry] + lex.entries[i + 1:], i
    else:
        entries, k = _placed(lex.entries[:i] + lex.entries[i + 1:], entry)
    return Edit(lex, 'entries', entries, 'Edit the entry %r' % word, ('lexicon', k)), k


def add_entry(lex, word, trans):
    """-> (Edit, the new entry's index)"""
    entries, k = _placed(lex.entries, _entry(word, trans))
    return Edit(lex, 'entries', entries, 'Add the entry %r' % word, ('lexicon', k)), k


def remove_entry(lex, i):
    entries = lex.entries[:i] + lex.entries[i + 1:]
    word = lex.entries[i][0].decode('latin-1')
    return Edit(lex, 'entries', entries, 'Remove the entry %r' % word,
                ('lexicon', max(0, min(i, len(entries) - 1))))
