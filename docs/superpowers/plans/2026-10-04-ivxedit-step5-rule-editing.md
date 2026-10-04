# ivxedit Step 5: Rule Editing Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the rules editable in `ivxedit`, one rule at a time, in rule notation or in instruction form, with undo, checks and the rest of step 4's editing machinery.

**Architecture:** The notation reader learns ASCII spellings for its symbols and reports errors of a whole rule without a column (Task 1). `edits.py` gains `read_rule` (notation or instruction form, by the first word) and rule edits that replace a module's rule list, so undo is exact; a carrying `REPL` word stays exempt from the checks only while it is the unchanged word of a shipped rule (Task 2). The rules view opens a line editor over the rule's line and makes the edits through the window's `try_edit`; the window caches notation per rule and keeps "Used by" and the tree's rule counts up to date (Task 3).

**Tech Stack:** Python 3.13, PySide6 ≥ 6.8, `unittest`.

**Spec:** `docs/superpowers/specs/2026-10-03-ivx-editor-design.md` (sections "Rule notation", "GUI", "Testing" 5–8 and 11). The previous steps' plans: `docs/superpowers/plans/2026-10-04-ivxedit-step3-rule-notation.md` (the notation and `listing.parse_rule`) and `docs/superpowers/plans/2026-10-04-ivxedit-step4-voice-data-editing.md` (`edits.py`, the undo stack, the Problems panel). The grammar is `editor/ivxedit/NOTATION.md`.

## Global Constraints

- "A rule is shown in notation only if `notation → instructions → bytes` reproduces its original bytes; otherwise it is shown in instruction form, the `ivxdasm.py` syntax made unambiguous, which also round-trips. Both forms are editable in step 5, in the same module view." (spec: Rule notation)
- "**Rules**: one module per view, one rule per line, syntax highlighting, fallback rules on a tinted background, phoneme names that jump to the phoneme." (spec: GUI)
- "Editing is enabled per editor in step 4 (all but Rules) and step 5 (Rules). Every edit is a `QUndoCommand` on the model; the title shows `*` when there are unsaved changes; the Problems panel runs the writer's checks after each edit." (spec)
- "each `REPL` word's low byte is exactly 12·n for its n ids (so at most 21), unless it is an unedited carrying word from the original file (the five shipped ones)" (spec: Checks before saving).
- Spec Testing 6: "**Notation** round-trips every rule it shows; coverage per module is printed and must not fall below a stored minimum". Testing 7: "**Parser robustness**: random edits of real rule lines give a parse error or a rule that encodes, never an exception or bad bytes." Testing 8: "**End to end**: a rule added in notation (an M0 respelling) changes the phonemes `ivxsay -E` reports for a test word as expected." Testing 11: "**Undo**: a script of edits, then undo of all of them, saves byte-identical to the original."
- Tests: `unittest`, run by `python -m unittest discover -s editor/tests -t editor` from the repository root; GUI tests run offscreen and skip without PySide6; tests that need `build/ivxsay.exe` skip with a message when it is missing (spec: Testing).
- `checks.py`, `writer.py`, `edits.py`, `listing.py`, `notation.py` use the Python standard library only.
- `src/`, `lib/`, `cli/`, `bin/` and the papers in `docs/` do not change (AGENTS.md, spec).
- Files are UTF-8 with LF line endings. Python style follows the surrounding code: lower-case docstrings that say what a function returns, short comments, lines up to about 110 characters.
- Commits end with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Facts found while planning

All measured on a working prototype of this plan's code (the code below), on the 12 files in `bin/`.

1. **Phoneme names use ASCII punctuation**: Swedish has phonemes named `-`, `.`, `=` and `!`, and two-character names may hold `=` or `!`. So the ASCII spellings are for symbols only (arrows, `...`, `<-`, the comparison operators inside a test, `-` inside a bundle); a name is never read as one. All 15,526 rules in notation still read back with them in the reader.
2. **The ends of the segment list differ** [CONFIRMED, `src/cwtest.c`, bit-exact with the DLL]: `R`/`R?` past the last segment call `cw_fail` (the rule gives up; `RULE*` restarts if it changed something; `cw_op_R_opt` 0x10020b60), `L`/`L?` before the first segment call `cw_retry` (the next start position; `cw_op_L_opt` 0x10020bb0). `REPL+` and `SETF+` move the focus one segment right and then act as `REPL`/`SETF` (`src/cwelem.c`, `cw_op_REPL_next`, `cw_op_SETF_next`). NOTATION.md said otherwise in two places (deferred from the step-3 review).
3. **Five shipped rules carry a `REPL` word** whose 12·n runs into the high byte (Danish M5). `decode_instr` marks them `legacy`; `parse_rule` marked any carrying word it read, so a carrying word typed by hand escaped the check (deferred from step 3).
4. **Costs**: `phoneme_uses` takes 4–6 ms per file, so the window can recompute "Used by" after every edit; notation for a module takes 34–82 ms (Swedish M2, 635 rules), so it is cached per rule and only changed rules are written again; filling the rules view with 635 lines takes 14–18 ms.
5. **Qt**: the view's shortcuts (`Ins`, `Del`, `Ctrl+D`, `F2`, `Alt+Up/Down`) are `WidgetShortcut`s, active only while the view has the focus, so `Del` in the line editor deletes a character. They work in a real window (checked natively); offscreen nothing has the focus, so tests trigger the actions and send Return and Escape to the widgets directly. A `QLineEdit` that commits on focus-out must not commit while its window is closing (it would call into a deleted window at exit).

## Decisions (where the spec leaves the choice to step 5)

- **One line per rule**: Enter, F2 or a double-click (not on a phoneme, which still jumps) opens a line editor over the rule's line, holding the line as shown (notation, or instruction form where the rule has no notation or View ▸ Instruction form is on). A line that starts with `RULE` or `RULE*` is read as instruction form, any other as notation (`edits.read_rule`). Enter or leaving the editor commits; Escape cancels.
- **Refuse, don't store**: a line that does not read keeps the editor open with a red background, its cursor at the problem and the reason in the status bar. Errors of the rule as a whole (no focus, a group that does not fit) have no column (`ParseError.pos` is `None`), instead of the column 1 they claimed before.
- **Rule edits** replace the module's rule list (`Edit(module, 'rules', new list)`), so undo is exact: `set_rule`, `insert_rule` (Insert: a new line after the current rule; Ctrl+D: a copy of it), `delete_rule`, `move_rule` (Alt+Up/Down, one place). A line that gives the same words as the rule is no edit. The view's context menu has the same actions; the line editor's context menu inserts the symbols that have no ASCII spelling (`∅ ↺ · ␠ ‹›`, and the others for convenience).
- **ASCII spellings** (NOTATION.md, Typing): `->`, `->+`, `...`, `<-`, `-bN` in a bundle, `<=` `>=` `!=` `==` and `->` inside a test. The editor always writes the symbols.
- **Carrying `REPL` words**: `parse_rule` never marks `legacy`; `set_rule` keeps the mark on an instruction whose words equal a marked instruction of the rule it replaces. A copy (Ctrl+D) or a typed carrying word is not marked, so `check()` refuses it, as the spec's checks require.
- **The window after a rule edit**: the tree's rule counts are part of the shape it compares; "Used by" is recomputed; notation is cached per `Rule` object, so only new rules are written; the rules view keeps its scroll position and current line when it is filled again (also for View ▸ Instruction form, deferred from step 3), and `goto_rule` scrolls only when the rule is out of view.
- **Not done**: `check()` does not re-validate every rule's instruction shape — every rule the editor makes comes from the parser, which refuses a wrong shape (`listing.check_shape`, `check_length`), and `check()` runs after every edit. `GEMIN` rules (23) are edited in instruction form.
- **Left for later** (recorded so they are not lost): the Speak polish of step 2 (hearing A in A/B, a common dB scale, killing `ivxsay` on close, the spectrogram off the GUI thread, a message without Qt Multimedia, the wheel over the signal view, a time ruler); the lexicon in national characters; the small step-4 leftovers (`add_item` ids outside 0..255, the inventory losing its current row after New/Delete, `open_file` running `_changed` twice, `.bak` of a read-only file).

## File structure

| File | Task | Responsibility |
|---|---|---|
| `editor/ivxedit/listing.py` | 1, 2 | `ParseError` without a column (1); `parse_rule` never marks `legacy` (2) |
| `editor/ivxedit/notation.py` | 1 | ASCII spellings of the symbols |
| `editor/ivxedit/NOTATION.md` | 1 | Typing; the ends of the list; where `REPL+`/`SETF+` leave the focus |
| `editor/tests/test_notation.py` | 1 | typed spellings, names that look like them, errors without a column, changed shipped rules |
| `editor/ivxedit/edits.py` | 2 | `read_rule`, `copy_rule`, `set_rule`, `insert_rule`, `delete_rule`, `move_rule` |
| `editor/tests/test_edits.py`, `test_listing.py` | 2 | rule edits, undo, refusals, carrying words |
| `editor/ivxedit/gui/rulesview.py` | 3 | the line editor, the rule actions, keeping the place |
| `editor/ivxedit/gui/mainwindow.py` | 3 | notation per rule, "Used by" and rule counts after edits |
| `editor/tests/test_rule_editing.py` | 3 | editing rules through the window (spec: Testing 8, 11) |
| `AGENTS.md`, the spec | 3 | step 5 as built |

