# ivxedit step 2: read-only GUI and Speak — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A PySide6 window that opens any `.ivx` file and shows everything in it read-only: header, parameter tables, phonemes, rules in instruction form, character tables, lexicons, the raw parts. Plus a Speak panel that runs `ivxsay` on the model and draws waveform, spectrogram and phoneme strip, with A/B against the file as opened.

**Architecture:**
- Pure-Python modules with no Qt, each unit-tested:
  - `info`: readable names for raw values;
  - `xref`: which rules use which phoneme;
  - `listing`: rules as instruction-form text with highlight/jump spans;
  - `speak`: runs `ivxsay`, reads WAV and events;
  - `dsp`: waveform envelope and spectrogram with numpy.
- `ivxedit/gui/` is a thin PySide6 layer over them:
  - a section tree on the left and a stacked view per item;
  - a Speak panel whose `ivxsay` runs in a `QThread`;
  - a custom-painted signal view.
- Nothing edits the model in this step.

**Tech Stack:** Python 3.13 (64-bit), PySide6 ≥ 6.8 (6.11.2 installed), numpy ≥ 2.0 (2.4 installed), `unittest`, and `build/ivxsay.exe` from the repo.

**Spec:** `docs/superpowers/specs/2026-10-03-ivx-editor-design.md` (step 2 of its "Delivery steps"; sections "GUI", "Speak", "Testing" 9–10). Read it first. Step 1's plan is `docs/superpowers/plans/2026-10-03-ivxedit-step1-model-writer.md`.

## Global Constraints

- Never write into `bin/` (AGENTS.md ground rule). Tests only read from it.
- `editor/ivxedit/*.py` outside `gui/` must not import PySide6. Only `gui/` and `tests/qt.py`-based tests do. numpy is allowed in `dsp.py`, `speak.py` and `gui/`.
- Tests:
  - use `unittest`;
  - run with `python -m unittest discover -s editor/tests -t editor` from the repo root;
  - tests that need PySide6 skip with `PySide6 is not installed: pip install -r editor/requirements.txt`;
  - tests that need `ivxsay` skip with `ivxsay is not built: run make (or set IVXSAY)`.
- GUI tests run offscreen (`QT_QPA_PLATFORM=offscreen`, set by `tests/qt.py` before PySide6 is imported) and must not play audio (`SpeakPanel.autoplay = False`).
- `tools/` stays unchanged. Its `ivx.py` leaves files open, so the test run prints harmless `ResourceWarning`s.
- Work on a branch `ivxedit-step2` off `develop`. Commit after each task. End every commit message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.
- **The whole plan was built and run as a prototype while planning.** Every code block below is the prototype's file verbatim. Assembled into a scratch copy of the repo, the 80 tests pass (39 existing + 41 new), and the window was rendered offscreen and inspected. Copy the code exactly; if something fails, report it rather than redesigning.

## Facts found while planning

1. **`ivxsay -E` event lines are fixed-width**: `'%10.3f %-7s '` then the value (`cli/ivxsay.c` `print_event`), on stderr, with CRLF on Windows.
   - A phoneme's value is two characters: the low byte, then the high byte or a space.
   - The first event of an utterance is a phoneme with value 0, which prints a NUL.
   - The parser cuts by column and strips the NUL and trailing spaces.
2. **Speak through the model is byte-identical to `ivxsay` on the file** for the test texts of all 12 languages. That covers both the WAV and the event list (`tests/test_speak.py`).
3. **Phoneme names include `;`, `[`, `{`, `<` and a single space** (`b'\x00 '`). Names are unique within each file.
   - The instruction form therefore writes tokens separated by single spaces and decides an operand's kind by its position.
   - Patterns are written `{ … }` with spaced braces.
   - The space name is written `␠`.
4. **Every operand in the 15,564 shipped rules has its opcode's shape.** All pattern tests parse, no pattern has header bits 0x1001 set, and every LD/ST index word has `w & 0x017F == 0`. So the listing never needs raw `#hhhh` words for a shipped file (`test_every_shipped_rule_has_typed_operands`).
5. **Opcodes that the old disassembler listed under the same mnemonic** get distinct ones here:
   - `0x178 A/=` vs `0x180 A/=*` (they differ only in the divide-by-zero status, `src/cwmath.c`);
   - `0x17C A/=B` vs `0x184 A/=B*`;
   - `0x18C A>>=w` (16-bit sign copy) vs `0x190 A>>=`.
6. **The build tag at body 0x40 is 10 characters, `<LL>RU<yymmdd>`, in all 12 files.** What follows up to 0x6A is binary. Swedish happens to continue with `.P2`, which is why REFERENCE §5.2 calls the tag `SWRU981118.P2`. The Header view shows the 10 characters, and the Unknown view shows all of 0x40..0x70 in hex. Task 5 records this in REFERENCE.
7. **Parameter-test compare ops read "constant OP value"** in the engine: op 0x18 passes if `k > v` (`src/cwtest.c` `cw_ptest_a`). The listing writes them value-first (`<F1 < 50>` for op 0x18), so they read naturally.
8. **Class code → flags**: codes −6..5 index `0x008 0x001 0x020 0x040 0x010 0x080 0x100 0x200 0x600 0xA00 0x1020 0x2100`. Any other code makes a letter by position. Then TAB/SP get +blank, `.`/0x17 +sentence end, and LF/FF/CR +line break (`ivx_build_class_flags`, `src/ttsctrl.c:535`).
9. **Offscreen rendering**: the full window (12 files, every tree node selected) runs in about 5 s. The offscreen platform has no fonts unless `QT_QPA_FONTDIR=C:/Windows/Fonts` is set. Only screenshots need it; the tests don't.

## Deferred items picked up from step 1

These come from the step-1 final review (memory `ivxedit-deferred-items`) and are folded in here:
- **`load()` converts `KeyError`, `OverflowError` and `RecursionError` too**, so it raises only `IvxFormatError` (Task 1).
- **Speak calls `check()` before `to_bytes`.** A `PhRef` to a removed phoneme is reported as a problem instead of a bare `KeyError` (Task 3, `speak_model`).
- **`speak.py` has its own `ivxsay` lookup with an existence check** (`find_ivxsay`, Task 3). It doesn't trust `$IVXSAY` the way the tests' `util.ivxsay()` does.

## Spec deviations (Task 5 records them in the spec)

- **Phoneme item cells are written `value@time`** (`165@2`), as in REFERENCE §5.4. The spec's "time@value" was loose wording for the same cell.
- **Menus**: step 2 has only a File menu (Open…, ivxsay…, Quit). Edit and View arrive with the editing steps, when they have something to hold.
- **The Problems panel is a status-bar label `Problems (n)`** with the list as its tooltip. With nothing editable it is always 0 for a file that loads. Step 4 turns it into the panel.
- **The Characters view shows "(built-in)"** for 0x80–0xFF in files without H[14]. Those tables live in the DLL (`TP_CLASS80`, `tp_xlat_char`), not in the file.
- **The spectrogram shows 0–8 kHz** (a 6 ms Hann window, 2 ms hop, 512-point FFT), where the formants are, rather than up to Nyquist.

## File structure

| File | Responsibility |
|---|---|
| `editor/requirements.txt` | `PySide6>=6.8`, `numpy>=2.0` |
| `editor/ivxedit.pyw` | launcher: `python editor/ivxedit.pyw [file.ivx]` |
| `editor/ivxedit/model.py` | (modify) `load()` converts three more exception types |
| `editor/ivxedit/info.py` | phoneme/parameter names, A1 type names, class flags, header fields, hex dumps |
| `editor/ivxedit/xref.py` | `phoneme_uses(f)` |
| `editor/ivxedit/listing.py` | `rule_line(rule) -> Line(text, spans)` |
| `editor/ivxedit/speak.py` | `find_ivxsay`, `Options`, `parse_events`, `read_wav`, `speak_bytes`, `speak_model` |
| `editor/ivxedit/dsp.py` | `minmax`, `spectrogram`, `levels` |
| `editor/ivxedit/gui/__init__.py` | package marker (empty) |
| `editor/ivxedit/gui/app.py` | `main(argv)` |
| `editor/ivxedit/gui/mainwindow.py` | `MainWindow`: tree, stacked views, jumps, File menu, status bar |
| `editor/ivxedit/gui/views.py` | `RowsModel`, `TableView`, `HeaderView`, `TextView`, `LexiconView`, `mono`, `compact` |
| `editor/ivxedit/gui/phonemeview.py` | `PhonemeView` |
| `editor/ivxedit/gui/rulesview.py` | `RulesView` (instruction form, highlighting, phoneme jumps) |
| `editor/ivxedit/gui/speakpanel.py` | `SpeakPanel`, `SpeakJob` |
| `editor/ivxedit/gui/waveview.py` | `SignalView` |
| `editor/tests/qt.py` | offscreen `QApplication`, `needs_qt`, `temp_settings`, `SlotErrors` |
| `editor/tests/test_info.py`, `test_listing.py`, `test_speak.py`, `test_gui.py`, `test_speakpanel.py` | tests |

---

### Task 1: Readable names, phoneme cross-references, `load()` exceptions

**Files:**
- Modify: `editor/ivxedit/model.py` (the `except` line in `load`, at the end of the file)
- Create: `editor/ivxedit/info.py`, `editor/ivxedit/xref.py`
- Test: `editor/tests/test_info.py`

**Interfaces:**
- Consumes (step 1): `ivxedit.model` (`load`, `PNAMES`, `IvxFile` with `header`, `head`, `directory`, `class_lo`, `class_hi`, `xlat_hi`, `modules`, `pads`, `end_pad`, `h2_rel`, `param(i)`, `version`; `Phoneme`, `VarItem`, `PhRef`), `ivxedit.binfmt` (`u16`, `u32`), `ivxedit.rules.ph_positions`.
- Produces:
  - `info.ph_name(name: bytes) -> str`
  - `info.param_name(pid) -> str`
  - `info.item_name(iid) -> str`
  - `info.a1_text(a1) -> str`
  - `info.s8(v) -> int`
  - `info.char_flags(ch, code) -> int`
  - `info.flags_text(fl) -> str`
  - `info.char_rows(f) -> [(byte, char, code|None, flags|None, translit|None)]` (256 rows)
  - `info.header_fields(f) -> [(label, text)]`
  - `info.hexdump(data, base=0) -> str`
  - `info.unknown_text(f) -> str`
  - `xref.phoneme_uses(f) -> {Phoneme: [(module name, rule index)]}`

- [ ] **Step 1: Create the branch.**

```bash
git checkout develop
git checkout -b ivxedit-step2
```

- [ ] **Step 2: Write the failing tests.** Create `editor/tests/test_info.py`:

```python
import unittest
from unittest import mock

from ivxedit import model
from ivxedit.binfmt import IvxFormatError
from ivxedit.info import (a1_text, char_flags, char_rows, flags_text, header_fields, hexdump,
                          item_name, ph_name, unknown_text)
from ivxedit.model import PNAMES, load
from ivxedit.rules import ph_positions
from ivxedit.xref import phoneme_uses
from tests.util import LANGS, ivx_bytes


class InfoTest(unittest.TestCase):
    def test_names(self):
        self.assertEqual(ph_name(b'\0+'), '+')
        self.assertEqual(ph_name(b'A:'), 'A:')
        self.assertEqual(item_name(5), 'F1')
        self.assertEqual(item_name(0xAC), 'vac')
        self.assertEqual(item_name(40), 'p40')

    def test_a1_text(self):
        self.assertEqual(a1_text(18 * 4), '18: approach → freq')     # spec: GUI, Parameters
        self.assertEqual(a1_text(0x80), '32: cosine, value remapped')
        self.assertEqual(a1_text(6), '0x6: unknown')

    def test_every_a1_in_the_files_has_a_name(self):
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            for i in range(len(PNAMES)):
                with self.subTest(lang=lang, param=PNAMES[i]):
                    self.assertNotIn('unknown', a1_text(f.param(i)[1]))

    def test_char_flags(self):
        # REFERENCE §5.2: codes -6..5 select a flag word; others make a letter by position
        self.assertEqual(char_flags(ord('a'), 0x40), 0x600)
        self.assertEqual(char_flags(ord('A'), 0x40), 0xA00)
        self.assertEqual(char_flags(0xE9, 0x40), 0x200)
        self.assertEqual(char_flags(ord('%'), 0), 0x100)             # code 0: symbol
        self.assertEqual(char_flags(ord('5'), -1), 0x80)             # code -1: digit
        self.assertEqual(char_flags(ord(' '), -5), 0x01 | 0x02)      # SP: blank added
        self.assertEqual(char_flags(ord('.'), 0), 0x110)             # '.': sentence end added
        self.assertEqual(char_flags(0x0D, -5), 0x05)
        self.assertEqual(flags_text(0x1020), '0x1020: 0x20, 0x1000')
        self.assertEqual(flags_text(0x600), '0x600: letter, lower case')

    def test_char_rows(self):
        sw, ge, am = (load(ivx_bytes(l)) for l in ('sw', 'ge', 'am'))
        rows = char_rows(sw)
        self.assertEqual(len(rows), 256)
        self.assertEqual(rows[ord('A')][:2], (0x41, 'A'))
        self.assertIsNotNone(rows[0xE5][4])                          # sw has H[14]
        self.assertEqual(char_rows(am)[0xE5][2:], (None, None, None))   # am: built-in tables
        self.assertEqual(len(char_rows(ge)[0xC4][4]), 2)

    def test_header_fields(self):
        fields = dict(header_fields(load(ivx_bytes('sw'))))
        self.assertTrue(fields['Copyright'].startswith('Copyright (C) Solna 1988'))
        self.assertEqual(fields['Build tag'], 'SWRU981118')
        self.assertEqual(fields['Lexicon name'], 'SWULEX.IVX')
        self.assertEqual(fields['Word 0x6E (2 × parameters)'], '58')

    def test_hexdump(self):
        self.assertEqual(hexdump(b'AB\0', 0x10), '000010  41 42 00' + ' ' * 39 + '  AB.')
        self.assertEqual(len(hexdump(bytes(40)).splitlines()), 3)

    def test_unknown_text(self):
        text = unknown_text(load(ivx_bytes('da')))
        self.assertIn('  H[2]   0x0000db00', text)
        self.assertIn('H[2] points 0x1800 bytes into the indexed lexicon', text)


class XrefTest(unittest.TestCase):
    def test_uses_count_every_rule_once(self):
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            uses = phoneme_uses(f)
            with self.subTest(lang=lang):
                for ph, refs in uses.items():
                    self.assertEqual(len(refs), len(set(refs)))
                expect = set()
                for name, m in f.modules.items():
                    for i, r in enumerate(m.rules):
                        for ins in r.instrs:
                            for k in ph_positions(ins):
                                if ins.args[k].ph is not None:
                                    expect.add((id(ins.args[k].ph), name, i))
                got = {(id(ph), n, i) for ph, refs in uses.items() for n, i in refs}
                self.assertEqual(got, expect)

    def test_swedish_plus(self):
        f = load(ivx_bytes('sw'))
        plus = next(ph for ph in f.phonemes if ph.name == b'\0+')
        rule = f.modules['M0'].rules[next(i for n, i in phoneme_uses(f)[plus] if n == 'M0')]
        self.assertTrue(any(ins.args and getattr(ins.args[0], 'ph', None) is plus for ins in rule.instrs))


class LoadErrorsTest(unittest.TestCase):
    def test_load_raises_only_ivx_format_error(self):
        for exc in (KeyError('k'), OverflowError('o'), RecursionError('r'), IndexError('i')):
            with self.subTest(exc=type(exc).__name__), \
                    mock.patch.object(model, '_load', side_effect=exc):
                with self.assertRaises(IvxFormatError):
                    load(b'')


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 3: Run them to see them fail.**

Run: `python -m unittest discover -s editor/tests -t editor -p test_info.py`
Expected: an import error for `ivxedit.info`.

- [ ] **Step 4: Widen `load()`'s exception list.** In `editor/ivxedit/model.py`, replace

```python
    except (IndexError, ValueError, struct.error) as e:
```

with

```python
    except (IndexError, KeyError, ValueError, OverflowError, RecursionError, struct.error) as e:
```

- [ ] **Step 5: Create `editor/ivxedit/info.py`:**

```python
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
              (0x010, 'sentence end'), (0x040, 'number separator'), (0x080, 'digit'),
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
    """the 256 rows of the Characters view: (byte, char, class code, flags, transliteration);
    code, flags and transliteration are None for 0x80-0xFF in files without H[14], which
    use the engine's built-in tables"""
    rows = []
    for ch in range(256):
        c = bytes([ch]).decode('cp1252', errors='replace')
        shown = c if c.isprintable() else ''
        if ch < 0x80:
            code, xl = s8(f.class_lo[ch]), None
        elif f.class_hi is not None:
            code = s8(f.class_hi[ch - 0x80])
            xl = f.xlat_hi[2 * (ch - 0x80):2 * (ch - 0x80) + 2].decode('latin-1')
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
```

- [ ] **Step 6: Create `editor/ivxedit/xref.py`:**

```python
"""Which rules refer to which phoneme (the phoneme view's "Used by")."""
from .model import PhRef
from .rules import ph_positions


def phoneme_uses(f):
    """{Phoneme: [(module name, rule index), ...]}: every rule with a phoneme-id operand
    in that phoneme's record, once per rule, modules in name order (M0..M5)"""
    uses = {}
    for name in sorted(f.modules):
        for i, rule in enumerate(f.modules[name].rules):
            seen = set()
            for ins in rule.instrs:
                for k in ph_positions(ins):
                    a = ins.args[k]
                    if isinstance(a, PhRef) and a.ph is not None and id(a.ph) not in seen:
                        seen.add(id(a.ph))
                        uses.setdefault(a.ph, []).append((name, i))
    return uses
```

- [ ] **Step 7: Run the tests.**

Run: `python -m unittest discover -s editor/tests -t editor`
Expected: `OK`, 50 tests (39 existing + 11 new).

- [ ] **Step 8: Commit.**

```bash
git add editor/ivxedit/model.py editor/ivxedit/info.py editor/ivxedit/xref.py editor/tests/test_info.py
git commit -m "ivxedit: readable names (info), phoneme cross-references (xref), load converts all decode errors

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Rules in instruction form

**Files:**
- Create: `editor/ivxedit/listing.py`
- Test: `editor/tests/test_listing.py`

**Interfaces:**
- Consumes:
  - `info.item_name`, `info.ph_name`, `info.s8` (Task 1);
  - `model.PhRef`, `model.Phoneme`, `model.load`;
  - `rules.FIXED`, `rules.PH_OPS`, `rules.REPL`, `rules.REPL_NEXT`, `rules.rep_split`, `rules.Instr`, `rules.Rule`.
- Produces:
  - `listing.Line(text: str, spans: list[(start, end, kind, obj)])`, where `kind` is one of `'op' 'ph' 'num' 'feat' 'param' 'raw'` and `obj` is the `PhRef` for `'ph'`;
  - `listing.rule_line(rule) -> Line`;
  - `listing.MNEMONICS: {opcode: str}`;
  - `listing.ph_text(ref) -> str`.

The format is described in the module docstring. Every operand word appears in the text, so a line determines the rule's words. The parser that reads lines back, and the round-trip test (spec Testing 5), come with step 3. `rule_line` must never raise, whatever the `Instr` holds: an instruction without its opcode's operand shape is written as raw words.

- [ ] **Step 1: Write the failing tests.** Create `editor/tests/test_listing.py`:

