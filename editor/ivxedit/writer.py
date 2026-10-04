"""IvxFile -> .ivx bytes (spec: Writer)."""
import os
import shutil
import struct
import tempfile
from pathlib import Path

from .binfmt import INV, SLOTS, TABLES, IvxFormatError, align16, pack_words
from .checks import Problem, check
from .model import MODULE_DIR, load
from .rules import encode_module

BIN = Path(__file__).resolve().parents[2] / 'bin'   # the shipped files: never written


class SaveRefused(Exception):
    """check() found problems; nothing was written"""

    def __init__(self, problems):
        super().__init__('%d problem(s): %s' % (len(problems), '; '.join(map(str, problems[:5]))))
        self.problems = problems


def lexicon_bytes(lex):
    body = b'\x7f'.join(o + b'\0' + t + b'\0' for o, t in lex.entries)
    npad = 1 + (-(4 + len(body) + 1)) % 16        # at least one 0x7F, to 16 bytes
    return struct.pack('>I', 4 + len(body) + npad) + body + b'\x7f' * npad


def to_bytes(f):
    body = bytearray(INV)
    body[0x40:0x70] = f.head
    for k, t in enumerate(f.tables):
        body[TABLES + 2 * SLOTS * k:TABLES + 2 * SLOTS * (k + 1)] = pack_words(t)

    # pass 1: the inventory gives every phoneme its id (its offset from H[8])
    offset, inv = {}, bytearray()
    for ph in f.phonemes:
        offset[id(ph)] = len(inv)
        inv += ph.to_bytes()
    inv += b'\0\0'

    # pass 2: the rules encode every PhRef with that id
    def ph_id(ref):
        if ref.ph is None:
            return len(inv) - 2
        return offset[id(ref.ph)] + ref.delta

    sections = [('INV', bytes(inv))]
    sections += [(n, encode_module(f.modules[n], ph_id)) for n in MODULE_DIR if n in f.modules]
    if f.xlat_hi is not None:
        sections.append(('H14', f.xlat_hi + f.class_hi))
    sections.append(('H13', f.class_lo + (f.ixlex or b'')))
    if f.lexicon is not None:
        sections.append(('H3', lexicon_bytes(f.lexicon)))

    starts = {}
    for i, (name, data) in enumerate(sections):
        starts[name] = len(body)
        body += data
        if i + 1 < len(sections):
            body += bytes(align16(len(body)) - len(body) + f.pads.get(name, 0))

    H = list(f.directory)
    for name, d in MODULE_DIR.items():
        H[d] = starts.get(name, 0)
    H[3], H[8], H[13], H[14] = starts.get('H3', 0), starts['INV'], starts['H13'], starts.get('H14', 0)
    if f.h2_rel is not None:
        H[2] = starts['H13'] + 0x80 + f.h2_rel
    H[15] = len(body)
    body += bytes(f.end_pad)
    body[0:0x40] = struct.pack('>16I', *H)
    header = bytearray(f.header)
    header[8:12] = struct.pack('>I', len(body))
    return bytes(header) + bytes(body)


def save(f, path):
    """check f, then write it to path through a temporary file in the same folder; an
    existing file is kept as <name>.bak"""
    path = Path(path).resolve()
    if BIN in path.parents:
        raise PermissionError('%s is in bin/, which holds the shipped files' % path)
    problems = check(f)
    if problems:
        raise SaveRefused(problems)
    data = to_bytes(f)
    try:
        again = to_bytes(load(data))
    except IvxFormatError as e:
        raise SaveRefused([Problem('file', 'the written file does not load again: %s' % e)]) from e
    if again != data:
        raise SaveRefused([Problem('file', 'the written file does not read back the same')])
    fd, tmp = tempfile.mkstemp(dir=path.parent, prefix=path.name + '.', suffix='.tmp')
    try:
        with os.fdopen(fd, 'wb') as out:
            out.write(data)
        if path.exists():
            shutil.copy2(path, path.with_name(path.name + '.bak'))
        os.replace(tmp, path)
    except BaseException:
        if os.path.exists(tmp):
            os.unlink(tmp)
        raise
