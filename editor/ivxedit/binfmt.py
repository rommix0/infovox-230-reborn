"""Big-endian helpers for .ivx files (REFERENCE §5)."""
import struct

HDR_SIZE = 0x200   # file header; every offset below is relative to the body after it
TABLES = 0x70      # A0..A3 per-parameter tables
SLOTS = 32         # words per table
INV = 0x170        # phoneme inventory, H[8], in every shipped file


class IvxFormatError(ValueError):
    """The bytes do not have the layout this package understands."""


def u16(b, o):
    return struct.unpack_from('>H', b, o)[0]


def u32(b, o):
    return struct.unpack_from('>I', b, o)[0]


def words(b, o, n):
    return list(struct.unpack_from('>%dH' % n, b, o))


def pack_words(ws):
    return struct.pack('>%dH' % len(ws), *ws)


def align16(n):
    return (n + 15) & ~15
