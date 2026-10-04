"""The .ivx model (REFERENCE §5) and the loader."""
import bisect
import struct
from dataclasses import dataclass

from .binfmt import INV, IvxFormatError, u16, u32
from .rules import ph_positions

PNAMES = ['DR', 'ST', 'FS', 'CS', 'FD', 'F1', 'B1', 'F2', 'B2', 'F3', 'B3', 'F4', 'B4', 'FH',
          'K1', 'C1', 'K2', 'C2', 'FN', 'BN', 'AK', 'AC', 'AH', 'AN', 'A0', 'F0', 'XX', 'YY', 'ZZ']


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