---

### Task 1: Typing notation: ASCII spellings, errors without a column, NOTATION.md

**Files:**
- Modify: `editor/ivxedit/listing.py` (`ParseError`, `check_length`), `editor/ivxedit/notation.py` (the lexer, `read_bundle`, `read_test`), `editor/ivxedit/NOTATION.md`
- Test: `editor/tests/test_notation.py`

**Interfaces:**
- Consumes (existing): `listing.ParseError`, `parse_rule`, `ph_names`, `words`, `rule_line`; `notation.parse_notation`, `notation_line`.
- Produces (Tasks 2 and 3 rely on these):
  - `listing.ParseError(message, pos=None)`: `.message`, `.pos` (a column from 0, or `None` for the rule as a whole); `str()` is `'column N: message'`, or just `message` when `pos` is `None`.
  - `notation.parse_notation(text, names)` also reads `->`, `->+`, `...`, `<-`, `-bN` in a bundle, and `<=` `>=` `!=` `==` `->` in a test; `notation.ALIASES` maps each typed spelling to its symbol.

- [ ] **Step 1: Write the failing tests**

In `editor/tests/test_notation.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 133, replace

```python
                    parse_notation(text, NAMES)


class ShippedTest(unittest.TestCase):
```

   with

```python
                    parse_notation(text, NAMES)

    def test_typed_spellings(self):
        # NOTATION.md, Typing: ASCII for the symbols; the editor writes the symbols
        for typed, written in (('#1 A -> S / B _ (B)', '#1 A → S / B _ (B)'),
                               ('#7 B ->+ S / A _', '#7 B →⁺ S / A _'),
                               ('#1 A -> S / ^ _ (B) ... ‹*›', '#1 A → S / ^ _ (B) … ‹*›'),
                               ('#1 A -> [+b1 -b2]', '#1 A → [+b1 −b2]'),
                               ('#818 ∅ -> # / [+b16]{3} <- _ A B', '#818 ∅ → # / [+b16]{3} ⟵ _ A B'),
                               ('#1 A<F1 >= 50> -> S / ^ _', '#1 A<F1 ≥ 50> → S / ^ _'),
                               ('#1 A<F1 <= 5> -> S / ^ _', '#1 A<F1 ≤ 5> → S / ^ _'),
                               ('#1 A<F1 != 5> -> S / ^ _', '#1 A<F1 ≠ 5> → S / ^ _'),
                               ('#1 A<F1 == 5> -> S / ^ _', '#1 A<F1 = 5> → S / ^ _'),
                               ('#1 A<F1 -> R1> -> S / ^ _', '#1 A<F1 → R1> → S / ^ _')):
            with self.subTest(typed=typed):
                self.assertEqual(notation_line(parse_notation(typed, NAMES), NAMES).text, written)

    def test_names_are_not_typed_symbols(self):
        # '==', '!=' and '-' can be phoneme names: only symbols have typed spellings
        ph = [Phoneme(n.encode().rjust(2, b'\0'), 0, 0, []) for n in ('A', '==', '!=', '-', '.')]
        names = ph_names(ph)
        for text in ('#1 == → - / . _ !=', '#1 - → == / _ . .'):
            with self.subTest(text=text):
                self.assertEqual(notation_line(parse_notation(text, names), names).text, text)

    def test_errors_of_the_whole_rule_have_no_column(self):
        with self.assertRaises(ParseError) as cm:
            parse_notation('#1 ∅ → S', NAMES)
        self.assertIsNone(cm.exception.pos)
        self.assertEqual(str(cm.exception), 'no focus: ∅ on the left needs right context')


class ShippedTest(unittest.TestCase):
```

2. Near line 222, replace

```python
                    self.assertTrue(reads_back(rule, ph_id))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
```

   with

```python
                    self.assertTrue(reads_back(rule, ph_id))

    def test_changed_rules(self):
        # shipped rules with an instruction dropped, repeated or swapped, or an operand word
        # changed: notation_line never raises, and a line it gives reads back to the rule
        rng = random.Random(5)
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            rules = [x for m in f.modules.values() for x in m.rules]
            for _ in range(300):
                rule = rng.choice(rules)
                instrs = [Instr(i.op, list(i.args)) for i in rule.instrs]
                k, c = rng.randrange(len(instrs)), rng.random()
                if c < 0.25 and len(instrs) > 1:
                    del instrs[k]
                elif c < 0.5:
                    instrs.insert(rng.randrange(len(instrs) + 1), instrs[k])
                elif c < 0.75:
                    j = rng.randrange(len(instrs))
                    instrs[k], instrs[j] = instrs[j], instrs[k]
                else:
                    ints = [n for n, a in enumerate(instrs[k].args) if isinstance(a, int)]
                    if ints:
                        instrs[k].args[rng.choice(ints)] = rng.randrange(0x10000)
                new = Rule(rule.iterative, instrs)
                line = notation_line(new, names)
                if line is not None:
                    with self.subTest(lang=lang, text=line.text):
                        self.assertEqual(words(parse_notation(line.text, names)), words(new))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest discover -s editor/tests -t editor -p test_notation.py`
Expected: FAILED (failures=1, errors=10): each of the 10 typed spellings in `test_typed_spellings` raises `ParseError`, and `test_errors_of_the_whole_rule_have_no_column` fails (`pos` is 0, the text says `column 1: …`). `test_names_are_not_typed_symbols` and `test_changed_rules` already pass: they guard the change.

- [ ] **Step 3: Implement**

In `editor/ivxedit/listing.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 260, replace

```python
class ParseError(ValueError):
    """a line that does not read; pos is the column of the problem"""

    def __init__(self, message, pos=0):
        super().__init__('column %d: %s' % (pos + 1, message))
        self.message, self.pos = message, pos
```

   with

```python
class ParseError(ValueError):
    """a line that does not read; pos is the column of the problem, or None when the
    problem is the rule as a whole"""

    def __init__(self, message, pos=None):
        super().__init__(message if pos is None else 'column %d: %s' % (pos + 1, message))
        self.message, self.pos = message, pos
```

2. Near line 510, replace

```python
    for i, (ins, k) in enumerate(zip(rule.instrs, starts)):
        name, pos = MNEMONICS[ins.op], poss[i] if poss else 0
        if k + COUNT_WORD.get(ins.op, 0) >= len(ws):
```

   with

```python
    for i, (ins, k) in enumerate(zip(rule.instrs, starts)):
        name, pos = MNEMONICS[ins.op], poss[i] if poss else None
        if k + COUNT_WORD.get(ins.op, 0) >= len(ws):
```


In `editor/ivxedit/notation.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 367, replace

```python
  | (?P<bundle>\[[^\]]*\])
  | (?P<test><[^\s<>.]+(?:\.last)?(?:\.hi)?\ (?:→\ \w+|[<≤=≠≥>]\ -?[0-9]+)>)
  | (?P<rep>\{[0-9]+(?:,[0-9]+)?\}|\*)
  | (?P<block>\{[^{}]*\})
  | (?P<arrow>→⁺|→)
  | (?P<once>once(?!\S))
  | (?P<sym>[()∅↺/_…^⟵;])
  | (?P<name>[%s]+)
''' % SAFE, re.X)
_HEAD = re.compile(r'\*?#[0-9]+(?!\S)|\*(?!\S)')
_NTEST = re.compile(r'<([^\s<>.]+)(\.last)?(\.hi)? (?:→ (\w+)|([<≤=≠≥>]) (-?[0-9]+))>')
ATOM = ('name', 'esc', 'space', 'any', 'bundle', 'test')
```

   with

```python
  | (?P<bundle>\[[^\]]*\])
  | (?P<test><[^\s<>.]+(?:\.last)?(?:\.hi)?\ (?:(?:→|->)\ \w+|(?:<=|>=|!=|==|[<≤=≠≥>])\ -?[0-9]+)>)
  | (?P<rep>\{[0-9]+(?:,[0-9]+)?\}|\*)
  | (?P<block>\{[^{}]*\})
  | (?P<arrow>→⁺|→|->\+|->)
  | (?P<once>once(?!\S))
  | (?P<sym>[()∅↺/_…^⟵;]|\.\.\.(?!\S)|<-(?!\S))
  | (?P<name>[%s]+)
''' % SAFE, re.X)
_HEAD = re.compile(r'\*?#[0-9]+(?!\S)|\*(?!\S)')
_NTEST = re.compile(r'<([^\s<>.]+)(\.last)?(\.hi)? (?:(?:→|->) (\w+)|(<=|>=|!=|==|[<≤=≠≥>]) (-?[0-9]+))>')
# what may be typed for a symbol (NOTATION.md, Typing); the editor writes the symbol
ALIASES = {'->': '→', '->+': '→⁺', '...': '…', '<-': '⟵', '<=': '≤', '>=': '≥', '!=': '≠', '==': '='}
ATOM = ('name', 'esc', 'space', 'any', 'bundle', 'test')
```

2. Near line 403, replace

```python
        else:
            out.append(Tok(m.lastgroup, m.group(), i, not gap))
            gap = False
