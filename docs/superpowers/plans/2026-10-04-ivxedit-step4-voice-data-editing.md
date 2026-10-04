# ivxedit Step 4: Voice-Data Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make everything but the rules editable in `ivxedit` — header, parameter tables, phonemes, characters and lexicon — with undo, a Problems panel that re-checks after every edit, Save and Save As.

**Architecture:** A new `edits.py` (standard library only) holds one function per kind of edit. Each checks its input and returns an `Edit` that replaces one attribute of a model object with a new value (a new header `bytes`, new parameter tables, a new items list, a new inventory list, a new lexicon list), so undo puts the old object back exactly. The window wraps each `Edit` in a `QUndoCommand`, re-runs `check()` after every change (now with warnings and a jump target per problem), and refreshes the shown view after the edited widget's signal has returned. `checks.py`, `writer.py` and `speak.py` gain the step-4 items the earlier reviews deferred.

**Tech Stack:** Python 3.13, PySide6 ≥ 6.8 (`QUndoStack` is in `QtGui`), `unittest`.

**Spec:** `docs/superpowers/specs/2026-10-03-ivx-editor-design.md` (sections "Writer: Checks before saving", "GUI", "Testing" 4, 9 and 11). The previous steps' plans: `docs/superpowers/plans/2026-10-03-ivxedit-step1-model-writer.md`, `docs/superpowers/plans/2026-10-04-ivxedit-step2-readonly-gui.md`, `docs/superpowers/plans/2026-10-04-ivxedit-step3-rule-notation.md`.

## Global Constraints

- "Editing is enabled per editor in step 4 (all but Rules) and step 5 (Rules). Every edit is a `QUndoCommand` on the model; the title shows `*` when there are unsaved changes; the Problems panel runs the writer's checks after each edit." (spec)
- Editors (spec): "**Header**: form (version, copyright, flags, lexicon name)." "**Parameters**: 29 rows: name, A1 type with its handler name (REFERENCE §5.3, e.g. "18: approach → freq"), A2 rate, A3 offset." "**Phoneme**: name, `feat`/`mask` as a bit grid, items table (`time@value` cells per parameter, variables), and "Used by N rules"" (the cells are `value@time`, as built in step 2). "**Characters**: 256 rows: byte, cp1252 character, class code with its flag meaning (REFERENCE §5.2), transliteration." "**Lexicon**: searchable table, edits keep the sort order." "**Lexicon 2, Unknown**: read-only hex view."
- "deleting a phoneme that rules use is refused, with the list of rules" (spec: Checks before saving).
- "Save As writes a temporary file in the target folder and renames it over the target; overwriting an existing file first keeps `<name>.bak`." "the target path is not inside `bin/`." (spec)
- Spec Testing 11: "**Undo**: a script of edits, then undo of all of them, saves byte-identical to the original."
- Tests: `unittest`, run by `python -m unittest discover -s editor/tests -t editor` from the repository root; GUI tests run offscreen and skip without PySide6; tests that need `build/ivxsay.exe` skip with a message when it is missing (spec: Testing).
- `checks.py`, `writer.py`, `edits.py` use the Python standard library only.
- `src/`, `lib/`, `cli/`, `bin/` and the papers in `docs/` do not change (AGENTS.md, spec).
- Files are UTF-8 with LF line endings. Python style follows the surrounding code: lower-case docstrings that say what a function returns, short comments, lines up to about 110 characters.
- Commits end with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Facts found while planning

All measured on a working prototype of this plan's code (the code below), on the 12 files in `bin/`.

1. **`check()` is fast enough to run after every edit**: 1–20 ms per file (Swedish 17 ms, British English 20 ms).
2. **Phoneme names**: every shipped name is two bytes with a non-NUL second byte (one-character names have a leading NUL); the defaults record is `@` in every file; no file has two phonemes with the same name. So "one or two characters, no other NUL" is a check every shipped file passes, and it rules out the names the listing cannot show (`b'\0\0'`, `b'A\0'`).
3. **Parameter tables**: every shipped A1 is a multiple of 4 up to 0x80 (types 0–32, `bl` uses 0x80); A2 is at most 20; A3 is −3..75. The approach handlers index the 68-entry rate table with A2 (`OP_RATE[A2]`, `src/opsmooth.c:591`), the smoothing handlers multiply by it.
4. **Transliterations** (H[14]) of one character are stored with a NUL after it (`{\0`), empty ones as `\0\0`; the Characters view used to show the NUL.
5. **Header text fields**: lexicon name at 0x10 (12 bytes), version at 0x80 (64), copyright at 0xC0 (64), each NUL-padded; flags is the big-endian dword at 0.
6. **Qt**: a view that rebuilds itself inside the signal of the widget being edited (`itemChanged` of a `QTableWidget`, `setData` of a model) can delete the item that emitted it, so the window refreshes the shown view from a zero-delay `QTimer` after the change (tests call `settle()`, which processes events). A button removed with `deleteLater()` stays visible until the event loop runs, so removed buttons are also given `setParent(None)`. On the offscreen platform `QApplication.focusWidget()` is `None`; tests type into the open cell editor found among the view's viewport children.
7. **Shipped lexicons** keep their order through every edit: the editor inserts an entry after any equal headwords (`bisect_right`), so `BORT` (twice in `sw`) stays as it is.

## Decisions (where the spec leaves the choice to step 4)

- **An edit replaces an attribute, never changes it in place.** `Edit(obj, attr, value)` keeps the old object for undo; `Phoneme.items` are copied item by item before a change, so the undone list still holds the original objects. Undo is therefore exact, and the spec's undo test holds by construction (it is still tested, model-level and through the window).
- **Refuse, don't store**: input the model cannot hold (a name of three characters, the point `300@0`, A1 type 40, a headword with 0x7F) is refused with a message in the status bar, and the cell shows the model's value again. Edits that change nothing push nothing.
- **Warnings**: `Problem` gets `warning` (does not stop a save) and `target` (what the Problems panel jumps to). A duplicate phoneme name is a warning on the later phoneme: the engine finds the first by name, rules refer by id.
- **New `check()` errors** (deferred from the step-1 and step-3 reviews): a name that is not one or two characters without NUL; a build tag starting "XX" (`ivx_bind` refuses it); a phoneme id (record offset + delta) past 0x7FFF; character tables that are not `bytes` of the right length. `check()` never raises on a model the editor can build.
- **Parameters**: A1 is edited as its type, A1/4 = 0–32 (`18` or `18: approach → freq`), A2 as 0–255, A3 as a signed word; A0 (the names slot) stays read-only.
- **Phonemes**: the inventory table has New (after the selected phoneme, named `Z0`, `Z1`, … whichever is free), Copy and Delete; the phoneme view edits the name, toggles bits by clicking, edits `value@time` cells (an empty cell after the last point appends one; clearing a point removes it), and adds or removes items, which stay in ascending signed-id order. Deleting the defaults record or a phoneme any rule uses is refused with the rules listed.
- **Lexicon**: edited as stored, in the file's 7-bit national notation (Swedish `[` = Ä); the view says so. An edited headword moves to its sorted place and the selection follows it. Lexicon 2 and Unknown stay read-only (spec).
- **Problems panel**: a dock under the window, opened from the `Problems (n)` button in the status bar or View ▸ Problems, and whenever a save is refused; activating a problem jumps to it.
- **Save**: File ▸ Save writes to the open file unless it is in `bin/`, in which case it is Save As. Unsaved changes are asked about (Save, Discard, Cancel) before Open and Quit. A saved file gets the permission bits of the file it replaces (or 0666 less the umask), and is flushed to disk before the rename.
- **After an edit, undo or redo** the window re-runs the checks, updates the title, rebuilds the tree when phoneme names or counts changed (dropping the cached rule lines, which show names), refreshes the shown view, and then shows what changed (an undo of a parameter edit goes to Parameters).
- **Speak** writes the model to bytes when Speak is pressed (on the GUI thread, so later edits cannot reach the running job) and drops results for a model that is no longer open.
- **Left for later** (recorded so they are not lost): hearing A in A/B (only B autoplays), a common dB scale for the A/B spectrograms, killing `ivxsay` on close, computing the spectrogram off the GUI thread, a message when Qt Multimedia is missing, the wheel over the signal view, a time ruler, and showing the lexicon in national characters (the transliteration table looks indexed by a DOS code page — Swedish 0x84 ä → `{`, 0x8E Ä → `[` — which needs checking against `tp_xlat_char` first).

## File structure

| File | Task | Responsibility |
|---|---|---|
| `editor/ivxedit/checks.py` | 1 | + warnings, jump targets, the new checks; never raises |
| `editor/ivxedit/writer.py` | 1 | save refuses on errors only; file mode, fsync |
| `editor/ivxedit/speak.py` | 1 | `model_bytes`: a model's bytes to speak, refused on errors |
| `editor/tests/test_checks.py`, `test_save.py`, `test_speak.py` | 1 | the new checks and save behaviour |
| `editor/ivxedit/edits.py` | 2 | one function per kind of edit → `Edit`; `EditError` |
| `editor/tests/test_edits.py` | 2 | every edit, its refusals, exact undo on all 12 files |
| `editor/ivxedit/info.py` | 3 | transliterations without NUL padding |
| `editor/ivxedit/gui/views.py` | 3 | editable tables (`RowsModel`), header form, lexicon view with Add/Remove |
| `editor/ivxedit/gui/phonemeview.py` | 3 | editable name, bits, items |
| `editor/ivxedit/gui/speakpanel.py` | 3 | speaks a snapshot; drops results for a closed file |
| `editor/ivxedit/gui/mainwindow.py` | 3 | undo stack, Edit menu, Save/Save As, Problems dock, refresh after edits |
| `editor/tests/test_editing.py`, `test_speakpanel.py`, `test_info.py` | 3 | editing through the window (spec: Testing 11) |
| `AGENTS.md`, the spec | 3 | step 4 as built |

---

### Task 1: Checks with warnings and targets; save and speak on errors only

**Files:**
- Modify (whole file): `editor/ivxedit/checks.py`
- Modify: `editor/ivxedit/writer.py`, `editor/ivxedit/speak.py`
- Test: `editor/tests/test_checks.py`, `editor/tests/test_save.py`, `editor/tests/test_speak.py`

**Interfaces:**
- Consumes (existing): `model.PhRef`, `VarItem`; `rules.REPL`, `REPL_NEXT`, `ph_positions`, `rep_split`; `writer.to_bytes`, `load`.
- Produces (Tasks 2 and 3 rely on these exact names):
  - `checks.Problem(where, message, target=None, warning=False)`; `str()` is `'where: message'`, or `'where: warning: message'`. Targets: `('phoneme', ph)`, `('rule', module, i)`, `('lexicon', i)`, `('header',)`, `('params',)`, `('chars',)`, `('phonemes',)`, `('rules',)`.
  - `checks.errors(problems) -> [Problem]` (the ones that stop a save), `checks.name_ok(name) -> bool`, `checks.ph_label(ph)`, `checks.rule_label(module, i, rule)` (unchanged), `checks.check(f) -> [Problem]`.
  - `writer.save(f, path)` raises `SaveRefused` for errors only.
  - `speak.model_bytes(f) -> bytes`, `SpeakError` when `check(f)` finds errors; `speak_model` uses it.

- [ ] **Step 1: Write the failing tests**

In `editor/tests/test_checks.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 2, replace

```python

from ivxedit.checks import check
from ivxedit.model import PNAMES, ParamItem, PhRef, Phoneme, load
from ivxedit.rules import REPL, Instr, ph_positions
from tests.util import LANGS, ivx_bytes
```

   with

```python

from ivxedit.checks import check, errors
from ivxedit.model import PNAMES, Lexicon, ParamItem, PhRef, Phoneme, VarItem, load
from ivxedit.rules import REPL, Instr, Rule, ph_positions
from tests.util import LANGS, ivx_bytes
```

2. Near line 73, replace

```python
        size = lambda: 2 + sum(len(p.to_bytes()) for p in f.phonemes)
        while size() + 12 <= 0x8000:
            f.phonemes.append(Phoneme(b'', 0, 0, []))
        self.assertEqual(check(f), [])
        f.phonemes.append(Phoneme(b'', 0, 0, []))
        self.assertIn('inventory', [p.where for p in check(f)])
