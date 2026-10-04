"""The .ivx model (REFERENCE §5) and the loader."""
import bisect
import struct
from dataclasses import dataclass, field

from .binfmt import HDR_SIZE, INV, SLOTS, TABLES, IvxFormatError, align16, u16, u32, words
from .rules import decode_module, ph_positions

PNAMES = ['DR', 'ST', 'FS', 'CS', 'FD', 'F1', 'B1', 'F2', 'B2', 'F3', 'B3', 'F4', 'B4', 'FH',
          'K1', 'C1', 'K2', 'C2', 'FN', 'BN', 'AK', 'AC', 'AH', 'AN', 'A0', 'F0', 'XX', 'YY', 'ZZ']

MODULE_DIR = {'M3': 5, 'M4': 6, 'M0': 9, 'M1': 10, 'M5': 11, 'M2': 12}   # in file order
SECTION_ORDER = ['INV', 'M3', 'M4', 'M0', 'M1', 'M5', 'M2', 'H14', 'H13', 'H3']


def _s8(v):
    return v - 0x100 if v & 0x80 else v


def _s16(v):
    return v - 0x10000 if v & 0x8000 else v


@dataclass(eq=False)
class ParamItem:
    pid: int          # < 0x80: parameter index (PNAMES)
    points: list      # [(time s8, value u8)]; time = signed frame offset from the segment start


@dataclass(eq=False)
class VarItem:
    vid: int          # >= 0x80: variable vXX
    value: int        # s16


@dataclass(eq=False)
class Phoneme:
    name: bytes       # 2 bytes as stored; one-char names have a leading NUL (b'\x00+')
    feat: int
    mask: int
    items: list       # ParamItem / VarItem, in ascending signed-id order as stored
    is_defaults: bool = False   # the first record: variable defaults

    def to_bytes(self):
        out = bytearray()
        for it in self.items:
            if isinstance(it, VarItem):
                out += bytes([it.vid, 2]) + struct.pack('>h', it.value)
            else:
                out += bytes([it.pid, 2 * len(it.points)])
                for t, v in it.points:
                    out += bytes([t & 0xFF, v])
        return struct.pack('>H2sII', 12 + len(out), self.name, self.feat, self.mask) + bytes(out)


@dataclass(eq=False, frozen=True)
class PhRef:
    """a phoneme-id operand: the record's offset plus delta. ph None = the inventory's 0
    terminator (8 ids in fr/it point there: a phoneme that does not exist)"""
    ph: Phoneme
    delta: int = 0


def decode_phoneme(rec):
    length, name, feat, mask = u16(rec, 0), rec[2:4], u32(rec, 4), u32(rec, 8)
    items, p = [], 12
    while p < length:
        iid, size = rec[p], rec[p + 1]
        data = rec[p + 2:p + 2 + size]
        if iid >= 0x80:
            if size != 2:
                raise IvxFormatError('variable v%02x has %d bytes' % (iid, size))
            items.append(VarItem(iid, _s16(u16(data, 0))))
        else:
            if size % 2:
                raise IvxFormatError('parameter %d has an odd size %d' % (iid, size))
            items.append(ParamItem(iid, [(_s8(data[k]), data[k + 1]) for k in range(0, size, 2)]))
        p += 2 + size
    if p != length:
        raise IvxFormatError('phoneme %r: items overrun the record' % name)
    return Phoneme(name, feat, mask, items)


def decode_inventory(b):
    """-> (phonemes, offsets from H[8], offset just past the 0 terminator)"""
    phs, offs, p = [], [], INV
    while True:
        length = u16(b, p)
        if length == 0:
            return phs, offs, p + 2
        ph = decode_phoneme(b[p:p + length])
        ph.is_defaults = not phs
        phs.append(ph)
        offs.append(p - INV)
        p += length


def resolve_refs(modules, phs, offs, limit):
    """phoneme-id operands -> PhRef (the record containing the id + delta); an id equal to
    limit (the terminator's offset) -> PhRef(None); ids past it stay ints"""
    for m in modules.values():
        for r in m.rules:
            for ins in r.instrs:
                for k in ph_positions(ins):
                    v = ins.args[k]
                    i = bisect.bisect_right(offs, v) - 1
                    if v == limit:
                        ins.args[k] = PhRef(None)
                    elif v < limit and i >= 0:
                        ins.args[k] = PhRef(phs[i], v - offs[i])


@dataclass(eq=False)
class Lexicon:
    entries: list     # [(orth, transcription)] as bytes, in file (binary-search) order