```

   with

```python
        else:
            t = m.group()
            if m.lastgroup in ('arrow', 'sym'):      # not names: '==' may be a phoneme's
                t = ALIASES.get(t, t)
            out.append(Tok(m.lastgroup, t, i, not gap))
            gap = False
```

3. Near line 502, replace

```python
    for part in body.split(' ') if body else []:
        m = re.fullmatch(r'([+−])b([0-9]+)', part)
        bit = dec(m.group(2), t.pos) if m else 99
```

   with

```python
    for part in body.split(' ') if body else []:
        m = re.fullmatch(r'([+−-])b([0-9]+)', part)
        bit = dec(m.group(2), t.pos) if m else 99
```

4. Near line 520, replace

```python
        return [w | CAPTURE_IDS[reg]]
    return [w | COMPARE_IDS[cmp], u16(k, t.pos)]

```

   with

```python
        return [w | CAPTURE_IDS[reg]]
    return [w | COMPARE_IDS[ALIASES.get(cmp, cmp)], u16(k, t.pos)]

```


In `editor/ivxedit/NOTATION.md`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 97, replace

```markdown

A bundle or a statement acts on the focus as it is at that point, and only `REPL` moves it
to a new place:
- after a `REPL` that inserts phonemes, the focus is the last inserted segment: in
  `W iy → D ah B Y [−b0]` the bundle clears `b0` on `Y` only (the same for statements);
- after a `REPL` that inserts nothing (`→ ∅`), it is the segment before the deleted ones;
- `SETF` does not move the focus. `SETF+` and `REPL+` first move it one segment right, and
  what follows acts there.

```

   with

```markdown

A bundle or a statement acts on the focus as it is at that point. `SETF` and statements do
not move the focus; `REPL`, `REPL+` and `SETF+` do:
- after a `REPL` that inserts phonemes, the focus is the last inserted segment: in
  `W iy → D ah B Y [−b0]` the bundle clears `b0` on `Y` only (the same for statements);
- after a `REPL` that inserts nothing (`→ ∅`), it is the segment before the deleted ones;
- `SETF+` moves the focus one segment right and sets its bits there; `REPL+` moves it one
  segment right and then replaces from there, leaving the focus as `REPL` does (the two
  points above).

```

2. Near line 129, replace

```markdown
  the rest of the match fails, the rule resumes after that segment.
- `(X)` is optional, but at the end of the segment list it fails, as a required element
  does: the rule gives up there.
- `:=s` and `:=u` clamp the accumulator itself (to −127…127 and 0…255) before storing it, so
```

   with

```markdown
  the rest of the match fails, the rule resumes after that segment.
- The ends of the segment list differ. On the right, an element that would read past the
  last segment, `(X)` included, fails the rule: it gives up there (a `RULE*` that has
  changed something starts again from the first segment). Before `⟵`, an element that
  would read before the first segment, `(X)` included, fails only this start position: the
  rule is tried at the next one.
- `:=s` and `:=u` clamp the accumulator itself (to −127…127 and 0…255) before storing it, so
```

3. Near line 152, replace

```markdown
- spacing: `( A)` and `(A)`.

```

   with

```markdown
- spacing: `( A)` and `(A)`.

## Typing

The reader also takes ASCII for the symbols that have one; the editor writes the symbols:

| Symbol | Typed |
|---|---|
| `→`, `→⁺` | `->`, `->+` |
| `…` | `...` |
| `⟵` | `<-` |
| `−` in a bundle | `-`: `[+b20 -b24]` |
| `≤` `≥` `≠` `=` in a test | `<=` `>=` `!=` `==` |
| `→` in a capture | `->`: `<F1 -> R1>` |

`...` and `<-` stand alone, with a space after them. `->+` is always `→⁺`: when the first
phoneme on the right is `+`, write `-> +`. Phoneme names are never read as these spellings:
`==` and `!=` are names where a name goes. The other symbols (`∅`, `↺`, `·`, `␠`, `‹ ›`)
have no ASCII spelling; the rule editor's context menu inserts them.

A line that starts with `RULE` or `RULE*` is read as instruction form, any other line as
notation.

```


- [ ] **Step 4: Run the tests to see them pass**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[ln]*.py"`
Expected: OK, and the coverage line unchanged: `rules in notation: M0 5691/5714, M1 1404/1404, M2 3881/3885, M3 878/882, M4 608/608, M5 3064/3071`.
Then the whole suite once: `python -m unittest discover -s editor/tests -t editor` → OK, 149 tests (1 skipped on Windows).

- [ ] **Step 5: Commit**

```bash
git add editor/ivxedit/listing.py editor/ivxedit/notation.py editor/ivxedit/NOTATION.md editor/tests/test_notation.py
git commit -m "ivxedit notation: ASCII spellings for typing, errors of a whole rule without a column; NOTATION.md: Typing, the ends of the list, the focus after REPL+/SETF+" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: Rule edits

**Files:**
- Modify: `editor/ivxedit/edits.py` (new section `# --- rules`), `editor/ivxedit/listing.py` (`_read_instr`)
- Test: `editor/tests/test_edits.py`, `editor/tests/test_listing.py`

**Interfaces:**
- Consumes (Task 1 and existing): `listing.ParseError` (with `pos` `None` for a whole rule), `parse_rule`, `words`; `notation.parse_notation`; `rules.REPL`, `REPL_NEXT`, `Instr`, `Rule`; `checks.rule_label`; `edits.Edit`, `EditError`.
- Produces (Task 3 relies on these exact names):
  - `edits.read_rule(text, names) -> Rule`: instruction form when the stripped line starts with `RULE` or `RULE*`, notation otherwise; raises `listing.ParseError` with `pos` counted in `text` (leading spaces included).
  - `edits.copy_rule(rule) -> Rule` (new `Instr` objects, no `legacy` marks).
  - `edits.set_rule(f, name, i, rule)` → `Edit` or `None` (same words); `edits.insert_rule(f, name, i, rule)` (i up to the number of rules) → `Edit`; `edits.delete_rule(f, name, i)` → `Edit`; `edits.move_rule(f, name, i, step)` (step −1 or 1) → `Edit` or `None` at either end. Each `Edit` has `obj` = the `Module`, `attr` `'rules'`, `target` `('rule', name, index of the rule afterwards)`. An unknown module or index raises `EditError`.
  - `listing.parse_rule` no longer sets `Instr.legacy`.

- [ ] **Step 1: Write the failing tests**

In `editor/tests/test_edits.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 1, replace

```python
"""Voice-data edits (spec: GUI, step 4): each edit changes what it says, undo restores the
bytes exactly, and the edited file saves and loads again."""
import unittest
```

   with

```python
"""Edits (spec: GUI, steps 4 and 5): each edit changes what it says, undo restores the bytes
exactly, and the edited file saves and loads again."""
import re
import unittest
```

2. Near line 9, replace

```python
from ivxedit.info import a1_text, header_fields
from ivxedit.model import PNAMES, ParamItem, VarItem, load
from ivxedit.writer import to_bytes
```

   with

```python
from ivxedit.info import a1_text, header_fields
from ivxedit.listing import ParseError, ph_names, rule_line, words
from ivxedit.model import PNAMES, ParamItem, VarItem, load
from ivxedit.notation import notation_line
from ivxedit.writer import to_bytes
```

3. Near line 249, replace

```python

if __name__ == '__main__':
```

   with

```python