```python
import random
import unittest

from ivxedit.listing import MNEMONICS, rule_line
from ivxedit.model import Phoneme, PhRef, load
from ivxedit.rules import Instr, Rule
from tests.util import LANGS, ivx_bytes

A = Phoneme(b'A:', 0, 0, [])
PLUS = Phoneme(b'\0+', 0, 0, [])
SPACE = Phoneme(b'\0 ', 0, 0, [])


def line(*instrs, iterative=False):
    return rule_line(Rule(iterative, list(instrs))).text


class ListingTest(unittest.TestCase):
    def test_swedish_rule_32(self):
        # REFERENCE §5.6: swrules M0 rule #32
        f = load(ivx_bytes('sw'))
        texts = [rule_line(r).text for r in f.modules['M0'].rules]
        self.assertIn('RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32', texts)

    def test_operand_kinds(self):
        self.assertEqual(line(Instr(0x40, [PhRef(A)]), Instr(0x10, [7]), iterative=True),
                         'RULE* SEEK A: ; NEXT #7')
        self.assertEqual(line(Instr(0x4C, [PhRef(SPACE)]), Instr(0x4C, [PhRef(None)]),
                              Instr(0x4C, [PhRef(A, 4)]), Instr(0x4C, [0x7000])),
                         'RULE R ␠ ; R <end> ; R A:+4 ; R #7000')
        self.assertEqual(line(Instr(0xC4, [0x0010, 0, 0x0110, 0])), 'RULE SETF [00100000/01100000]')
        self.assertEqual(line(Instr(0xBC, [0x010C, PhRef(A)])), 'RULE REPL del 1 -> A:')
        self.assertEqual(line(Instr(0xBC, [0x0314] + [PhRef(A)] * 23)),
                         'RULE REPL del 2 -> ' + ' '.join(['A:'] * 23))      # a carrying word
        self.assertEqual(line(Instr(0x150, [0xFFFB]), Instr(0x188, [3, 2])), 'RULE A= -5 ; A*=frac 3/2^2')

    def test_ld_st(self):
        self.assertEqual(line(Instr(0x100, [0x0005, 0x0000])), 'RULE LD F1@0')
        self.assertEqual(line(Instr(0x108, [0x4005, 0xFC80])), 'RULE STu F1@-2.hi t=64')
        self.assertEqual(line(Instr(0x10C, [0x80AC])), 'RULE ST vac t=-128')
        self.assertEqual(line(Instr(0x100, [0x0005, 0x0203])), 'RULE LD F1@#0203')

    def test_patterns(self):
        self.assertEqual(line(Instr(0x68, [0x8004, PhRef(PLUS)])), 'RULE FOCUSP { + }')
        self.assertEqual(line(Instr(0x6C, [0x037C, 0x400A, 0x0001, 0, 0x0003, 0])),
                         'RULE RP 3,124 { [00010000/00030000] }')
        self.assertEqual(line(Instr(0x68, [0xA00A, PhRef(PLUS), 0xC018, 10, 0x0500])),
                         'RULE FOCUSP { + <vc0 < 10> <F1 -> R0> }')
        self.assertEqual(line(Instr(0x68, [0x2006, 0x05C6, 0x0533])),
                         'RULE FOCUSP { <F1.last.hi -> R1> <#0533> }')
        # header bit 13 with no tests: the parts do not give the header, so it is raw
        self.assertEqual(line(Instr(0x68, [0xA004, PhRef(PLUS)])), 'RULE FOCUSP { #a004 + }')

    def test_unshaped_instructions_are_raw(self):
        self.assertEqual(line(Instr(0x40, [1, 2])), 'RULE SEEK #0001 #0002')
        self.assertEqual(line(Instr(0x1234, [PhRef(A)])), 'RULE ?1234 A:')
        self.assertEqual(line(Instr(0xBC, [0x0124, PhRef(A)])), 'RULE REPL #0124 A:')

    def test_spans(self):
        ln = rule_line(Rule(False, [Instr(0x40, [PhRef(PLUS)]), Instr(0x10, [32])]))
        self.assertEqual([(ln.text[s:e], k) for s, e, k, _ in ln.spans],
                         [('RULE', 'op'), ('SEEK', 'op'), ('+', 'ph'), ('NEXT', 'op'), ('#32', 'num')])
        self.assertIs(ln.spans[2][3].ph, PLUS)

    def test_every_shipped_rule_has_typed_operands(self):
        n = 0
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            for name, m in f.modules.items():
                for i, r in enumerate(m.rules):
                    ln = rule_line(r)
                    n += 1
                    kinds = {k for _, _, k, _ in ln.spans}
                    bad = [ln.text[s:e] for s, e, k, _ in ln.spans
                           if k == 'raw' or (k == 'op' and ln.text[s] == '?')]
                    with self.subTest(lang=lang, module=name, rule=i):
                        self.assertEqual(bad, [])
                        self.assertNotIn('#', ''.join(ln.text[s:e] for s, e, k, _ in ln.spans if k == 'param'))
                        self.assertTrue(kinds <= {'op', 'ph', 'num', 'feat', 'param'})
        self.assertEqual(n, 15564)

    def test_never_raises(self):
        rng = random.Random(1)
        ops = list(MNEMONICS) + [0x02, 0x1234]
        for _ in range(3000):
            args = [rng.choice([rng.randrange(0x10000), PhRef(A), PhRef(None), -1, 0x12345])
                    for _ in range(rng.randrange(6))]
            rule_line(Rule(False, [Instr(rng.choice(ops), args)]))


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run them to see them fail.**

Run: `python -m unittest discover -s editor/tests -t editor -p test_listing.py`
Expected: an import error for `ivxedit.listing`.

- [ ] **Step 3: Create `editor/ivxedit/listing.py`:**

```python
"""Rules in instruction form: one line of text per rule (spec: Rule notation, the fallback).

A line is 'RULE' or 'RULE*', then the rule's instructions, each a mnemonic and its
operands, separated by ' ; '. Every operand word is in the text, so a line determines its
rule's words; the parser that reads lines back comes with the rule notation (step 3).
Tokens are separated by single spaces, and an operand's kind follows from its position:

  phoneme      its name: A:  +  [  ;  (the name ' ' is written ␠); 'NAME+d' for a
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
from dataclasses import dataclass, field

from .info import item_name, ph_name, s8
from .model import PhRef
from .rules import FIXED, PH_OPS, REPL, REPL_NEXT, rep_split

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
        name = '␠' if name == ' ' else name
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
    parts, i = [], 1
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
        return len(a) >= 1 and isinstance(a[0], int) and rep_split(a[0])[1] == len(a) - 1
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
```

- [ ] **Step 4: Run the tests.**

Run: `python -m unittest discover -s editor/tests -t editor`
Expected: `OK`, 58 tests.

- [ ] **Step 5: Commit.**

```bash
git add editor/ivxedit/listing.py editor/tests/test_listing.py
git commit -m "ivxedit: rules in instruction form with highlight/jump spans (listing)

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Speak through `ivxsay`, and the signal helpers

**Files:**
- Create: `editor/ivxedit/speak.py`, `editor/ivxedit/dsp.py`
- Test: `editor/tests/test_speak.py`

**Interfaces:**
- Consumes:
  - `checks.check` and `writer.to_bytes` (step 1);
  - `tests.util.ivxsay`, `ivx_path`, `ivx_bytes`, `lang_texts`, `LANGS`.
- Produces:
  - `speak.ROOT`, and `speak.BUILDS` (a tuple of `Path`; tests patch it);
  - `speak.SpeakError(Exception)`;
  - `speak.Options(voice=None, rate=None, pitch=None, fd=None, fps=None, fs=22050)` with `.args() -> [str]`;
  - `speak.Event(time: float, kind: str, value: str)`;
  - `speak.Speech(wav: bytes, samples: np.ndarray[int16], fs: int, events: [Event], messages: [str])` with the `.phonemes` property;
  - `speak.find_ivxsay(override=None) -> Path | None`;
  - `speak.parse_events(err: bytes) -> ([Event], [str])`;
  - `speak.read_wav(data) -> (samples, fs)`;
  - `speak.speak_bytes(data, text, opts=None, exe=None) -> Speech`;
  - `speak.speak_model(f, text, opts=None, exe=None) -> Speech` (raises `SpeakError` when `check(f)` finds problems);
  - `dsp.NFFT = 512`;
  - `dsp.minmax(x, n) -> (mins, maxs)`;
  - `dsp.spectrogram(x, fs, win_s=0.006, hop_s=0.002) -> (dB[frames, NFFT//2+1], hop seconds)`;
  - `dsp.levels(db, range_db=70.0, fs=None, fmax=8000) -> uint8[bins, frames]`.

- [ ] **Step 1: Write the failing tests.** Create `editor/tests/test_speak.py`:

```python
"""Speak without the GUI (spec: Testing, 10) and the signal helpers."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import numpy as np

from ivxedit import dsp
from ivxedit.model import load
from ivxedit.speak import (Event, Options, SpeakError, find_ivxsay, parse_events, read_wav,
                           speak_bytes, speak_model)
from tests.util import LANGS, ivx_bytes, ivx_path, ivxsay, lang_texts

SAY = ivxsay()


def direct(lang, text):
    """ivxsay run by hand on bin/<lang>rules.ivx -> (WAV bytes, stderr)"""
    with tempfile.TemporaryDirectory() as d:
        txt, wav = Path(d) / 't.txt', Path(d) / 'o.wav'
        txt.write_bytes(text.encode('utf-8'))
        r = subprocess.run([str(SAY), '-l', str(ivx_path(lang)), '-i', str(txt), '-o', str(wav),
                            '-E', '-s', '22050'], capture_output=True, check=True)
        return wav.read_bytes(), r.stderr


class EventsTest(unittest.TestCase):
    def test_parse_events(self):
        err = (b'     0.000 phoneme \0 \r\n     0.100 phoneme G \r\n     0.210 phoneme O:\r\n'
               b'     1.000 mark    7\r\nivxsay: something\r\n     2.640 done    0\r\n')
        events, other = parse_events(err)
        self.assertEqual(events, [Event(0.0, 'phoneme', ''), Event(0.1, 'phoneme', 'G'),
                                  Event(0.21, 'phoneme', 'O:'), Event(1.0, 'mark', '7'),
                                  Event(2.64, 'done', '0')])
        self.assertEqual(other, ['ivxsay: something'])

    def test_options(self):
        self.assertEqual(Options().args(), ['-s', '22050'])
        self.assertEqual(Options(voice=2, rate=50, pitch=40, fd=30, fps=120, fs=16000).args(),
                         ['-s', '16000', '-v', '2', '-r', '50', '-p', '40', '-f', '30', '-F', '120'])

    def test_find_ivxsay(self):
        with tempfile.TemporaryDirectory() as d:
            exe = Path(d) / 'ivxsay.exe'
            exe.write_bytes(b'')
            with mock.patch('ivxedit.speak.BUILDS', ()):
                self.assertEqual(find_ivxsay(str(exe)), exe)
                self.assertIsNone(find_ivxsay(str(Path(d) / 'missing.exe')))
                self.assertIsNone(find_ivxsay())
            with mock.patch('ivxedit.speak.BUILDS', (exe,)):
                self.assertEqual(find_ivxsay(str(Path(d) / 'missing.exe')), exe)

    def test_missing_ivxsay(self):
        with mock.patch('ivxedit.speak.BUILDS', ()):
            with self.assertRaisesRegex(SpeakError, 'run make'):
                speak_bytes(ivx_bytes('sw'), 'God morgon.')

    def test_model_with_problems_is_refused(self):
        f = load(ivx_bytes('sw'))
        f.phonemes.remove(next(ph for ph in f.phonemes if ph.name == b'\0+'))   # rules use '+'
        with self.assertRaisesRegex(SpeakError, 'problem'):
            speak_model(f, 'God morgon.', exe=Path('never-run'))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