@dataclass(eq=False)
class IvxFile:
    header: bytes     # the 0x200-byte file header; the body length at 0x08 is set on save
    directory: list   # H[0..15] as loaded; the writer replaces the section offsets
    head: bytes       # body 0x40..0x70: build tag, then the words at 0x6A, 0x6C, 0x6E
    tables: list      # A0..A3, 32 words each, as stored (slot ntotal - i = parameter i)
    phonemes: list
    modules: dict     # 'M0'..'M5' -> rules.Module
    class_lo: bytes   # H[13]: class codes for 0x00-0x7F
    xlat_hi: bytes = None   # H[14]: 2-char transliterations of 0x80-0xFF
    class_hi: bytes = None  # H[14] + 0x100: class codes for 0x80-0xFF
    ixlex: bytes = None     # no H[3]: H[13] + 0x80 up to H[15] (index, then indexed lexicon)
    h2_rel: int = None      # Danish H[2], relative to the start of ixlex
    lexicon: Lexicon = None # H[3]
    pads: dict = field(default_factory=dict)   # zero padding beyond 16-byte alignment, per section
    end_pad: int = 0        # zero bytes after H[15], the end of the data (am, it, no, sp)

    @property
    def version(self):
        return self.header[0x80:0xC0].split(b'\0')[0].decode('latin-1')

    @property
    def ntotal(self):
        return u16(self.head, 0x2E) // 2             # body 0x6E

    def param(self, i):
        """(A0, A1, A2, A3) of parameter i (PNAMES order); A3 signed"""
        a0, a1, a2, a3 = (t[self.ntotal - i] for t in self.tables)
        return a0, a1, a2, _s16(a3)


def decode_lexicon(b, s):
    """H[3]: BE u32 size (counting itself), 'orth\\0trans\\0' entries separated by 0x7F,
    then 0x7F padding -> (Lexicon, end)"""
    size = u32(b, s)
    data, p, entries = b[s + 4:s + size], 0, []
    while True:
        if p:
            if data[p] != 0x7F:
                raise IvxFormatError('lexicon: no 0x7F before body 0x%x' % (s + 4 + p))
            p += 1
        if p >= len(data) or data[p] == 0x7F:
            break
        o = data.index(b'\0', p)
        t = data.index(b'\0', o + 1)
        entries.append((data[p:o], data[o + 1:t]))
        p = t + 1
    if data[p:].strip(b'\x7f'):
        raise IvxFormatError('lexicon: data after the padding')
    return Lexicon(entries), s + size


def _pads(b, spans):
    """extra zero padding (beyond 16-byte alignment) after each section but the last"""
    names = [n for n in SECTION_ORDER if n in spans]
    pads = {}
    for n, nxt in zip(names, names[1:] + [None]):
        start, end = spans[n]
        if nxt is None:
            if end != len(b):
                raise IvxFormatError('0x%x bytes after the last section' % (len(b) - end))
            break
        nstart = spans[nxt][0]
        extra = nstart - align16(end)
        if extra < 0 or extra % 16 or any(b[end:nstart]):
            raise IvxFormatError('unexpected gap after %s: body 0x%x..0x%x' % (n, end, nstart))
        pads[n] = extra
    return pads


def _load(data):
    """bytes of an .ivx file -> IvxFile"""
    if len(data) < HDR_SIZE + INV:
        raise IvxFormatError('file too short')
    header, b = bytes(data[:HDR_SIZE]), bytes(data[HDR_SIZE:])
    H = list(struct.unpack_from('>16I', b, 0))
    if u32(header, 8) != len(b):
        raise IvxFormatError('body length in the header does not match the file')
    if H[15] > len(b) or any(b[H[15]:]):
        raise IvxFormatError('H[15] is not the end of the data')
    end_pad, b = len(b) - H[15], b[:H[15]]
    if H[8] != INV:
        raise IvxFormatError('phoneme inventory is not at body 0x170')
    tables = [words(b, TABLES + 2 * SLOTS * k, SLOTS) for k in range(4)]
    phs, offs, inv_end = decode_inventory(b)
    spans, modules = {'INV': (INV, inv_end)}, {}
    for name, d in MODULE_DIR.items():
        if H[d]:
            modules[name], end = decode_module(b, H[d])
            spans[name] = (H[d], end)
    resolve_refs(modules, phs, offs, inv_end - 2 - INV)
    f = IvxFile(header, H, b[0x40:0x70], tables, phs, modules, b[H[13]:H[13] + 0x80])
    if H[14]:
        if H[13] != H[14] + 0x180:
            raise IvxFormatError('H[13] does not follow H[14]')
        f.xlat_hi, f.class_hi = b[H[14]:H[14] + 0x100], b[H[14] + 0x100:H[13]]
        spans['H14'] = (H[14], H[13])
    if H[3]:
        if H[2]:
            raise IvxFormatError('H[2] in a file with a lexicon')
        spans['H13'] = (H[13], H[13] + 0x80)
        f.lexicon, end = decode_lexicon(b, H[3])
        spans['H3'] = (H[3], end)
    else:
        f.ixlex = b[H[13] + 0x80:]
        spans['H13'] = (H[13], len(b))
        if H[2]:
            f.h2_rel = H[2] - (H[13] + 0x80)
            if not 0 <= f.h2_rel < len(f.ixlex):
                raise IvxFormatError('H[2] outside the indexed lexicon region')
    f.pads, f.end_pad = _pads(b, spans), end_pad
    return f


def load(data):
    """bytes of an .ivx file -> IvxFile; raises IvxFormatError for anything it cannot read"""
    try:
        return _load(data)
    except IvxFormatError:
        raise
    except (IndexError, ValueError, struct.error) as e:
        raise IvxFormatError('cannot read the file: %s' % e) from e