class RuleEditTest(unittest.TestCase):
    def test_read_rule(self):
        # notation or instruction form, by the first word; columns count the leading spaces
        f = sw()
        names = ph_names(f.phonemes)
        rule = edits.read_rule('#1 L → M / _ O', names)
        self.assertEqual(rule_line(rule).text, 'RULE SEEK L ; R O ; REPL del 1 -> M ; NEXT #1')
        self.assertEqual(words(edits.read_rule('  RULE SEEK L ; R O ; REPL del 1 -> M ; NEXT #1 ', names)),
                         words(rule))
        for text in ('  #1 L → QQ / _ O', '  RULE SEEK L ; R QQ ; NEXT #1'):
            with self.subTest(text=text), self.assertRaises(ParseError) as cm:
                edits.read_rule(text, names)
            self.assertEqual(cm.exception.pos, text.index('QQ'))

    def test_rule_edits_undo(self):
        # spec: Testing, 11, for rules: each edit does what it says; undo restores the bytes
        f = sw()
        names, m, before = ph_names(f.phonemes), f.modules['M0'], to_bytes(f)
        r0, r1, n = m.rules[0], m.rules[1], len(m.rules)
        done = []

        def do(e, target):
            self.assertEqual(e.target, target)
            e.apply()
            done.append(e)
        do(edits.insert_rule(f, 'M0', 0, edits.read_rule('#1 L → M / _ O', names)), ('rule', 'M0', 0))
        self.assertEqual(notation_line(m.rules[0], names).text, '#1 L → M / _ O')
        do(edits.set_rule(f, 'M0', 0, edits.read_rule('#2 L → M / _ A', names)), ('rule', 'M0', 0))
        self.assertIsNone(edits.set_rule(f, 'M0', 0, edits.read_rule('#2 L → M / _ A', names)))
        do(edits.move_rule(f, 'M0', 0, 1), ('rule', 'M0', 1))
        self.assertIs(m.rules[0], r0)
        self.assertIsNone(edits.move_rule(f, 'M0', 0, -1))
        self.assertIsNone(edits.move_rule(f, 'M0', n, 1))
        do(edits.insert_rule(f, 'M0', 3, edits.copy_rule(r1)), ('rule', 'M0', 3))
        self.assertEqual(words(m.rules[3]), words(r1))
        do(edits.delete_rule(f, 'M0', n + 1), ('rule', 'M0', n))
        self.assertEqual(len(m.rules), n + 1)
        self.assertEqual(errors(check(f)), [])
        again = load(to_bytes(f))
        self.assertEqual([rule_line(x).text for x in again.modules['M0'].rules],
                         [rule_line(x).text for x in m.rules])
        for e in reversed(done):
            e.revert()
        self.assertEqual(to_bytes(f), before)

    def test_rule_refusals(self):
        f = sw()
        rule = edits.read_rule('#1 L → M / _ O', ph_names(f.phonemes))
        n = len(f.modules['M0'].rules)
        for call in (lambda: edits.set_rule(f, 'M0', n, rule), lambda: edits.set_rule(f, 'M9', 0, rule),
                     lambda: edits.insert_rule(f, 'M0', n + 1, rule), lambda: edits.insert_rule(f, 'M0', -1, rule),
                     lambda: edits.delete_rule(f, 'M0', n), lambda: edits.move_rule(f, 'M0', -1, 1)):
            with self.assertRaises(EditError):
                call()
        self.assertIsNotNone(edits.insert_rule(f, 'M0', n, rule))

    def test_carrying_repl_words(self):
        # a shipped carrying REPL word stays exempt while it is unchanged; a copy is new and refused
        f = load(ivx_bytes('da'))
        names = ph_names(f.phonemes)
        name, i, r = next((n, i, r) for n, m in sorted(f.modules.items()) for i, r in enumerate(m.rules)
                          if any(x.legacy for x in r.instrs))
        text = rule_line(r).text
        self.assertFalse(any(x.legacy for x in edits.read_rule(text, names).instrs))
        relabelled = re.sub(r'NEXT #[0-9]+$', 'NEXT #9999', text)
        self.assertNotEqual(relabelled, text)
        edits.set_rule(f, name, i, edits.read_rule(relabelled, names)).apply()
        self.assertEqual(check(f), [])
        edits.insert_rule(f, name, i + 1, edits.copy_rule(f.modules[name].rules[i])).apply()
        self.assertEqual([p.target for p in check(f)], [('rule', name, i + 1)])


if __name__ == '__main__':
```


In `editor/tests/test_listing.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 3, replace

```python

from ivxedit.checks import check
from ivxedit.listing import MNEMONICS, ParseError, parse_rule, ph_names, ph_text, rule_line, words
```

   with

```python

from ivxedit.listing import MNEMONICS, ParseError, parse_rule, ph_names, ph_text, rule_line, words
```

2. Near line 157, replace

```python

    def test_legacy_repl_flag_and_checks_survive(self):
        # a REPL word with a carry (5 in the shipped files) is read back as legacy, so checks
        # report nothing for an unchanged rule
        legacy = 0
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names, before = ph_names(f.phonemes), check(f)
            for name, m in f.modules.items():
                for i, r in enumerate(m.rules):
                    new = parse_rule(rule_line(r).text, names)
                    self.assertEqual([x.legacy for x in new.instrs], [x.legacy for x in r.instrs],
                                     (lang, name, i))
                    legacy += sum(x.legacy for x in r.instrs)
                    m.rules[i] = new
            self.assertEqual(check(f), before, lang)
        self.assertEqual(legacy, 5)

```

   with

```python

    def test_typed_lines_are_never_legacy(self):
        # only a loaded file marks a carrying REPL word legacy (5 rules); a line read back
        # does not (edits.set_rule keeps the mark of an unchanged one)
        n = 0
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            for m in f.modules.values():
                for r in m.rules:
                    if any(x.legacy for x in r.instrs):
                        n += 1
                        self.assertFalse(any(x.legacy for x in parse_rule(rule_line(r).text, names).instrs))
        self.assertEqual(n, 5)

```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[el]*.py"`
Expected: FAILED: the four `RuleEditTest` tests (`AttributeError: module 'ivxedit.edits' has no attribute 'read_rule'`) and `test_typed_lines_are_never_legacy` (`parse_rule` still marks the carrying words).

- [ ] **Step 3: Implement**

In `editor/ivxedit/listing.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 39, replace

```python
from .rules import (FIXED, PH_OPS, REPL, REPL_NEXT, Instr, Rule, encode_rule, operand_count,
                    ph_positions, rep_carries, rep_split)

```

   with

```python
from .rules import (FIXED, PH_OPS, REPL, REPL_NEXT, Instr, Rule, encode_rule, operand_count,
                    ph_positions, rep_split)

```

2. Near line 467, replace

```python
    check_shape(ins, mp)
    if op in (REPL, REPL_NEXT) and isinstance(args[0], int) and rep_carries(args[0]):
        ins.legacy = True   # as decode_instr marks it: the shipped files have 5 such words
    return ins

```

   with

```python
    check_shape(ins, mp)
    return ins      # never legacy: edits.set_rule keeps the mark of an unchanged shipped REPL

```


In `editor/ivxedit/edits.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 1, replace

```python
"""Edits of the voice data (spec: GUI, step 4). Each function checks its input and returns
an Edit, which replaces one attribute of a model object with a new value, or None when
the value is already there. Values are replaced, never changed in place, so undo (putting
the old value back) is exact."""
import bisect
```

   with

```python
"""Edits of the voice data and the rules (spec: GUI, steps 4 and 5). Each function checks its
input and returns an Edit, which replaces one attribute of a model object with a new value,
or None when the value is already there. Values are replaced, never changed in place, so
undo (putting the old value back) is exact."""
import bisect
```

2. Near line 11, replace

```python
from .checks import ph_label, rule_label
from .model import PNAMES, ParamItem, Phoneme, VarItem
from .xref import phoneme_uses
```

   with

```python
from .checks import ph_label, rule_label
from .listing import ParseError, parse_rule, words
from .model import PNAMES, ParamItem, Phoneme, VarItem
from .notation import parse_notation
from .rules import REPL, REPL_NEXT, Instr, Rule
from .xref import phoneme_uses
```

3. Near line 291, replace

```python
                ('lexicon', max(0, min(i, len(entries) - 1))))
```

   with

```python
                ('lexicon', max(0, min(i, len(entries) - 1))))


# --- rules: a module's rule list is replaced as a whole
def read_rule(text, names):
    """a rule as typed -> Rule: instruction form when the line starts with RULE or RULE*,
    notation otherwise (names from listing.ph_names); raises listing.ParseError"""
    body, lead = text.strip(), len(text) - len(text.lstrip())
    try:
        if body.split(' ', 1)[0] in ('RULE', 'RULE*'):
            return parse_rule(body, names)
        return parse_notation(body, names)
    except ParseError as e:
        raise ParseError(e.message, None if e.pos is None else e.pos + lead) from None


def _module(f, name, i, end):
    """f.modules[name]; i must index one of its rules (or, with end, be one past the last)"""
    m = f.modules.get(name)
    if m is None:
        raise EditError('there is no module %s' % name)
    if not 0 <= i < len(m.rules) + end:
        raise EditError('%s has no rule %d' % (name, i))
    return m


def _iwords(ins):
    return (ins.op,) + tuple(a if isinstance(a, int) else (id(a.ph), a.delta) for a in ins.args)


def _marked(rule, old):
    """rule, each REPL marked legacy if old has the same REPL marked so: an unedited carrying
    word of a shipped file stays exempt from the checks, and any other is new and refused"""
    kept = {_iwords(x) for x in old.instrs if x.legacy} if old is not None else set()
    for x in rule.instrs:
        x.legacy = x.op in (REPL, REPL_NEXT) and _iwords(x) in kept
    return rule


def copy_rule(rule):
    """a new Rule with the same instructions"""
    return Rule(rule.iterative, [Instr(x.op, list(x.args)) for x in rule.instrs])


def set_rule(f, name, i, rule):
    """rule i of module `name` becomes rule (from read_rule)"""
    m = _module(f, name, i, False)
    old = m.rules[i]
    if words(rule) == words(old):
        return None
    rules = m.rules[:i] + [_marked(rule, old)] + m.rules[i + 1:]
    return Edit(m, 'rules', rules, 'Edit %s' % rule_label(name, i, old), ('rule', name, i))


def insert_rule(f, name, i, rule):
    """rule inserted as rule i of module `name` (i up to its number of rules)"""
    m = _module(f, name, i, True)
    rules = m.rules[:i] + [_marked(rule, None)] + m.rules[i:]
    return Edit(m, 'rules', rules, 'Insert %s rule %d' % (name, i), ('rule', name, i))


def delete_rule(f, name, i):
    m = _module(f, name, i, False)
    rules = m.rules[:i] + m.rules[i + 1:]
    return Edit(m, 'rules', rules, 'Delete %s' % rule_label(name, i, m.rules[i]),
                ('rule', name, max(0, min(i, len(rules) - 1))))


def move_rule(f, name, i, step):
    """rule i moved up (step -1) or down (1); None at either end of the module"""
    m = _module(f, name, i, False)
    j = i + step
    if not 0 <= j < len(m.rules):
        return None
    rules = list(m.rules)
    rules[i], rules[j] = rules[j], rules[i]
    return Edit(m, 'rules', rules, 'Move %s %s' % (rule_label(name, i, m.rules[i]), 'up' if step < 0 else 'down'),
                ('rule', name, j))
```