```

   with

```python
        size = lambda: 2 + sum(len(p.to_bytes()) for p in f.phonemes)
        new = lambda: Phoneme(bytes([1 + len(f.phonemes) // 250, 1 + len(f.phonemes) % 250]), 0, 0, [])
        while size() + 12 <= 0x8000:
            f.phonemes.append(new())
        self.assertEqual(check(f), [])
        f.phonemes.append(new())
        self.assertIn('inventory', [p.where for p in check(f)])
```

3. Near line 103, replace

```python

if __name__ == '__main__':
```

   with

```python

    def test_names(self):
        f = sw()
        a = next(p for p in f.phonemes if p.name == b'A:')
        for bad in (b'\0\0', b'A\0', b'ABC', 'A:'):
            a.name = bad
            with self.subTest(name=bad):
                self.assertEqual([(p.target, p.warning) for p in check(f)], [(('phoneme', a), False)])
        e = next(p for p in f.phonemes if p.name == b'E:')
        a.name = b'E:'                          # a duplicate: the warning is on the later one
        probs = check(f)
        self.assertEqual([(p.target, p.warning) for p in probs], [(('phoneme', e), True)])
        self.assertIn("also phoneme %d's" % f.phonemes.index(a), probs[0].message)
        self.assertEqual(errors(probs), [])

    def test_build_tag(self):
        f = sw()
        f.head = b'XX' + f.head[2:]
        self.assertEqual([p.target for p in check(f)], [('header',)])

    def test_phoneme_id_past_0x7fff(self):
        f = sw()
        ins = f.modules['M0'].rules[0].instrs[0]                 # SEEK +
        ins.args[0] = PhRef(ins.args[0].ph, 0x8000)
        self.assertEqual([p.target for p in check(f)], [('rule', 'M0', 0)])
        self.assertIn('signed 16-bit', check(f)[0].message)

    def test_rule_length(self):
        f = sw()
        f.modules['M1'].rules[3].instrs += [Instr(0x10, [5])] * 0x3FFF
        self.assertEqual([p.target for p in check(f)], [('rule', 'M1', 3)])

    def test_operand_range(self):
        f = sw()
        f.modules['M2'].rules[0].instrs.append(Instr(0x10, [0x10000]))
        self.assertEqual([p.message for p in check(f)], ['an operand does not fit 16 bits'])

    def test_table_sizes(self):
        for attr, value in (('class_lo', bytes(0x7F)), ('class_lo', bytearray(0x80)),
                            ('xlat_hi', bytes(0x101)), ('class_hi', list(bytes(0x80)))):
            f = sw()
            setattr(f, attr, value)
            with self.subTest(attr=attr, value=type(value)):
                self.assertEqual([p.target for p in check(f)], [('chars',)])
        f = sw()
        f.tables[2] = f.tables[2][:31]
        self.assertEqual([p.target for p in check(f)], [('params',)])
        f = sw()
        f.tables[1][5] = -1
        self.assertEqual([p.target for p in check(f)], [('params',)])

    def test_check_never_raises(self):
        f = sw()
        f.phonemes[1].name = None
        f.phonemes[2].items.append('junk')
        f.phonemes[3].items[0] = VarItem(0x90, 'x')
        f.lexicon.entries[5] = ('A', 'B')
        f.modules['M0'].rules[0].instrs.append(Instr(None, []))
        probs = check(f)
        self.assertEqual({p.target for p in probs if p.target[0] != 'rule'},
                         {('phoneme', f.phonemes[k]) for k in (1, 2, 3)} | {('lexicon', 5)})

    def test_targets(self):
        f = sw()
        f.lexicon.entries[1], f.lexicon.entries[2] = f.lexicon.entries[2], f.lexicon.entries[1]
        del f.modules['M5']
        f.phonemes.append(f.phonemes.pop(0))
        self.assertEqual(sorted(p.target for p in check(f)), [('lexicon', 2), ('phonemes',), ('rules',)])

if __name__ == '__main__':
```


In `editor/tests/test_save.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 1, replace

```python
import tempfile
```

   with

```python
import os
import tempfile
```

2. Near line 55, replace

```python

if __name__ == '__main__':
```

   with

```python

    def test_a_warning_does_not_stop_a_save(self):
        f = load(ivx_bytes('sw'))
        f.phonemes[2].name = f.phonemes[1].name
        self.assertTrue(check(f) and all(p.warning for p in check(f)))
        save(f, self.dir / 'x.ivx')
        self.assertEqual((self.dir / 'x.ivx').read_bytes(), to_bytes(f))

    @unittest.skipUnless(os.name == 'posix', 'permission bits are POSIX')
    def test_saved_file_mode(self):
        out = self.dir / 'sw.ivx'
        umask = os.umask(0o022)
        try:
            save(load(ivx_bytes('sw')), out)
            self.assertEqual(out.stat().st_mode & 0o777, 0o644)
            out.chmod(0o640)
            save(load(ivx_bytes('sw')), out)
            self.assertEqual(out.stat().st_mode & 0o777, 0o640)
        finally:
            os.umask(umask)

if __name__ == '__main__':
```


In `editor/tests/test_speak.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 11, replace

```python
from ivxedit.model import load
from ivxedit.speak import (Event, Options, SpeakError, find_ivxsay, parse_events, read_wav,
                           speak_bytes, speak_model)
from tests.util import LANGS, ivx_bytes, ivx_path, ivxsay, lang_texts
```

   with

```python
from ivxedit.model import load
from ivxedit.speak import (Event, Options, SpeakError, find_ivxsay, model_bytes, parse_events,
                           read_wav, speak_bytes, speak_model)
from ivxedit.writer import to_bytes
from tests.util import LANGS, ivx_bytes, ivx_path, ivxsay, lang_texts
```

2. Near line 65, replace

```python
            speak_model(f, 'God morgon.', exe=Path('never-run'))

```

   with

```python
            speak_model(f, 'God morgon.', exe=Path('never-run'))

    def test_model_bytes(self):
        f = load(ivx_bytes('sw'))
        f.phonemes[2].name = f.phonemes[1].name                # a warning only: still spoken
        self.assertEqual(model_bytes(f), to_bytes(f))
        del f.modules['M0']
        with self.assertRaisesRegex(SpeakError, 'M0'):
            model_bytes(f)

```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[cs]*.py"`
Expected: an ImportError (`errors` from `ivxedit.checks`, `model_bytes` from `ivxedit.speak`).

- [ ] **Step 3: Implement**

Replace the whole of `editor/ivxedit/checks.py` with:

```python
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
        if name_ok(name) and not ph.is_defaults:
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
```


In `editor/ivxedit/writer.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 8, replace

```python
from .binfmt import INV, SLOTS, TABLES, IvxFormatError, align16, pack_words
from .checks import Problem, check
from .model import MODULE_DIR, load
```

   with

```python
from .binfmt import INV, SLOTS, TABLES, IvxFormatError, align16, pack_words
from .checks import Problem, check, errors
from .model import MODULE_DIR, load
```

2. Near line 77, replace

```python

def save(f, path):
    """check f, then write it to path through a temporary file in the same folder; an
    existing file is kept as <name>.bak"""
    path = Path(path).resolve()
    if BIN in path.parents:
        raise PermissionError('%s is in bin/, which holds the shipped files' % path)
    problems = check(f)
    if problems:
```

   with

```python

def _mode(path):
    """the permission bits for a saved file: the existing file's, else 0666 less the umask"""
    if path.exists():
        return path.stat().st_mode & 0o777
    umask = os.umask(0)
    os.umask(umask)
    return 0o666 & ~umask


def save(f, path):
    """check f, then write it to path through a temporary file in the same folder; an
    existing file is kept as <name>.bak. Warnings do not stop a save."""
    path = Path(path).resolve()
    if BIN in path.parents:
        raise PermissionError('%s is in bin/, which holds the shipped files' % path)
    problems = errors(check(f))
    if problems:
```

3. Near line 106, replace

```python
            out.write(data)
        if path.exists():
```

   with

```python
            out.write(data)
            out.flush()
            os.fsync(out.fileno())
        os.chmod(tmp, _mode(path))              # mkstemp makes the file 0600
        if path.exists():
```


In `editor/ivxedit/speak.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 11, replace

```python

from .checks import check
from .writer import to_bytes
```

   with

```python

from .checks import check, errors
from .writer import to_bytes
```

2. Near line 123, replace

```python

def speak_model(f, text, opts=None, exe=None):
    """speak `text` with the model f; refuses (SpeakError) when check(f) finds problems"""
    problems = check(f)
    if problems:
        raise SpeakError('the file has %d problem(s): %s' % (
            len(problems), '; '.join(str(p) for p in problems[:3])))
    return speak_bytes(to_bytes(f), text, opts, exe)
```

   with

```python

def model_bytes(f):
    """the .ivx bytes to speak f with; SpeakError when check(f) finds errors"""
    problems = errors(check(f))
    if problems:
        raise SpeakError('the file has %d problem(s): %s' % (
            len(problems), '; '.join(str(p) for p in problems[:3])))
    return to_bytes(f)


def speak_model(f, text, opts=None, exe=None):
    """speak `text` with the model f; refuses (SpeakError) when check(f) finds errors"""
    return speak_bytes(model_bytes(f), text, opts, exe)
```


- [ ] **Step 4: Run the tests to see them pass**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[cs]*.py"`
Expected: OK (`test_saved_file_mode` skips on Windows: permission bits are POSIX).
Then the whole suite once: `python -m unittest discover -s editor/tests -t editor` → OK.

- [ ] **Step 5: Commit**

```bash
git add editor/ivxedit/checks.py editor/ivxedit/writer.py editor/ivxedit/speak.py editor/tests/test_checks.py editor/tests/test_save.py editor/tests/test_speak.py
git commit -m "ivxedit checks: warnings and jump targets, name/build-tag/id/table checks, never raises; save and speak stop on errors only" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The edits

**Files:**
- Create: `editor/ivxedit/edits.py`
- Test: `editor/tests/test_edits.py`

**Interfaces:**
- Consumes (Task 1 and existing): `checks.ph_label`, `checks.rule_label`, `checks.check`, `checks.errors`; `model.PNAMES`, `ParamItem`, `Phoneme`, `VarItem`, `IvxFile.ntotal`, `IvxFile.param(i)`; `xref.phoneme_uses(f)`; `writer.to_bytes`; `info.header_fields`.
- Produces (Task 3 relies on these exact names):
  - `EditError(ValueError)`; `Edit(obj, attr, value, text, target)` with `.apply()`, `.revert()`, `.text` (the undo text), `.target` (as `Problem.target`).
  - Each function returns an `Edit`, or `None` when nothing changes; the lexicon's `set_entry`/`add_entry` return `(Edit or None, index of the entry)`:
    `set_header_text(f, field, text)` (field `'version'`, `'copyright'`, `'lexicon name'`), `set_header_flags(f, text)`, `set_param(f, i, column, text)` (column in `PARAM_COLUMNS = ('A1', 'A2', 'A3')`), `set_class(f, ch, text)`, `set_xlat(f, ch, text)`, `rename_phoneme(ph, text)`, `set_bit(ph, which, bit, on)`, `set_cell(ph, row, k, text)`, `add_item(ph, iid)`, `remove_item(ph, row)`, `free_name(f) -> str`, `add_phoneme(f, index, text, model=None)`, `delete_phoneme(f, ph)`, `set_entry(lex, i, word, trans)`, `add_entry(lex, word, trans)`, `remove_entry(lex, i)`; `number(text, lo, hi, what) -> int`.

- [ ] **Step 1: Write the failing tests**

Create `editor/tests/test_edits.py`:

```python
"""Voice-data edits (spec: GUI, step 4): each edit changes what it says, undo restores the
bytes exactly, and the edited file saves and loads again."""
import struct
import unittest

from ivxedit import edits
from ivxedit.checks import check, errors
from ivxedit.edits import EditError
from ivxedit.info import header_fields
from ivxedit.model import PNAMES, ParamItem, VarItem, load
from ivxedit.writer import to_bytes
from ivxedit.xref import phoneme_uses
from tests.util import LANGS, ivx_bytes


def sw():
    return load(ivx_bytes('sw'))


def ph(f, name):
    return next(p for p in f.phonemes if p.name == name)


def unused(f):
    """a phoneme no rule refers to"""
    uses = phoneme_uses(f)
    return next(p for p in f.phonemes[1:] if p not in uses)


def script(f):
    """one edit of each kind that applies to f, as functions of the model"""
    out = [lambda: edits.set_header_text(f, 'version', 'edited'),
           lambda: edits.set_header_text(f, 'copyright', ''),
           lambda: edits.set_header_text(f, 'lexicon name', 'X.IVX'),
           lambda: edits.set_header_flags(f, '0x1234'),
           lambda: edits.set_param(f, PNAMES.index('F1'), 'A1', '19'),
           lambda: edits.set_param(f, PNAMES.index('F2'), 'A2', '7'),
           lambda: edits.set_param(f, PNAMES.index('F0'), 'A3', '-12'),
           lambda: edits.set_class(f, ord('#'), '-3'),
           lambda: edits.rename_phoneme(f.phonemes[1], 'Q'),
           lambda: edits.set_bit(f.phonemes[2], 'feat', 31, not f.phonemes[2].feat >> 31),
           lambda: edits.set_bit(f.phonemes[2], 'mask', 0, not f.phonemes[2].mask & 1),
           lambda: edits.add_item(f.phonemes[3], 0x9F if all(
               getattr(it, 'vid', None) != 0x9F for it in f.phonemes[3].items) else 0x9E),
           lambda: edits.set_cell(f.phonemes[3], 0, 0, '-7' if isinstance(f.phonemes[3].items[0], VarItem)
                                  else '9@1'),
           lambda: edits.remove_item(f.phonemes[4], 0),
           lambda: edits.add_phoneme(f, 1, edits.free_name(f)),
           lambda: edits.add_phoneme(f, len(f.phonemes), 'Z9', f.phonemes[5]),
           lambda: edits.delete_phoneme(f, unused(f))]
    if f.class_hi is not None:
        out += [lambda: edits.set_class(f, 0xC4, '5'), lambda: edits.set_xlat(f, 0xC4, 'A:')]
    if f.lexicon is not None:
        out += [lambda: edits.add_entry(f.lexicon, 'ZZTOP', "S'ITS")[0],
                lambda: edits.set_entry(f.lexicon, 5, 'AAA', 'A')[0],
                lambda: edits.remove_entry(f.lexicon, 1)]
    return out


class EditTest(unittest.TestCase):
    def test_undo_restores_every_file(self):
        """every edit, applied in order and then undone in reverse, gives the original bytes"""
        for lang in LANGS:
            with self.subTest(lang=lang):
                data = ivx_bytes(lang)
                f = load(data)
                done = []
                for make in script(f):
                    e = make()
                    self.assertIsNotNone(e)
                    e.apply()
                    done.append(e)
                self.assertEqual(errors(check(f)), [])
                edited = to_bytes(f)
                self.assertNotEqual(edited, data)
                self.assertEqual(to_bytes(load(edited)), edited)        # saves and loads again
                for e in reversed(done):
                    e.revert()
                self.assertEqual(to_bytes(f), data)
                for e in done:                                          # redo
                    e.apply()
                self.assertEqual(to_bytes(f), edited)

    def test_header(self):
        f = sw()
        edits.set_header_text(f, 'version', 'v2 ÅÄÖ').apply()
        edits.set_header_flags(f, '0x00010002').apply()
        fields = dict(header_fields(load(to_bytes(f))))
        self.assertEqual(fields['Version'], 'v2 ÅÄÖ')
        self.assertEqual(fields['Flags'], '0x00010002')
        self.assertEqual(f.header[0x80 + 6:0xC0], bytes(0x3A))
        for field, text in (('version', 'x' * 64), ('lexicon name', 'ABCDEFGHIJKL'), ('copyright', 'a\0b'),
                            ('version', '→')):
            with self.subTest(field=field, text=text), self.assertRaises(EditError):
                edits.set_header_text(f, field, text)
        self.assertIsNone(edits.set_header_text(f, 'version', 'v2 ÅÄÖ'))

    def test_params(self):
        f = sw()
        i = PNAMES.index('F0')
        for column, text, want in (('A1', '18: approach → freq', 72), ('A1', '0', 0), ('A2', '255', 255),
                                   ('A3', '-5', -5), ('A3', '0x10', 16)):
            edits.set_param(f, i, column, text).apply()
            self.assertEqual(f.param(i)[1 + edits.PARAM_COLUMNS.index(column)], want)
        for column, text in (('A1', '33'), ('A1', 'x'), ('A2', '256'), ('A2', '-1'), ('A3', '40000'),
                             ('A3', '²')):
            with self.subTest(column=column, text=text), self.assertRaises(EditError):
                edits.set_param(f, i, column, text)

    def test_characters(self):
        f = sw()
        edits.set_class(f, 0x41, '-6').apply()
        edits.set_class(f, 0xE5, '127').apply()
        edits.set_xlat(f, 0xE5, 'a').apply()
        g = load(to_bytes(f))
        self.assertEqual((g.class_lo[0x41], g.class_hi[0x65], g.xlat_hi[0xCA:0xCC]), (0xFA, 127, b'a\0'))
        for fn, args in ((edits.set_class, (0x41, '128')), (edits.set_xlat, (0x41, 'a')),
                         (edits.set_xlat, (0xE5, 'abc'))):
            with self.subTest(args=args), self.assertRaises(EditError):
                fn(f, *args)
        it = load(ivx_bytes('it'))                                   # no H[14]
        with self.assertRaises(EditError):
            edits.set_class(it, 0xC4, '1')
        with self.assertRaises(EditError):
            edits.set_xlat(it, 0xC4, 'a')

    def test_phoneme_name(self):
        f = sw()
        a = ph(f, b'A:')
        edits.rename_phoneme(a, 'Ä').apply()
        self.assertEqual(a.name, b'\0\xc4')
        for text in ('', 'ABC', 'a\0', '→'):
            with self.subTest(text=text), self.assertRaises(EditError):
                edits.rename_phoneme(a, text)
        self.assertIsNone(edits.rename_phoneme(a, 'Ä'))

    def test_points_and_variables(self):
        f = sw()
        a = ph(f, b'A:')
        row = next(k for k, it in enumerate(a.items) if isinstance(it, ParamItem) and len(it.points) == 2)
        before = list(a.items[row].points)
        e = edits.set_cell(a, row, 1, '200@3')
        e.apply()
        self.assertEqual(a.items[row].points, [before[0], (3, 200)])
        self.assertEqual(e.old[row].points, before)                   # the old list is untouched
        edits.set_cell(a, row, 2, '201@4').apply()                   # appends
        edits.set_cell(a, row, 0, '').apply()                        # removes point 0
        self.assertEqual(a.items[row].points, [(3, 200), (4, 201)])
        self.assertIsNone(edits.set_cell(a, row, 0, '200@3'))
        for k, text in ((0, '256@0'), (0, '1@128'), (0, '1'), (5, '1@1')):
            with self.subTest(text=text), self.assertRaises(EditError):
                edits.set_cell(a, row, k, text)
        var = next(k for k, it in enumerate(a.items) if isinstance(it, VarItem))
        edits.set_cell(a, var, 0, '-300').apply()
        self.assertEqual(a.items[var].value, -300)
        with self.assertRaises(EditError):
            edits.set_cell(a, var, 0, '40000')
        self.assertEqual(errors(check(f)), [])

    def test_items_keep_their_order(self):
        f = sw()
        a = ph(f, b'A:')
        have = {getattr(it, 'vid', getattr(it, 'pid', None)) for it in a.items}
        new = [iid for iid in (PNAMES.index('XX'), PNAMES.index('B4'), 0x81, 0x90, 0xFE, 100)
               if iid not in have]
        self.assertGreaterEqual(len(new), 4)
        for iid in new:
            edits.add_item(a, iid).apply()
        self.assertEqual(errors(check(f)), [])
        with self.assertRaises(EditError):
            edits.add_item(a, new[0])
        n = len(a.items)
        edits.remove_item(a, 0).apply()
        self.assertEqual(len(a.items), n - 1)
        one = next(k for k, it in enumerate(a.items) if isinstance(it, ParamItem) and len(it.points) == 1)
        with self.assertRaises(EditError):
            edits.set_cell(a, one, 0, '')

    def test_inventory(self):
        f = sw()
        data = to_bytes(f)
        name = edits.free_name(f)
        e = edits.add_phoneme(f, 1, name)
        e.apply()
        new = f.phonemes[1]
        self.assertEqual((new.name, e.target), (name.encode(), ('phoneme', new)))
        g = load(to_bytes(f))                       # every id moved; the rules still name the same phonemes
        self.assertEqual([r.label for r in g.modules['M0'].rules], [r.label for r in f.modules['M0'].rules])
        self.assertEqual(len(g.phonemes), len(f.phonemes))
        edits.delete_phoneme(f, new).apply()
        self.assertEqual(to_bytes(f), data)
        with self.assertRaises(EditError) as cm:
            edits.delete_phoneme(f, ph(f, b'A:'))
        self.assertIn('used by', str(cm.exception))
        with self.assertRaises(EditError):
            edits.delete_phoneme(f, f.phonemes[0])
        with self.assertRaises(EditError):
            edits.add_phoneme(f, 0, 'X')
        copy = edits.add_phoneme(f, 3, 'Z9', ph(f, b'A:'))
        copy.apply()
        self.assertEqual(f.phonemes[3].to_bytes()[2:], ph(f, b'A:').to_bytes()[2:].replace(b'A:', b'Z9', 1))

    def test_lexicon_keeps_its_order(self):
        f = sw()
        lex = f.lexicon
        e, k = edits.add_entry(lex, 'BORT', 'B\'ORT')                  # a third BORT goes after the two
        e.apply()
        words = [o for o, _ in lex.entries]
        self.assertEqual(words[k - 2:k + 1], [b'BORT'] * 3)
        self.assertEqual(lex.entries[k], (b'BORT', b"B'ORT"))
        e, k = edits.set_entry(lex, 5, 'ZZZZ', 'Z')
        e.apply()
        self.assertEqual(lex.entries[k], (b'ZZZZ', b'Z'))
        self.assertTrue(lex.entries[k - 1][0] <= b'ZZZZ' < lex.entries[k + 1][0])
        e, j = edits.set_entry(lex, k, 'ZZZZ', 'Y')                   # same headword: stays put
        e.apply()
        self.assertEqual(j, k)
        self.assertEqual(edits.set_entry(lex, k, 'ZZZZ', 'Y'), (None, k))
        edits.remove_entry(lex, 0).apply()
        self.assertEqual(errors(check(f)), [])
        self.assertEqual(load(to_bytes(f)).lexicon.entries, lex.entries)    # through the writer
        for word, trans in (('', 'X'), ('A\x7f', 'X'), ('A', 'B\0'), ('→', 'X')):
            with self.subTest(word=word), self.assertRaises(EditError):
                edits.add_entry(lex, word, trans)

    def test_numbers(self):
        for text, want in (('5', 5), (' -5 ', -5), ('0x1F', 31), ('-0x10', -16), ('+7', 7)):
            self.assertEqual(edits.number(text, -100, 100, 'n'), want)
        for text in ('', '5a', '0x', '²', '1' * 11, '5.0', '0x123456789'):
            with self.subTest(text=text), self.assertRaises(EditError):
                edits.number(text, -10 ** 12, 10 ** 12, 'n')


if __name__ == '__main__':
    unittest.main()
```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest discover -s editor/tests -t editor -p test_edits.py`
Expected: ImportError (no `ivxedit.edits`).

- [ ] **Step 3: Implement**

Create `editor/ivxedit/edits.py`:

```python
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
        m = re.match(r'\s*([0-9]{1,3})', text)
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
```


- [ ] **Step 4: Run the tests to see them pass**

Run: `python -m unittest discover -s editor/tests -t editor -p test_edits.py`
Expected: OK, 10 tests. Then the whole suite once → OK.

- [ ] **Step 5: Commit**

```bash
git add editor/ivxedit/edits.py editor/tests/test_edits.py
git commit -m "ivxedit: voice-data edits (edits.py): header, parameters, characters, phonemes, lexicon; exact undo" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Editing in the window

**Files:**
- Modify: `editor/ivxedit/info.py`, `editor/ivxedit/gui/speakpanel.py`
- Modify (whole files): `editor/ivxedit/gui/views.py`, `editor/ivxedit/gui/phonemeview.py`, `editor/ivxedit/gui/mainwindow.py`
- Create: `editor/tests/test_editing.py`
- Test: `editor/tests/test_speakpanel.py`, `editor/tests/test_info.py`
- Docs: `AGENTS.md`, `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`

**Interfaces:**
- Consumes (Tasks 1 and 2): every `edits` function above, `EditError`, `Edit.target`; `checks.check`, `checks.errors`, `Problem.target`, `Problem.warning`; `writer.save`, `SaveRefused`, `BIN`; `speak.model_bytes`.
- Produces:
  - `MainWindow.undo` (`QUndoStack`), `try_edit(fn, *args)` (→ the extra value of `(Edit, extra)` results, else `True`; `False` when refused), `save()`, `save_as()`, `write(path)`, `maybe_save()`, `show_target(target)`, `show_problems(on=True)`, `refresh()`, `problem_dock`, `problem_list`, `problems` (the status-bar button; its text is `Problems (n)` or `Problems (n, k warnings)`); the dialogs `ask_save_path()`, `ask_unsaved()`, `ask_text(title, label)`, `warn(title, text)` are methods so tests can replace them.
  - `EditCommand(edit, window)`, a `QUndoCommand`.
  - `views.RowsModel(headers, rows, row_data=None, editable=None, on_edit=None)` with `update(rows, row_data=None)`; `TableView.set_rows(title, headers, rows, row_data=None, editable=None, on_edit=None, actions=())`, `TableView.buttons`, `current()`, `select(row)`; `HeaderView(on_edit)` with `set_fields([(label, text, key or None)])` and `widgets`; `LexiconView(on_edit, on_add, on_remove)` with `set_lexicon`, `reset`, `select(i)`, `current()`, `add`, `remove`; `views.plain(label)`.
  - `PhonemeView(on_rule, edit)` with `name`, `bits`, `items`, `add`, `remove`, `ask_item(names)`.

- [ ] **Step 1: Write the failing tests**

Create `editor/tests/test_editing.py`:

```python
"""Editing in the window, offscreen (spec: GUI, step 4; Testing, 11)."""
import tempfile
import unittest
from pathlib import Path

from ivxedit.model import PNAMES, load
from ivxedit.writer import to_bytes
from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import ivx_bytes, ivx_path


def settle():
    """run the queued view refreshes"""
    for _ in range(3):
        app().processEvents()


@needs_qt
class EditingTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.win = MainWindow(temp_settings(self))
        self.win.warn = lambda title, text: self.warnings.append(text)
        self.warnings = []
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.addCleanup(self._close)

    def _close(self):
        self.win.undo.setClean()                     # no unsaved-changes question at the end
        self.win.close()

    def node(self, kind):
        from ivxedit.gui.mainwindow import ROLE
        item = next(t for t in self.win.items() if t.data(0, ROLE)[0] == kind)
        self.win.tree.setCurrentItem(item)
        return item

    def cell(self, view_model, row, col, text):
        from PySide6.QtCore import Qt
        self.assertTrue(view_model.flags(view_model.index(row, col)) & Qt.ItemFlag.ItemIsEditable)
        view_model.setData(view_model.index(row, col), text)
        settle()

    def edit_everything(self):
        """one edit through each editor of the window"""
        w = self.win
        f = w.f
        self.node('header')
        field = w.header_view.widgets['Version']
        field.setText('edited in the window')
        field.editingFinished.emit()
        settle()
        self.node('params')
        self.cell(w.table_view.model, PNAMES.index('F2'), 5, '36')
        self.node('chars')
        self.cell(w.table_view.model, ord('#'), 2, '-3')
        self.cell(w.table_view.model, 0xC4, 4, 'AE')
        a = next(p for p in f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        pv = w.phoneme_view
        pv.name.setText('A2')
        pv.name.editingFinished.emit()
        settle()
        pv.bits.cellClicked.emit(0, 0)                         # feat bit 31
        settle()
        row = next(r for r in range(pv.items.rowCount()) if pv.items.item(r, 0).text() == 'F1')
        pv.items.item(row, 1).setText('150@-1')
        settle()
        pv.ask_item = lambda names: ('v9f', True)
        pv.add.click()
        settle()
        pv.items.setCurrentCell(0, 1)
        pv.remove.click()
        settle()
        self.node('phonemes')
        tv = w.table_view
        tv.select(3)
        tv.buttons['Copy'].click()                             # a copy after phoneme 3, then shown
        settle()
        self.node('phonemes')
        tv.select(len(f.phonemes) - 1)
        tv.buttons['New'].click()
        settle()
        self.node('lexicon')
        lv = w.lexicon_view
        self.cell(lv.model, 5, 2, "N'YTT")
        w.ask_text = lambda title, label: ('ÖVERALLT', True)
        lv.add.click()
        settle()
        lv.select(1)
        lv.remove.click()
        settle()

    def test_undo_restores_the_file(self):
        """spec Testing 11: a script of edits, then undo of all of them, saves byte-identical"""
        w = self.win
        data = ivx_bytes('sw')
        with SlotErrors() as errors:
            w.open_file(ivx_path('sw'))
            self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx')
            self.edit_everything()
            self.assertEqual(errors, [])
        self.assertEqual(w.undo.count(), 14)
        self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx *')
        edited = to_bytes(w.f)
        g = load(edited)
        self.assertEqual(g.version, 'edited in the window')
        self.assertEqual(sum(p.name == b'A2' for p in g.phonemes), 1)
        self.assertEqual(len(g.phonemes), len(load(data).phonemes) + 2)
        self.assertIn((b'\xd6VERALLT', b'\xd6VERALLT'), g.lexicon.entries)
        with SlotErrors() as errors:
            while w.undo.canUndo():
                w.undo.undo()
                settle()
            self.assertEqual(to_bytes(w.f), data)
            self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx')
            while w.undo.canRedo():
                w.undo.redo()
                settle()
            self.assertEqual(errors, [])
        self.assertEqual(to_bytes(w.f), edited)
        out = Path(self.tmp.name) / 'sw.ivx'
        w.ask_save_path = lambda: str(out)
        self.assertTrue(w.save())                              # bin/ is never written: Save As
        self.assertEqual(out.read_bytes(), edited)
        self.assertEqual((w.path, w.windowTitle()), (out, 'IVX Editor — sw.ivx'))

    def test_undo_shows_what_it_changed(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.node('params')
        self.cell(w.table_view.model, 0, 4, '9')
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        w.undo.undo()
        settle()
        self.assertEqual(w.tree.currentItem().text(0), 'Parameters (29)')
        self.assertEqual(w.table_view.model.rows[0][4], str(load(ivx_bytes('sw')).param(0)[2]))

    def test_rename_reaches_the_rules(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        plus = next(p for p in w.f.phonemes if p.name == b'\0+')
        w.show_rule('M0', 0)
        self.assertEqual(w.rule_views['M0'].toPlainText().splitlines()[0], '    0  #32 + B → S')
        w.show_phoneme(plus)
        w.phoneme_view.name.setText('PL')
        w.phoneme_view.name.editingFinished.emit()
        settle()
        self.assertEqual(w.tree.currentItem().text(0), 'PL')
        w.show_rule('M0', 0)
        self.assertEqual(w.rule_views['M0'].toPlainText().splitlines()[0], '    0  #32 PL B → S')
        w.undo.undo()
        settle()
        w.show_rule('M0', 0)
        self.assertEqual(w.rule_views['M0'].toPlainText().splitlines()[0], '    0  #32 + B → S')

    def test_refused_edits(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        w.phoneme_view.name.setText('ABC')
        w.phoneme_view.name.editingFinished.emit()
        settle()
        self.assertEqual(w.phoneme_view.name.text(), 'A:')
        self.assertIn('one or two characters', w.statusBar().currentMessage())
        w.phoneme_view.items.item(3, 1).setText('nonsense')
        settle()
        self.assertNotEqual(w.phoneme_view.items.item(3, 1).text(), 'nonsense')
        self.node('phonemes')
        w.table_view.select(w.f.phonemes.index(a))
        w.table_view.buttons['Delete'].click()
        settle()
        self.assertIn('used by', w.statusBar().currentMessage())
        self.node('params')
        self.cell(w.table_view.model, 0, 3, '40')
        self.assertIn('A1 type must be 0..32', w.statusBar().currentMessage())
        self.assertEqual(w.undo.count(), 0)
        self.assertEqual(w.windowTitle(), 'IVX Editor — swrules.ivx')

    def test_problems_panel(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.assertEqual(w.problems.text(), 'Problems (0)')
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        w.phoneme_view.name.setText('E:')                      # a duplicate name: a warning
        w.phoneme_view.name.editingFinished.emit()
        settle()
        self.assertEqual(w.problems.text(), 'Problems (1, 1 warning)')
        e = next(p for p in w.f.phonemes if p.name == b'E:' and p is not a)
        self.node('header')
        w.problem_list.itemActivated.emit(w.problem_list.item(0))
        self.assertIs(w.tree.currentItem(), w.ph_items[e])
        a.items[0].value = 70000                               # an error the editors cannot make
        w._changed()
        self.assertEqual(w.problems.text(), 'Problems (2, 1 warning)')
        out = Path(self.tmp.name) / 'x.ivx'
        self.assertFalse(w.write(out))
        self.assertFalse(out.exists())
        self.assertIn('Not saved: 1 problem', self.warnings[0])
        self.assertFalse(w.problem_dock.isHidden())

    def test_unsaved_changes(self):
        from PySide6.QtWidgets import QMessageBox
        b = QMessageBox.StandardButton
        w = self.win
        w.open_file(ivx_path('sw'))
        self.assertTrue(w.maybe_save())                        # nothing to save
        self.node('params')
        self.cell(w.table_view.model, 0, 4, '9')
        w.ask_unsaved = lambda: b.Cancel
        self.assertFalse(w.maybe_save())
        self.assertFalse(w.close())                            # Cancel keeps the window open
        out = Path(self.tmp.name) / 'sw.ivx'
        w.ask_save_path = lambda: str(out)
        w.ask_unsaved = lambda: b.Save
        self.assertTrue(w.maybe_save())
        self.assertTrue(out.exists() and w.undo.isClean())
        self.cell(w.table_view.model, 0, 4, '8')
        w.ask_unsaved = lambda: b.Discard
        self.assertTrue(w.maybe_save())
        w.ask_save_path = lambda: ''
        self.assertFalse(w.save_as())                          # the dialog was cancelled

    def test_lexicon_selection_follows_the_entry(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.node('lexicon')
        lv = w.lexicon_view
        lv.filter.setText('BORT')
        settle()
        row = lv.proxy.mapToSource(lv.proxy.index(0, 1)).row()
        lv.proxy.setData(lv.proxy.index(0, 1), 'ZZBORT')
        settle()
        k = lv.current()
        self.assertEqual(w.f.lexicon.entries[k][0], b'ZZBORT')
        self.assertNotEqual(k, row)
        self.assertEqual(w.tree.currentItem().text(0), 'Lexicon (3370)')


    def test_typing_in_cells(self):
        """real editing through the cell editors, which the window refreshes after the edit"""
        from PySide6.QtCore import Qt
        from PySide6.QtTest import QTest
        from PySide6.QtWidgets import QLineEdit

        def type_into(view, text):
            editor = next(e for e in view.viewport().findChildren(QLineEdit) if e.isVisible())
            QTest.keyClicks(editor, text)
            QTest.keyClick(editor, Qt.Key.Key_Return)
            settle()
        w = self.win
        w.open_file(ivx_path('sw'))
        w.show()
        a = next(p for p in w.f.phonemes if p.name == b'A:')
        w.show_phoneme(a)
        items = w.phoneme_view.items
        row = next(r for r in range(items.rowCount()) if items.item(r, 0).text() == 'F1')
        k = len(a.items[row].points)
        with SlotErrors() as errors:
            items.setCurrentCell(row, 1 + k)                   # the empty cell after the last point
            items.editItem(items.item(row, 1 + k))
            type_into(items, '99@6')
            self.node('params')
            t = w.table_view.table
            t.setCurrentIndex(t.model().index(PNAMES.index('F0'), 4))
            t.edit(t.currentIndex())
            type_into(t, '7')
        self.assertEqual(errors, [])
        self.assertEqual(a.items[row].points[-1], (6, 99))
        self.assertEqual(items.item(row, 1 + k).text(), '99@6')
        self.assertEqual(w.f.param(PNAMES.index('F0'))[2], 7)
        self.assertEqual(w.undo.count(), 2)

    def test_buttons_belong_to_their_table(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        self.node('phonemes')
        self.assertEqual(list(w.table_view.buttons), ['New', 'Copy', 'Delete'])
        self.node('chars')
        self.assertEqual(w.table_view.buttons, {})

if __name__ == '__main__':
    unittest.main()
```


In `editor/tests/test_speakpanel.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 73, replace

```python
        self.panel.button.click()
        wait_for(self.panel)
        self.assertIn('M0', self.panel.status.text())

```

   with

```python
        self.panel.button.click()
        self.assertIsNone(self.panel.job)      # refused before a job starts
        self.assertIn('M0', self.panel.status.text())

    @unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
    def test_speaks_the_model_as_it_was_when_asked(self):
        self.settings.setValue('ivxsay', str(SAY))
        self.win.open_file(ivx_path('sw'))
        p = self.panel
        p.text.setText('God morgon.')
        p.button.click()
        del self.win.f.modules['M0']           # an edit while the job runs does not reach it
        wait_for(p)
        self.assertEqual([t for t, _ in p.results], ['Edited'])
        self.assertTrue(p.results[0][1].phonemes)

    @unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
    def test_results_for_a_closed_file_are_dropped(self):
        self.settings.setValue('ivxsay', str(SAY))
        self.win.open_file(ivx_path('sw'))
        p = self.panel
        p.text.setText('God morgon.')
        p.button.click()
        self.win.open_file(ivx_path('ge'))
        wait_for(p)
        self.assertEqual((p.results, p.status.text()), ([], ''))

```


In `editor/tests/test_info.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 53, replace

```python
        self.assertEqual(char_rows(am)[0xE5][2:], (None, None, None))   # am: built-in tables
        self.assertEqual(len(char_rows(ge)[0xC4][4]), 2)

```

   with

```python
        self.assertEqual(char_rows(am)[0xE5][2:], (None, None, None))   # am: built-in tables
        self.assertEqual([char_rows(ge)[ch][4] for ch in (0x80, 0xC4)], ['"C', ''])   # without NUL padding
        self.assertEqual(rows[0x84][4], '{')

```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[eis]*.py"`
Expected: errors and failures in `test_editing` (no `undo` on the window, no `buttons`), `test_speakpanel` (the refused model still starts a job) and `test_info` (`'{\x00'` instead of `'{'`).

- [ ] **Step 3: Implement**

In `editor/ivxedit/info.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 80, replace

```python
def char_rows(f):
    """the 256 rows of the Characters view: (byte, char, class code, flags, transliteration);
    code, flags and transliteration are None for 0x80-0xFF in files without H[14], which
    use the engine's built-in tables"""
    rows = []
```

   with

```python
def char_rows(f):
    """the 256 rows of the Characters view: (byte, char, class code, flags, transliteration
    without its NUL padding); code, flags and transliteration are None for 0x80-0xFF in
    files without H[14], which use the engine's built-in tables"""
    rows = []
```

2. Near line 91, replace

```python
            code = s8(f.class_hi[ch - 0x80])
            xl = f.xlat_hi[2 * (ch - 0x80):2 * (ch - 0x80) + 2].decode('latin-1')
        else:
```

   with

```python
            code = s8(f.class_hi[ch - 0x80])
            xl = f.xlat_hi[2 * (ch - 0x80):2 * (ch - 0x80) + 2].decode('latin-1').rstrip('\0')   # '{\0' = '{'
        else:
```


Replace the whole of `editor/ivxedit/gui/views.py` with:

```python
"""Views: header form, tables (parameters, characters, the phoneme and module summaries),
lexicon and text/hex. Editable cells and fields report edits through a callback; the
window turns them into undoable edits and refreshes the view."""
from PySide6.QtCore import QAbstractTableModel, QModelIndex, QSortFilterProxyModel, Qt
from PySide6.QtGui import QFontDatabase
from PySide6.QtWidgets import (QAbstractItemView, QFormLayout, QHBoxLayout, QHeaderView, QLabel,
                               QLineEdit, QPlainTextEdit, QPushButton, QTableView, QVBoxLayout,
                               QWidget)


def mono():
    return QFontDatabase.systemFont(QFontDatabase.SystemFont.FixedFont)


def compact(table):
    """monospaced, one text line per row, edited by double-click or typing (where a cell
    is editable at all)"""
    table.setEditTriggers(QAbstractItemView.EditTrigger.DoubleClicked
                          | QAbstractItemView.EditTrigger.EditKeyPressed
                          | QAbstractItemView.EditTrigger.AnyKeyPressed)
    table.setFont(mono())
    table.verticalHeader().setSectionResizeMode(QHeaderView.ResizeMode.Fixed)
    table.verticalHeader().setDefaultSectionSize(table.fontMetrics().height() + 6)


def plain(label):
    """a QLabel that shows its text as it is, never as rich text"""
    label.setTextFormat(Qt.TextFormat.PlainText)
    return label


class RowsModel(QAbstractTableModel):
    """a table of rows (lists of str); row_data[i] is kept for jumps. Cells for which
    editable(row, col) is true can be edited: on_edit(row, col, text) -> True if taken."""

    def __init__(self, headers, rows, row_data=None, editable=None, on_edit=None):
        super().__init__()
        self.headers, self.rows = headers, rows
        self.row_data = row_data if row_data is not None else [None] * len(rows)
        self.editable, self.on_edit = editable, on_edit

    def update(self, rows, row_data=None):
        """new contents; the view keeps its scroll position and current cell when the
        number of rows stays the same"""
        row_data = row_data if row_data is not None else [None] * len(rows)
        if len(rows) == len(self.rows):
            self.rows, self.row_data = rows, row_data
            if rows:
                self.dataChanged.emit(self.index(0, 0), self.index(len(rows) - 1, len(self.headers) - 1))
        else:
            self.beginResetModel()
            self.rows, self.row_data = rows, row_data
            self.endResetModel()

    def rowCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.rows)

    def columnCount(self, parent=QModelIndex()):
        return 0 if parent.isValid() else len(self.headers)

    def data(self, index, role=Qt.ItemDataRole.DisplayRole):
        if role in (Qt.ItemDataRole.DisplayRole, Qt.ItemDataRole.EditRole) and index.isValid():
            return self.rows[index.row()][index.column()]
        return None

    def flags(self, index):
        fl = super().flags(index)
        if self.editable is not None and index.isValid() and self.editable(index.row(), index.column()):
            fl |= Qt.ItemFlag.ItemIsEditable
        return fl

    def setData(self, index, value, role=Qt.ItemDataRole.EditRole):
        if role != Qt.ItemDataRole.EditRole or self.on_edit is None or not index.isValid():
            return False
        return bool(self.on_edit(index.row(), index.column(), str(value)))

    def headerData(self, section, orientation, role=Qt.ItemDataRole.DisplayRole):
        if role == Qt.ItemDataRole.DisplayRole and orientation == Qt.Orientation.Horizontal:
            return self.headers[section]
        return None


class TableView(QWidget):
    """a title, buttons and a table; activated(row_data) on double-click or Enter in a
    cell that is not editable"""

    def __init__(self, on_activate=None):
        super().__init__()
        self.title = plain(QLabel())
        self.table = QTableView()
        compact(self.table)
        self.table.setSelectionBehavior(QAbstractItemView.SelectionBehavior.SelectRows)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.on_activate = on_activate
        self.table.activated.connect(self._activated)
        self.bar = QHBoxLayout()
        self.bar.addStretch(1)
        self.buttons = {}
        lay = QVBoxLayout(self)
        top = QHBoxLayout()
        top.addWidget(self.title, 1)
        top.addLayout(self.bar)
        lay.addLayout(top)
        lay.addWidget(self.table)
        self.model = None

    def set_rows(self, title, headers, rows, row_data=None, editable=None, on_edit=None, actions=()):
        """actions: [(button text, callback(row_data of the current row, or None))]. The same
        table shown again (same title and headers) keeps its scroll position."""
        rows = [[str(c) for c in r] for r in rows]
        same = self.model is not None and self.title.text() == title and self.model.headers == headers
        if same:
            scroll = self.table.verticalScrollBar().value()
            self.model.editable, self.model.on_edit = editable, on_edit
            self.model.update(rows, row_data)
            self.table.verticalScrollBar().setValue(scroll)
        else:
            self.title.setText(title)
            self.model = RowsModel(headers, rows, row_data, editable, on_edit)
            self.table.setModel(self.model)
            self.table.resizeColumnsToContents()
        self._actions(actions)

    def _actions(self, actions):
        if [a[0] for a in actions] == list(self.buttons):
            for text, fn in actions:
                self.buttons[text].callback = fn
            return
        for b in self.buttons.values():
            self.bar.removeWidget(b)
            b.setParent(None)
            b.deleteLater()
        self.buttons = {}
        for text, fn in actions:
            b = QPushButton(text)
            b.callback = fn
            b.clicked.connect(lambda checked=False, b=b: b.callback(self.current()))
            self.bar.addWidget(b)
            self.buttons[text] = b

    def current(self):
        """the row_data of the current row, or None"""
        i = self.table.currentIndex()
        return self.model.row_data[i.row()] if self.model is not None and i.isValid() else None

    def select(self, row):
        if self.model is not None and 0 <= row < len(self.model.rows):
            self.table.setCurrentIndex(self.model.index(row, 0))

    def _activated(self, index):
        if self.model.flags(index) & Qt.ItemFlag.ItemIsEditable:
            return
        data = self.model.row_data[index.row()]
        if self.on_activate is not None and data is not None:
            self.on_activate(data)


class HeaderView(QWidget):
    """label: value rows; a field with a key is a line edit: on_edit(key, text) when it
    is edited"""

    def __init__(self, on_edit=None):
        super().__init__()
        self.on_edit = on_edit
        self.form = QFormLayout(self)
        self.widgets, self.labels = {}, []

    def set_fields(self, fields):
        """fields: [(label, text, key or None)]"""
        if [f[0] for f in fields] == self.labels:
            for label, text, key in fields:
                w = self.widgets[label]
                if w.text() != text:
                    w.setText(text)
            return
        while self.form.rowCount():
            self.form.removeRow(0)
        self.widgets, self.labels = {}, [f[0] for f in fields]
        for label, text, key in fields:
            if key is None:
                w = plain(QLabel(text))
                w.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse)
            else:
                w = QLineEdit(text)
                w.editingFinished.connect(lambda w=w, key=key: self._edited(key, w))
            self.widgets[label] = w
            self.form.addRow(label, w)

    def _edited(self, key, w):
        if self.on_edit is not None:
            self.on_edit(key, w.text())


class TextView(QPlainTextEdit):
    """read-only monospaced text (hex dumps, the Unknown view)"""

    def __init__(self):
        super().__init__()
        self.setReadOnly(True)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)


class LexiconView(QWidget):
    """the lexicon (H[3]) with a filter on the headword; headword and transcription are
    editable: on_edit(entry index, word, transcription); on_add(), on_remove(index)"""

    def __init__(self, on_edit=None, on_add=None, on_remove=None):
        super().__init__()
        self.on_edit, self.on_add, self.on_remove = on_edit, on_add, on_remove
        self.filter = QLineEdit()
        self.filter.setPlaceholderText('Filter headwords')
        self.note = plain(QLabel('As stored: the 7-bit national notation (Swedish [ = Ä, \\ = Ö, ] = Å)'))
        self.add = QPushButton('Add entry')
        self.add.clicked.connect(lambda: self.on_add and self.on_add())
        self.remove = QPushButton('Remove entry')
        self.remove.clicked.connect(self._remove)
        self.table = QTableView()
        compact(self.table)
        self.table.verticalHeader().hide()
        self.table.horizontalHeader().setStretchLastSection(True)
        self.proxy = QSortFilterProxyModel()
        self.proxy.setFilterKeyColumn(1)
        self.proxy.setFilterCaseSensitivity(Qt.CaseSensitivity.CaseInsensitive)
        self.table.setModel(self.proxy)
        self.filter.textChanged.connect(self.proxy.setFilterFixedString)
        self.model = None
        top = QHBoxLayout()
        top.addWidget(self.filter, 1)
        top.addWidget(self.add)
        top.addWidget(self.remove)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addWidget(self.note)
        lay.addWidget(self.table)

    def set_lexicon(self, lex):
        """show lex; shown again, the view keeps its filter and scroll position"""
        rows = [[str(i), o.decode('latin-1'), t.decode('latin-1')] for i, (o, t) in enumerate(lex.entries)]
        if self.model is None:
            self.model = RowsModel(['#', 'Headword', 'Transcription'], rows,
                                   editable=lambda r, c: c > 0, on_edit=self._edited)
            self.proxy.setSourceModel(self.model)
            self.table.setColumnWidth(0, 60)
            self.table.setColumnWidth(1, 220)
        else:
            scroll = self.table.verticalScrollBar().value()
            self.model.update(rows)
            self.table.verticalScrollBar().setValue(scroll)

    def reset(self):
        """forget the shown lexicon (another file was opened)"""
        self.model = None
        self.proxy.setSourceModel(None)
        self.filter.clear()

    def select(self, i):
        """make entry i current (clearing the filter if it hides the entry)"""
        if self.model is None or not 0 <= i < len(self.model.rows):
            return
        index = self.proxy.mapFromSource(self.model.index(i, 1))
        if not index.isValid():
            self.filter.clear()
            index = self.proxy.mapFromSource(self.model.index(i, 1))
        self.table.setCurrentIndex(index)
        self.table.scrollTo(index)

    def current(self):
        """the current entry's index, or None"""
        index = self.table.currentIndex()
        return self.proxy.mapToSource(index).row() if index.isValid() else None

    def _edited(self, row, col, text):
        _, word, trans = self.model.rows[row]
        word, trans = (text, trans) if col == 1 else (word, text)
        return self.on_edit is not None and self.on_edit(row, word, trans)

    def _remove(self):
        i = self.current()
        if i is not None and self.on_remove is not None:
            self.on_remove(i)
```


Replace the whole of `editor/ivxedit/gui/phonemeview.py` with:

```python
"""The phoneme view: name, feature bits, items and the rules that use the phoneme. The
name, the bits (click to toggle) and the item cells are editable."""
from PySide6.QtCore import Qt
from PySide6.QtWidgets import (QHBoxLayout, QInputDialog, QLabel, QLineEdit, QListWidget,
                               QListWidgetItem, QPushButton, QTableWidget, QTableWidgetItem,
                               QVBoxLayout, QWidget)

from .. import edits
from ..checks import rule_label
from ..info import item_name, ph_name
from ..model import PNAMES, VarItem
from .views import compact, mono, plain


def free_items(ph):
    """[(id, name)] of the items ph does not have: parameters, then variables"""
    have = {it.vid if isinstance(it, VarItem) else it.pid for it in ph.items}
    return [(i, item_name(i)) for i in list(range(len(PNAMES))) + list(range(0x80, 0x100)) if i not in have]


class PhonemeView(QWidget):
    def __init__(self, on_rule=None, edit=None):
        super().__init__()
        self.on_rule = on_rule            # called with (module name, rule index)
        self.edit = edit                  # edit(function, *args): make the edit and push it
        self.ph = None
        self.what = plain(QLabel())
        self.name = QLineEdit()
        self.name.setMaximumWidth(80)
        self.name.editingFinished.connect(self._rename)
        self.hex = plain(QLabel())
        self.bits = QTableWidget(2, 32)
        compact(self.bits)
        self.bits.setEditTriggers(QTableWidget.EditTrigger.NoEditTriggers)
        self.bits.setVerticalHeaderLabels(['feat', 'mask'])
        self.bits.setHorizontalHeaderLabels([str(b) for b in range(31, -1, -1)])
        self.bits.horizontalHeader().setDefaultSectionSize(24)
        self.bits.setFixedHeight(90)
        self.bits.cellClicked.connect(self._toggle)
        self.items = QTableWidget()
        compact(self.items)
        self.items.itemChanged.connect(self._cell)
        self.add = QPushButton('Add item…')
        self.add.clicked.connect(self._add_item)
        self.remove = QPushButton('Remove item')
        self.remove.clicked.connect(self._remove_item)
        self.uses_label = plain(QLabel())
        self.uses = QListWidget()
        self.uses.setFont(mono())
        self.uses.itemActivated.connect(self._jump)
        top = QHBoxLayout()
        top.addWidget(self.what)
        top.addWidget(self.name)
        top.addWidget(self.hex, 1)
        buttons = QHBoxLayout()
        buttons.addWidget(plain(QLabel('Items (value@time; time in frames from the segment start)')), 1)
        buttons.addWidget(self.add)
        buttons.addWidget(self.remove)
        lower = QHBoxLayout()
        left, right = QVBoxLayout(), QVBoxLayout()
        left.addLayout(buttons)
        left.addWidget(self.items)
        right.addWidget(self.uses_label)
        right.addWidget(self.uses)
        lower.addLayout(left, 3)
        lower.addLayout(right, 1)
        lay = QVBoxLayout(self)
        lay.addLayout(top)
        lay.addWidget(self.bits)
        lay.addLayout(lower)

    def set_phoneme(self, ph, uses, modules):
        """uses: [(module name, rule index)]; modules: the file's modules, for labels. The same
        phoneme shown again keeps the current cell."""
        same, self.ph = self.ph is ph, ph
        self.what.setText('defaults record, name' if ph.is_defaults else 'phoneme')
        if self.name.text() != ph_name(ph.name) or not same:
            self.name.setText(ph_name(ph.name))
        self.hex.setText('feat 0x%08x  mask 0x%08x' % (ph.feat, ph.mask))
        for row, v in enumerate((ph.feat, ph.mask)):
            for col in range(32):
                self.bits.setItem(row, col, QTableWidgetItem('●' if v >> (31 - col) & 1 else ''))
        width = max([1] + [len(it.points) for it in ph.items if not isinstance(it, VarItem)])
        cur = (self.items.currentRow(), self.items.currentColumn()) if same else None
        self.items.blockSignals(True)
        self.items.clear()
        self.items.setRowCount(len(ph.items))
        self.items.setColumnCount(2 + width)            # one empty column to add a point in
        self.items.setHorizontalHeaderLabels(['item'] + ['%d' % k for k in range(width + 1)])
        for row, it in enumerate(ph.items):
            if isinstance(it, VarItem):
                cells = [item_name(it.vid), str(it.value)]
                last = 1
            else:
                cells = [item_name(it.pid)] + ['%d@%d' % (v, t) for t, v in it.points]
                last = 1 + len(it.points)              # the cell that appends a point
            for col in range(2 + width):
                cell = QTableWidgetItem(cells[col] if col < len(cells) else '')
                if col == 0 or col > last:
                    cell.setFlags(cell.flags() & ~Qt.ItemFlag.ItemIsEditable)
                self.items.setItem(row, col, cell)
        if cur is not None and cur[0] < self.items.rowCount():
            self.items.setCurrentCell(*cur)
        self.items.blockSignals(False)
        self.uses.clear()
        self.uses_label.setText('Used by %d rule%s' % (len(uses), '' if len(uses) == 1 else 's'))
        for mod, i in uses:
            item = QListWidgetItem(rule_label(mod, i, modules[mod].rules[i]))
            item.setData(Qt.ItemDataRole.UserRole, (mod, i))
            self.uses.addItem(item)

    def _rename(self):
        if self.ph is not None and self.edit is not None:
            if not self.edit(edits.rename_phoneme, self.ph, self.name.text()):
                self.name.setText(ph_name(self.ph.name))

    def _toggle(self, row, col):
        if self.ph is not None and self.edit is not None:
            which, bit = ('feat', 'mask')[row], 31 - col
            self.edit(edits.set_bit, self.ph, which, bit, not getattr(self.ph, which) >> bit & 1)

    def _cell(self, cell):
        if self.ph is None or self.edit is None or cell.column() == 0:
            return
        row, k = cell.row(), cell.column() - 1
        if not self.edit(edits.set_cell, self.ph, row, k, cell.text()):
            self._restore()

    def _restore(self):
        """put the model's values back into the cells after a refused edit"""
        self.items.blockSignals(True)
        for row, it in enumerate(self.ph.items):
            cells = [str(it.value)] if isinstance(it, VarItem) else ['%d@%d' % (v, t) for t, v in it.points]
            for col in range(1, self.items.columnCount()):
                cell = self.items.item(row, col)
                if cell is not None:
                    cell.setText(cells[col - 1] if col - 1 < len(cells) else '')
        self.items.blockSignals(False)

    def _add_item(self):
        if self.ph is None or self.edit is None:
            return
        choices = free_items(self.ph)
        name, ok = self.ask_item([n for _, n in choices])
        if ok:
            iid = dict((n, i) for i, n in choices)[name]
            self.edit(edits.add_item, self.ph, iid)

    def ask_item(self, names):
        """-> (chosen name, ok); a dialog, replaced in tests"""
        return QInputDialog.getItem(self, 'Add item', 'Parameter or variable:', names, 0, False)

    def _remove_item(self):
        row = self.items.currentRow()
        if self.ph is not None and self.edit is not None and 0 <= row < len(self.ph.items):
            self.edit(edits.remove_item, self.ph, row)

    def _jump(self, item):
        if self.on_rule is not None:
            self.on_rule(*item.data(Qt.ItemDataRole.UserRole))
```


In `editor/ivxedit/gui/speakpanel.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 8, replace

```python

from ..speak import Options, SpeakError, find_ivxsay, speak_bytes, speak_model
from .waveview import SignalView
```

   with

```python

from ..speak import Options, SpeakError, find_ivxsay, model_bytes, speak_bytes
from .waveview import SignalView
```

2. Near line 19, replace

```python

    def __init__(self, calls):
        super().__init__()
        self.calls = calls          # [(title, function returning Speech)]

```

   with

```python

    def __init__(self, calls, model):
        super().__init__()
        self.calls = calls          # [(title, function returning Speech)]
        self.model = model          # the model the job speaks (results for another are dropped)

```

3. Near line 101, replace

```python
            return
        text, opts, f, orig = self.text.text(), self.options(), self.model, self.original
        calls = []
```

   with

```python
            return
        try:
            edited = model_bytes(self.model)   # here, not in the job: edits may follow at once
        except SpeakError as e:
            self.status.setText(str(e))
            return
        text, opts, orig = self.text.text(), self.options(), self.original
        calls = []
```

4. Near line 111, replace

```python
        calls.append(('B: edited' if self.ab.isChecked() else 'Edited',
                      lambda: speak_model(f, text, opts, exe)))
        self.status.setText('Speaking…')
        self.button.setEnabled(False)
        self.job = SpeakJob(calls)
        self.job.done.connect(self._done)
```

   with

```python
        calls.append(('B: edited' if self.ab.isChecked() else 'Edited',
                      lambda: speak_bytes(edited, text, opts, exe)))
        self.status.setText('Speaking…')
        self.button.setEnabled(False)
        self.job = SpeakJob(calls, self.model)
        self.job.done.connect(self._done)
```

5. Near line 121, replace

```python
        self.button.setEnabled(True)
        self.results = results
```

   with

```python
        self.button.setEnabled(True)
        if self.sender() is not None and self.sender().model is not self.model:
            self.status.setText('')                # spoken with a file that is no longer open
            return
        self.results = results
```

6. Near line 133, replace

```python
        self.button.setEnabled(True)
        self.status.setText(message)
```

   with

```python
        self.button.setEnabled(True)
        if self.sender() is not None and self.sender().model is not self.model:
            self.status.setText('')
            return
        self.status.setText(message)
```


Replace the whole of `editor/ivxedit/gui/mainwindow.py` with:

```python
"""The main window: section tree, the view for the selected item, the Speak panel, the
Problems panel; editing with undo, Save and Save As."""
from pathlib import Path

from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QAction, QKeySequence, QUndoCommand, QUndoStack
from PySide6.QtWidgets import (QDockWidget, QFileDialog, QInputDialog, QLabel, QListWidget,
                               QListWidgetItem, QMainWindow, QMessageBox, QSplitter,
                               QStackedWidget, QToolButton, QTreeWidget, QTreeWidgetItem)

from .. import edits
from ..binfmt import IvxFormatError
from ..checks import check, errors
from ..edits import EditError
from ..info import a1_text, char_rows, flags_text, header_fields, hexdump, ph_name, unknown_text
from ..listing import ph_names
from ..model import PNAMES, load
from ..notation import notation_line
from ..writer import BIN, SaveRefused, save
from ..xref import phoneme_uses
from .phonemeview import PhonemeView
from .rulesview import RulesView
from .speakpanel import SpeakPanel
from .views import HeaderView, LexiconView, TableView, TextView

ROLE = Qt.ItemDataRole.UserRole
# the header fields the Header view edits: label -> the key _header_edit takes
HEADER_KEYS = {'Version': 'version', 'Copyright': 'copyright', 'Flags': 'flags',
               'Lexicon name': 'lexicon name'}


class EditCommand(QUndoCommand):
    """an edits.Edit on the undo stack; undo and redo tell the window what they changed"""

    def __init__(self, edit, window):
        super().__init__(edit.text)
        self.edit, self.window = edit, window

    def redo(self):
        self.edit.apply()
        self.window.target = self.edit.target

    def undo(self):
        self.edit.revert()
        self.window.target = self.edit.target


class MainWindow(QMainWindow):
    def __init__(self, settings):
        super().__init__()
        self.settings = settings
        self.f = self.path = None
        self.uses = {}
        self.ph_items = {}            # Phoneme -> its tree item
        self.module_items = {}        # module name -> its tree item
        self.rule_views = {}          # module name -> RulesView, per open file
        self.names = {}               # ph_names of the open file
        self.notation = {}            # module name -> [notation Line or None] per rule
        self.target = None            # what the last edit, undo or redo changed (Edit.target)
        self.shape = None             # the names and counts the tree shows
        self.undo = QUndoStack(self)
        self.undo.indexChanged.connect(self._changed)
        self.undo.cleanChanged.connect(lambda clean: self._title())
        self.tree = QTreeWidget()
        self.tree.setHeaderHidden(True)
        self.tree.currentItemChanged.connect(lambda cur, prev: self.show_item(cur))
        self.stack = QStackedWidget()
        self.header_view = HeaderView(self._header_edit)
        self.table_view = TableView(self._activate)
        self.phoneme_view = PhonemeView(self.show_rule, self.try_edit)
        self.lexicon_view = LexiconView(self._lex_edit, self._lex_add, self._lex_remove)
        self.text_view = TextView()
        for w in (self.header_view, self.table_view, self.phoneme_view, self.lexicon_view, self.text_view):
            self.stack.addWidget(w)
        self.speak = SpeakPanel(settings)
        right = QSplitter(Qt.Orientation.Vertical)
        right.addWidget(self.stack)
        right.addWidget(self.speak)
        right.setSizes([500, 300])
        main = QSplitter(Qt.Orientation.Horizontal)
        main.addWidget(self.tree)
        main.addWidget(right)
        main.setSizes([220, 900])
        self.setCentralWidget(main)
        self.problem_list = QListWidget()
        self.problem_list.itemActivated.connect(lambda item: self.show_target(item.data(ROLE)))
        self.problem_dock = QDockWidget('Problems', self)
        self.problem_dock.setObjectName('problems')
        self.problem_dock.setWidget(self.problem_list)
        self.addDockWidget(Qt.DockWidgetArea.BottomDockWidgetArea, self.problem_dock)
        self.problem_dock.hide()
        self.problems = QToolButton()
        self.problems.setAutoRaise(True)
        self.problems.clicked.connect(lambda: self.show_problems(not self.problem_dock.isVisible()))
        self.statusBar().addWidget(self.problems)
        self.coverage = QLabel()      # the shown module's rules in notation
        self.statusBar().addWidget(self.coverage)
        self._refresh_timer = QTimer(self)
        self._refresh_timer.setSingleShot(True)
        self._refresh_timer.timeout.connect(self.refresh)
        self._menus()
        self.resize(1200, 800)
        self._title()

    def _menus(self):
        m = self.menuBar().addMenu('&File')
        for text, key, slot in (('&Open…', QKeySequence.StandardKey.Open, self.open_dialog),
                                ('&Save', QKeySequence.StandardKey.Save, self.save),
                                ('Save &As…', QKeySequence.StandardKey.SaveAs, self.save_as),
                                ('&ivxsay…', None, self.ivxsay_dialog),
                                ('&Quit', QKeySequence.StandardKey.Quit, self.close)):
            a = QAction(text, self)
            if key is not None:
                a.setShortcut(key)
            a.triggered.connect(slot)
            m.addAction(a)
        m = self.menuBar().addMenu('&Edit')
        undo, redo = self.undo.createUndoAction(self, '&Undo'), self.undo.createRedoAction(self, '&Redo')
        undo.setShortcut(QKeySequence.StandardKey.Undo)
        redo.setShortcut(QKeySequence.StandardKey.Redo)
        m.addAction(undo)
        m.addAction(redo)
        self.instr_action = QAction('&Instruction form', self)
        self.instr_action.setCheckable(True)
        self.instr_action.setShortcut(QKeySequence('Ctrl+I'))
        self.instr_action.toggled.connect(self.set_instruction_form)
        view = self.menuBar().addMenu('&View')
        view.addAction(self.instr_action)
        view.addAction(self.problem_dock.toggleViewAction())

    def _title(self):
        name = ' — %s%s' % (self.path.name, '' if self.undo.isClean() else ' *') if self.path else ''
        self.setWindowTitle('IVX Editor' + name)

    # --- files
    def open_dialog(self):
        if not self.maybe_save():
            return
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
        """load path into the window, dropping any unsaved edits; raises OSError or IvxFormatError"""
        data = Path(path).read_bytes()
        f = load(data)
        uses = phoneme_uses(f)
        self.f, self.path = f, Path(path)
        self.uses = uses
        self.names, self.notation = ph_names(f.phonemes), {}
        self._drop_rule_views()
        self.lexicon_view.reset()
        self.speak.set_model(f, data)
        self.undo.clear()
        self.target, self.shape = None, self._shape()
        self._problems()
        self._fill_tree()
        self._title()

    def ivxsay_dialog(self):
        path, _ = QFileDialog.getOpenFileName(self, 'ivxsay to run', '', 'ivxsay (ivxsay*)')
        if path:
            self.settings.setValue('ivxsay', path)

    def save(self):
        """save to the open file; a file in bin/ is saved elsewhere (Save As). -> True if saved"""
        if self.f is None:
            return False
        if BIN in self.path.resolve().parents:
            return self.save_as()
        return self.write(self.path)

    def save_as(self):
        if self.f is None:
            return False
        path = self.ask_save_path()
        return bool(path) and self.write(Path(path))

    def ask_save_path(self):
        """the path to save to, or '' (a dialog; replaced in tests)"""
        start = self.settings.value('save_dir', '') or str(Path.home())
        path, _ = QFileDialog.getSaveFileName(self, 'Save As', str(Path(start) / self.path.name),
                                              'Infovox language files (*.ivx)')
        if path:
            self.settings.setValue('save_dir', str(Path(path).parent))
        return path

    def write(self, path):
        """save the model to path; on failure say why. -> True if saved"""
        try:
            save(self.f, path)
        except SaveRefused as e:
            self.show_problems()
            self.warn('Save', 'Not saved: %d problem(s).\n\n%s' % (
                len(e.problems), '\n'.join(str(p) for p in e.problems[:10])))
            return False
        except OSError as e:
            self.warn('Save', 'Not saved: %s' % e)
            return False
        self.path = Path(path)
        self.undo.setClean()
        self._title()
        self.statusBar().showMessage('Saved %s' % self.path, 5000)
        return True

    def warn(self, title, text):
        """a message box (replaced in tests)"""
        QMessageBox.warning(self, title, text)

    def ask_unsaved(self):
        """Save, Discard or Cancel for unsaved changes (a dialog; replaced in tests)"""
        b = QMessageBox.StandardButton
        return QMessageBox.question(self, 'Unsaved changes', '%s has unsaved changes.' % self.path.name,
                                    b.Save | b.Discard | b.Cancel, b.Save)

    def maybe_save(self):
        """-> True if the window may drop the open file: no unsaved changes, saved, or discarded"""
        if self.f is None or self.undo.isClean():
            return True
        answer = self.ask_unsaved()
        if answer == QMessageBox.StandardButton.Save:
            return self.save()
        return answer == QMessageBox.StandardButton.Discard

    # --- editing
    def try_edit(self, fn, *args):
        """make an edit with an edits function and push it; an EditError is shown in the
        status bar. -> the extra value of a function that returns (Edit, extra), else True;
        False if refused"""
        try:
            result = fn(*args)
        except EditError as e:
            self.statusBar().showMessage(str(e), 10000)
            return False
        extra = True
        if isinstance(result, tuple):
            result, extra = result
        if result is not None:
            self.undo.push(EditCommand(result, self))
        return extra

    def _shape(self):
        lex = self.f.lexicon
        return (tuple((id(p), p.name) for p in self.f.phonemes), None if lex is None else len(lex.entries))

    def _changed(self):
        """after an edit, undo or redo: problems, title, the tree's names and counts, and
        (after this signal) the shown view"""
        shape = self._shape()
        if shape[0] != self.shape[0]:                 # phoneme names: the rule lines change
            self.names, self.notation = ph_names(self.f.phonemes), {}
            self._drop_rule_views()
        if shape != self.shape:
            self.shape = shape
            self._fill_tree(self._current())
        self._problems()
        self._title()
        self._refresh_timer.start(0)                  # not inside the signal of the edited widget

    def refresh(self):
        """show the current item again with the model's values, then the edit's target"""
        target, self.target = self.target, None
        self.show_item(self.tree.currentItem())
        if target is not None:
            self.show_target(target)

    def _problems(self):
        problems = check(self.f)
        n = len(errors(problems))
        self.problems.setText('Problems (%d)' % len(problems) if n == len(problems) else
                              'Problems (%d, %d warning%s)' % (len(problems), len(problems) - n,
                                                               '' if len(problems) - n == 1 else 's'))
        self.problem_list.clear()
        for p in problems:
            item = QListWidgetItem(str(p))
            item.setData(ROLE, p.target)
            self.problem_list.addItem(item)

    def show_problems(self, on=True):
        self.problem_dock.setVisible(on)
        if on:
            self.resizeDocks([self.problem_dock], [140], Qt.Orientation.Vertical)

    def _drop_rule_views(self):
        for v in self.rule_views.values():
            self.stack.removeWidget(v)
            v.deleteLater()
        self.rule_views = {}

    def _header_edit(self, key, text):
        if key == 'flags':
            self.try_edit(edits.set_header_flags, self.f, text)
        else:
            self.try_edit(edits.set_header_text, self.f, key, text)

    def _lex_edit(self, i, word, trans):
        k = self.try_edit(edits.set_entry, self.f.lexicon, i, word, trans)
        return k is not False

    def _lex_add(self):
        word, ok = self.ask_text('Add entry', 'Headword:')
        if ok and word:
            self.try_edit(edits.add_entry, self.f.lexicon, word, word)

    def _lex_remove(self, i):
        self.try_edit(edits.remove_entry, self.f.lexicon, i)

    def ask_text(self, title, label):
        """-> (text, ok) (a dialog; replaced in tests)"""
        return QInputDialog.getText(self, title, label)

    def _phoneme_action(self, action, ph):
        f = self.f
        if action == 'delete':
            if ph is not None:
                self.try_edit(edits.delete_phoneme, f, ph)
            return
        at = len(f.phonemes) if ph is None else f.phonemes.index(ph) + 1
        model = ph if action == 'copy' and ph is not None and not ph.is_defaults else None
        self.try_edit(edits.add_phoneme, f, at, edits.free_name(f), model)

    # --- the tree
    def _node(self, parent, text, kind, obj=None):
        item = QTreeWidgetItem([text])
        item.setData(0, ROLE, (kind, obj))
        (parent.addChild if isinstance(parent, QTreeWidgetItem) else parent.addTopLevelItem)(item)
        return item

    def _current(self):
        item = self.tree.currentItem()
        return item.data(0, ROLE) if item is not None else None

    def _fill_tree(self, select=None):
        """rebuild the tree; select the item for (kind, obj), else the first"""
        f, t = self.f, self.tree
        expanded = {self.tree.topLevelItem(k).data(0, ROLE)[0] for k in range(t.topLevelItemCount())
                    if t.topLevelItem(k).isExpanded()}
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
        for item in self.items():
            if item.data(0, ROLE)[0] in expanded:
                item.setExpanded(True)
        found = next((it for it in self.items() if select is not None and it.data(0, ROLE)[0] == select[0]
                      and it.data(0, ROLE)[1] is select[1]), None)
        if found is None and select is not None and select[0] == 'phoneme':
            found = self._top('phonemes')                  # the phoneme is gone (deleted)
        t.setCurrentItem(found if found is not None else t.topLevelItem(0))
        t.blockSignals(False)
        self.show_item(t.currentItem())

    def _top(self, kind):
        return next((self.tree.topLevelItem(k) for k in range(self.tree.topLevelItemCount())
                     if self.tree.topLevelItem(k).data(0, ROLE)[0] == kind), None)

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
        self.coverage.setText('')
        if kind == 'header':
            self.header_view.set_fields([(label, text, HEADER_KEYS.get(label))
                                         for label, text in header_fields(f)])
            self.stack.setCurrentWidget(self.header_view)
        elif kind == 'params':
            rows = []
            for i, name in enumerate(PNAMES):
                a0, a1, a2, a3 = f.param(i)
                rows.append([i, name, '0x%04x' % a0, a1_text(a1), a2, a3])
            self._table('Parameters: A0 name slot, A1 type → handler, A2 rate, A3 offset (REFERENCE §5.3)',
                        ['#', 'Name', 'A0', 'A1 type', 'A2', 'A3'], rows,
                        editable=lambda r, c: c >= 3,
                        on_edit=lambda r, c, text: self.try_edit(
                            edits.set_param, f, r, edits.PARAM_COLUMNS[c - 3], text))
        elif kind == 'phonemes':
            rows = [[i, 'defaults' if ph.is_defaults else ph_name(ph.name), '0x%08x' % ph.feat,
                     '0x%08x' % ph.mask, len(ph.items), len(self.uses.get(ph, []))]
                    for i, ph in enumerate(f.phonemes)]
            self._table('Phoneme inventory', ['#', 'Name', 'feat', 'mask', 'Items', 'Used by'],
                        rows, [('phoneme', ph) for ph in f.phonemes],
                        actions=[(text, lambda data, a=a: self._phoneme_action(a, data and data[1]))
                                 for text, a in (('New', 'new'), ('Copy', 'copy'), ('Delete', 'delete'))])
        elif kind == 'phoneme':
            self.phoneme_view.set_phoneme(obj, self.uses.get(obj, []), f.modules)
            self.stack.setCurrentWidget(self.phoneme_view)
        elif kind == 'rules':
            rows = [[n, len(m.rules), self._in_notation(n), sum(r.iterative for r in m.rules),
                     '0x%04x' % m.lead] for n, m in sorted(f.modules.items())]
            self._table('Rule modules (REFERENCE §5.5)',
                        ['Module', 'Rules', 'In notation', 'RULE*', 'Lead word'],
                        rows, [('module', n) for n in sorted(f.modules)])
        elif kind == 'module':
            self.stack.setCurrentWidget(self._rules_view(obj))
            self.coverage.setText('%s: %s / %s rules in notation' % (
                obj, format(self._in_notation(obj), ','), format(len(f.modules[obj].rules), ',')))
        elif kind == 'chars':
            rows = [['0x%02x' % ch, c, '' if code is None else code,
                     '(built-in)' if fl is None else flags_text(fl), '' if xl is None else xl]
                    for ch, c, code, fl, xl in char_rows(f)]
            self._table('Characters: class code and flags (REFERENCE §5.2), transliteration (H[14])',
                        ['Byte', 'Char', 'Class', 'Flags', 'Translit.'], rows,
                        editable=lambda r, c: (c == 2 and (r < 0x80 or f.class_hi is not None))
                        or (c == 4 and r >= 0x80 and f.xlat_hi is not None),
                        on_edit=lambda r, c, text: self.try_edit(
                            edits.set_class if c == 2 else edits.set_xlat, f, r, text))
        elif kind == 'lexicon':
            self.lexicon_view.set_lexicon(f.lexicon)
            self.stack.setCurrentWidget(self.lexicon_view)
        elif kind == 'ixlex':
            self._text('Indexed lexicon (H[13] + 0x80 to the end of the data), not decoded\n\n'
                       + hexdump(f.ixlex))
        elif kind == 'unknown':
            self._text(unknown_text(f))

    def _table(self, title, headers, rows, row_data=None, editable=None, on_edit=None, actions=()):
        self.table_view.set_rows(title, headers, rows, row_data, editable, on_edit, actions)
        self.stack.setCurrentWidget(self.table_view)

    def _text(self, text):
        self.text_view.setPlainText(text)
        self.stack.setCurrentWidget(self.text_view)

    def _notation(self, name):
        """[notation Line or None] for each rule of module `name`, made once per file"""
        if name not in self.notation:
            self.notation[name] = [notation_line(r, self.names) for r in self.f.modules[name].rules]
        return self.notation[name]

    def _in_notation(self, name):
        return sum(n is not None for n in self._notation(name))

    def _rules_view(self, name):
        if name not in self.rule_views:
            v = RulesView(self.show_phoneme)
            v.set_instruction_form(self.instr_action.isChecked())
            v.set_module(name, self.f.modules[name], self._notation(name))
            self.stack.addWidget(v)
            self.rule_views[name] = v
        return self.rule_views[name]

    def set_instruction_form(self, on):
        """View ▸ Instruction form: every rule in instruction form, or notation where it exists"""
        for v in self.rule_views.values():
            v.set_instruction_form(on)

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

    def show_target(self, target):
        """go to a Problem's or an Edit's target"""
        if not target or self.f is None:
            return
        kind = target[0]
        if kind == 'phoneme' and target[1] in self.ph_items:
            self.show_phoneme(target[1])
        elif kind == 'rule':
            self.show_rule(target[1], target[2])
        elif kind == 'lexicon':
            self.tree.setCurrentItem(self._top('lexicon'))
            self.lexicon_view.select(target[1])
        elif kind != 'phoneme' and self._top(kind) is not None:
            self.tree.setCurrentItem(self._top(kind))

    def closeEvent(self, event):
        if not self.maybe_save():
            event.ignore()
            return
        self.speak.cleanup()
        super().closeEvent(event)
```


- [ ] **Step 4: Run the tests to see them pass**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[eis]*.py"`
Expected: OK. Then the whole suite once: `python -m unittest discover -s editor/tests -t editor` → OK, 142 tests (1 skipped on Windows).

- [ ] **Step 5: Look at it**

Run `python editor/ivxedit.pyw bin/swrules.ivx` (native fonts, not offscreen). Check: the Header fields are editable; Parameters A1–A3, Characters Class/Translit. and the lexicon cells edit on double-click; the Phonemes table shows New/Copy/Delete and the Characters table shows no buttons; renaming `A:` to `E:` shows `Problems (1, 1 warning)` and the Problems dock lists it; Ctrl+Z undoes and the title loses its `*`; Save on a file from `bin/` asks for another path. Close without saving: the unsaved-changes question appears.

- [ ] **Step 6: Documentation**

In `AGENTS.md`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 21, replace

```text
| `cli/ivxsay.c` | A client of the `infovox230` library (`build/ivxsay.exe` / `build/linux/ivxsay`): `ivxsay -l sw "God morgon."` speaks through the speaker by default (`cli/audio_win.c` waveOut on Windows, `cli/audio_alsa.c` ALSA on Linux) or writes a WAV with `-o out.wav`. Options cover the voice, rate, pitch, FD, sample rate, text mode, frame rate, input encoding (`-e utf8|cp1252`, default UTF-8) and an event trace (`-E`), dictionary files (`-d FILE`: `[!?]word = replacement`, `/…/` phonetic); text can come from a file or stdin. Its WAV output is byte-identical to the DLL's in all 12 languages (`tools/check_ivxsay.py`, run by `make test`) and `-d` must match the text with its replacements made. |
| `editor/` | `ivxedit`, the `.ivx` editor (spec `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`). Steps 1–3 are done. `ivxedit.model.load` reads a file into a model (phonemes, rule modules with phoneme references, tables, character tables, lexicons); `ivxedit.writer.to_bytes`/`save` write it back (byte-identical for all 12 files, phoneme ids relocated, `bin/` refused); `ivxedit.checks.check` lists what would stop the engine loading it. `load` raises only `IvxFormatError` for a file it cannot read. The read-only GUI (`python editor/ivxedit.pyw [file.ivx]`, PySide6) shows every section, with the rules in the notation `A → B / C _ D` (`ivxedit.notation`, grammar in `editor/ivxedit/NOTATION.md`; 15,526 of the 15,564 shipped rules) and the rest in instruction form (`ivxedit.listing`, whose `parse_rule` reads it back). Its Speak panel runs `ivxsay` on the model (`ivxedit.speak`) and draws waveform, spectrogram and phoneme strip, with A/B against the file as opened. Tests: `make editor-test`. |
| `tools/` | Static-analysis helpers (see below). `full.asm`, `xref.json`, `ops.txt`, `handlers.txt` and `out/*.lst` are generated outputs, and you can regenerate them. |
```

   with

```text
| `cli/ivxsay.c` | A client of the `infovox230` library (`build/ivxsay.exe` / `build/linux/ivxsay`): `ivxsay -l sw "God morgon."` speaks through the speaker by default (`cli/audio_win.c` waveOut on Windows, `cli/audio_alsa.c` ALSA on Linux) or writes a WAV with `-o out.wav`. Options cover the voice, rate, pitch, FD, sample rate, text mode, frame rate, input encoding (`-e utf8|cp1252`, default UTF-8) and an event trace (`-E`), dictionary files (`-d FILE`: `[!?]word = replacement`, `/…/` phonetic); text can come from a file or stdin. Its WAV output is byte-identical to the DLL's in all 12 languages (`tools/check_ivxsay.py`, run by `make test`) and `-d` must match the text with its replacements made. |
| `editor/` | `ivxedit`, the `.ivx` editor (spec `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`). Steps 1–4 are done. `ivxedit.model.load` reads a file into a model (phonemes, rule modules with phoneme references, tables, character tables, lexicons); `ivxedit.writer.to_bytes`/`save` write it back (byte-identical for all 12 files, phoneme ids relocated, `bin/` refused); `ivxedit.checks.check` lists what would stop the engine loading it, plus warnings (a duplicate phoneme name). `load` raises only `IvxFormatError` for a file it cannot read. The GUI (`python editor/ivxedit.pyw [file.ivx]`, PySide6) shows every section and edits the header, parameters, phonemes, characters and lexicon (`ivxedit.edits`, with undo, Save, Save As and a Problems panel; the rules become editable in step 5), with the rules in the notation `A → B / C _ D` (`ivxedit.notation`, grammar in `editor/ivxedit/NOTATION.md`; 15,526 of the 15,564 shipped rules) and the rest in instruction form (`ivxedit.listing`, whose `parse_rule` reads it back). Its Speak panel runs `ivxsay` on the model (`ivxedit.speak`) and draws waveform, spectrogram and phoneme strip, with A/B against the file as opened. Tests: `make editor-test`. |
| `tools/` | Static-analysis helpers (see below). `full.asm`, `xref.json`, `ops.txt`, `handlers.txt` and `out/*.lst` are generated outputs, and you can regenerate them. |
```

2. Near line 145, replace

```text

This starts the editor (read-only until step 4; View ▸ Instruction form shows every rule in instruction form). Speak looks for `build/ivxsay.exe` (or `build/linux/ivxsay`), or the file set with File ▸ ivxsay…. For offscreen screenshots, set `QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:/Windows/Fonts`.

```

   with

```text

This starts the editor (the rules are read-only until step 5; View ▸ Instruction form shows every rule in instruction form). Edit ▸ Undo and Redo cover every edit; File ▸ Save never writes into `bin/` and asks for another path instead. Speak looks for `build/ivxsay.exe` (or `build/linux/ivxsay`), or the file set with File ▸ ivxsay…. For offscreen screenshots, set `QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:/Windows/Fonts`.

```


In `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 184, replace

```text

**Speak (`speak.py`):**
```

   with

```text

**As built in step 4** (plan `docs/superpowers/plans/2026-10-04-ivxedit-step4-voice-data-editing.md`): every edit is an `ivxedit.edits.Edit` that replaces one attribute of a model object (a new header, parameter tables, items list, inventory list or lexicon list), so undo puts the old value back exactly; `EditCommand` puts it on the `QUndoStack`. Input that cannot be stored (a three-character name, the point `300@0`, deleting a phoneme the rules use) is refused with a message in the status bar, and edits keep the item and lexicon order. `check()` also gives warnings (a duplicate phoneme name), which do not stop a save, and every problem has a target the Problems panel jumps to. Save writes to the open file unless it is in `bin/` (then Save As); unsaved changes are asked about before Open and Quit. Speak speaks the model as it is when Speak is pressed and drops results for a file that is no longer open. A1 is edited as its type (A1/4, 0–32), A2 as 0–255 and A3 as a signed word; A0 stays read-only. The lexicon is edited as stored, in its 7-bit national notation. New phonemes get a free name (`Z0`, `Z1`, …) and are renamed in the phoneme view.

**Speak (`speak.py`):**
```


- [ ] **Step 7: Commit**

```bash
git add editor/ivxedit/info.py editor/ivxedit/gui/views.py editor/ivxedit/gui/phonemeview.py editor/ivxedit/gui/speakpanel.py editor/ivxedit/gui/mainwindow.py editor/tests/test_editing.py editor/tests/test_speakpanel.py editor/tests/test_info.py AGENTS.md docs/superpowers/specs/2026-10-03-ivx-editor-design.md
git commit -m "ivxedit gui: edit header, parameters, phonemes, characters and lexicon with undo; Problems panel, Save and Save As; docs for step 4" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Self-review against the spec

- **Editors** (spec: GUI): Header form (Task 3, `HeaderView` + `edits.set_header_*`), Parameters (A1 type with handler name, A2, A3), Phoneme (name, bit grid, `value@time` cells, variables, Used by), Characters (class code with flags, transliteration), Lexicon (searchable, order kept: `edits._placed`), Lexicon 2/Unknown read-only — Tasks 2 and 3.
- **Every edit a `QUndoCommand`; `*` in the title; Problems after each edit** — Task 3 (`EditCommand`, `_title`, `_changed`).
- **Checks before saving**: "deleting a phoneme that rules use is refused, with the list of rules" — `edits.delete_phoneme` (Task 2); the save checks gain the deferred ones (Task 1).
- **Save As, `.bak`, never `bin/`** — `writer.save` (step 1) + Task 1 (mode, fsync) + Task 3 (Save goes to Save As for `bin/`).
- **Testing 4** (one test per refusal): the new refusals are in `test_checks.py` (Task 1). **Testing 9** (smoke): `test_gui.py` still opens every node of every file. **Testing 11** (undo): `test_edits.test_undo_restores_every_file` (all 12 files, model level) and `test_editing.test_undo_restores_the_file` (through the window).
