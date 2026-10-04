# ivxedit step 1: model and writer — Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A Python package, `editor/ivxedit`, that reads any of the 12 `bin/*.ivx` files into an editable model and writes it back. It saves all 12 byte-identical when nothing is edited, relocates every phoneme id when the inventory changes, and refuses to save a model the engine could not load.

**Architecture:**
- `binfmt` has the big-endian helpers.
- `rules` decodes and encodes the rule bytecode. Phoneme-id operands can be replaced by reference objects.
- `model` holds the data classes and the loader. It turns phoneme ids into `PhRef`s that point at `Phoneme` objects.
- `writer` lays the file out in the shipped section order and encodes every `PhRef` with its phoneme's new offset.
- `checks` validates a model before `save` writes it.
- No GUI in this step.

**Tech Stack:** Python 3.13 (64-bit, standard library only), `unittest`, and `build/ivxsay.exe` (the repo's own CLI) for the audio-identity test.

**Spec:** `docs/superpowers/specs/2026-10-03-ivx-editor-design.md` (step 1 of its "Delivery steps"). Read it first.

## Global Constraints

- Never write into `bin/`; it holds the shipped files (AGENTS.md ground rule). Tests read from `bin/` only.
- Pure Python 3.13 standard library in `editor/ivxedit`; no third-party packages in this step.
- Tests use `unittest` (pytest is not installed) and run with `python -m unittest discover -s editor/tests -t editor` from the repo root.
- Tests that need `ivxsay` skip with a message when it is not built; `make editor-test` builds it first.
- `tools/ivx.py` and `tools/ivxdasm.py` stay unchanged; tests import them as an independent decoder. `ivx.py` leaves files open, so the test run prints `ResourceWarning`s from it. They're harmless; don't "fix" them in `tools/`.
- The whole plan was dry-run while planning: every code block, extracted into a scratch copy of the repo, passes all 32 tests.
- Tag every new `REFERENCE.md` claim `[CONFIRMED]`/`[LIKELY]`/`[HYPOTHESIS]`/`[NOT FOUND]` with the address, file or test that proves it.
- Commit after each task. Branch: `develop` (not `main`). End every commit message with `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Facts found while planning (all verified with a prototype on the 12 files)

These refine the spec. Task 7 records them in `REFERENCE.md` and in the spec.

1. **`H[15]` is the end of the data, not the body length**, in am, it, no and sp. Zero bytes follow it: am 80, it 64, no 32, sp 48. The model keeps this as `end_pad`.
2. **Sections start on 16-byte boundaries with zero padding.** In 6 files (da, fr, ge, it, no, sw) the inventory has 16 extra zero bytes after it. The model keeps them as `pads['INV'] = 16`.
3. **The 8 "stray" phoneme ids (fr 1, it 7) all equal the offset of the inventory's 0 terminator.** They aren't offsets into a record. They're modelled as `PhRef(None)`, which the writer encodes as the new terminator offset.
4. **5 `REPL` words only decode with a carry** (`12·n` > 255): Danish `0x314`, `0x320`, `0x32c` ×2 and Icelandic `0x1108`. They keep `Instr.legacy = True`, and only those may keep the carry.
5. **The H[3] lexicons contain duplicate headwords** (sw 6, bl 10; for example `BORT` twice with different transcriptions), so the required order is non-decreasing. The size dword counts itself, and the entries are followed by 1 or more `0x7F` bytes up to a 16-byte boundary.
6. **The second lexicon** at `H[13] + 0x80` (a 128-word index, then the data at `H[13] + 0x180`) is the engine's indexed lexicon. `lex_lookup` uses it when `H[3] == 0` (`src/ttsctrl.c:523-524`, `src/tpstr.c:415`), and its index values are relative to `H[13] + 0x180`. Danish `H[2]` lies 0x1800 bytes into the region.
7. **One-character phoneme names are stored with a leading NUL**, e.g. `b'\x00+'`.
8. **Modules end with the words `0x04 0x00`.** Their lead words vary per file (`9401`, `1023`, `8888`…).
9. **Items in a phoneme record are in ascending signed-id order** (variables, ids ≥ 0x80, come first), in all 1,557 records.
10. **Inserting a dummy phoneme after the defaults record**, which shifts every id, gives byte-identical `ivxsay` audio in all 12 languages; raising every F1 value by 20 changes it.

## Spec deviations

These come from what planning found; Task 7 updates the spec to match.
- The words at body 0x6A/0x6C/0x6E stay raw in `IvxFile.head` instead of being derived. The parameter count isn't editable, so deriving them buys nothing.
- The save-time checks live in their own `checks.py` (the spec put them in `writer.py`), so each file has one job.
- The stray ids are `PhRef(None)` (fact 3), not a `PhRef` with a non-zero delta. `PhRef.delta` stays for ids that would point inside a record; none do in the shipped files.

## File structure

| File | Responsibility |
|---|---|
| `editor/ivxedit/__init__.py` | package marker |
| `editor/ivxedit/binfmt.py` | big-endian readers/packers, constants, `IvxFormatError`, `align16` |
| `editor/ivxedit/rules.py` | `Instr`/`Rule`/`Module`, operand sizes, phoneme-id positions, decode/encode |
| `editor/ivxedit/model.py` | `Phoneme`, items, `PhRef`, `Lexicon`, `IvxFile`, and `load()` |
| `editor/ivxedit/writer.py` | `to_bytes()` (layout and id fixups), `save()` |
| `editor/ivxedit/checks.py` | `Problem`, `check()` |
| `editor/tests/__init__.py` | test package marker |
| `editor/tests/util.py` | repo paths, the 12 languages, `ivxsay` lookup, test texts, `tools/` imports |
| `editor/tests/test_*.py` | one test file per module |
| `Makefile` | `editor-test` target, run by `make test` |

---

### Task 1: Package scaffold, big-endian helpers, test runner

**Files:**
- Create: `editor/ivxedit/__init__.py`
- Create: `editor/ivxedit/binfmt.py`
- Create: `editor/tests/__init__.py`
- Create: `editor/tests/util.py`
- Create: `editor/tests/test_binfmt.py`
- Modify: `Makefile` (the two `ifeq ($(OS),Windows_NT)` platform blocks, the two `test:` rules, `.PHONY`)

**Interfaces:**
- Produces:
  - `binfmt`: `HDR_SIZE = 0x200`, `TABLES = 0x70`, `SLOTS = 32`, `INV = 0x170`, `class IvxFormatError(ValueError)`, `u16(b, o) -> int`, `u32(b, o) -> int`, `words(b, o, n) -> list[int]`, `pack_words(ws) -> bytes`, `align16(n) -> int`.
  - `tests.util`: `ROOT: Path`, `LANGS: list[str]` (`'am'…'sw'`), `ivx_path(lang) -> Path`, `ivx_bytes(lang) -> bytes`, `ivxsay() -> Path | None`, `lang_texts() -> dict[str, bytes]` (keyed `'swrules.ivx'`…), `tools_import(*names) -> list[module]`.

- [ ] **Step 1: Write the failing test**

`editor/tests/__init__.py`: empty file.

`editor/tests/test_binfmt.py`:

```python
import unittest

from ivxedit.binfmt import align16, pack_words, u16, u32, words


class BinfmtTest(unittest.TestCase):
    def test_align16(self):
        self.assertEqual([align16(n) for n in (0, 1, 15, 16, 17)], [0, 16, 16, 16, 32])

    def test_words_are_big_endian(self):
        b = pack_words([0x0102, 0xFFFE])
        self.assertEqual(b, b'\x01\x02\xff\xfe')
        self.assertEqual(words(b, 0, 2), [0x0102, 0xFFFE])
        self.assertEqual(u16(b, 2), 0xFFFE)
        self.assertEqual(u32(b, 0), 0x0102FFFE)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the test to check that it fails**

Run: `python -m unittest discover -s editor/tests -t editor -v`
Expected: ERROR `ModuleNotFoundError: No module named 'ivxedit'`.

- [ ] **Step 3: Write the package and helpers**

`editor/ivxedit/__init__.py`:

```python
"""ivxedit: read, edit and write Infovox 230 .ivx language files."""
```

`editor/ivxedit/binfmt.py`:

```python
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
```

`editor/tests/util.py`:

```python
"""Shared test helpers: repository paths, the 12 language files, ivxsay, the test texts."""
import importlib
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
LANGS = ['am', 'bl', 'da', 'du', 'fi', 'fr', 'ge', 'ic', 'it', 'no', 'sp', 'sw']


def ivx_path(lang):
    return ROOT / 'bin' / ('%srules.ivx' % lang)


def ivx_bytes(lang):
    return ivx_path(lang).read_bytes()


def tools_import(*names):
    """import modules from tools/ (ivx, ivxdasm, check_ivxsay)"""
    if str(ROOT / 'tools') not in sys.path:
        sys.path.insert(0, str(ROOT / 'tools'))
    return [importlib.import_module(n) for n in names]


def ivxsay():
    """ivxsay to run: $IVXSAY, else the Windows or the Linux build; None if not built"""
    env = os.environ.get('IVXSAY')
    if env:
        return Path(env).resolve()
    for p in (ROOT / 'build' / 'ivxsay.exe', ROOT / 'build' / 'linux' / 'ivxsay'):
        if p.exists():
            return p
    return None


def lang_texts():
    """{'amrules.ivx': cp1252 bytes, ...}: the texts of tests/harness.c langs[], the same
    ones tools/check_ivxsay.py speaks"""
    (check,) = tools_import('check_ivxsay')
    src = (ROOT / 'tests' / 'harness.c').read_text(encoding='latin-1')
    table = src[src.index('const Lang langs[] = {'):src.index('const int nlangs')]
    texts = {}
    for name, lits in re.findall(r'\{"(\w+rules\.ivx)",((?:\s*"(?:[^"\\]|\\.)*")+)\}', table):
        texts[name] = check.c_string(re.findall(r'"((?:[^"\\]|\\.)*)"', lits))
    return texts
```

- [ ] **Step 4: Add the `editor-test` make target**

In `Makefile`, add `PY = python` as the last line of the Windows platform block (just before `else`, after `PLAT    = win32`), and `PY = python3` as the last line of the Linux block (just before `endif`, after `PLAT    = linux-i386`).

Change the Windows test rule's first line from `test: all` to `test: all editor-test`, and do the same for the Linux test rule (the one after `else`).

Add this rule just before the `# release folder and archive for this platform` comment:

```make
# the .ivx editor's model and writer (editor/); the relocation test speaks through ivxsay
editor-test: $(B)/ivxsay$(EXE)
	IVXSAY=$(B)/ivxsay$(EXE) $(PY) -m unittest discover -s editor/tests -t editor
```

Change `.PHONY: all test clean dist` to `.PHONY: all test clean dist editor-test`.

- [ ] **Step 5: Run the tests to check that they pass**

Run: `python -m unittest discover -s editor/tests -t editor -v`
Expected: `Ran 2 tests` … `OK`.

Run: `export PATH=/c/winlibs/mingw32/bin:$PATH; make editor-test`
Expected: the same 2 tests, `OK`.

- [ ] **Step 6: Commit**

```bash
git add editor/ivxedit/__init__.py editor/ivxedit/binfmt.py editor/tests/__init__.py editor/tests/util.py editor/tests/test_binfmt.py Makefile
git commit -m "ivxedit: package scaffold, big-endian helpers, make editor-test

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Rule bytecode decode/encode

**Files:**
- Create: `editor/ivxedit/rules.py`
- Test: `editor/tests/test_rules.py`

**Interfaces:**
- Consumes: `binfmt.IvxFormatError`, `u16`, `pack_words`.
- Produces (`ivxedit.rules`):
  - constants `RULE = 0x08`, `RULE_ITER = 0x0C`, `NEXT = 0x10`, `REPL_NEXT = 0xB8`, `REPL = 0xBC`, `PH_OPS`;
  - `rep_split(w) -> (del, n)`, `rep_carries(w) -> bool`;
  - `@dataclass Instr(op: int, args: list, legacy: bool = False)`. `args` holds the operand words, where any phoneme-id word may be a non-int (a `PhRef`).
  - `@dataclass Rule(iterative: bool, instrs: list)` with property `label -> int | None`;
  - `@dataclass Module(lead: int, rules: list, end: list = [0x04, 0x00])`;
  - `operand_count(b, p) -> int`, `ph_positions(ins) -> list[int]`;
  - `decode_instr(b, p) -> (Instr, int)`, `decode_module(b, start) -> (Module, end)`;
  - `encode_instr(ins, ph_id) -> list[int]`, `encode_rule(rule, ph_id) -> list[int]`, `encode_module(m, ph_id) -> bytes`. `ph_id` is called for each non-int arg and returns its 16-bit id.

- [ ] **Step 1: Write the failing test**

`editor/tests/test_rules.py`:

```python
import struct
import unittest

from ivxedit.rules import (Instr, decode_module, encode_module, ph_positions, rep_carries,
                           rep_split)
from tests.util import LANGS, ivx_bytes, ivx_path, tools_import

SECTIONS = {'M0': 9, 'M1': 10, 'M2': 12, 'M3': 5, 'M4': 6, 'M5': 11}


def modules_of(lang):
    """(name, section index, body bytes, start) for each module the file has"""
    b = ivx_bytes(lang)[0x200:]
    H = struct.unpack_from('>16I', b, 0)
    return [(name, sec, b, H[sec]) for name, sec in SECTIONS.items() if H[sec]]


class RulesTest(unittest.TestCase):
    def test_rep_split(self):
        self.assertEqual(rep_split(0x0224), (2, 3))       # del 2, 3 phonemes
        self.assertEqual(rep_split(0x0314), (2, 23))      # 12*23 = 276 carries
        self.assertFalse(rep_carries(0x0224))
        self.assertTrue(rep_carries(0x0314))

    def test_ph_positions(self):
        self.assertEqual(ph_positions(Instr(0x40, [0x12D2])), [0])            # SEEK ph
        self.assertEqual(ph_positions(Instr(0xBC, [0x0118, 1, 2])), [1, 2])   # REPL del 1 -> 2
        self.assertEqual(ph_positions(Instr(0x68, [0x8004, 7])), [1])         # FOCUSP {ph}
        self.assertEqual(ph_positions(Instr(0x68, [0x4006, 0, 0, 0, 0])), [])  # FOCUSP {features}
        self.assertEqual(ph_positions(Instr(0x6C, [0x0100, 0x8004, 7])), [2])  # RP {1,0}{ph}
        self.assertEqual(ph_positions(Instr(0x150, [5])), [])                 # A = 5

    def test_every_module_round_trips(self):
        total = 0
        for lang in LANGS:
            for name, sec, b, start in modules_of(lang):
                with self.subTest(lang=lang, module=name):
                    m, end = decode_module(b, start)
                    self.assertEqual(encode_module(m, None), b[start:end])
                    self.assertEqual(m.end, [0x04, 0x00])
                    total += len(m.rules)
        self.assertEqual(total, 15564)

    def test_rule_counts_match_ivxdasm(self):
        ivx, ivxdasm = tools_import('ivx', 'ivxdasm')
        for lang in LANGS:
            x = ivx.Ivx(str(ivx_path(lang)))
            for name, sec, b, start in modules_of(lang):
                with self.subTest(lang=lang, module=name):
                    n = ivxdasm.check(x, sec)[0]
                    self.assertEqual(len(decode_module(b, start)[0].rules), n)

    def test_label_is_the_next_operand(self):
        name, sec, b, start = next(m for m in modules_of('sw') if m[0] == 'M0')
        rule = decode_module(b, start)[0].rules[0]          # SEEK + ; R B ; REPL ; NEXT #32
        self.assertEqual(rule.label, 32)
        self.assertEqual([i.op for i in rule.instrs], [0x40, 0x4C, 0xBC, 0x10])

    def test_legacy_repl_words(self):
        found = {}
        for lang in LANGS:
            for name, sec, b, start in modules_of(lang):
                for r in decode_module(b, start)[0].rules:
                    for ins in r.instrs:
                        if ins.legacy:
                            found.setdefault(lang, []).append(ins.args[0])
        self.assertEqual({k: sorted(v) for k, v in found.items()},
                         {'da': [0x314, 0x320, 0x32C, 0x32C], 'ic': [0x1108]})


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the test to check that it fails**

Run: `python -m unittest tests.test_rules -v` (from `editor/`), or `python -m unittest discover -s editor/tests -t editor -p test_rules.py -v` from the root.
Expected: ERROR `ModuleNotFoundError: No module named 'ivxedit.rules'`.

- [ ] **Step 3: Write `rules.py`**

`editor/ivxedit/rules.py`:

```python
"""RULSYS rule bytecode: instructions, rules and modules (REFERENCE §5.6).

A module is a lead word, its rules, and the closing words 0x04 0x00. A rule is RULE or
RULE* {length in bytes, counting the length word} followed by instructions. An Instr keeps
its operand words as found, except that a phoneme-id word may hold any non-int object (the
model's PhRef); encoding turns those back into ids through a callback.
"""
from dataclasses import dataclass, field

from .binfmt import IvxFormatError, pack_words, u16

RULE, RULE_ITER, NEXT, REPL_NEXT, REPL = 0x08, 0x0C, 0x10, 0xB8, 0xBC
PH_OPS = (0x40, 0x44, 0x48, 0x4C, 0x50, 0x7C, 0x80)   # SEEK FOCUS SCAN R R? L L?: one id
FIXED = {0x00: 0, 0x04: 0, 0x10: 1, 0xB0: 0, 0xB4: 1, 0xC8: 1, 0xCC: 0, 0xD0: 0, 0xD4: 0,
         0x40: 1, 0x44: 1, 0x48: 1, 0x4C: 1, 0x50: 1, 0x7C: 1, 0x80: 1,
         0x54: 4, 0x58: 4, 0x5C: 4, 0x60: 4, 0x64: 4, 0x90: 4, 0x94: 4, 0xC0: 4, 0xC4: 4,
         0x150: 1, 0x154: 0, 0x158: 0, 0x15C: 1, 0x160: 0, 0x164: 1, 0x168: 0, 0x16C: 0,
         0x170: 1, 0x174: 0, 0x178: 1, 0x17C: 0, 0x180: 1, 0x184: 0, 0x188: 2, 0x18C: 1,
         0x190: 1, 0x194: 1, 0x198: 0, 0x19C: 0, 0x1A0: 0, 0x1A4: 0, 0x1A8: 0, 0x1AC: 0,
         0x1B0: 0, 0x1B4: 0, 0x1B8: 0, 0x1BC: 0, 0x1C0: 0, 0x1C4: 0, 0x1C8: 1, 0x1CC: 1,
         0x1F0: 1, 0x1F4: 1, 0x1F8: 1}


def rep_split(w):
    """REPL word -> (del, n): w = del*256 + 12*n, where 12*n may carry into the high byte"""
    for d in range(w >> 8, -1, -1):
        r = w - d * 256
        if r % 12 == 0:
            return d, r // 12
    return w >> 8, (w & 0xFF) // 12


def rep_carries(w):
    """True for a REPL word whose 12*n carried into the high byte (the engine misreads it)"""
    return 12 * rep_split(w)[1] > 0xFF


@dataclass(eq=False)
class Instr:
    op: int
    args: list = field(default_factory=list)
    legacy: bool = False   # a REPL word with a carry, as loaded from a shipped file


@dataclass(eq=False)
class Rule:
    iterative: bool        # RULE* (0x0C) rather than RULE (0x08)
    instrs: list

    @property
    def label(self):
        """the NEXT operand: never read by the interpreter, the rule's source number"""
        if self.instrs and self.instrs[-1].op == NEXT:
            return self.instrs[-1].args[0]
        return None


@dataclass(eq=False)
class Module:
    lead: int              # first word; varies per file (a date or version stamp?)
    rules: list
    end: list = field(default_factory=lambda: [0x04, 0x00])


def operand_count(b, p):
    """number of operand words of the instruction whose opcode word is at b[p]"""
    op = u16(b, p)
    if op in FIXED:
        return FIXED[op]
    if op == 0x68:                                   # FOCUSP pattern
        return (u16(b, p + 2) & 0x0FFE) // 2
    if op in (0x6C, 0x9C):                           # RP/LP {min,opt} pattern
        return 1 + (u16(b, p + 4) & 0x0FFE) // 2
    if op in (REPL_NEXT, REPL):
        return 1 + rep_split(u16(b, p + 2))[1]
    if op in (0x100, 0x104, 0x108, 0x10C):           # LD/ST spec [index]
        return 1 if b[p + 3] & 0x80 else 2
    raise IvxFormatError('unknown opcode 0x%x at body 0x%x' % (op, p))


def ph_positions(ins):
    """indices into ins.args that hold phoneme ids"""
    op, a = ins.op, ins.args
    if op in PH_OPS:
        return [0]
    if op in (REPL_NEXT, REPL):
        return list(range(1, len(a)))
    if op == 0x68 and a[0] & 0x8000:
        return [1]
    if op in (0x6C, 0x9C) and a[1] & 0x8000:
        return [2]
    return []


def decode_instr(b, p):
    """-> (Instr, offset after it)"""
    n = operand_count(b, p)
    ins = Instr(u16(b, p), [u16(b, p + 2 + 2 * i) for i in range(n)])
    if ins.op in (REPL_NEXT, REPL) and rep_carries(ins.args[0]):
        ins.legacy = True
    return ins, p + 2 + 2 * n


def decode_module(b, start):
    """-> (Module, offset just past its closing 0x00 word)"""
    lead, p, rules = u16(b, start), start + 2, []
    while u16(b, p) in (RULE, RULE_ITER):
        end, q, instrs = p + 2 + u16(b, p + 2), p + 4, []
        while q < end:
            ins, q = decode_instr(b, q)
            instrs.append(ins)
        if q != end:
            raise IvxFormatError('rule at body 0x%x overruns its length' % p)
        rules.append(Rule(u16(b, p) == RULE_ITER, instrs))
        p = end
    closing = []
    while True:
        w = u16(b, p)
        if w not in (0x04, 0x00):
            raise IvxFormatError('unexpected word 0x%x at body 0x%x' % (w, p))
        closing.append(w)
        p += 2
        if w == 0:
            return Module(lead, rules, closing), p


def encode_instr(ins, ph_id):
    return [ins.op] + [a if isinstance(a, int) else ph_id(a) for a in ins.args]


def encode_rule(rule, ph_id):
    body = []
    for ins in rule.instrs:
        body += encode_instr(ins, ph_id)
    length = 2 + 2 * len(body)
    if length > 0xFFFF:
        raise IvxFormatError('rule is %d bytes, more than a length word holds' % length)
    return [RULE_ITER if rule.iterative else RULE, length] + body


def encode_module(m, ph_id):
    ws = [m.lead]
    for r in m.rules:
        ws += encode_rule(r, ph_id)
    return pack_words(ws + m.end)
```

- [ ] **Step 4: Run the tests to check that they pass**

Run: `python -m unittest discover -s editor/tests -t editor -v`
Expected: all tests `OK`, including `test_every_module_round_trips` (15,564 rules).

- [ ] **Step 5: Commit**

```bash
git add editor/ivxedit/rules.py editor/tests/test_rules.py
git commit -m "ivxedit: rule bytecode decode/encode, round trip of all 15,564 rules

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Phoneme inventory and phoneme references

**Files:**
- Create: `editor/ivxedit/model.py` (first part: items, `Phoneme`, `PhRef`, inventory decoding, `resolve_refs`)
- Test: `editor/tests/test_inventory.py`

**Interfaces:**
- Consumes: `binfmt` (`INV`, `IvxFormatError`, `u16`, `u32`), `rules.ph_positions`, `rules.decode_module`.
- Produces (`ivxedit.model`):
  - `PNAMES` (29 parameter names);
  - `@dataclass ParamItem(pid: int, points: list[tuple[int, int]])` with `(time s8, value u8)` points;
  - `@dataclass VarItem(vid: int, value: int)`;
  - `@dataclass Phoneme(name: bytes, feat: int, mask: int, items: list, is_defaults: bool = False)` with `to_bytes() -> bytes`;
  - `@dataclass(frozen) PhRef(ph: Phoneme | None, delta: int = 0)`, where `ph=None` means the inventory terminator;
  - `decode_phoneme(rec) -> Phoneme`, `decode_inventory(b) -> (phonemes, offsets, end)`, `resolve_refs(modules, phonemes, offsets, limit) -> None`.

All model dataclasses use `eq=False`, so identity is equality. A `Phoneme` is found by identity (`id(ph)`), never by value.

- [ ] **Step 1: Write the failing test**

`editor/tests/test_inventory.py`:

```python
import struct
import unittest

from ivxedit.binfmt import INV
from ivxedit.model import (PNAMES, ParamItem, PhRef, VarItem, decode_inventory,
                           resolve_refs)
from ivxedit.rules import decode_module, ph_positions
from tests.util import LANGS, ivx_bytes, ivx_path, tools_import


def body(lang):
    return ivx_bytes(lang)[0x200:]


class InventoryTest(unittest.TestCase):
    def test_records_round_trip(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                b = body(lang)
                phs, offs, end = decode_inventory(b)
                self.assertEqual(b''.join(p.to_bytes() for p in phs) + b'\0\0', b[INV:end])
                self.assertTrue(phs[0].is_defaults)
                self.assertFalse(any(p.is_defaults for p in phs[1:]))
                self.assertEqual(offs[0], 0)

    def test_counts_match_ivx_py(self):
        (ivx,) = tools_import('ivx')
        for lang in LANGS:
            with self.subTest(lang=lang):
                phs = decode_inventory(body(lang))[0]
                self.assertEqual(len(phs), len(ivx.Ivx(str(ivx_path(lang))).chain()))

    def test_swedish_long_a(self):
        # REFERENCE §5.4: A0=24@0 F1=165@*/165@2 F2=87@*/83@2 ... FD=100@* va5=40 vac=110 vad=30
        phs = decode_inventory(body('sw'))[0]
        a = next(p for p in phs if p.name == b'A:')
        params = {PNAMES[i.pid]: i.points for i in a.items if isinstance(i, ParamItem)}
        vars_ = {i.vid: i.value for i in a.items if isinstance(i, VarItem)}
        self.assertEqual(params['A0'], [(0, 24)])
        self.assertEqual(params['F1'], [(-1, 165), (2, 165)])
        self.assertEqual(params['F2'], [(-1, 87), (2, 83)])
        self.assertEqual(params['FD'], [(-1, 100)])
        self.assertEqual((vars_[0xA5], vars_[0xAC], vars_[0xAD]), (40, 110, 30))

    def test_one_char_names_have_a_leading_nul(self):
        names = [p.name for p in decode_inventory(body('sw'))[0]]
        self.assertIn(b'\x00+', names)

    def test_resolve_refs(self):
        terminator_refs = {}
        for lang in LANGS:
            with self.subTest(lang=lang):
                b = body(lang)
                H = struct.unpack_from('>16I', b, 0)
                phs, offs, end = decode_inventory(b)
                mods = {s: decode_module(b, H[s])[0] for s in (5, 6, 9, 10, 11, 12) if H[s]}
                resolve_refs(mods, phs, offs, end - 2 - INV)
                for m in mods.values():
                    for r in m.rules:
                        for ins in r.instrs:
                            for k in ph_positions(ins):
                                a = ins.args[k]
                                self.assertIsInstance(a, PhRef)
                                self.assertEqual(a.delta, 0)
                                if a.ph is None:
                                    terminator_refs[lang] = terminator_refs.get(lang, 0) + 1
        self.assertEqual(terminator_refs, {'fr': 1, 'it': 7})


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the test to check that it fails**

Run: `python -m unittest discover -s editor/tests -t editor -p test_inventory.py -v`
Expected: ERROR `ModuleNotFoundError: No module named 'ivxedit.model'`.

- [ ] **Step 3: Write the first part of `model.py`**

`editor/ivxedit/model.py`:

```python
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
```

- [ ] **Step 4: Run the tests to check that they pass**

Run: `python -m unittest discover -s editor/tests -t editor -v`
Expected: all `OK`.

- [ ] **Step 5: Commit**

```bash
git add editor/ivxedit/model.py editor/tests/test_inventory.py
git commit -m "ivxedit: phoneme inventory and phoneme references

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 4: Whole-file load and write, byte-identical on all 12 files

**Files:**
- Modify: `editor/ivxedit/model.py` (add imports, `MODULE_DIR`, `SECTION_ORDER`, `Lexicon`, `IvxFile`, `decode_lexicon`, `_pads`, `load`)
- Create: `editor/ivxedit/writer.py` (`lexicon_bytes`, `to_bytes`)
- Test: `editor/tests/test_roundtrip.py`

**Interfaces:**
- Consumes: Task 2 (`decode_module`, `encode_module`) and Task 3 (`decode_inventory`, `resolve_refs`, `Phoneme.to_bytes`, `PhRef`).
- Produces:
  - `model.MODULE_DIR = {'M3': 5, 'M4': 6, 'M0': 9, 'M1': 10, 'M5': 11, 'M2': 12}`.
  - `model.Lexicon(entries: list[tuple[bytes, bytes]])`.
  - `model.IvxFile`, with fields:
    - `header`, `directory`, `head`, `tables`, `phonemes`, `modules`, `class_lo`;
    - `xlat_hi = None`, `class_hi = None`, `ixlex = None`, `h2_rel = None`, `lexicon = None`;
    - `pads: dict`, `end_pad: int`.

    It also has properties `version -> str` and `ntotal -> int`, and the method `param(i) -> (a0, a1, a2, a3)`.
  - `model.load(data: bytes) -> IvxFile` (raises `IvxFormatError`).
  - `writer.to_bytes(f: IvxFile) -> bytes` and `writer.lexicon_bytes(lex) -> bytes`.

- [ ] **Step 1: Write the failing test**

`editor/tests/test_roundtrip.py`:

```python
import unittest

from ivxedit.model import PNAMES, load
from ivxedit.writer import to_bytes
from tests.util import LANGS, ivx_bytes, ivx_path, tools_import


class RoundTripTest(unittest.TestCase):
    def test_all_files_byte_identical(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                data = ivx_bytes(lang)
                self.assertEqual(to_bytes(load(data)), data)

    def test_header_matches_ivx_py(self):
        (ivx,) = tools_import('ivx')
        for lang in LANGS:
            with self.subTest(lang=lang):
                self.assertEqual(load(ivx_bytes(lang)).version, ivx.Ivx(str(ivx_path(lang))).version)

    def test_parameter_tables(self):
        # REFERENCE §5.3: Swedish A3 is F1 3, F2 35, F3 59, F0 -3
        f = load(ivx_bytes('sw'))
        self.assertEqual(f.ntotal, 29)
        self.assertEqual([f.param(PNAMES.index(n))[3] for n in ('F1', 'F2', 'F3', 'F0')],
                         [3, 35, 59, -3])

    def test_sections(self):
        files = {lang: load(ivx_bytes(lang)) for lang in LANGS}
        self.assertEqual(sorted(l for l, f in files.items() if f.lexicon), ['bl', 'sw'])
        self.assertEqual(sorted(l for l, f in files.items() if f.ixlex is not None),
                         sorted(set(LANGS) - {'bl', 'sw'}))
        self.assertEqual(sorted(l for l, f in files.items() if f.xlat_hi is not None),
                         ['bl', 'da', 'du', 'fi', 'fr', 'ge', 'ic', 'sw'])
        self.assertEqual({l: f.end_pad for l, f in files.items() if f.end_pad},
                         {'am': 80, 'it': 64, 'no': 32, 'sp': 48})
        self.assertEqual(sorted(l for l, f in files.items() if f.pads.get('INV')),
                         ['da', 'fr', 'ge', 'it', 'no', 'sw'])
        self.assertEqual({l: f.h2_rel for l, f in files.items() if f.h2_rel is not None},
                         {'da': 0x1800})
        self.assertEqual(sorted(files['fi'].modules), ['M0', 'M1', 'M2', 'M5'])

    def test_lexicon(self):
        lex = load(ivx_bytes('sw')).lexicon.entries
        self.assertEqual(lex[0], (b' ', b'es'))                # REFERENCE §5.7
        self.assertIn((b'%', b"PROS'ENT"), lex)
        self.assertEqual(len(lex), 3370)


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the test to check that it fails**

Run: `python -m unittest discover -s editor/tests -t editor -p test_roundtrip.py -v`
Expected: ERROR `ImportError: cannot import name 'load' from 'ivxedit.model'`.

- [ ] **Step 3: Extend `model.py`**

Replace the imports at the top of `editor/ivxedit/model.py` with:

```python
import bisect
import struct
from dataclasses import dataclass, field

from .binfmt import HDR_SIZE, INV, SLOTS, TABLES, IvxFormatError, align16, u16, u32, words
from .rules import decode_module, ph_positions
```

Add these two constants after `PNAMES`:

```python
MODULE_DIR = {'M3': 5, 'M4': 6, 'M0': 9, 'M1': 10, 'M5': 11, 'M2': 12}   # in file order
SECTION_ORDER = ['INV', 'M3', 'M4', 'M0', 'M1', 'M5', 'M2', 'H14', 'H13', 'H3']
```

Append at the end of the file:

```python
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


def load(data):
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
```

- [ ] **Step 4: Write `writer.py`**

`editor/ivxedit/writer.py`:

```python
"""IvxFile -> .ivx bytes (spec: Writer)."""
import struct

from .binfmt import INV, SLOTS, TABLES, align16, pack_words
from .model import MODULE_DIR
from .rules import encode_module


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
```

- [ ] **Step 5: Run the tests to check that they pass**

Run: `python -m unittest discover -s editor/tests -t editor -v`
Expected: all `OK`; `test_all_files_byte_identical` passes for all 12 subtests.

If a subtest fails, find the first differing byte with this one-off command and check which section it falls in (`directory` gives the starts):

```bash
cd editor && python -c "from ivxedit.model import load; from ivxedit.writer import to_bytes; d=open('../bin/swrules.ivx','rb').read(); o=to_bytes(load(d)); print(len(d), len(o), next((hex(i) for i in range(min(len(d),len(o))) if d[i]!=o[i]), None))"
```

- [ ] **Step 6: Commit**

```bash
git add editor/ivxedit/model.py editor/ivxedit/writer.py editor/tests/test_roundtrip.py
git commit -m "ivxedit: load and write whole .ivx files, byte-identical on all 12

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 5: Prove phoneme-id relocation by audio

The fixups are already in Task 4's `to_bytes`. This task adds the end-to-end test that proves them: if any id were missed, the shifted file would speak differently.

**Files:**
- Test: `editor/tests/test_relocation.py`

**Interfaces:**
- Consumes: `model.load`, `model.Phoneme`, `model.ParamItem`, `writer.to_bytes`, and `tests.util.ivxsay`/`lang_texts`/`ivx_bytes`/`LANGS`.

- [ ] **Step 1: Write the test**

`editor/tests/test_relocation.py`:

```python
"""Shift every phoneme id and require identical audio from ivxsay (spec: Testing, 3)."""
import os
import subprocess
import tempfile
import unittest

from ivxedit.model import ParamItem, Phoneme, load
from ivxedit.rules import ph_positions
from ivxedit.writer import to_bytes
from tests.util import LANGS, ivx_bytes, ivxsay, lang_texts

SAY = ivxsay()


def shifted(data):
    """the file with a 12-byte record no rule or text can reach inserted after the defaults
    record, so every later phoneme id moves by 12"""
    f = load(data)
    f.phonemes.insert(1, Phoneme(b'\x01\x01', 0, 0, []))
    return to_bytes(f)


def ref_names(f):
    """every phoneme-id operand in rule order, as (phoneme name or None, delta); names are
    unique in every shipped inventory"""
    return [a if isinstance(a, int) else (None if a.ph is None else a.ph.name, a.delta)
            for m in f.modules.values() for r in m.rules for i in r.instrs
            for a in (i.args[k] for k in ph_positions(i))]


class ReferencesTest(unittest.TestCase):
    def test_shifted_file_has_the_same_references(self):
        # also covers rules the test texts never reach, such as the 8 terminator references
        for lang in LANGS:
            with self.subTest(lang=lang):
                data = ivx_bytes(lang)
                self.assertEqual(ref_names(load(shifted(data))), ref_names(load(data)))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
class RelocationTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.texts = lang_texts()

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def speak(self, data, text):
        ivx, txt, wav = (os.path.join(self.tmp.name, n) for n in ('x.ivx', 'x.txt', 'x.wav'))
        with open(ivx, 'wb') as f:
            f.write(data)
        with open(txt, 'wb') as f:
            f.write(text)
        subprocess.run([str(SAY), '-l', ivx, '-e', 'cp1252', '-i', txt, '-o', wav],
                       check=True, capture_output=True)
        with open(wav, 'rb') as f:
            return f.read()

    def test_shifted_ids_speak_the_same(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                data, text = ivx_bytes(lang), self.texts['%srules.ivx' % lang]
                moved = shifted(data)
                self.assertNotEqual(moved, data)
                self.assertEqual(self.speak(moved, text), self.speak(data, text))

    def test_control_edit_changes_the_audio(self):
        # proves the comparison can fail: raise every F1 point of every phoneme by 20
        for lang in LANGS:
            with self.subTest(lang=lang):
                data, text = ivx_bytes(lang), self.texts['%srules.ivx' % lang]
                f = load(data)
                for ph in f.phonemes[1:]:
                    for it in ph.items:
                        if isinstance(it, ParamItem) and it.pid == 5:
                            it.points = [(t, min(255, v + 20)) for t, v in it.points]
                self.assertNotEqual(self.speak(to_bytes(f), text), self.speak(data, text))


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run it**

Run: `python -m unittest discover -s editor/tests -t editor -p test_relocation.py -v`
Expected: 3 tests, 36 subtests, `OK`, in a few seconds. If the two audio tests are `skipped 'ivxsay is not built'`, run `export PATH=/c/winlibs/mingw32/bin:$PATH; make build/ivxsay.exe` first.

- [ ] **Step 3: Check that the tests catch missed fixups (two mutations, both verified while planning)**

Mutation A: the terminator references. In `ph_id` in `editor/ivxedit/writer.py`, temporarily change `return len(inv) - 2` to `return 0x1BAE` (the old French terminator offset).

Run: `python -m unittest discover -s editor/tests -t editor -p test_relocation.py -v`
Expected: `test_shifted_file_has_the_same_references` FAILs (or ERRORs) for `fr` and `it`. The audio tests still pass, because the test texts never reach those 8 rules; that is why the reference test exists. Revert.

Mutation B: every other reference. Temporarily change `return offset[id(ref.ph)] + ref.delta` to `return offset[id(ref.ph)] - 12 * (not ref.ph.is_defaults)`.

Run the same command.
Expected: `test_shifted_ids_speak_the_same` fails or errors in every language. Some subtests error with `CalledProcessError … 3221225477` because ivxsay crashes on bad ids, which also counts as red. Revert.

Run the full suite: `python -m unittest discover -s editor/tests -t editor -v`. Expected: `OK`.

- [ ] **Step 4: Commit**

```bash
git add editor/tests/test_relocation.py
git commit -m "ivxedit: audio test for phoneme-id relocation in all 12 languages

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 6: Checks before saving, and safe saving

**Files:**
- Create: `editor/ivxedit/checks.py`
- Modify: `editor/ivxedit/writer.py` (add `BIN`, `SaveRefused`, `save`)
- Test: `editor/tests/test_checks.py`, `editor/tests/test_save.py`

**Interfaces:**
- Consumes: `model.PhRef`, `model.VarItem`, `rules.REPL`/`REPL_NEXT`/`ph_positions`/`rep_carries`/`rep_split`, `writer.to_bytes`.
- Produces:
  - `checks`: `@dataclass Problem(where: str, message: str)` with `str()` giving `"where: message"`; `check(f) -> list[Problem]`; `ph_label(ph) -> str`; `rule_label(module, i, rule) -> str` (e.g. `"M0 rule 0 (#32)"`).
  - `writer`: `BIN: Path`, `class SaveRefused(Exception)` with `.problems`, and `save(f, path) -> None`. It raises `PermissionError` for paths inside `bin/` and `SaveRefused` when `check` finds problems; it writes atomically and keeps `<name>.bak`.

- [ ] **Step 1: Write the failing tests**

`editor/tests/test_checks.py`:

```python
import unittest

from ivxedit.checks import check
from ivxedit.model import PNAMES, ParamItem, PhRef, load
from ivxedit.rules import REPL, Instr, ph_positions
from tests.util import LANGS, ivx_bytes


def sw():
    return load(ivx_bytes('sw'))


class ChecksTest(unittest.TestCase):
    def test_shipped_files_pass(self):
        for lang in LANGS:
            with self.subTest(lang=lang):
                self.assertEqual(check(load(ivx_bytes(lang))), [])

    def test_deleted_phoneme_still_used(self):
        f = sw()
        ins = f.modules['M0'].rules[0].instrs[0]               # SEEK +
        self.assertIsInstance(ins.args[0], PhRef)
        f.phonemes.remove(ins.args[0].ph)
        probs = check(f)
        self.assertIn('M0 rule 0 (#32)', [p.where for p in probs])
        self.assertTrue(all('not in the inventory' in p.message for p in probs))
        self.assertIn("phoneme '+'", probs[0].message)

    def test_new_repl_word_with_carry(self):
        f = load(ivx_bytes('da'))
        rule, i = next((r, i) for m in f.modules.values() for r in m.rules
                       for i, x in enumerate(r.instrs) if x.legacy)
        old = rule.instrs[i]
        rule.instrs[i] = Instr(old.op, list(old.args))         # an edit: no longer legacy
        probs = check(f)
        self.assertEqual(len(probs), 1)
        self.assertIn('at most 21', probs[0].message)

    def test_repl_word_must_match_its_phonemes(self):
        f = sw()
        ins = next(x for m in f.modules.values() for r in m.rules for x in r.instrs
                   if x.op == REPL and len(x.args) > 1)
        ins.args.append(ins.args[1])
        self.assertTrue(any('is not for' in p.message for p in check(f)))

    def test_lexicon_order(self):
        f = sw()
        e = f.lexicon.entries
        e[1], e[2] = e[2], e[1]                                  # '!!' before '!'
        probs = check(f)
        self.assertEqual([p.where for p in probs], ["lexicon entry 2 ('!')"])

    def test_point_out_of_range(self):
        f = sw()
        a = next(p for p in f.phonemes if p.name == b'A:')
        f1 = next(i for i in a.items if isinstance(i, ParamItem) and i.pid == PNAMES.index('F1'))
        f1.points[0] = (-1, 256)
        self.assertEqual([p.where for p in check(f)], ["phoneme 'A:'"])

    def test_item_order(self):
        f = sw()
        a = next(p for p in f.phonemes if p.name == b'A:')
        a.items.reverse()
        self.assertTrue(any('signed-id order' in p.message for p in check(f)))

    def test_defaults_must_stay_first(self):
        f = sw()
        f.phonemes.append(f.phonemes.pop(0))
        self.assertIn('inventory', [p.where for p in check(f)])


if __name__ == '__main__':
    unittest.main()
```

`editor/tests/test_save.py`:

```python
import tempfile
import unittest
from pathlib import Path

from ivxedit.model import Phoneme, load
from ivxedit.writer import SaveRefused, save, to_bytes
from tests.util import ROOT, ivx_bytes


class SaveTest(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.dir = Path(self.tmp.name)

    def tearDown(self):
        self.tmp.cleanup()

    def test_save_and_backup(self):
        data = ivx_bytes('sw')
        f, out = load(data), self.dir / 'sw.ivx'
        save(f, out)
        self.assertEqual(out.read_bytes(), data)
        f.phonemes.insert(1, Phoneme(b'\x01\x01', 0, 0, []))
        save(f, out)
        self.assertEqual((self.dir / 'sw.ivx.bak').read_bytes(), data)
        self.assertEqual(out.read_bytes(), to_bytes(f))
        self.assertEqual(sorted(p.name for p in self.dir.iterdir()), ['sw.ivx', 'sw.ivx.bak'])

    def test_refuses_bin(self):
        target = ROOT / 'bin' / 'ivxedit-test.ivx'
        with self.assertRaises(PermissionError):
            save(load(ivx_bytes('sw')), target)
        self.assertFalse(target.exists())

    def test_refused_save_writes_nothing(self):
        f = load(ivx_bytes('sw'))
        f.phonemes[1].items[0].value = 70000                   # A:'s va3, a 16-bit variable
        with self.assertRaises(SaveRefused) as cm:
            save(f, self.dir / 'x.ivx')
        self.assertTrue(cm.exception.problems)
        self.assertEqual(list(self.dir.iterdir()), [])


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the tests to check that they fail**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[cs]*.py" -v`
Expected: ERROR `No module named 'ivxedit.checks'` and `cannot import name 'SaveRefused'`.

- [ ] **Step 3: Write `checks.py`**

`editor/ivxedit/checks.py`:

```python
"""The checks the writer runs before saving (spec: Writer, "Checks before saving")."""
from dataclasses import dataclass

from .model import PhRef, VarItem
from .rules import REPL, REPL_NEXT, ph_positions, rep_carries, rep_split


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


def check(f):
    """-> list of Problem; empty when to_bytes(f) gives a file the engine can load"""
    out = []
    if not f.phonemes or not f.phonemes[0].is_defaults or any(p.is_defaults for p in f.phonemes[1:]):
        out.append(Problem('inventory', 'the defaults record must be first, and only there'))
    size = 2 + sum(_check_phoneme(ph, out) for ph in f.phonemes)
    if size > 0x10000:
        out.append(Problem('inventory', 'is %d bytes; phoneme ids are 16-bit' % size))

    present = {id(ph) for ph in f.phonemes}
    for name, m in f.modules.items():
        for i, r in enumerate(m.rules):
            w, nwords = rule_label(name, i, r), 0
            for ins in r.instrs:
                nwords += 1 + len(ins.args)
                for k in ph_positions(ins):
                    a = ins.args[k]
                    if isinstance(a, PhRef) and a.ph is not None and id(a.ph) not in present:
                        out.append(Problem(w, 'uses %s, which is not in the inventory' % ph_label(a.ph)))
                if ins.op in (REPL, REPL_NEXT):
                    n = rep_split(ins.args[0])[1]
                    if n != len(ins.args) - 1:
                        out.append(Problem(w, 'REPL word 0x%x is not for %d phonemes'
                                           % (ins.args[0], len(ins.args) - 1)))
                    elif rep_carries(ins.args[0]) and not ins.legacy:
                        out.append(Problem(w, 'REPL of %d phonemes: at most 21 fit its count byte' % n))
                if any(isinstance(a, int) and not 0 <= a <= 0xFFFF for a in ins.args):
                    out.append(Problem(w, 'an operand does not fit 16 bits'))
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
```

- [ ] **Step 4: Add `save` to `writer.py`**

Replace the imports at the top of `editor/ivxedit/writer.py` with:

```python
import os
import shutil
import struct
import tempfile
from pathlib import Path

from .binfmt import INV, SLOTS, TABLES, align16, pack_words
from .checks import check
from .model import MODULE_DIR
from .rules import encode_module

BIN = Path(__file__).resolve().parents[2] / 'bin'   # the shipped files: never written


class SaveRefused(Exception):
    """check() found problems; nothing was written"""

    def __init__(self, problems):
        super().__init__('%d problem(s): %s' % (len(problems), '; '.join(map(str, problems[:5]))))
        self.problems = problems
```

Append at the end of `writer.py`:

```python
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
```

- [ ] **Step 5: Run the tests to check that they pass**

Run: `python -m unittest discover -s editor/tests -t editor -v`
Expected: all `OK`. `test_shipped_files_pass` must give `[]` for all 12 files; if one doesn't, the check is wrong, not the file.

- [ ] **Step 6: Commit**

```bash
git add editor/ivxedit/checks.py editor/ivxedit/writer.py editor/tests/test_checks.py editor/tests/test_save.py
git commit -m "ivxedit: checks before saving, atomic save with .bak, bin/ protected

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 7: Record the findings (REFERENCE.md, AGENTS.md, spec)

**Files:**
- Modify: `REFERENCE.md` (§5 intro, §5.2 table and layout, §5.4, §5.6 table, §5.6.1, §5.7)
- Modify: `AGENTS.md` (repository layout table, a short "Editor" section after "Tools")
- Modify: `docs/superpowers/specs/2026-10-03-ivx-editor-design.md` (facts, model, open questions)

- [ ] **Step 1: `REFERENCE.md` §5 intro.** Replace the line

`All multi-byte values are **big-endian**, which suggests the files were compiled on 68000-era tooling. The DLL converts them in place when loading (§5.1). Tools:`

with:

```markdown
All multi-byte values are **big-endian**, which suggests the files were compiled on 68000-era tooling. The DLL converts them in place when loading (§5.1). The files hold **no 68k machine code** [CONFIRMED]: the rule sections disassemble as 68000 code to junk (capstone: `ori.b #$8,(a0)`, an invalid word every few instructions), while the RULSYS decoder reads every rule to its exact length. The opcodes are multiples of 4 (jump-table offsets), so the original compiler and interpreter probably ran on a 68k machine [LIKELY]. Tools:
```

and add a bullet after the `tools/ivxdasm.py` bullet:

```markdown
- `editor/ivxedit`: a model of the whole file that writes all 12 files back byte-identical and relocates phoneme ids (`python -m unittest discover -s editor/tests -t editor`).
```

- [ ] **Step 2: §5.2.** Replace the table row `| H[15] | Body length | — |` with:

```markdown
| H[15] | End of the data. Equal to the body length, except in am, it, no and sp, where 80, 64, 32 and 48 zero bytes follow it [CONFIRMED, `editor/tests/test_roundtrip.py`]. | — |
```

After the table (before "The DLL converts class codes…"), add:

```markdown
**Section order and padding** [CONFIRMED on all 12 files, `editor/ivxedit` round trip]: the inventory at 0x170, then M3, M4, M0, M1, M5, M2, H[14], H[13], H[3]; absent sections are skipped. Every section starts on a 16-byte boundary, with zero padding. In da, fr, ge, it, no and sw the inventory is followed by 16 more zero bytes. Each rule module is a lead word, its rules and the words `04 00`; the lead words vary per file (`9401`, `1023`, `8888`…) and may be a date or version stamp [HYPOTHESIS]. In files without H[3], H[13] is followed by the indexed lexicon (§5.7). Danish H[2] points 0x1800 bytes into that region, past the last index target (0x16d0); what it addresses is unknown.
```

- [ ] **Step 3: §5.4.** After the line beginning `This is a chain of records`, add:

```markdown
- Names are two bytes; one-character names have a **leading** NUL (`00 2B` = `+`) [CONFIRMED].
- Items are in ascending **signed** id order, so variables (ids ≥ 0x80) come first; each id at most once [CONFIRMED, all 1,557 records].
```

- [ ] **Step 4: §5.6 table.** Replace the row `| 10 | NEXT | rule# | advance start position and retry; operand = rule number |` with:

```markdown
| 10 | NEXT | rule# | advance start position and retry. The operand is **never read** (`cw_op_NEXT` resets `ip` to the rule start, `src/cwflow.c:45`) [CONFIRMED]; it is the rule's number in the source [LIKELY] |
```

After the `B8/BC` row's text "…the port stops with status 0x10 (a guard, §2.2.2).", append: ` Five shipped words carry: Danish 0x314, 0x320, 0x32c ×2, Icelandic 0x1108 [CONFIRMED].`

After the table, add:

```markdown
**Phoneme ids that point at the inventory's terminator** [CONFIRMED]: 1 operand in `frrules.ivx` (0x1bae) and 7 in `itrules.ivx` (0xe3c) equal the offset of the inventory's 0 terminator, so they name a phoneme that does not exist (no segment can match it). Probably a rule-compiler artifact for an undefined symbol [HYPOTHESIS]. Every other phoneme-id operand in the 12 files is a record start. The rule bytecode is the only place a file stores phoneme offsets: at run time every segment's id comes from `ph_find` (by name, `src/tpelem.c:274`) or from a rule operand [CONFIRMED, every `phinv` use in `src/`].
```

- [ ] **Step 5: §5.6.1.** Replace `They are sorted by descending signed id on segments, but by ascending id in phoneme records.` with `They are sorted by descending signed id on segments, but by ascending signed id in phoneme records (§5.4).`

- [ ] **Step 6: §5.7.** Replace the line `- Only Swedish and British English have H[3]. Danish has H[2] and the external \`darules.dll\`. The other languages rely on rules.` with:

```markdown
- The size dword counts itself. The entries end with one or more 0x7F bytes, up to a 16-byte boundary [CONFIRMED, sw 2, bl 3]. The order is non-decreasing: sw has 6 and bl 10 repeated headwords (`BORT` twice with different transcriptions, `LOSE` twice the same) [CONFIRMED].
- Only Swedish and British English have H[3]. **The other 10 files have an indexed lexicon instead** [CONFIRMED, `ivx_bind` `src/ttsctrl.c:517-525`, `lex_lookup` `src/tpstr.c:415`]: a 128-word index at H[13] + 0x80 (one entry per first byte, offsets relative to H[13] + 0x180), then the entries at H[13] + 0x180. Keys lack their first byte, strings are padded to even length, and each first-byte group ends with 0x7F. Examples: German `!` → `'UM#GE0K'E:R`, Spanish `ABR'IR INTERROGATHI'ON`. Danish also has H[2] (§5.2) and the external `darules.dll`.
```

- [ ] **Step 7: `AGENTS.md`.** In the repository layout table, add this row just before the `` | `tools/` | `` row:

```markdown
| `editor/` | `ivxedit`, the `.ivx` editor (spec `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`). Step 1 is done: `ivxedit.model.load` reads a file into a model (phonemes, rule modules with phoneme references, tables, character tables, lexicons), `ivxedit.writer.to_bytes`/`save` write it back (byte-identical for all 12 files, phoneme ids relocated, `bin/` refused), `ivxedit.checks.check` lists what would stop the engine loading it. Tests: `make editor-test`. |
```

After the "Tools" section's last paragraph (before "## Reading the papers"), add:

```markdown
## Editor (`editor/`)

```bash
python -m unittest discover -s editor/tests -t editor -v
```

This runs the editor's tests. `make editor-test` builds `ivxsay` first and runs them too, so the relocation test is not skipped; `make test` includes it. Python 3.13 standard library only so far; the GUI (later steps) adds PySide6.
```

- [ ] **Step 8: The spec.** In `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`:
  - Replace the bullet starting `- **8 phoneme ids in the rules do not point at a record start**` with: `- **8 phoneme ids in the rules point at the inventory's 0 terminator**: 1 in \`frrules.ivx\` (0x1bae) and 7 in \`itrules.ivx\` (0xe3c) [CONFIRMED]. They name a phoneme that does not exist; probably a compiler artifact [HYPOTHESIS].`
  - Replace the bullet starting `- **One shipped \`REPL\` word does not encode` with: `- **Five shipped \`REPL\` words do not encode as \`d·256 + 12·n\` without a carry**: Danish 0x314, 0x320, 0x32c ×2 and Icelandic 0x1108 [CONFIRMED].`
  - In "Model", replace the `**\`PhRef\`**` bullet with: `- **\`PhRef\`**: a reference to a \`Phoneme\` object plus a byte \`delta\`; \`PhRef(None)\` is the inventory terminator (the 8 ids above), encoded as the new terminator offset.` Replace `- \`build_tag\` (body 0x40) and the word at 0x6C, copied as found. The words at 0x6A and 0x6E are derived from the parameter count on save and checked against the originals in the round-trip test.` with `- \`head\`: body 0x40..0x70 (build tag, then the words at 0x6A, 0x6C, 0x6E), copied as found.`, and add `- \`end_pad\`: zero bytes after H[15] (am, it, no, sp); \`pads\`: extra zero padding after a section (16 after the inventory in 6 files).`
  - Replace the whole "Open questions (investigated in step 1)" section body with: `Answered while planning step 1 (plan \`docs/superpowers/plans/2026-10-03-ivxedit-step1-model-writer.md\`, "Facts found while planning"): sections are 16-byte aligned (plus 16 after the inventory in 6 files, and zero padding after H[15] in 4); lexicon2 is the engine's indexed lexicon with region-relative offsets; Danish H[2] points into it; no engine code stores phoneme offsets; the words at 0x6A/0x6E are kept raw. The save-time checks are in \`checks.py\`.`

- [ ] **Step 9: Verify and commit**

Run: `python -m unittest discover -s editor/tests -t editor -v`
Expected: all `OK` (no code changed in this task).

```bash
git add REFERENCE.md AGENTS.md docs/superpowers/specs/2026-10-03-ivx-editor-design.md
git commit -m "REFERENCE, AGENTS, spec: ivx layout findings from ivxedit step 1

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Done when

- `python -m unittest discover -s editor/tests -t editor -v` reports all tests `OK`, with the relocation tests run (not skipped).
- `make test` passes as before, now including `editor-test`.
- The next plan (step 2: read-only GUI and Speak) can build on `load`, `to_bytes`, `save`, `check` and the model classes without changing them.