- [ ] **Step 4: Run the tests to see them pass**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[el]*.py"`
Expected: OK. Then the whole suite once → OK, 153 tests (1 skipped).

- [ ] **Step 5: Commit**

```bash
git add editor/ivxedit/edits.py editor/ivxedit/listing.py editor/tests/test_edits.py editor/tests/test_listing.py
git commit -m "ivxedit: rule edits (read_rule, set/insert/delete/move_rule); a carrying REPL word stays exempt only while unchanged" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Editing rules in the window

**Files:**
- Modify (whole file): `editor/ivxedit/gui/rulesview.py`
- Modify: `editor/ivxedit/gui/mainwindow.py`
- Create: `editor/tests/test_rule_editing.py`
- Docs: `AGENTS.md`, `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`

**Interfaces:**
- Consumes (Tasks 1 and 2): `edits.read_rule`, `copy_rule`, `set_rule`, `insert_rule`, `delete_rule`, `move_rule`; `listing.ParseError` (`.pos` may be `None`); the window's `try_edit(fn, *args)` (→ `False` when refused), `EditCommand`, `_changed`, `refresh`, `show_target`, `show_rule`.
- Produces:
  - `RulesView(on_phoneme=None, edit=None, status=None)`: `set_module(name, module, notation, f=None, names=None)` (editable when `f` is given; shown again with the same rules and lines it stays as it is), `set_instruction_form(on)`, `goto_rule(i)`, `current()`, `rule_at(row)`, `row_of(i)`, `ref_at(rule, col)`, `edit_rule(i=None)`, `insert_rule()`, `commit()`, `close_editor(refill=True)`, `copy_rule()`, `delete_rule()`, `move_rule(step)`, `rule_actions` (QActions: `Edit rule`, `Insert rule`, `Copy rule`, `Delete rule`, `Move rule up`, `Move rule down`), `editor` (the open `RuleEditor` or `None`; its `.i`, `.adding`), `blank` (the text line of a new rule or `None`), `lines`.
  - `MainWindow.notation` is `{Rule: Line or None}`; `_rules_view(name)` returns the module's view showing its current rules; `uses` is recomputed after every change; the tree's module labels follow the rule counts.

- [ ] **Step 1: Write the failing tests**

Create `editor/tests/test_rule_editing.py`:

```python
"""Editing rules in the window, offscreen (spec: GUI, step 5; Testing, 11)."""
import unittest

from ivxedit.listing import rule_line
from ivxedit.notation import notation_line
from ivxedit.writer import to_bytes
from tests.qt import SlotErrors, app, needs_qt, temp_settings
from tests.util import ivx_path


def settle():
    """run the queued view refreshes"""
    for _ in range(3):
        app().processEvents()


@needs_qt
class RuleEditingTest(unittest.TestCase):
    def setUp(self):
        from ivxedit.gui.mainwindow import MainWindow
        app()
        self.win = MainWindow(temp_settings(self))
        self.win.warn = lambda title, text: None
        self.addCleanup(self._close)
        self.win.open_file(ivx_path('sw'))
        self.before = to_bytes(self.win.f)
        self.win.show_rule('M0', 0)
        self.view = self.win.rule_views['M0']
        self.m = self.win.f.modules['M0']

    def _close(self):
        self.win.undo.setClean()                     # no unsaved-changes question at the end
        self.win.close()

    def key(self, widget, key):
        from PySide6.QtCore import Qt
        from PySide6.QtTest import QTest
        QTest.keyClick(widget, getattr(Qt.Key, 'Key_' + key))
        settle()

    def type_line(self, text):
        """the open editor's text becomes text, then Enter"""
        self.assertIsNotNone(self.view.editor)
        self.view.editor.setText(text)
        self.key(self.view.editor, 'Return')

    def action(self, text):
        next(a for a in self.view.rule_actions if a.text() == text).trigger()
        settle()

    def test_insert_and_edit(self):
        # a rule added in notation (spec: Testing, 8, in the window), then edited in
        # instruction form; undo restores the file
        w, v, m = self.win, self.view, self.m
        n = len(m.rules)
        v.goto_rule(0)
        with SlotErrors() as errs:
            v.insert_rule()
            self.assertEqual(v.document().blockCount(), n + 1)     # the line the rule is typed on
            self.assertEqual(v.editor.text(), '')
            self.type_line('#1 L -> M / _ O')
            self.assertIsNone(v.editor)
            self.assertEqual(len(m.rules), n + 1)
            self.assertEqual(notation_line(m.rules[1], w.names).text, '#1 L → M / _ O')
            self.assertEqual((v.current(), v.lines[1].text), (1, '#1 L → M / _ O'))
            self.assertEqual(w.module_items['M0'].text(0), 'M0 (%d)' % (n + 1))
            self.assertIn(('M0', 1), w.uses[next(p for p in w.f.phonemes if p.name == b'\0L')])
            self.assertTrue(w.windowTitle().endswith(' *'))
            v.edit_rule(1)
            self.assertEqual(v.editor.text(), '#1 L → M / _ O')
            self.type_line('RULE SEEK L ; R A ; REPL del 1 -> M ; NEXT #2')
            self.assertEqual(rule_line(m.rules[1]).text, 'RULE SEEK L ; R A ; REPL del 1 -> M ; NEXT #2')
            self.assertEqual(v.lines[1].text, '#2 L → M / _ A')
            w.undo.undo()
            w.undo.undo()
            settle()
        self.assertEqual(errs, [])
        self.assertEqual(to_bytes(w.f), self.before)
        self.assertEqual(v.document().blockCount(), n)
        self.assertEqual(w.module_items['M0'].text(0), 'M0 (%d)' % n)

    def test_a_line_that_does_not_read(self):
        # the editor stays open at the problem, the status bar says why, nothing changes
        w, v = self.win, self.view
        text = v.lines[0].text
        with SlotErrors() as errs:
            self.key(v, 'Return')                                  # edits the current rule
            self.assertEqual(v.editor.text(), text)
            self.type_line('#1 L → QQ / _ O')
            self.assertIsNotNone(v.editor)
            self.assertEqual(v.editor.cursorPosition(), '#1 L → QQ / _ O'.index('QQ'))
            self.assertIn("no phoneme 'QQ'", w.statusBar().currentMessage())
            self.key(v.editor, 'Escape')
        self.assertEqual(errs, [])
        self.assertIsNone(v.editor)
        self.assertEqual((w.undo.count(), v.lines[0].text), (0, text))
        self.assertEqual(to_bytes(w.f), self.before)

    def test_unchanged_line_is_no_edit(self):
        v = self.view
        v.edit_rule(0)
        self.key(v.editor, 'Return')
        self.assertIsNone(v.editor)
        self.assertEqual(self.win.undo.count(), 0)

    def test_escape_drops_a_new_line(self):
        v, n = self.view, len(self.m.rules)
        v.insert_rule()
        self.key(v.editor, 'Escape')
        self.assertEqual((v.editor, v.document().blockCount(), self.win.undo.count()), (None, n, 0))

    def test_copy_move_delete(self):
        w, v, m = self.win, self.view, self.m
        old = list(m.rules)
        v.goto_rule(0)
        with SlotErrors() as errs:
            for text in ('Copy rule', 'Move rule down', 'Move rule down', 'Delete rule', 'Move rule up'):
                self.action(text)
        self.assertEqual(errs, [])
        self.assertEqual(m.rules[:4], [old[0], old[1], old[3], old[2]])
        self.assertEqual(m.rules[4:], old[4:])
        self.assertEqual(v.current(), 2)
        while w.undo.canUndo():
            w.undo.undo()
        settle()
        self.assertEqual(to_bytes(w.f), self.before)

    def test_double_click_edits_the_rule(self):
        from PySide6.QtCore import Qt
        from PySide6.QtGui import QTextCursor
        from PySide6.QtTest import QTest
        v = self.view
        cur = QTextCursor(v.document().findBlockByNumber(3))
        cur.setPosition(cur.position() + 1)                        # in the rule number
        QTest.mouseDClick(v.viewport(), Qt.MouseButton.LeftButton, pos=v.cursorRect(cur).center())
        self.assertIsNotNone(v.editor)
        self.assertEqual((v.editor.i, v.editor.text()), (3, v.lines[3].text))

    def test_instruction_form_keeps_the_place(self):
        w, v = self.win, self.view
        w.show()
        w.show_rule('M0', 300)
        settle()
        place = (v.verticalScrollBar().value(), v.textCursor().blockNumber())
        self.assertGreater(place[0], 0)
        for on in (True, False):
            w.instr_action.setChecked(on)
            self.assertEqual((v.verticalScrollBar().value(), v.textCursor().blockNumber()), place)


