"""Readable names for raw .ivx values: phoneme and parameter names, the parameter type
codes (REFERENCE §5.3), character classes (§5.2), header fields and hex dumps."""
from .binfmt import u16, u32
from .model import PNAMES

# A1/4 -> what the frame generator does with the parameter (REFERENCE §5.3)
A1_TYPES = {
    0: 'not output', 1: 'sets rates of AC AH AN A0', 2: 'sets rates of F2 F3 F4 FH',
    3: 'sets rate of F1', 4: 'smooth → freq', 5: 'smooth → freq', 6: 'smooth → div16',
    7: 'smooth → amp2', 8: 'amp decay', 9: 'amp decay', 10: 'smooth → amp', 11: 'cosine',
    12: 'linear', 13: 'sets rates of AC AH AN A0, ×voice', 14: 'sets rates of F2 F3 F4 FH, ×voice',
    15: 'sets rate of F1, ×voice', 16: 'smooth → offset', 17: 'not output',
    18: 'approach → freq', 19: 'approach → freq', 20: 'approach → amp', 21: 'approach → offset',
    22: 'amp approach', 23: 'amp approach', 24: 'copies to rates', 25: 'copies to rates',
    26: 'copies to rates', 27: 'not output', 28: 'approach → amp2', 29: 'not output',
    30: 'not output', 31: 'not output', 32: 'cosine, value remapped'}

# class codes -6..5 -> class flags (ivx_build_class_flags, src/ttsctrl.c)
CODE_FLAGS = (0x008, 0x001, 0x020, 0x040, 0x010, 0x080, 0x100, 0x200, 0x600, 0xA00, 0x1020, 0x2100)
FLAG_NAMES = ((0x001, 'ignored'), (0x002, 'blank'), (0x004, 'line break'),
              (0x010, 'sentence punctuation'), (0x040, 'number separator'), (0x080, 'digit'),
              (0x100, 'symbol'), (0x200, 'letter'), (0x400, 'lower case'),
              (0x800, '@D prefix in mode 2'), (0x2000, 'name "c%"'))


def ph_name(name):
    """a phoneme's stored 2-byte name as text: one-char names have a leading NUL"""
    return name.lstrip(b'\0').decode('latin-1')


def param_name(pid):
    return PNAMES[pid] if pid < len(PNAMES) else 'p%d' % pid


def item_name(iid):
    """an item id: a parameter (< 0x80) or a variable vXX"""
    return 'v%02x' % iid if iid >= 0x80 else param_name(iid)


def a1_text(a1):
    if a1 % 4:
        return '0x%x: unknown' % a1
    return '%d: %s' % (a1 // 4, A1_TYPES.get(a1 // 4, 'unknown'))


def s8(v):
    return v - 0x100 if v & 0x80 else v


def char_flags(ch, code):
    """the class flags the engine gives byte ch with class code `code` (a signed byte)"""
    if -6 <= code <= 5:
        fl = CODE_FLAGS[code + 6]
    elif 0x61 <= ch <= 0x7D:
        fl = 0x600
    elif 0x41 <= ch <= 0x5D:
        fl = 0xA00
    else:
        fl = 0x200
    if ch in (0x09, 0x20):
        fl |= 0x02
    if ch in (0x2E, 0x17):
        fl |= 0x10
    if ch in (0x0A, 0x0C, 0x0D):
        fl |= 0x04
    return fl


def flags_text(fl):
    """'0x600: letter, lower case'; bits without a known meaning are listed in hex"""
    names, rest = [], fl
    for bit, name in FLAG_NAMES:
        if fl & bit:
            names.append(name)
            rest &= ~bit
    names += ['0x%x' % (1 << b) for b in range(16) if rest & (1 << b)]
    return '0x%x: %s' % (fl, ', '.join(names))


def char_rows(f):
    """the 256 rows of the Characters view: (byte, char, class code, flags, transliteration
    without its NUL padding); code, flags and transliteration are None for 0x80-0xFF in
    files without H[14], which use the engine's built-in tables"""
    rows = []
    for ch in range(256):
        c = bytes([ch]).decode('cp1252', errors='replace')
        shown = c if c.isprintable() else ''
        if ch < 0x80:
            code, xl = s8(f.class_lo[ch]), None
        elif f.class_hi is not None:
            code = s8(f.class_hi[ch - 0x80])
            xl = f.xlat_hi[2 * (ch - 0x80):2 * (ch - 0x80) + 2].decode('latin-1').rstrip('\0')   # '{\0' = '{'
        else:
            code = xl = None
        rows.append((ch, shown, code, None if code is None else char_flags(ch, code), xl))
    return rows


def _cstr(b):
    return b.split(b'\0')[0].decode('latin-1')


def header_fields(f):
    """[(label, text)] for the Header view"""
    h, hd = f.header, f.head
    return [('Version', f.version),
            ('Copyright', _cstr(h[0xC0:0x100])),
            ('Flags', '0x%08x' % u32(h, 0)),
            ('Lexicon name', _cstr(h[0x10:0x1C])),
            ('Header dword 0x1C', str(u32(h, 0x1C))),
            ('Build tag', hd[:10].decode('latin-1')),
            ('Word 0x6A (2 × parameters to DR..A0)', str(u16(hd, 0x2A))),
            ('Word 0x6C', str(u16(hd, 0x2C))),
            ('Word 0x6E (2 × parameters)', str(u16(hd, 0x2E)))]


def hexdump(data, base=0):
    """16 bytes per line: offset, hex, latin-1 text"""
    out = []
    for p in range(0, len(data), 16):
        row = data[p:p + 16]
        text = ''.join(chr(c) if 0x20 <= c < 0x7F else '.' for c in row)
        out.append('%06x  %-47s  %s' % (base + p, row.hex(' '), text))
    return '\n'.join(out)


def unknown_text(f):
    """everything the model keeps raw without decoding it, for the Unknown view"""
    out = ['Directory (body offsets as loaded)']
    out += ['  %-6s 0x%08x' % ('H[%d]' % i, h) for i, h in enumerate(f.directory)]
    out += ['', 'Body 0x40..0x70 (build tag, words 0x6A 0x6C 0x6E)', hexdump(f.head, 0x40), '']
    out += ['Module lead words and closing words']
    out += ['  %s  lead 0x%04x  end %s' % (n, m.lead, ' '.join('%02x' % w for w in m.end))
            for n, m in sorted(f.modules.items())]
    out += ['', 'Extra zero padding after a section: %s' % (
        ', '.join('%s %d' % kv for kv in f.pads.items() if kv[1]) or 'none'),
            'Zero bytes after H[15]: %d' % f.end_pad]
    if f.h2_rel is not None:
        out.append('H[2] points 0x%x bytes into the indexed lexicon' % f.h2_rel)
    out += ['', 'File header (0x200 bytes)', hexdump(f.header)]
    return '\n'.join(out)