class SpeakTest(unittest.TestCase):
    def test_same_as_ivxsay_in_every_language(self):
        texts = lang_texts()
        for lang in LANGS:
            text = texts['%srules.ivx' % lang].decode('cp1252')
            with self.subTest(lang=lang):
                sp = speak_model(load(ivx_bytes(lang)), text, exe=SAY)
                wav, err = direct(lang, text)
                self.assertEqual(sp.wav, wav)
                self.assertEqual(sp.events, parse_events(err)[0])
                self.assertEqual(sp.fs, 22050)
                self.assertEqual(len(sp.samples), (len(wav) - 44) // 2)
                self.assertTrue(sp.phonemes)

    def test_failure_is_reported(self):
        with self.assertRaisesRegex(SpeakError, 'ivxsay failed'):
            speak_bytes(b'not an ivx file', 'God morgon.', exe=SAY)

    def test_empty_text(self):
        with self.assertRaisesRegex(SpeakError, 'no text'):
            speak_bytes(ivx_bytes('sw'), '  ', exe=SAY)


class DspTest(unittest.TestCase):
    def test_spectrogram_peak(self):
        fs = 22050
        x = (8000 * np.sin(2 * np.pi * 1000 * np.arange(fs) / fs)).astype(np.int16)
        db, hop = dsp.spectrogram(x, fs)
        self.assertAlmostEqual(hop, 44 / fs)
        self.assertEqual(db.shape, (-(-fs // 44), dsp.NFFT // 2 + 1))
        self.assertEqual(int(db[100].argmax()), round(1000 / (fs / dsp.NFFT)))

    def test_levels(self):
        db = np.array([[0.0, -10.0], [-100.0, -35.0]])            # 2 frames, 2 bins
        lv = dsp.levels(db, range_db=70, fs=22050, fmax=None)
        self.assertEqual(lv.dtype, np.uint8)
        self.assertEqual(lv.shape, (2, 2))                          # [bins, frames]
        self.assertEqual(lv[1, 0], 255)                             # bin 0 at the bottom
        self.assertEqual(lv[1, 1], 0)

    def test_levels_crop(self):
        db = np.zeros((3, dsp.NFFT // 2 + 1))
        self.assertEqual(dsp.levels(db, fs=16000, fmax=8000).shape[0], dsp.NFFT // 2 + 1)
        self.assertEqual(dsp.levels(db, fs=32000, fmax=8000).shape[0], dsp.NFFT // 4 + 1)

    def test_minmax(self):
        lo, hi = dsp.minmax(np.arange(10), 3)
        self.assertEqual(lo.tolist(), [0, 3, 6])
        self.assertEqual(hi.tolist(), [2, 5, 9])
        lo, hi = dsp.minmax(np.arange(2), 4)
        self.assertEqual(lo.tolist(), [0, 0, 1, 1])
        self.assertEqual(len(dsp.minmax(np.array([], dtype=np.int16), 5)[0]), 5)

    def test_read_wav(self):
        import io, wave
        buf = io.BytesIO()
        with wave.open(buf, 'wb') as w:
            w.setnchannels(1)
            w.setsampwidth(2)
            w.setframerate(16000)
            w.writeframes(np.array([1, -2, 3], dtype='<i2').tobytes())
        samples, fs = read_wav(buf.getvalue())
        self.assertEqual((samples.tolist(), fs), ([1, -2, 3], 16000))


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run them to see them fail.**

Run: `python -m unittest discover -s editor/tests -t editor -p test_speak.py`
Expected: an import error for `ivxedit.dsp`.

- [ ] **Step 3: Create `editor/ivxedit/speak.py`:**

```python
"""Speak: run ivxsay on a model or on .ivx bytes, read back the audio and the events
(spec: GUI, Speak). ivxsay runs as its own process: the library is 32-bit, Python 64-bit."""
import io
import subprocess
import tempfile
import wave
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np

from .checks import check
from .writer import to_bytes

ROOT = Path(__file__).resolve().parents[2]
BUILDS = (ROOT / 'build' / 'ivxsay.exe', ROOT / 'build' / 'linux' / 'ivxsay')
KINDS = ('mark', 'phoneme', 'error', 'done', '?')


class SpeakError(Exception):
    """ivxsay is missing or failed, or the model cannot be written; str() says which"""


@dataclass
class Options:
    """ivxsay options; None leaves the engine's default"""
    voice: int = None   # -v 0..9
    rate: int = None    # -r 0..100
    pitch: int = None   # -p 0..100
    fd: int = None      # -f 0..100
    fps: int = None     # -F 2..1000 frames per second (speed)
    fs: int = 22050     # -s sample rate

    def args(self):
        out = ['-s', str(self.fs)]
        for flag, v in (('-v', self.voice), ('-r', self.rate), ('-p', self.pitch),
                        ('-f', self.fd), ('-F', self.fps)):
            if v is not None:
                out += [flag, str(v)]
        return out


@dataclass
class Event:
    time: float       # seconds, as ivxsay prints it (ms resolution)
    kind: str         # 'mark', 'phoneme', 'error', 'done'
    value: str        # the phoneme's name ('' for none), else the number


@dataclass
class Speech:
    wav: bytes        # the WAV file ivxsay wrote
    samples: np.ndarray   # int16, mono
    fs: int
    events: list      # [Event]
    messages: list = field(default_factory=list)   # stderr lines that are not events

    @property
    def phonemes(self):
        return [e for e in self.events if e.kind == 'phoneme']


def find_ivxsay(override=None):
    """the ivxsay to run: `override` if it names an existing file, else build/ivxsay.exe or
    build/linux/ivxsay of this repository; None if there is none"""
    for p in ([Path(override)] if override else []) + list(BUILDS):
        if p.is_file():
            return p
    return None


def parse_events(err):
    """ivxsay -E's stderr (bytes) -> ([Event], [other lines]); an event line is
    '%10.3f %-7s value' (cli/ivxsay.c print_event)"""
    events, other = [], []
    for raw in err.split(b'\n'):
        line = raw.rstrip(b'\r').decode('latin-1')
        kind = line[11:18].rstrip()
        try:
            t = float(line[:10])
        except ValueError:
            t = None
        if t is not None and len(line) >= 19 and line[10] == ' ' and line[18] == ' ' and kind in KINDS:
            value = line[19:]
            if kind == 'phoneme':
                value = value.replace('\0', '').rstrip(' ')
            events.append(Event(t, kind, value))
        elif line:
            other.append(line)
    return events, other


def read_wav(data):
    """WAV bytes (16-bit mono) -> (int16 samples, sample rate)"""
    with wave.open(io.BytesIO(data)) as w:
        if w.getnchannels() != 1 or w.getsampwidth() != 2:
            raise SpeakError('ivxsay wrote a WAV that is not 16-bit mono')
        return np.frombuffer(w.readframes(w.getnframes()), dtype='<i2').copy(), w.getframerate()


def speak_bytes(data, text, opts=None, exe=None):
    """speak `text` with the .ivx file `data` -> Speech"""
    opts = opts or Options()
    exe = exe or find_ivxsay()
    if exe is None:
        raise SpeakError('ivxsay is not built: run make in the repository root')
    if not text.strip():
        raise SpeakError('there is no text to speak')
    with tempfile.TemporaryDirectory(prefix='ivxedit-') as d:
        ivx, txt, wav = (Path(d) / n for n in ('speak.ivx', 'text.txt', 'speak.wav'))
        ivx.write_bytes(data)
        txt.write_bytes(text.encode('utf-8'))
        r = subprocess.run([str(exe), '-l', str(ivx), '-i', str(txt), '-o', str(wav), '-E']
                           + opts.args(), capture_output=True, timeout=300,
                           creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
        events, other = parse_events(r.stderr)
        if r.returncode != 0 or not wav.is_file():
            raise SpeakError('ivxsay failed (exit %d): %s' % (r.returncode, '; '.join(other) or 'no message'))
        out = wav.read_bytes()
    samples, fs = read_wav(out)
    return Speech(out, samples, fs, events, other)


def speak_model(f, text, opts=None, exe=None):
    """speak `text` with the model f; refuses (SpeakError) when check(f) finds problems"""
    problems = check(f)
    if problems:
        raise SpeakError('the file has %d problem(s): %s' % (
            len(problems), '; '.join(str(p) for p in problems[:3])))
    return speak_bytes(to_bytes(f), text, opts, exe)
```

- [ ] **Step 4: Create `editor/ivxedit/dsp.py`:**

```python
"""Signal helpers for the Speak panel: waveform envelope and spectrogram (numpy only)."""
import numpy as np

NFFT = 512


def minmax(x, n):
    """split x into n nearly equal columns -> (mins, maxs), each n long; empty columns
    (n > len(x)) repeat the sample before them"""
    if len(x) == 0:
        z = np.zeros(n, dtype=x.dtype)
        return z, z
    starts = np.minimum((np.arange(n) * len(x)) // n, len(x) - 1)
    return np.minimum.reduceat(x, starts), np.maximum.reduceat(x, starts)


def spectrogram(x, fs, win_s=0.006, hop_s=0.002):
    """wide-band spectrogram -> (dB array [frames, NFFT//2 + 1], hop in seconds); frame i
    is centred on sample i*hop"""
    win, hop = max(16, round(win_s * fs)), max(1, round(hop_s * fs))
    x = np.asarray(x, dtype=np.float64)
    pad = np.concatenate([np.zeros(win // 2), x, np.zeros(win)])
    nfr = max(1, -(-len(x) // hop))
    idx = np.arange(win)[None, :] + hop * np.arange(nfr)[:, None]
    frames = pad[idx] * np.hanning(win)[None, :]
    mag = np.abs(np.fft.rfft(frames, NFFT, axis=1))
    return 20 * np.log10(mag + 1e-3), hop / fs


def levels(db, range_db=70.0, fs=None, fmax=8000):
    """dB array [frames, bins] -> uint8 image rows [bins, frames], low frequencies at the
    bottom (last row), the loudest point 255 and anything range_db below it 0; with fs,
    only the bins up to fmax Hz"""
    if fs and fmax:
        db = db[:, :min(db.shape[1], int(fmax / (fs / 2) * (db.shape[1] - 1)) + 1)]
    top = db.max()
    v = np.clip((db - (top - range_db)) / range_db, 0, 1) * 255
    return np.ascontiguousarray(v.T[::-1].astype(np.uint8))
```

- [ ] **Step 5: Run the tests.** `ivxsay` must be built (`make build/ivxsay.exe`, or plain `make`), or `SpeakTest` skips.

Run: `python -m unittest discover -s editor/tests -t editor`
Expected: `OK`, 71 tests, with no skips when `build/ivxsay.exe` exists.

- [ ] **Step 6: Commit.**

```bash
git add editor/ivxedit/speak.py editor/ivxedit/dsp.py editor/tests/test_speak.py
git commit -m "ivxedit: speak a model through ivxsay (WAV and -E events), spectrogram and envelope

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: The read-only window

**Files:**
- Create:
  - `editor/requirements.txt`, `editor/ivxedit.pyw`;
  - `editor/ivxedit/gui/__init__.py` (empty), `gui/app.py`, `gui/views.py`, `gui/phonemeview.py`, `gui/rulesview.py`, `gui/mainwindow.py`;
  - `editor/tests/qt.py`.
- Test: `editor/tests/test_gui.py`

**Interfaces:**
- Consumes:
  - from Task 1: `info.*`, `xref.phoneme_uses`;
  - from Task 2: `listing.rule_line`;
  - from step 1: `checks.check`, `checks.rule_label`, `model.load`, `model.PNAMES`, `model.VarItem`, `binfmt.IvxFormatError`;
  - `tests.util.LANGS`, `ivx_path`.
- Produces (Task 5 relies on these):
  - `MainWindow(settings: QSettings)`, with attributes `f`, `path`, `uses`, `tree`, `stack`, `ph_items`, `module_items`, `rule_views`, `phoneme_view`, `problems` (a `QLabel`);
  - `MainWindow` methods `open_file(path)` (raises `OSError`/`IvxFormatError`), `open_path(path)` (shows a message box), `items()`, `show_item(item)`, `show_phoneme(ph)`, `show_rule(module, i)`;
  - `RulesView` with `.lines`, `ref_at(rule, col)`, `goto_rule(i)`;
  - `PhonemeView.uses` (a `QListWidget`);
  - `tests.qt.app()`, `needs_qt`, `temp_settings(test)`, `SlotErrors`.

The window has no Speak panel yet; Task 5 adds it with five small edits to `mainwindow.py`.

- [ ] **Step 1: Check that PySide6 is installed.**

Run: `python -c "import PySide6; print(PySide6.__version__)"`
Expected: `6.11.2` (≥ 6.8). If it is missing, stop and report; don't install packages yourself.

- [ ] **Step 2: Create the test helpers, `editor/tests/qt.py`:**

```python
"""Qt test helpers: an offscreen QApplication, a skip when PySide6 is missing, and a
catcher for exceptions raised inside slots (Qt prints them and carries on)."""
import os
import sys
import tempfile
import unittest

os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
try:
    from PySide6.QtCore import QSettings
    from PySide6.QtWidgets import QApplication
except ImportError:
    QApplication = None

needs_qt = unittest.skipIf(QApplication is None,
                           'PySide6 is not installed: pip install -r editor/requirements.txt')


def app():
    return QApplication.instance() or QApplication([])


def temp_settings(test):
    """QSettings in a temporary INI file, removed after the test"""
    d = tempfile.TemporaryDirectory()
    test.addCleanup(d.cleanup)
    return QSettings(os.path.join(d.name, 'ivxedit.ini'), QSettings.Format.IniFormat)


class SlotErrors:
    """with SlotErrors() as errs: ... -> errs holds the exceptions slots raised"""

    def __enter__(self):
        self.errors, self._old = [], sys.excepthook
        sys.excepthook = lambda t, v, tb: self.errors.append(v)
        return self.errors

    def __exit__(self, *exc):
        sys.excepthook = self._old
        return False
```

- [ ] **Step 3: Write the failing smoke tests.** Create `editor/tests/test_gui.py`:

```python
"""The read-only GUI, offscreen (spec: Testing, 9)."""
import tempfile
import unittest
from pathlib import Path

from ivxedit.binfmt import IvxFormatError
from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import LANGS, ivx_path


@needs_qt
class SmokeTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.win = MainWindow(temp_settings(self))
        self.addCleanup(self.win.close)

    def top_level(self):
        t = self.win.tree
        return [t.topLevelItem(k).text(0) for k in range(t.topLevelItemCount())]

    def test_every_node_of_every_file(self):
        for lang in LANGS:
            with self.subTest(lang=lang), SlotErrors() as errors:
                self.win.open_file(ivx_path(lang))
                for item in self.win.items():
                    self.win.tree.setCurrentItem(item)
                    self.assertIs(self.win.tree.currentItem(), item)
                self.assertEqual(errors, [])

    def test_tree_has_every_section(self):
        self.win.open_file(ivx_path('sw'))
        self.assertEqual(self.top_level(), [
            'Header', 'Parameters (29)', 'Phonemes (%d)' % len(self.win.f.phonemes), 'Rules',
            'Characters (256)', 'Lexicon (3370)', 'Unknown'])
        self.assertEqual(self.win.windowTitle(), 'IVX Editor — swrules.ivx')
        self.win.open_file(ivx_path('ge'))
        self.assertIn('Lexicon 2 (raw)', self.top_level())
        self.assertNotIn('Lexicon (3370)', self.top_level())

    def test_jumps(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        plus = next(ph for ph in w.f.phonemes if ph.name == b'\0+')
        w.show_phoneme(plus)
        self.assertIs(w.stack.currentWidget(), w.phoneme_view)
        module, i = w.uses[plus][0]
        w.phoneme_view.uses.itemActivated.emit(w.phoneme_view.uses.item(0))
        view = w.rule_views[module]
        self.assertIs(w.stack.currentWidget(), view)
        self.assertEqual(view.textCursor().blockNumber(), i)
        line = view.document().findBlockByNumber(i).text()
        ref = view.ref_at(i, line.index(' + ') + 1)
        self.assertIs(ref.ph, plus)
        w.show_phoneme(ref.ph)
        self.assertIs(w.tree.currentItem(), w.ph_items[plus])

    def test_rules_view_shows_instruction_form(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        w.show_rule('M0', 0)
        text = w.rule_views['M0'].toPlainText().splitlines()
        self.assertEqual(len(text), len(w.f.modules['M0'].rules))
        self.assertIn('RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32', '\n'.join(text))

    def test_bad_file_raises(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'bad.ivx'
            p.write_bytes(bytes(100))
            with self.assertRaises(IvxFormatError):
                self.win.open_file(p)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 4: Run them to see them fail.**

Run: `python -m unittest discover -s editor/tests -t editor -p test_gui.py`
Expected: `ModuleNotFoundError: No module named 'ivxedit.gui'` in each test.

- [ ] **Step 5: Create `editor/requirements.txt`:**

```
PySide6>=6.8
numpy>=2.0
```

- [ ] **Step 6: Create the launcher, `editor/ivxedit.pyw`.** Python finds the package directory `ivxedit/` before this file of the same stem, so `import ivxedit` works from here.

```python
"""IVX Editor launcher: python editor/ivxedit.pyw [file.ivx]"""
import sys

from ivxedit.gui.app import main

sys.exit(main())
```

- [ ] **Step 7: Create an empty `editor/ivxedit/gui/__init__.py` and `editor/ivxedit/gui/app.py`:**

```python
"""Start the editor: python editor/ivxedit.pyw [file.ivx]"""
import sys

from PySide6.QtCore import QSettings
from PySide6.QtWidgets import QApplication

from .mainwindow import MainWindow


def main(argv=None):
    argv = sys.argv if argv is None else argv
    app = QApplication.instance() or QApplication(argv)
    win = MainWindow(QSettings('infovox230', 'ivxedit'))
    win.show()
    if len(argv) > 1:
        win.open_path(argv[1])
    return app.exec()
```

- [ ] **Step 8: Create `editor/ivxedit/gui/views.py`:**

```python
"""Read-only views: header, tables (parameters, characters, the phoneme and module
summaries), lexicon and text/hex."""
from PySide6.QtCore import QAbstractTableModel, QModelIndex, QSortFilterProxyModel, Qt
from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import (QAbstractItemView, QFormLayout, QHeaderView, QLabel, QLineEdit,
                               QPlainTextEdit, QTableView, QVBoxLayout, QWidget)


def mono():
    return QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)


def compact(table):
    """read-only, monospaced, one text line per row"""
    table.setEditTriggers(QAbstractItemView.EditTrigger.NoEditTriggers)
    table.setFont(mono())
    table.verticalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
    table.verticalHeader().setDefaultSectionSize(table.fontMetrics().height() + 6)


class RowsModel(QAbstractTableModel):
    """a read-only table of rows (lists of str); row_data[i] is kept for jumps"""

    def __init__(self, headers, rows, row_data=None):
        super().__init__()
        self.headers, self.rows = headers, rows
        self.row_data = row_data if row_data is not None else [None] * len(rows)

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def columnCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.headers)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and index.isValid():
            return self.rows[index.row()][index.column()]
        return None

    def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and orientation == Qt.Orientation.Horizontal:
            return self.headers[section]
        return None


class TableView(QWidget):
    """a title and a read-only table; activated(row_data) on double-click or Enter"""

    def __init__(self, on_activate=None):
        super().__init__()
        self.title = QLabel()
        self.table = QTableView()
        compact(self.table)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.on_activate = on_activate
        self.table.activated.connect(self._activated)
        lay = QVBoxLayout(self)
        lay.addWidget(self.title)
        lay.addWidget(self.table)
        self.model = None

    def set_rows(self, title, headers, rows, row_data=None):
        self.title.setText(title)
        self.model = RowsModel(headers, [[str(c) for c in r] for r in rows], row_data)
        self.table.setModel(self.model)
        self.table.resizeColumnsToContents()

    def _activated(self, index):
        data = self.model.row_data[index.row()]
        if self.on_activate is not None and data is not None:
            self.on_activate(data)


class HeaderView(QWidget):
    def __init__(self):
        super().__init__()
        self.form = QFormLayout(self)

    def set_fields(self, fields):
        while self.form.rowCount():
            self.form.removeRow(0)
        for label, text in fields:
            value = QLabel(text)
            value.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            self.form.addRow(label, value)


class TextView(QPlainTextEdit):
    """read-only monospaced text (hex dumps, the Unknown view)"""

    def __init__(self):
        super().__init__()
        self.setReadOnly(True)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)


class LexiconView(QWidget):
    """the lexicon (H[3]) with a filter on the headword"""

    def __init__(self):
        super().__init__()
        self.filter = QLineEdit()
        self.filter.setPlaceholderText('Filter headwords')
        self.table = QTableView()
        compact(self.table)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.proxy = QSortFilterProxyModel()
        self.proxy.setFilterKeyColumn(1)
        self.proxy.setFilterCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        self.table.setModel(self.proxy)
        self.filter.textChanged.connect(self.proxy.setFilterFixedString)
        lay = QVBoxLayout(self)
        lay.addWidget(self.filter)
        lay.addWidget(self.table)

    def set_lexicon(self, lex):
        rows = [[str(i), o.decode('latin-1'), t.decode('latin-1')] for i, (o, t) in enumerate(lex.entries)]
        self.model = RowsModel(['#', 'Headword', 'Transcription'], rows)
        self.proxy.setSourceModel(self.model)
        self.filter.clear()
        self.table.setColumnWidth(0, 60)
        self.table.setColumnWidth(1, 220)
```

- [ ] **Step 9: Create `editor/ivxedit/gui/phonemeview.py`:**

```python
"""The phoneme view: name, feature bits, items and the rules that use the phoneme."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QHBoxLayout, QLabel, QListWidget, QListWidgetItem, QTableWidget,
                               QTableWidgetItem, QVBoxLayout, QWidget)

from ..checks import rule_label
from ..info import item_name, ph_name
from ..model import VarItem
from .views import compact, mono


class PhonemeView(QWidget):
    def __init__(self, on_rule=None):
        super().__init__()
        self.on_rule = on_rule            # called with (module name, rule index)
        self.name = QLabel()
        self.bits = QTableWidget(2, 32)
        compact(self.bits)
        self.bits.setVerticalHeaderLabels(['feat', 'mask'])
        self.bits.setHorizontalHeaderLabels([str(b) for b in range(31, -1, -1)])
        self.bits.horizontalHeader().setDefaultSectionSize(24)
        self.bits.setFixedHeight(90)
        self.items = QTableWidget()
        compact(self.items)
        self.uses_label = QLabel()
        self.uses = QListWidget()
        self.uses.setFont(mono())
        self.uses.itemActivated.connect(self._jump)
        lower = QHBoxLayout()
        left, right = QVBoxLayout(), QVBoxLayout()
        left.addWidget(QLabel('Items (value@time; time in frames from the segment start)'))
        left.addWidget(self.items)
        right.addWidget(self.uses_label)
        right.addWidget(self.uses)
        lower.addLayout(left, 3)
        lower.addLayout(right, 1)
        lay = QVBoxLayout(self)
        lay.addWidget(self.name)
        lay.addWidget(self.bits)
        lay.addLayout(lower)

    def set_phoneme(self, ph, uses, modules):
        """uses: [(module name, rule index)]; modules: the file's modules, for labels"""
        what = 'defaults record' if ph.is_defaults else 'phoneme %s' % ph_name(ph.name)
        self.name.setText('%s    feat 0x%08x  mask 0x%08x' % (what, ph.feat, ph.mask))
        for row, v in enumerate((ph.feat, ph.mask)):
            for col in range(32):
                self.bits.setItem(row, col, QTableWidgetItem('●' if v >> (31 - col) & 1 else ''))
        width = max([1] + [len(it.points) for it in ph.items if not isinstance(it, VarItem)])
        self.items.clear()
        self.items.setRowCount(len(ph.items))
        self.items.setColumnCount(1 + width)
        self.items.setHorizontalHeaderLabels(['item'] + ['%d' % k for k in range(width)])
        for row, it in enumerate(ph.items):
            if isinstance(it, VarItem):
                cells = [item_name(it.vid), str(it.value)]
            else:
                cells = [item_name(it.pid)] + ['%d@%d' % (v, t) for t, v in it.points]
            for col, text in enumerate(cells):
                self.items.setItem(row, col, QTableWidgetItem(text))
        self.uses.clear()
        self.uses_label.setText('Used by %d rule%s' % (len(uses), '' if len(uses) == 1 else 's'))
        for mod, i in uses:
            item = QListWidgetItem(rule_label(mod, i, modules[mod].rules[i]))
            item.setData(Qt.ItemDataRole.UserRole, (mod, i))
            self.uses.addItem(item)

    def _jump(self, item):
        if self.on_rule is not None:
            self.on_rule(*item.data(Qt.ItemDataRole.UserRole))
```

- [ ] **Step 10: Create `editor/ivxedit/gui/rulesview.py`:**

```python
"""The rules view: one module, one rule per line in instruction form, highlighted;
double-click a phoneme to jump to it."""
from PySide6.QtCore import Qt
from PySide6.QtGui import QColor, QSyntaxHighlighter, QTextCharFormat, QTextCursor, QTextFormat
from PySide6.QtWidgets import QPlainTextEdit, QTextEdit

from ..listing import rule_line
from .views import mono

PREFIX = '%5d  '          # the rule index, as in the Problems panel's "M0 rule 12"
COLOURS = {'op': '#1f5fbf', 'ph': '#a0400a', 'num': '#2a7f2a', 'feat': '#7a2a9a',
           'param': '#8a6d00', 'raw': '#c00000'}


class _Highlighter(QSyntaxHighlighter):
    def __init__(self, doc, view):
        super().__init__(doc)
        self.view = view
        self.formats = {}
        for kind, colour in COLOURS.items():
            fmt = QTextCharFormat()
            fmt.setForeground(QColor(colour))
            if kind == 'ph':
                fmt.setFontUnderline(True)
            self.formats[kind] = fmt

    def highlightBlock(self, text):
        n = self.currentBlock().blockNumber()
        if n < len(self.view.lines):
            for s, e, kind, _ in self.view.lines[n].spans:
                self.setFormat(len(PREFIX % n) + s, e - s, self.formats[kind])


class RulesView(QPlainTextEdit):
    def __init__(self, on_phoneme=None):
        super().__init__()
        self.on_phoneme = on_phoneme      # called with a Phoneme
        self.setReadOnly(True)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.lines = []
        self.module = None
        self._hl = _Highlighter(self.document(), self)

    def set_module(self, name, module):
        if module is self.module:
            return
        self.module = module
        self.lines = [rule_line(r) for r in module.rules]
        self.setPlainText('\n'.join(PREFIX % i + ln.text for i, ln in enumerate(self.lines)))

    def ref_at(self, rule, col):
        """the PhRef at column `col` of rule line `rule`, or None"""
        col -= len(PREFIX % rule)
        for s, e, kind, obj in self.lines[rule].spans:
            if kind == 'ph' and s <= col < e:
                return obj
        return None

    def goto_rule(self, i):
        cur = QTextCursor(self.document().findBlockByNumber(i))
        self.setTextCursor(cur)
        self.centerCursor()
        sel = QTextEdit.ExtraSelection()
        sel.format.setBackground(QColor('#fff3b0'))
        sel.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
        sel.cursor = cur
        self.setExtraSelections([sel])

    def mouseDoubleClickEvent(self, event):
        cur = self.cursorForPosition(event.position().toPoint())
        ref = self.ref_at(cur.blockNumber(), cur.positionInBlock())
        if ref is not None and ref.ph is not None and self.on_phoneme is not None:
            self.on_phoneme(ref.ph)
        else:
            super().mouseDoubleClickEvent(event)
```

- [ ] **Step 11: Create `editor/ivxedit/gui/mainwindow.py`** (without the Speak panel; Task 5 adds it):

```python
"""The main window: section tree and the view for the selected item."""
from pathlib import Path

from PySide6.QtCore import Qt
from PySide6.QtGui import QAction, QKeySequence
from PySide6.QtWidgets import (QFileDialog, QLabel, QMainWindow, QMessageBox, QSplitter,
                               QStackedWidget, QTreeWidget, QTreeWidgetItem)

from ..binfmt import IvxFormatError
from ..checks import check
from ..info import a1_text, char_rows, flags_text, header_fields, hexdump, ph_name, unknown_text
from ..model import PNAMES, load
from ..xref import phoneme_uses
from .phonemeview import PhonemeView
from .rulesview import RulesView
from .views import HeaderView, LexiconView, TableView, TextView

ROLE = Qt.ItemDataRole.UserRole


class MainWindow(QMainWindow):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.f = self.path = None
        self.uses = {}
        self.ph_items = {}            # Phoneme -> its tree item
        self.module_items = {}        # module name -> its tree item
        self.rule_views = {}          # module name -> RulesView, per open file
        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.currentItemChanged.connect(lambda cur, prev: self.show_item(cur))
        self.stack = QStackedWidget()
        self.header_view = HeaderView()
        self.table_view = TableView(self._activate)
        self.phoneme_view = PhonemeView(self.show_rule)
        self.lexicon_view = LexiconView()
        self.text_view = TextView()
        for w in (self.header_view, self.table_view, self.phoneme_view, self.lexicon_view, self.text_view):
            self.stack.addWidget(w)
        main = QSplitter(Qt.Orientation.Horizontal)
        main.addWidget(self.tree)
        main.addWidget(self.stack)
        main.setSizes([220, 900])
        self.setCentralWidget(main)
        self.problems = QLabel()
        self.statusBar().addWidget(self.problems)
        self._menus()
        self.resize(1200, 800)
        self._title()

    def _menus(self):
        m = self.menuBar().addMenu('&File')
        for text, key, slot in (('&Open…', QKeySequence.StandardKey.Open, self.open_dialog),
                                ('&ivxsay…', None, self.ivxsay_dialog),
                                ('&Quit', QKeySequence.StandardKey.Quit, self.close)):
            a = QAction(text, self)
            if key is not None:
                a.setShortcut(key)
            a.triggered.connect(slot)
            m.addAction(a)

    def _title(self):
        self.setWindowTitle('IVX Editor' + (' — %s' % self.path.name if self.path else ''))

    # --- files
    def open_dialog(self):
        path, _ = QFileDialog.getOpenFileName(self, 'Open', self.settings.value('dir', ''),
                                              'Infovox language files (*.ivx);;All files (*)')
        if path:
            self.settings.setValue('dir', str(Path(path).parent))
            self.open_path(path)

    def open_path(self, path):
        """open_file with a message box instead of an exception"""
        try:
            self.open_file(path)
        except (OSError, IvxFormatError) as e:
            QMessageBox.critical(self, 'Open', 'Cannot open %s:\n%s' % (path, e))

    def open_file(self, path):
        """load path into the window; raises OSError or IvxFormatError"""
        data = Path(path).read_bytes()
        f = load(data)
        self.f, self.path = f, Path(path)
        self.uses = phoneme_uses(f)
        for v in self.rule_views.values():
            self.stack.removeWidget(v)
            v.deleteLater()
        self.rule_views = {}
        problems = check(f)
        self.problems.setText('Problems (%d)' % len(problems))
        self.problems.setToolTip('\n'.join(str(p) for p in problems))
        self._fill_tree()
        self._title()

    def ivxsay_dialog(self):
        path, _ = QFileDialog.getOpenFileName(self, 'ivxsay to run', '', 'ivxsay (ivxsay*)')
        if path:
            self.settings.setValue('ivxsay', path)

    # --- the tree
    def _node(self, parent, text, kind, obj=None):
        item = QTreeWidgetItem([text])
        item.setData(0, ROLE, (kind, obj))
        (parent.addChild if isinstance(parent, QTreeWidgetItem) else parent.addTopLevelItem)(item)
        return item

    def _fill_tree(self):
        f, t = self.f, self.tree
        t.blockSignals(True)
        t.clear()
        self.ph_items, self.module_items = {}, {}
        self._node(t, 'Header', 'header')
        self._node(t, 'Parameters (%d)' % len(PNAMES), 'params')
        phs = self._node(t, 'Phonemes (%d)' % len(f.phonemes), 'phonemes')
        for ph in f.phonemes:
            label = 'defaults' if ph.is_defaults else ph_name(ph.name)
            self.ph_items[ph] = self._node(phs, label, 'phoneme', ph)
        rules = self._node(t, 'Rules', 'rules')
        for name in sorted(f.modules):
            self.module_items[name] = self._node(
                rules, '%s (%d)' % (name, len(f.modules[name].rules)), 'module', name)
        self._node(t, 'Characters (256)', 'chars')
        if f.lexicon is not None:
            self._node(t, 'Lexicon (%d)' % len(f.lexicon.entries), 'lexicon')
        if f.ixlex is not None:
            self._node(t, 'Lexicon 2 (raw)', 'ixlex')
        self._node(t, 'Unknown', 'unknown')
        t.blockSignals(False)
        t.setCurrentItem(t.topLevelItem(0))

    def items(self):
        """every tree item, depth first"""
        out = []

        def walk(item):
            out.append(item)
            for k in range(item.childCount()):
                walk(item.child(k))
        for k in range(self.tree.topLevelItemCount()):
            walk(self.tree.topLevelItem(k))
        return out

    # --- views
    def show_item(self, item):
        if item is None or self.f is None:
            return
        kind, obj = item.data(0, ROLE)
        f = self.f
        if kind == 'header':
            self.header_view.set_fields(header_fields(f))
            self.stack.setCurrentWidget(self.header_view)
        elif kind == 'params':
            rows = []
            for i, name in enumerate(PNAMES):
                a0, a1, a2, a3 = f.param(i)
                rows.append([i, name, '0x%04x' % a0, a1_text(a1), a2, a3])
            self._table('Parameters: A0 name slot, A1 type → handler, A2 rate, A3 offset (REFERENCE §5.3)',
                        ['#', 'Name', 'A0', 'A1 type', 'A2', 'A3'], rows)
        elif kind == 'phonemes':
            rows = [[i, 'defaults' if ph.is_defaults else ph_name(ph.name), '0x%08x' % ph.feat,
                     '0x%08x' % ph.mask, len(ph.items), len(self.uses.get(ph, []))]
                    for i, ph in enumerate(f.phonemes)]
            self._table('Phoneme inventory', ['#', 'Name', 'feat', 'mask', 'Items', 'Used by'],
                        rows, [('phoneme', ph) for ph in f.phonemes])
        elif kind == 'phoneme':
            self.phoneme_view.set_phoneme(obj, self.uses.get(obj, []), f.modules)
            self.stack.setCurrentWidget(self.phoneme_view)
        elif kind == 'rules':
            rows = [[n, len(m.rules), sum(r.iterative for r in m.rules), '0x%04x' % m.lead]
                    for n, m in sorted(f.modules.items())]
            self._table('Rule modules (REFERENCE §5.5)', ['Module', 'Rules', 'RULE*', 'Lead word'],
                        rows, [('module', n) for n in sorted(f.modules)])
        elif kind == 'module':
            self.stack.setCurrentWidget(self._rules_view(obj))
        elif kind == 'chars':
            rows = [['0x%02x' % ch, c, '' if code is None else code,
                     '(built-in)' if fl is None else flags_text(fl), '' if xl is None else xl]
                    for ch, c, code, fl, xl in char_rows(f)]
            self._table('Characters: class code and flags (REFERENCE §5.2), transliteration (H[14])',
                        ['Byte', 'Char', 'Class', 'Flags', 'Translit.'], rows)
        elif kind == 'lexicon':
            self.lexicon_view.set_lexicon(f.lexicon)
            self.stack.setCurrentWidget(self.lexicon_view)
        elif kind == 'ixlex':
            self._text('Indexed lexicon (H[13] + 0x80 to the end of the data), not decoded\n\n'
                       + hexdump(f.ixlex))
        elif kind == 'unknown':
            self._text(unknown_text(f))

    def _table(self, title, headers, rows, row_data=None):
        self.table_view.set_rows(title, headers, rows, row_data)
        self.stack.setCurrentWidget(self.table_view)

    def _text(self, text):
        self.text_view.setPlainText(text)
        self.stack.setCurrentWidget(self.text_view)

    def _rules_view(self, name):
        if name not in self.rule_views:
            v = RulesView(self.show_phoneme)
            v.set_module(name, self.f.modules[name])
            self.stack.addWidget(v)
            self.rule_views[name] = v
        return self.rule_views[name]

    def _activate(self, data):
        kind, obj = data
        if kind == 'phoneme':
            self.show_phoneme(obj)
        elif kind == 'module':
            self.tree.setCurrentItem(self.module_items[obj])

    # --- jumps
    def show_phoneme(self, ph):
        self.tree.setCurrentItem(self.ph_items[ph])

    def show_rule(self, module, i):
        self.tree.setCurrentItem(self.module_items[module])
        self._rules_view(module).goto_rule(i)
```

- [ ] **Step 12: Run the tests.**

Run: `python -m unittest discover -s editor/tests -t editor`
Expected: `OK`, 76 tests.

- [ ] **Step 13: Look at it.** Run `python editor/ivxedit.pyw bin/swrules.ivx`. Then:
  - click through Header, Parameters, a phoneme, M0, Characters and Lexicon;
  - double-click a rule in a phoneme's "Used by" list: it jumps to the rule;
  - double-click a phoneme name in a rule line: it jumps to the phoneme.

  Close the window. Report anything that looks wrong; don't restyle it.

- [ ] **Step 14: Commit.**

```bash
git add editor/requirements.txt editor/ivxedit.pyw editor/ivxedit/gui editor/tests/qt.py editor/tests/test_gui.py
git commit -m "ivxedit: read-only window: section tree, header, parameters, phonemes, rules, characters, lexicons

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: The Speak panel, A/B, and the docs

**Files:**
- Create: `editor/ivxedit/gui/waveview.py`, `editor/ivxedit/gui/speakpanel.py`
- Modify: `editor/ivxedit/gui/mainwindow.py` (five edits below)
- Modify: `AGENTS.md` (repository layout row `editor/`, section "Editor (`editor/`)"), `REFERENCE.md` (§5.2 layout block), `docs/superpowers/specs/2026-10-03-ivx-editor-design.md` (GUI section)
- Test: `editor/tests/test_speakpanel.py`

**Interfaces:**
- Consumes:
  - from Task 3: `speak.Options`, `SpeakError`, `find_ivxsay`, `speak_bytes`, `speak_model`, `parse_events`, `dsp.*`;
  - from Task 4: `MainWindow`, `tests.qt.*`.
- Produces:
  - `SpeakPanel(settings)`, with attributes `text`, `button`, `ab`, `voice`, `rate`, `pitch`, `fd`, `fps`, `fs`, `status`, `view`, `job`, `results`, `autoplay`;
  - `SpeakPanel` methods `set_model(f, original_bytes)`, `options()`, `speak()`, `play(speech)`, `cleanup()`;
  - `SignalView` with `rows` (each with `.speech`), `set_results([(title, Speech)])`, `reset_view()`, `zoom()`, `pan()`;
  - `MainWindow.speak`.

- [ ] **Step 1: Write the failing tests.** Create `editor/tests/test_speakpanel.py`:

```python
"""The Speak panel, offscreen (spec: Testing, 10)."""
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import ivx_path, ivxsay

SAY = ivxsay()


def wait_for(panel):
    panel.job.wait(120000)
    for _ in range(20):
        app().processEvents()
        if panel.button.isEnabled():
            return


@needs_qt
class SpeakPanelTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.settings = temp_settings(self)
        self.win = MainWindow(self.settings)
        self.addCleanup(self.win.close)
        self.panel = self.win.speak
        self.panel.autoplay = False

    @unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
    def test_speak_matches_ivxsay(self):
        self.settings.setValue('ivxsay', str(SAY))
        self.win.open_file(ivx_path('sw'))
        p = self.panel
        p.text.setText('God morgon.')
        p.ab.setChecked(True)
        with SlotErrors() as errors:
            p.button.click()
            wait_for(p)
        self.assertEqual(errors, [])
        self.assertEqual([t for t, _ in p.results], ['A: as opened', 'B: edited'])
        with tempfile.TemporaryDirectory() as d:
            txt, wav = Path(d) / 't.txt', Path(d) / 'o.wav'
            txt.write_bytes('God morgon.'.encode('utf-8'))
            r = subprocess.run([str(SAY), '-l', str(ivx_path('sw')), '-i', str(txt), '-o', str(wav),
                                '-E', '-s', '22050'], capture_output=True, check=True)
            direct = wav.read_bytes()
        from ivxedit.speak import parse_events
        events = parse_events(r.stderr)[0]
        for title, sp in p.results:
            self.assertEqual(sp.wav, direct, title)
        self.assertEqual([row.speech.events for row in p.view.rows], [events, events])
        self.assertEqual(len(p.view.rows), 2)

    def test_missing_ivxsay(self):
        self.settings.setValue('ivxsay', str(Path(tempfile.gettempdir()) / 'no-such-ivxsay.exe'))
        self.win.open_file(ivx_path('sw'))
        self.panel.text.setText('God morgon.')
        with mock.patch('ivxedit.speak.BUILDS', ()):
            self.panel.button.click()
        self.assertIsNone(self.panel.job)
        self.assertIn('run make', self.panel.status.text())

    @unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
    def test_refused_model_reports(self):
        self.settings.setValue('ivxsay', str(SAY))
        self.win.open_file(ivx_path('sw'))
        del self.win.f.modules['M0']           # M0 is required (checks)
        self.panel.text.setText('God morgon.')
        self.panel.button.click()
        wait_for(self.panel)
        self.assertIn('M0', self.panel.status.text())

    def test_options(self):
        p = self.panel
        self.assertEqual(p.options().args(), ['-s', '22050'])
        p.voice.setValue(3)
        p.fps.setValue(150)
        p.fs.setCurrentIndex(p.fs.findData(16000))
        self.assertEqual(p.options().args(), ['-s', '16000', '-v', '3', '-F', '150'])


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run them to see them fail.**

Run: `python -m unittest discover -s editor/tests -t editor -p test_speakpanel.py`
Expected: `AttributeError: 'MainWindow' object has no attribute 'speak'` in each test.

- [ ] **Step 3: Create `editor/ivxedit/gui/waveview.py`:**

```python
"""Waveform, spectrogram and phoneme strip of one or more Speech results on one time axis.
Wheel zooms around the pointer, dragging pans, double-click shows everything."""
from PySide6.QtCore import QRectF, Qt
from PySide6.QtGui import QColor, QImage, QPainter, QPen
from PySide6.QtWidgets import QSizePolicy, QWidget

from .. import dsp

WAVE_H, SPEC_H, STRIP_H, TITLE_H, GAP = 70, 140, 22, 16, 8


class Row:
    """one Speech, prepared for drawing"""

    def __init__(self, title, speech):
        self.title, self.speech = title, speech
        db, self.hop = dsp.spectrogram(speech.samples, speech.fs)
        lv = dsp.levels(db, fs=speech.fs)
        lv = 255 - lv                       # dark = loud
        self.image = QImage(lv.data, lv.shape[1], lv.shape[0], lv.strides[0],
                            QImage.Format.Format_Grayscale8).copy()
        self.duration = len(speech.samples) / speech.fs


class SignalView(QWidget):
    def __init__(self):
        super().__init__()
        self.rows = []
        self.t0, self.t1 = 0.0, 1.0
        self._drag = None
        self.setSizePolicy(QSizePolicy.Policy.Expanding, QSizePolicy.Policy.Expanding)
        self.setMinimumHeight(TITLE_H + WAVE_H + SPEC_H + STRIP_H)

    def set_results(self, results):
        """results: [(title, Speech)], drawn top to bottom"""
        self.rows = [Row(t, s) for t, s in results]
        self.reset_view()
        self.setMinimumHeight(max(1, len(self.rows)) * (TITLE_H + WAVE_H + SPEC_H + STRIP_H + GAP))
        self.update()

    def reset_view(self):
        self.t0, self.t1 = 0.0, max([0.1] + [r.duration for r in self.rows])
        self.update()

    def total(self):
        return max([0.1] + [r.duration for r in self.rows])

    # --- time <-> x
    def x_of(self, t):
        return (t - self.t0) / (self.t1 - self.t0) * self.width()

    def t_of(self, x):
        return self.t0 + x / max(1, self.width()) * (self.t1 - self.t0)

    def zoom(self, factor, at_x):
        t = self.t_of(at_x)
        span = min(self.total(), max(0.01, (self.t1 - self.t0) * factor))
        self.t0 = t - (at_x / max(1, self.width())) * span
        self.t1 = self.t0 + span
        self._clamp()

    def pan(self, dt):
        self.t0 += dt
        self.t1 += dt
        self._clamp()

    def _clamp(self):
        span = self.t1 - self.t0
        self.t0 = min(max(0.0, self.t0), max(0.0, self.total() - span))
        self.t1 = self.t0 + span
        self.update()

    # --- events
    def wheelEvent(self, e):
        self.zoom(0.8 if e.angleDelta().y() > 0 else 1.25, e.position().x())

    def mousePressEvent(self, e):
        self._drag = e.position().x()

    def mouseMoveEvent(self, e):
        if self._drag is not None:
            x = e.position().x()
            self.pan(self.t_of(self._drag) - self.t_of(x))
            self._drag = x

    def mouseReleaseEvent(self, e):
        self._drag = None

    def mouseDoubleClickEvent(self, e):
        self.reset_view()

    # --- drawing
    def paintEvent(self, e):
        p = QPainter(self)
        pal = self.palette()
        p.fillRect(self.rect(), pal.base())
        if not self.rows:
            p.setPen(pal.text().color())
            p.drawText(self.rect(), Qt.AlignmentFlag.AlignCenter, 'Type a text and press Speak')
            return
        y = 0
        for row in self.rows:
            p.setPen(pal.text().color())
            p.drawText(4, y + TITLE_H - 4, row.title)
            y += TITLE_H
            self._wave(p, row, y)
            y += WAVE_H
            self._spec(p, row, y)
            y += SPEC_H
            self._strip(p, row, y)
            y += STRIP_H + GAP
        p.end()

    def _wave(self, p, row, y):
        w = self.width()
        s = row.speech
        i0, i1 = (max(0, min(len(s.samples), int(t * s.fs))) for t in (self.t0, self.t1))
        if i1 <= i0 or w <= 0:
            return
        lo, hi = dsp.minmax(s.samples[i0:i1], w)
        mid, scale = y + WAVE_H / 2, WAVE_H / 2 / 32768
        p.setPen(QPen(QColor('#3060a0')))
        for x in range(w):
            p.drawLine(x, int(mid - hi[x] * scale), x, int(mid - lo[x] * scale))

    def _spec(self, p, row, y):
        f0, f1 = self.t0 / row.hop, self.t1 / row.hop
        p.drawImage(QRectF(0, y, self.width(), SPEC_H), row.image,
                    QRectF(f0, 0, f1 - f0, row.image.height()))

    def _strip(self, p, row, y):
        pal = self.palette()
        p.setPen(pal.text().color())
        p.drawLine(0, y, self.width(), y)
        for ev in row.speech.phonemes:
            x = self.x_of(ev.time)
            if -50 <= x <= self.width():
                p.drawLine(int(x), y, int(x), y + STRIP_H)
                p.drawText(int(x) + 2, y + STRIP_H - 6, ev.value)
```

- [ ] **Step 4: Create `editor/ivxedit/gui/speakpanel.py`:**

```python
"""The Speak panel: text, options, Speak, A/B, and the signal view."""
import tempfile
from pathlib import Path

from PySide6.QtCore import QThread, QUrl, Signal
from PySide6.QtWidgets import (QCheckBox, QComboBox, QHBoxLayout, QLabel, QLineEdit,
                               QPushButton, QScrollArea, QSpinBox, QVBoxLayout, QWidget)

from ..speak import Options, SpeakError, find_ivxsay, speak_bytes, speak_model
from .waveview import SignalView

RATES = (8000, 11025, 16000, 22050, 32000, 44100, 48000)


class SpeakJob(QThread):
    """runs the speak calls off the GUI thread; done([(title, Speech)]) or failed(message)"""
    done = Signal(list)
    failed = Signal(str)

    def __init__(self, calls):
        super().__init__()
        self.calls = calls          # [(title, function returning Speech)]

    def run(self):
        try:
            self.done.emit([(title, call()) for title, call in self.calls])
        except SpeakError as e:
            self.failed.emit(str(e))
        except Exception as e:     # any other failure: report it, keep the editor running
            self.failed.emit('%s: %s' % (type(e).__name__, e))


def _spin(lo, hi, special):
    s = QSpinBox()
    s.setRange(lo - 1, hi)
    s.setSpecialValueText(special)
    s.setValue(lo - 1)
    return s


class SpeakPanel(QWidget):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.model = self.original = None
        self.job = None
        self.autoplay = True
        self._player, self._plays, self.results = None, 0, []
        self._tmp = None            # a TemporaryDirectory for the played WAVs, made on first play
        self.text = QLineEdit()
        self.text.setPlaceholderText('Text to speak')
        self.text.returnPressed.connect(self.speak)
        self.button = QPushButton('▶ Speak')
        self.button.clicked.connect(self.speak)
        self.ab = QCheckBox('A/B')
        self.ab.setToolTip('Also speak the file as it was opened, shown above the edited result')
        self.voice, self.rate, self.pitch = _spin(0, 9, 'voice'), _spin(0, 100, 'rate'), _spin(0, 100, 'pitch')
        self.fd, self.fps = _spin(0, 100, 'FD'), _spin(2, 1000, 'frames/s')
        self.fs = QComboBox()
        for r in RATES:
            self.fs.addItem('%d Hz' % r, r)
        self.fs.setCurrentIndex(RATES.index(22050))
        self.status = QLabel()
        self.view = SignalView()
        top = QHBoxLayout()
        top.addWidget(self.text, 1)
        top.addWidget(self.button)
        top.addWidget(self.ab)
        opts = QHBoxLayout()
        for w in (self.voice, self.rate, self.pitch, self.fd, self.fps, self.fs):
            opts.addWidget(w)
        opts.addWidget(self.status, 1)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setWidget(self.view)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addLayout(opts)
        lay.addWidget(scroll, 1)

    def set_model(self, f, original):
        """f: the model to speak; original: the bytes of the file as opened (for A/B)"""
        self.model, self.original = f, original

    def options(self):
        def val(spin):
            return None if spin.value() < spin.minimum() + 1 else spin.value()
        return Options(val(self.voice), val(self.rate), val(self.pitch), val(self.fd),
                       val(self.fps), self.fs.currentData())

    def ivxsay(self):
        return find_ivxsay(self.settings.value('ivxsay') or None)

    def speak(self):
        if self.model is None or (self.job is not None and self.job.isRunning()):
            return
        exe = self.ivxsay()
        if exe is None:
            self.status.setText('ivxsay not found: run make in the repository root, or set it in File ▸ ivxsay…')
            return
        text, opts, f, orig = self.text.text(), self.options(), self.model, self.original
        calls = []
        if self.ab.isChecked():
            calls.append(('A: as opened', lambda: speak_bytes(orig, text, opts, exe)))
        calls.append(('B: edited' if self.ab.isChecked() else 'Edited',
                      lambda: speak_model(f, text, opts, exe)))
        self.status.setText('Speaking…')
        self.button.setEnabled(False)
        self.job = SpeakJob(calls)
        self.job.done.connect(self._done)
        self.job.failed.connect(self._failed)
        self.job.start()

    def _done(self, results):
        self.button.setEnabled(True)
        self.results = results
        self.view.set_results(results)
        sp = results[-1][1]
        self.status.setText('%.2f s, %d phonemes' % (len(sp.samples) / sp.fs, len(sp.phonemes)))
        if self.autoplay:
            self.play(sp)

    def _failed(self, message):
        self.button.setEnabled(True)
        self.status.setText(message)

    def play(self, speech):
        """play a Speech's WAV through Qt Multimedia (silently nothing if it is unavailable)"""
        try:
            from PySide6.QtMultimedia import QAudioOutput, QMediaPlayer
        except ImportError:
            return
        if self._player is None:
            self._player = QMediaPlayer(self)
            self._player.setAudioOutput(QAudioOutput(self._player))
        self._player.stop()
        if self._tmp is None:
            self._tmp = tempfile.TemporaryDirectory(prefix='ivxedit-play-')
        self._plays += 1                    # a new file each time: the old one may be open
        path = Path(self._tmp.name) / ('play%d.wav' % self._plays)
        path.write_bytes(speech.wav)
        self._player.setSource(QUrl.fromLocalFile(str(path)))
        self._player.play()

    def cleanup(self):
        """stop playing and remove the played WAVs"""
        if self._player is not None:
            self._player.stop()
            self._player.setSource(QUrl())
        if self._tmp is not None:
            self._tmp.cleanup()
            self._tmp = None
```

- [ ] **Step 5: Add the panel to `editor/ivxedit/gui/mainwindow.py`** with these five edits.

1. In the module docstring, replace

```python
"""The main window: section tree and the view for the selected item."""
```

   with

```python
"""The main window: section tree, the view for the selected item, the Speak panel."""
```

2. In the imports: add the panel, replace

```python
from .rulesview import RulesView
from .views import
```

   with

```python
from .rulesview import RulesView
from .speakpanel import SpeakPanel
from .views import
```

3. In the layout in `__init__`: the views and the panel share the right side, replace

```python
        main = QSplitter(Qt.Orientation.Horizontal)
        main.addWidget(self.tree)
        main.addWidget(self.stack)
```

   with

```python
        self.speak = SpeakPanel(settings)
        right = QSplitter(Qt.Orientation.Vertical)
        right.addWidget(self.stack)
        right.addWidget(self.speak)
        right.setSizes([500, 300])
        main = QSplitter(Qt.Orientation.Horizontal)
        main.addWidget(self.tree)
        main.addWidget(right)
```

4. In `open_file`: hand the panel the model and the bytes as opened (for A/B), replace

```python
        self.rule_views = {}
        problems = check(f)
```

   with

```python
        self.rule_views = {}
        self.speak.set_model(f, data)
        problems = check(f)
```

5. In the end of the class: stop playback and remove its files on close, replace

```python
        self._rules_view(module).goto_rule(i)
```

   with

```python
        self._rules_view(module).goto_rule(i)

    def closeEvent(self, event):
        self.speak.cleanup()
        super().closeEvent(event)
```


- [ ] **Step 6: Run the tests.**

Run: `python -m unittest discover -s editor/tests -t editor`
Expected: `OK`, 80 tests, with no skips when `build/ivxsay.exe` exists.

- [ ] **Step 7: Listen to it.** Run `python editor/ivxedit.pyw bin/swrules.ivx`.
  - Type `God morgon, hur mår du?` and press Speak. You should hear it, and see the waveform, the spectrogram and the phoneme strip (`G O: D M ] R ...`).
  - Tick A/B and speak again: two identical rows.
  - Wheel zooms, dragging pans, double-click resets.

- [ ] **Step 8: `AGENTS.md`.** In the repository-layout row for `editor/`, replace

`Step 1 is done: ` … `Tests: \`make editor-test\`. |`

(the whole cell text after the spec link) with:

```markdown
Steps 1 and 2 are done. `ivxedit.model.load` reads a file into a model (phonemes, rule modules with phoneme references, tables, character tables, lexicons); `ivxedit.writer.to_bytes`/`save` write it back (byte-identical for all 12 files, phoneme ids relocated, `bin/` refused); `ivxedit.checks.check` lists what would stop the engine loading it. `load` raises only `IvxFormatError` for a file it cannot read. The read-only GUI (`python editor/ivxedit.pyw [file.ivx]`, PySide6) shows every section, with the rules in instruction form (`ivxedit.listing`). Its Speak panel runs `ivxsay` on the model (`ivxedit.speak`) and draws waveform, spectrogram and phoneme strip, with A/B against the file as opened. Tests: `make editor-test`. |
```

In the section `## Editor (\`editor/\`)`, replace the paragraph starting `This runs the editor's tests.` with:

```markdown
This runs the editor's tests. `make editor-test` builds `ivxsay` first and runs them too, so the relocation and Speak tests are not skipped; `make test` includes it. The model, writer and checks use the Python 3.13 standard library only. The GUI needs PySide6 and numpy (`pip install -r editor/requirements.txt`); its tests run offscreen and skip when PySide6 is missing.

```bash
python editor/ivxedit.pyw bin/swrules.ivx
```

This starts the editor (read-only in step 2). Speak looks for `build/ivxsay.exe` (or `build/linux/ivxsay`), or the file set with File ▸ ivxsay…. For offscreen screenshots, set `QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:/Windows/Fonts`.
```

- [ ] **Step 9: `REFERENCE.md` §5.2.** In the layout block, replace the line

```
   0x40  build tag "SWRU981118.P2"
```

with

```
   0x40  build tag, 10 chars "<LL>RU<yymmdd>" ("SWRU981118"); then binary up to 0x6A [CONFIRMED, all 12 files; meaning NOT FOUND]
```

- [ ] **Step 10: The spec.** In `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`, add this paragraph after the `**Speak (\`speak.py\`):**` list (before `## Testing`):

```markdown
**As built in step 2** (plan `docs/superpowers/plans/2026-10-04-ivxedit-step2-readonly-gui.md`): the rules are listed by `listing.py` (format in its docstring; the parser comes with step 3), the readable names are in `info.py`, the "Used by" index in `xref.py`, the spectrogram in `dsp.py` (0–8 kHz, 6 ms window). Only a File menu so far; the Problems panel is a `Problems (n)` status-bar label until step 4; phoneme cells are `value@time` as in REFERENCE §5.4; the Characters view shows "(built-in)" for 0x80–0xFF in files without H[14]. `ivxsay -E` prints times in milliseconds, so the phoneme strip is exact to 1 ms.
```

- [ ] **Step 11: Run the full suite through make.**

Run: `make editor-test`
Expected: `OK`, 80 tests.

- [ ] **Step 12: Commit.**

```bash
git add editor/ivxedit/gui/waveview.py editor/ivxedit/gui/speakpanel.py editor/ivxedit/gui/mainwindow.py editor/tests/test_speakpanel.py AGENTS.md REFERENCE.md docs/superpowers/specs/2026-10-03-ivx-editor-design.md
git commit -m "ivxedit: Speak panel with waveform, spectrogram, phoneme strip and A/B; docs for step 2

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

## Done when

- `make test` is green: the C tests show 0 failures, the editor reports 80 tests OK with no skips, and `ivxsay` output is identical in 12 of 12 languages.
- `python editor/ivxedit.pyw bin/<any>rules.ivx` opens every file, and every tree node shows its view.
- Speak plays, draws the three rows, and its WAV and events equal `ivxsay`'s on the same file.