if __name__ == '__main__':
    unittest.main()
```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest discover -s editor/tests -t editor -p test_rule_editing.py`
Expected: FAILED (errors: the view has no `insert_rule`, `edit_rule`, `rule_actions`, `current`; double-click selects a word).

- [ ] **Step 3: Implement**

Replace the whole of `editor/ivxedit/gui/rulesview.py` with:

```python
"""The rules view: one module, one rule per line, in rule notation where it says the rule
exactly and in instruction form otherwise (those lines tinted); highlighted; double-click
a phoneme to jump to it.

Rules are edited one line at a time (spec: GUI, step 5). Enter, F2 or a double-click opens
an editor over the rule's line; it takes notation or instruction form (edits.read_rule).
Insert opens one on a new line after the current rule, Ctrl+D copies the rule, Delete
removes it, Alt+Up and Alt+Down move it. The context menu has the same."""
from PySide6.QtCore import Qt
from PySide6.QtGui import (QAction, QColor, QKeySequence, QSyntaxHighlighter, QTextBlockFormat,
                           QTextCharFormat, QTextCursor, QTextFormat)
from PySide6.QtWidgets import QLineEdit, QPlainTextEdit, QTextEdit

from .. import edits
from ..listing import ParseError, rule_line
from .views import mono

PREFIX = '%5d  '          # the rule index, as in the Problems panel's "M0 rule 12"
NEW = '    +  '           # the prefix of the line a new rule is typed on
COLOURS = {'op': '#1f5fbf', 'ph': '#a0400a', 'num': '#2a7f2a', 'feat': '#7a2a9a',
           'param': '#8a6d00', 'raw': '#c00000'}
FALLBACK = QColor('#f1ece0')   # background of a rule shown in instruction form for want of notation
REFUSED = 'QLineEdit { background: #fde2e2; }'     # the editor's text does not read
# the context menu of the editor inserts these (NOTATION.md: Typing)
SYMBOLS = ('→', '→⁺', '∅', '↺', '…', '⟵', '·', '−', '␠', '‹›', '≤', '≥', '≠')


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
        i = self.view.rule_at(self.currentBlock().blockNumber())
        if i is not None and i < len(self.view.lines):
            for s, e, kind, _ in self.view.lines[i].spans:
                self.setFormat(len(PREFIX % i) + s, e - s, self.formats[kind])


class RuleEditor(QLineEdit):
    """the editor over one line of the view: Enter commits, Escape cancels, and losing the
    focus commits too (a line that does not read stays open)"""

    def __init__(self, view, i, adding, text):
        super().__init__(text, view.viewport())
        self.view, self.i, self.adding = view, i, adding
        self.setFont(mono())
        self.textEdited.connect(lambda _: self.setStyleSheet(''))

    def keyPressEvent(self, event):
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter):
            self.view.commit()
        elif event.key() == Qt.Key.Key_Escape:
            self.view.close_editor()
        else:
            super().keyPressEvent(event)

    def focusOutEvent(self, event):
        super().focusOutEvent(event)
        if (event.reason() != Qt.FocusReason.PopupFocusReason and self.view.editor is self
                and self.view.isVisible()):          # not while the window closes
            self.view.commit()

    def contextMenuEvent(self, event):
        menu = self.createStandardContextMenu()
        symbols = menu.addMenu('Insert symbol')
        for s in SYMBOLS:
            symbols.addAction(s, lambda s=s: self.put(s))
        menu.exec(event.globalPos())
        menu.deleteLater()

    def put(self, symbol):
        self.insert(symbol)
        if symbol == '‹›':
            self.cursorBackward(False)              # between the quotes


class RulesView(QPlainTextEdit):
    def __init__(self, on_phoneme=None, edit=None, status=None):
        super().__init__()
        self.on_phoneme = on_phoneme      # called with a Phoneme
        self.edit = edit                  # the window's try_edit(fn, *args); None: read-only
        self.status = status or (lambda text: None)    # shows why a line does not read
        self.setReadOnly(True)
        self.setTextInteractionFlags(Qt.TextInteractionFlag.TextSelectableByMouse
                                     | Qt.TextInteractionFlag.TextSelectableByKeyboard)
        self.setFont(mono())
        self.setLineWrapMode(QPlainTextEdit.LineWrapMode.NoWrap)
        self.document().setUndoRedoEnabled(False)
        self.f = self.name = self.module = None
        self.names = {}                   # listing.ph_names of the file, for reading lines
        self.rules = []                   # the rules shown
        self.lines = []                   # the Line shown for each rule
        self.notation = []                # per rule: its Line in notation, or None
        self.instruction_form = False     # show every rule in instruction form
        self.editor = None                # the open RuleEditor
        self.blank = None                 # the text line a new rule is typed on
        self._hl = _Highlighter(self.document(), self)
        self.rule_actions = []
        for text, key, slot in (('Edit rule', 'F2', lambda: self.edit_rule()),
                                ('Insert rule', 'Ins', self.insert_rule),
                                ('Copy rule', 'Ctrl+D', self.copy_rule),
                                ('Delete rule', 'Del', self.delete_rule),
                                ('Move rule up', 'Alt+Up', lambda: self.move_rule(-1)),
                                ('Move rule down', 'Alt+Down', lambda: self.move_rule(1))):
            a = QAction(text, self)
            a.setShortcut(QKeySequence(key))
            a.setShortcutContext(Qt.ShortcutContext.WidgetShortcut)
            a.triggered.connect(slot)
            self.addAction(a)
            self.rule_actions.append(a)

    def set_module(self, name, module, notation, f=None, names=None):
        """show module; notation: per rule, its notation Line or None (notation.notation_line).
        Its rules can be edited when f (the file) and names are given. Shown again with the
        same rules and lines, the view stays as it is (an open editor too)"""
        self.f, self.names = f, names or {}
        same = (module is self.module and list(module.rules) == self.rules
                and list(notation) == self.notation)
        self.name = name
        if not same:
            self.module, self.rules, self.notation = module, list(module.rules), list(notation)
            self.close_editor(refill=False)
            self._fill()

    def set_instruction_form(self, on):
        if on != self.instruction_form:
            self.instruction_form = on
            self._fill()

    def _fill(self):
        """the text from the rules, keeping the scroll position and the current line"""
        scroll, row = self.verticalScrollBar().value(), self.textCursor().blockNumber()
        self.lines = [n if n is not None and not self.instruction_form else rule_line(r)
                      for r, n in zip(self.rules, self.notation)]
        rows = [PREFIX % i + ln.text for i, ln in enumerate(self.lines)]
        if self.blank is not None:
            rows.insert(self.blank, NEW)
        self.setExtraSelections([])
        self.setPlainText('\n'.join(rows))
        tint = QTextBlockFormat()
        tint.setBackground(FALLBACK)
        for i, n in enumerate(self.notation):
            if n is None:
                QTextCursor(self.document().findBlockByNumber(self.row_of(i))).setBlockFormat(tint)
        self.setTextCursor(QTextCursor(self.document().findBlockByNumber(
            min(row, self.document().blockCount() - 1))))
        self.verticalScrollBar().setValue(scroll)

    def rule_at(self, row):
        """the index of the rule on text line row; None for the line of a new rule"""
        if self.blank is not None and row >= self.blank:
            if row == self.blank:
                return None
            row -= 1
        return row if 0 <= row < len(self.rules) else None

    def row_of(self, i):
        """the text line of rule i"""
        return i + (self.blank is not None and i >= self.blank)

    def current(self):
        """the index of the rule the cursor is on, or None"""
        return self.rule_at(self.textCursor().blockNumber())

    def ref_at(self, rule, col):
        """the PhRef at column `col` of the line of rule `rule`, or None"""
        if not 0 <= rule < len(self.lines):
            return None
        col -= len(PREFIX % rule)
        for s, e, kind, obj in self.lines[rule].spans:
            if kind == 'ph' and s <= col < e:
                return obj
        return None

    def goto_rule(self, i):
        """make rule i current and mark it; scroll it to the middle unless it is in view"""
        if not 0 <= i < len(self.lines):
            return
        cur = QTextCursor(self.document().findBlockByNumber(self.row_of(i)))
        r = self.cursorRect(cur)
        seen = r.top() >= 0 and r.bottom() <= self.viewport().height()
        self.setTextCursor(cur)
        if not seen:
            self.centerCursor()
        sel = QTextEdit.ExtraSelection()
        sel.format.setBackground(QColor('#fff3b0'))
        sel.format.setProperty(QTextFormat.Property.FullWidthSelection, True)
        sel.cursor = cur
        self.setExtraSelections([sel])

    # --- editing
    def edit_rule(self, i=None):
        """open the editor on rule i (default: the current rule), with the line as shown"""
        i = self.current() if i is None else i
        if self.edit is not None and i is not None and 0 <= i < len(self.lines):
            self._open(i, False, self.lines[i].text)

    def insert_rule(self):
        """open the editor on a new line after the current rule"""
        if self.edit is None or self.module is None:
            return
        i = self.current()
        at = len(self.rules) if i is None else i + 1
        self.close_editor(refill=False)
        self.blank = at
        self._fill()
        self._open(at, True, '')

    def _open(self, i, adding, text):
        if self.editor is not None and not adding:
            self.close_editor()
        self.setTextCursor(QTextCursor(self.document().findBlockByNumber(
            self.blank if adding else self.row_of(i))))
        self.editor = RuleEditor(self, i, adding, text)
        self._place()
        self.editor.show()
        self.editor.setFocus()

    def _place(self):
        """put the editor over its line, after the prefix"""
        ed = self.editor
        if ed is None:
            return
        block = self.document().findBlockByNumber(self.blank if ed.adding else self.row_of(ed.i))
        cur = QTextCursor(block)
        cur.setPosition(block.position() + len(PREFIX % ed.i))
        r = self.cursorRect(cur)
        h = max(r.height() + 6, ed.sizeHint().height())
        ed.setGeometry(r.left(), r.top() - (h - r.height()) // 2, self.viewport().width() - r.left(), h)

    def commit(self):
        """read the editor's line and make the edit. A line that does not read keeps the editor
        open, its cursor at the problem. -> False if the line or the edit is refused"""
        ed = self.editor
        if ed is None:
            return True
        try:
            rule = edits.read_rule(ed.text(), self.names)
        except ParseError as e:
            self.status(str(e))
            ed.setStyleSheet(REFUSED)
            if e.pos is not None:
                ed.setCursorPosition(e.pos)
            return False
        self.status('')                     # the line reads: no message about an earlier try
        self.close_editor()                 # before the edit: the window then shows the new rules
        fn = edits.insert_rule if ed.adding else edits.set_rule
        return self.edit(fn, self.f, self.name, ed.i, rule) is not False

    def close_editor(self, refill=True):
        """drop the open editor (and the line of a new rule) without an edit"""
        ed, self.editor = self.editor, None
        if ed is not None:
            ed.hide()
            ed.deleteLater()
            self.setFocus()
        if self.blank is not None:
            self.blank = None
            if refill:
                self._fill()

    def copy_rule(self):
        i = self.current()
        if self.edit is not None and i is not None:
            self.edit(edits.insert_rule, self.f, self.name, i + 1, edits.copy_rule(self.rules[i]))

    def delete_rule(self):
        i = self.current()
        if self.edit is not None and i is not None:
            self.edit(edits.delete_rule, self.f, self.name, i)

    def move_rule(self, step):
        i = self.current()
        if self.edit is not None and i is not None:
            self.edit(edits.move_rule, self.f, self.name, i, step)

    # --- events
    def keyPressEvent(self, event):
        if event.key() in (Qt.Key.Key_Return, Qt.Key.Key_Enter) and self.edit is not None:
            self.edit_rule()
        else:
            super().keyPressEvent(event)

    def contextMenuEvent(self, event):
        menu = self.createStandardContextMenu(event.pos())
        if self.edit is not None:
            if not self.textCursor().hasSelection():
                self.setTextCursor(self.cursorForPosition(event.pos()))
            menu.addSeparator()
            for a in self.rule_actions:
                menu.addAction(a)
        menu.exec(event.globalPos())
        menu.deleteLater()

    def resizeEvent(self, event):
        super().resizeEvent(event)
        self._place()

    def scrollContentsBy(self, dx, dy):
        super().scrollContentsBy(dx, dy)
        self._place()

    def mouseDoubleClickEvent(self, event):
        cur = self.cursorForPosition(event.position().toPoint())
        i = self.rule_at(cur.blockNumber())
        ref = self.ref_at(i, cur.positionInBlock()) if i is not None else None
        if ref is not None and ref.ph is not None and self.on_phoneme is not None:
            self.on_phoneme(ref.ph)
        elif i is not None and self.edit is not None:
            self.edit_rule(i)
        else:
            super().mouseDoubleClickEvent(event)
```


In `editor/ivxedit/gui/mainwindow.py`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 59, replace

```python
        self.names = {}               # ph_names of the open file
        self.notation = {}            # module name -> [notation Line or None] per rule
        self.target = None            # what the last edit, undo or redo changed (Edit.target)
```

   with

```python
        self.names = {}               # ph_names of the open file
        self.notation = {}            # Rule -> its notation Line, or None
        self.target = None            # what the last edit, undo or redo changed (Edit.target)
```

2. Near line 270, replace

```python
        lex = self.f.lexicon
        return (tuple((id(p), p.name) for p in self.f.phonemes), None if lex is None else len(lex.entries))

```

   with

```python
        lex = self.f.lexicon
        return (tuple((id(p), p.name) for p in self.f.phonemes), None if lex is None else len(lex.entries),
                tuple((n, len(m.rules)) for n, m in sorted(self.f.modules.items())))

```

3. Near line 283, replace

```python
            self._fill_tree(self._current())
        self._problems()
```

   with

```python
            self._fill_tree(self._current())
        self.uses = phoneme_uses(self.f)              # rule edits change "Used by"
        self._problems()
```

4. Near line 494, replace

```python
    def _notation(self, name):
        """[notation Line or None] for each rule of module `name`, made once per file"""
        if name not in self.notation:
            self.notation[name] = [notation_line(r, self.names) for r in self.f.modules[name].rules]
        return self.notation[name]

```

   with

```python
    def _notation(self, name):
        """[notation Line or None] for each rule of module `name`, made once per rule"""
        out = []
        for r in self.f.modules[name].rules:
            if r not in self.notation:
                self.notation[r] = notation_line(r, self.names)
            out.append(self.notation[r])
        return out

```

5. Near line 506, replace

```python
    def _rules_view(self, name):
        if name not in self.rule_views:
            v = RulesView(self.show_phoneme)
            v.set_instruction_form(self.instr_action.isChecked())
            v.set_module(name, self.f.modules[name], self._notation(name))
            self.stack.addWidget(v)
            self.rule_views[name] = v
        return self.rule_views[name]

```

   with

```python
    def _rules_view(self, name):
        """the view of module `name`, showing its rules as they are now"""
        if name not in self.rule_views:
            v = RulesView(self.show_phoneme, self.try_edit, lambda text: self.statusBar().showMessage(text, 10000))
            v.set_instruction_form(self.instr_action.isChecked())
            self.stack.addWidget(v)
            self.rule_views[name] = v
        v = self.rule_views[name]
        v.set_module(name, self.f.modules[name], self._notation(name), self.f, self.names)
        return v

```


- [ ] **Step 4: Run the tests to see them pass**

Run: `python -m unittest discover -s editor/tests -t editor -p "test_[rge]*.py"`
Expected: OK. Then the whole suite once: `python -m unittest discover -s editor/tests -t editor` → OK, 160 tests (1 skipped).

- [ ] **Step 5: Look at it**

Run `python editor/ivxedit.pyw bin/swrules.ivx` (a real window: the shortcuts need the focus). In Rules ▸ M0: click rule 20 and press Enter: the line editor opens over the line with its notation; type `#1 L -> QQ / _ O` and Enter: the editor turns red, its cursor before `QQ`, and the status bar says `column 9: no phoneme 'QQ'`; Escape. Press Ins: a line `+` opens under rule 20; type `#1 L -> M / _ O` and Enter: rule 21 shows `#1 L → M / _ O`, the tree says `M0 (580)` and the title has `*`. Alt+Up, Alt+Down, Ctrl+D, Del act on the current rule; Ctrl+Z undoes each. View ▸ Instruction form keeps the place. Close without saving: the unsaved-changes question appears.

- [ ] **Step 6: Documentation**

In `AGENTS.md`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 21, replace

```markdown
| `cli/ivxsay.c` | A client of the `infovox230` library (`build/ivxsay.exe` / `build/linux/ivxsay`): `ivxsay -l sw "God morgon."` speaks through the speaker by default (`cli/audio_win.c` waveOut on Windows, `cli/audio_alsa.c` ALSA on Linux) or writes a WAV with `-o out.wav`. Options cover the voice, rate, pitch, FD, sample rate, text mode, frame rate, input encoding (`-e utf8|cp1252`, default UTF-8) and an event trace (`-E`), dictionary files (`-d FILE`: `[!?]word = replacement`, `/…/` phonetic); text can come from a file or stdin. Its WAV output is byte-identical to the DLL's in all 12 languages (`tools/check_ivxsay.py`, run by `make test`) and `-d` must match the text with its replacements made. |
| `editor/` | `ivxedit`, the `.ivx` editor (spec `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`). Steps 1–4 are done. `ivxedit.model.load` reads a file into a model (phonemes, rule modules with phoneme references, tables, character tables, lexicons); `ivxedit.writer.to_bytes`/`save` write it back (byte-identical for all 12 files, phoneme ids relocated, `bin/` refused); `ivxedit.checks.check` lists what would stop the engine loading it, plus warnings (a duplicate phoneme name). `load` raises only `IvxFormatError` for a file it cannot read. The GUI (`python editor/ivxedit.pyw [file.ivx]`, PySide6) shows every section and edits the header, parameters, phonemes, characters and lexicon (`ivxedit.edits`, with undo, Save, Save As and a Problems panel; the rules become editable in step 5), with the rules in the notation `A → B / C _ D` (`ivxedit.notation`, grammar in `editor/ivxedit/NOTATION.md`; 15,526 of the 15,564 shipped rules) and the rest in instruction form (`ivxedit.listing`, whose `parse_rule` reads it back). Its Speak panel runs `ivxsay` on the model (`ivxedit.speak`) and draws waveform, spectrogram and phoneme strip, with A/B against the file as opened. Tests: `make editor-test`. |
| `tools/` | Static-analysis helpers (see below). `full.asm`, `xref.json`, `ops.txt`, `handlers.txt` and `out/*.lst` are generated outputs, and you can regenerate them. |
```

   with

```markdown
| `cli/ivxsay.c` | A client of the `infovox230` library (`build/ivxsay.exe` / `build/linux/ivxsay`): `ivxsay -l sw "God morgon."` speaks through the speaker by default (`cli/audio_win.c` waveOut on Windows, `cli/audio_alsa.c` ALSA on Linux) or writes a WAV with `-o out.wav`. Options cover the voice, rate, pitch, FD, sample rate, text mode, frame rate, input encoding (`-e utf8|cp1252`, default UTF-8) and an event trace (`-E`), dictionary files (`-d FILE`: `[!?]word = replacement`, `/…/` phonetic); text can come from a file or stdin. Its WAV output is byte-identical to the DLL's in all 12 languages (`tools/check_ivxsay.py`, run by `make test`) and `-d` must match the text with its replacements made. |
| `editor/` | `ivxedit`, the `.ivx` editor (spec `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`). Steps 1–5 are done. `ivxedit.model.load` reads a file into a model (phonemes, rule modules with phoneme references, tables, character tables, lexicons); `ivxedit.writer.to_bytes`/`save` write it back (byte-identical for all 12 files, phoneme ids relocated, `bin/` refused); `ivxedit.checks.check` lists what would stop the engine loading it, plus warnings (a duplicate phoneme name). `load` raises only `IvxFormatError` for a file it cannot read. The GUI (`python editor/ivxedit.pyw [file.ivx]`, PySide6) shows every section and edits all of it but the second lexicon and the unknown bytes (`ivxedit.edits`, with undo, Save, Save As and a Problems panel), with the rules in the notation `A → B / C _ D` (`ivxedit.notation`, grammar in `editor/ivxedit/NOTATION.md`; 15,526 of the 15,564 shipped rules) and the rest in instruction form (`ivxedit.listing`, whose `parse_rule` reads it back); a rule is edited as one line in either form (`edits.read_rule`). Its Speak panel runs `ivxsay` on the model (`ivxedit.speak`) and draws waveform, spectrogram and phoneme strip, with A/B against the file as opened. Tests: `make editor-test`. |
| `tools/` | Static-analysis helpers (see below). `full.asm`, `xref.json`, `ops.txt`, `handlers.txt` and `out/*.lst` are generated outputs, and you can regenerate them. |
```

2. Near line 145, replace

```markdown

This starts the editor (the rules are read-only until step 5; View ▸ Instruction form shows every rule in instruction form). Edit ▸ Undo and Redo cover every edit; File ▸ Save never writes into `bin/` and asks for another path instead. Speak looks for `build/ivxsay.exe` (or `build/linux/ivxsay`), or the file set with File ▸ ivxsay…. For offscreen screenshots, set `QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:/Windows/Fonts`.

```

   with

```markdown

This starts the editor (View ▸ Instruction form shows every rule in instruction form). In a rules view, Enter, F2 or a double-click edits the current rule as one line, in notation or instruction form; Insert adds a rule after it, Ctrl+D copies it, Delete removes it and Alt+Up/Alt+Down move it. Edit ▸ Undo and Redo cover every edit; File ▸ Save never writes into `bin/` and asks for another path instead. Speak looks for `build/ivxsay.exe` (or `build/linux/ivxsay`), or the file set with File ▸ ivxsay…. For offscreen screenshots, set `QT_QPA_PLATFORM=offscreen` and `QT_QPA_FONTDIR=C:/Windows/Fonts`.

```


In `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`, make these replacements (each old text occurs exactly once; apply them in order):

1. Near line 186, replace

```markdown

**Speak (`speak.py`):**
```

   with

```markdown

**As built in step 5** (plan `docs/superpowers/plans/2026-10-04-ivxedit-step5-rule-editing.md`): a rule is edited as one line in the rules view, over the line it is shown on (Enter, F2 or a double-click): a line that starts with `RULE` or `RULE*` is read as instruction form, any other as notation (`edits.read_rule`). A line that does not read keeps the editor open, red, with its cursor at the problem and the reason in the status bar; an error of the rule as a whole has no column. Insert opens the editor on a new line after the current rule, Ctrl+D copies the rule, Delete removes it, Alt+Up/Down move it; the context menu has the same, and the editor's context menu inserts the symbols that have no ASCII spelling. Each is an `Edit` that replaces the module's rule list (`set_rule`, `insert_rule`, `delete_rule`, `move_rule`). The notation reader also takes ASCII spellings (`->`, `->+`, `...`, `<-`, `-b5`, `<=` `>=` `!=` `==`; NOTATION.md, Typing). A carrying `REPL` word is exempt from the checks only while it is the unchanged word of a shipped rule: the parser never marks one, and `set_rule` keeps the mark of an unchanged instruction, so a copy or a typed one is refused. After a rule edit the tree's rule counts, the notation of the changed rules and the phonemes' "Used by" are updated; switching View ▸ Instruction form keeps the place.

**Speak (`speak.py`):**
```


- [ ] **Step 7: Commit**

```bash
git add editor/ivxedit/gui/rulesview.py editor/ivxedit/gui/mainwindow.py editor/tests/test_rule_editing.py AGENTS.md docs/superpowers/specs/2026-10-03-ivx-editor-design.md
git commit -m "ivxedit gui: edit rules one line at a time, in notation or instruction form; insert, copy, delete, move; docs for step 5" -m "Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Self-review against the spec

- **"Both forms are editable in step 5, in the same module view"** (spec: Rule notation): the line editor takes either form (`edits.read_rule`, Task 2; `RulesView`, Task 3); the view keeps notation where it exists and the tint for the rest.
- **Every edit a `QUndoCommand`; `*`; Problems after each edit**: rule edits are `edits.Edit`s pushed through `try_edit` (Task 3), so step 4's `EditCommand`, title and checks apply unchanged.
- **Checks before saving**, the carrying `REPL` words: exempt only while unedited (Task 2, `test_carrying_repl_words`).
- **Testing 6** (coverage): unchanged, still checked by `test_coverage` (Task 1 keeps the numbers). **Testing 7** (robustness): `test_random_edits` in both test files plus `test_changed_rules` (Task 1). **Testing 8** (end to end): `EndToEndTest` (model level, step 3) and `test_insert_and_edit` (the same rule added through the window, Task 3). **Testing 11** (undo): `test_rule_edits_undo` (model) and `test_insert_and_edit`, `test_copy_move_delete` (window).
- **Deferred items for step 5** (from the step-3 and step-4 reviews): ASCII spellings, errors without a column, NOTATION.md wording, the changed-rules fuzz (Task 1); `legacy` tied to the original word, indices checked (Task 2); the place kept on View ▸ Instruction form, rule editing outside `mainwindow.py` (Task 3). Already done in step 3: the operand-count and shape checks of the parser and the `REPL` count word computed from `del` and the ids.
