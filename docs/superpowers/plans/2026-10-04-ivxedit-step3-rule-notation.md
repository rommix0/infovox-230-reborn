# ivxedit Step 3: Rule Notation Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show every rule that the `A → B / C _ D` notation can say exactly in that notation (read-only), with a parser for both the notation and the instruction form, and per-module coverage in the GUI.

**Architecture:** `listing.py` gains `parse_rule`, the inverse of `rule_line` (instruction form). A new `notation.py` turns a rule into a `Form` (left context, groups of left-hand side → actions, right context), writes it as text, reads text back into a `Form` and encodes it as instructions; `notation_line` keeps a rule's notation only if reading it back gives the rule's words exactly. `NOTATION.md` is the grammar, with examples a test checks. The rules view shows notation lines and tints the rules left in instruction form; View ▸ Instruction form switches every rule to instruction form.

**Tech Stack:** Python 3.13 (standard library for `listing.py` and `notation.py`), PySide6 ≥ 6.8 for the GUI, `unittest`.

**Spec:** `docs/superpowers/specs/2026-10-03-ivx-editor-design.md` (sections "Rule notation", "GUI", "Testing" 5–8). The previous steps' plans: `docs/superpowers/plans/2026-10-03-ivxedit-step1-model-writer.md`, `docs/superpowers/plans/2026-10-04-ivxedit-step2-readonly-gui.md`.

## Global Constraints

- "A rule is shown in notation only if `notation → instructions → bytes` reproduces its original bytes; otherwise it is shown in instruction form, the `ivxdasm.py` syntax made unambiguous, which also round-trips." (spec)
- "Every encoding choice has its own spelling; no two encodings are treated as equivalent." (spec)
- "A rule typed without markers gets the **default encoding**" (spec): the focus by `SEEK` when there is no left context; otherwise the first left-context element by `SCAN`, the rest by `R`, the focus by `FOCUS`; right context by `R`/`R?`; the `F` opcode variants for a feature bundle; `FOCUSP`/`RP`/`LP` for a repetition, parameter tests, or both a phoneme and features; `REPL` and `SETF` (not the `+` forms); `ST` (raw) for `:=`.
- "Feature bits are written `b0`…`b31`. Named features (`[+vowel]`) are out of scope." (spec)
- "The complete grammar is written down in step 3 as `editor/ivxedit/NOTATION.md`, held to these rules and to the round-trip test." (spec)
- "The status bar shows the coverage per module, e.g. "M0: 5,214 / 5,714 in rule notation"." (spec)
- Tests: `unittest`, run by `python -m unittest discover -s editor/tests -t editor` from the repository root; GUI tests run offscreen and skip without PySide6; tests that need `build/ivxsay.exe` skip with a message when it is missing (spec: Testing).
- `listing.py` and `notation.py` use the Python standard library only (like the model, writer and checks).
- `src/`, `lib/`, `cli/`, `bin/` and the papers in `docs/` do not change (AGENTS.md, spec).
- Files are UTF-8 with LF line endings. Python style follows the surrounding code: lower-case docstrings that say what a function returns, short comments, lines up to about 110 characters.
- Commits end with the trailer `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Facts found while planning

All measured on a working prototype of this plan's code (the code below), on the 12 files in `bin/`.

1. **Instruction form reads back 100 %**: `parse_rule(rule_line(r).text, ph_names(f.phonemes))` gives the same words for all 15,564 rules (spec: Testing, 5).
2. **Notation coverage**, rules in notation over the 12 files: M0 5,691 / 5,714, M1 1,404 / 1,404, M2 3,881 / 3,885, M3 878 / 882, M4 608 / 608, M5 3,064 / 3,071: **15,526 / 15,564**. The other 38 are exactly: 23 `GEMIN` rules, 2 rules with `CHEAD`/`CTAIL`, 8 rules that name the inventory's terminator (`<end>`, REFERENCE §5.6), and the 5 `REPL` words whose count carries.
3. **Several actions are chains** (about 650 rules): `REPL ; SETF ; REPL+ ; SETF ; REPL+ …`, each `REPL+`/`SETF+` acting one segment further on. The notation writes them as groups separated by `;`: `#818 O → O [−b20 +b27] ; M A →⁺ M A [+b20 +b24 +b25 +b26] ; N →⁺ N / [+b0 −b1 +b16] [+b1 +b16]* _`.
4. **Robustness**: random edits of real lines (about 480,000, both forms) give a `ParseError` or a rule whose bytes decode back to the same words; no other exception (spec: Testing, 7). `str.isdigit()` accepts `²`, which `int()` refuses, so the parsers match ASCII digits only (`[0-9]`).
5. **End to end**: on `swrules.ivx`, `ivxsay -E` reports `['', 'Ph', 'I:', 'L', 'O', ',', '.']` for "pilo"; with `#1 L → M / _ O` parsed and inserted first in M0 it reports `['', 'Ph', 'I:', 'M', 'O', ',', '.']`, and "pila" still gives `['', 'Ph', 'I:', 'L', 'A', ',', '.']` (spec: Testing, 8).
6. **Speed**: `notation_line` (write, read back, compare) takes about 0.15 ms per rule, 2.3 s for all 15,564; the window makes each module's lines once per open file.
7. **Fonts**: with native Windows fonts the rules view uses Courier New, which renders `→ ∅ ↺ ⟵ ‹ ›` at an even line height. The offscreen platform's fallback fonts give uneven line heights; that only affects offscreen screenshots.

## Decisions (where the spec leaves the choice to step 3)

- **`CPREV` context** ("step 3 decides whether these rules leave the fallback"): left context matched after the rest of the match, stepping left from the focus, is written in reading order before `⟵`: `#818 ∅ → # / [+b16]{3} ⟵ _ A D R E S S` (the spec's example without the marker would read as `SCAN`-based left context, a different encoding).
- **Several actions** ("separated by `;`"): groups `lhs → rhs`, the second and later with `→⁺`. The `⁺` marker is on the arrow for `SETF+` as well as `REPL+` (the spec allowed "the arrow or bracket").
- **`UNWRAP`** (not in the spec's table) is `A → ∅ ↺` (delete, then restart the rule), distinct from `A → ∅` (`REPL del 1` with no phonemes).
- **A pattern with no parts** (any segment; 75 shipped rules) is `·`.
- **Phoneme names** made of letters, digits and `: $ % ' @ ! " `` ` . , = | ~ \ + - ? # &` are written bare (except a `#` followed by a digit, which would read as a label); the name ` ` is `␠`; any other name is written `‹name›`, e.g. `‹[›`, `‹*›`.
- **Statements** in `{ }`: `X := n` for `A= n ; ST X` (`:=s`, `:=u` for `STs`, `STu`), `X := A` for a lone store, `B = X` for `LD`, and any other accumulator instruction in its instruction form (`A+= -10`, `A*=frac 3/2^2`).
- **`{min,opt}`** keeps the spec's meaning: min required matches, then up to opt more.
- **Left in instruction form**: `GEMIN`, `CHEAD`, `CTAIL`, references with a delta or to the terminator, and `REPL` words whose count carries (the engine misreads them).
- **The instruction-form parser refuses hand-built instructions** whose words would not decode as the same instruction (wrong operand count, a phoneme where no id goes). Loaded files never have them, so the 100 % read-back holds; random edits cannot produce bytes that load differently.

## File structure

| File | Task | Responsibility |
|---|---|---|
| `editor/ivxedit/listing.py` | 1 | + `parse_rule`, `ph_names`, `ParseError`, `words`, the shared token readers |
| `editor/tests/fuzz.py` | 1 | random line edits and the "bytes read back" check, for the robustness tests |
| `editor/tests/test_listing.py` | 1 | + `ParseTest` (spec: Testing, 5 and 7) |
| `editor/ivxedit/notation.py` | 2 | rule ↔ `Form` ↔ notation text; `notation_line`, `parse_notation` |
| `editor/ivxedit/NOTATION.md` | 2 | the grammar, with checked examples |
| `editor/tests/test_notation.py` | 2 | examples, spellings, coverage minimum, robustness, end to end (Testing, 6–8) |
| `editor/ivxedit/gui/rulesview.py` | 3 | notation lines, tinted fallback rules, instruction-form switch |
| `editor/ivxedit/gui/mainwindow.py` | 3 | View menu, coverage label, "In notation" column, lines made once per file |
| `editor/tests/test_gui.py` | 3 | the rules view and the Rules table with notation |
| `AGENTS.md`, the spec | 3 | step 3 as built |

---

### Task 1: The instruction-form parser

**Files:**
- Modify: `editor/ivxedit/listing.py` (docstring, imports, then a new section at the end)
- Create: `editor/tests/fuzz.py`
- Modify: `editor/tests/test_listing.py`

**Interfaces:**
- Consumes (existing): `listing.MNEMONICS`, `CAPTURE`, `COMPARE`, `FEAT_OPS`, `LDST_OPS`, `ph_text`, `rule_line`; `rules.operand_count(b, p)`, `ph_positions(ins)`, `encode_rule(rule, ph_id)`, `decode_module(b, start)`, `encode_module(m, ph_id)`, `Module(lead, rules)`; `binfmt.pack_words(ws)`; `info.item_name(iid)`.
- Produces (Task 2 relies on these exact names):
  - `ParseError(message, pos=0)`, a `ValueError` with `.message` and `.pos` (0-based column); `str()` is `'column N: message'`.
  - `ph_names(phonemes) -> {text: Phoneme}`, keyed by `ph_text` (`'␠'` for the name `' '`, `'⁏'` for `';'`).
  - `parse_rule(text, names) -> Rule`.
  - `words(rule) -> list`: the rule's encoded words with each `PhRef` as `(id(ph), delta)`; equal words mean the same rule.
  - `check_length(rule) -> rule` (raises `ParseError` past 65,535 bytes), `check_shape(ins, pos)`.
  - `u16(text, pos)`, `phoneme(tok, pos, names)`, `feat_words(tok, pos)`, `test_words(tok, pos)`, `ldst_words(toks)` (`toks` = `[(token, column)]`), `_read_instr(text, pos, names) -> Instr`.
  - `ITEM_IDS`, `CAPTURE_IDS`, `COMPARE_IDS`, `OPCODES`.
  - `tests/fuzz.py`: `edit(rng, text) -> str`, `ph_ids(f) -> (PhRef -> id)`, `reads_back(rule, ph_id) -> bool`.

- [ ] **Step 1: Write the failing tests**

Create `editor/tests/fuzz.py`:

```python
"""Random edits of rule lines and the check that a parsed rule writes good bytes (spec:
Testing, 7)."""
from ivxedit.rules import Module, decode_module, encode_module, encode_rule

# characters and tokens an edit inserts: the syntax of both forms, names, digits
PIECES = list('RULE*SEKFP;#{}[]<>()0123456789abcf/+-−_^…∅→⁺↺⟵·‹›␠⁏:=@., ') + [
    ' ; ', ' ', 'del', '->', 'once', 'A', 'B', 't=', '.hi', 'v80']


def edit(rng, text):
    """text with 1-3 random edits: delete or insert a character, repeat or swap tokens"""
    for _ in range(rng.randint(1, 3)):
        k, c = rng.randrange(len(text) + 1), rng.random()
        if c < 0.35 and text:
            text = text[:k] + text[k + 1:]
        elif c < 0.7:
            text = text[:k] + rng.choice(PIECES) + text[k:]
        else:
            t = text.split(' ')
            i, j = rng.randrange(len(t)), rng.randrange(len(t))
            if c < 0.85:
                t.insert(j, t[i])
            else:
                t[i], t[j] = t[j], t[i]
            text = ' '.join(t)
    return text


def ph_ids(f):
    """PhRef -> its id in f as the writer lays the inventory out"""
    off, p = {}, 0
    for ph in f.phonemes:
        off[id(ph)] = p
        p += len(ph.to_bytes())
    return lambda ref: p if ref.ph is None else off[id(ref.ph)] + ref.delta


def reads_back(rule, ph_id):
    """True if the rule's bytes, in a module of their own, decode to the same words"""
    data = encode_module(Module(0, [rule]), ph_id)
    back, end = decode_module(data, 0)
    return (end == len(data) and len(back.rules) == 1
            and encode_rule(back.rules[0], None) == encode_rule(rule, ph_id))
```

In `editor/tests/test_listing.py`:

1. In the imports, replace

```python
from ivxedit.listing import MNEMONICS, ph_text, rule_line
from ivxedit.model import Phoneme, PhRef, load
from ivxedit.rules import Instr, Rule, rep_split
from tests.util import LANGS, ivx_bytes
```

   with

```python
from ivxedit.listing import MNEMONICS, ParseError, parse_rule, ph_names, ph_text, rule_line, words
from ivxedit.model import Phoneme, PhRef, load
from ivxedit.rules import Instr, Rule, rep_split
from tests.fuzz import edit, ph_ids, reads_back
from tests.util import LANGS, ivx_bytes
```

2. In the end of the file: add `ParseTest` before the `__main__` block, replace

```python
            rule_line(Rule(False, [Instr(rng.choice(ops), args)]))


if __name__
```

   with

```python
            rule_line(Rule(False, [Instr(rng.choice(ops), args)]))


NAMES = ph_names([A, PLUS, SPACE, Phoneme(b'\0;', 0, 0, [])])


def again(rule):
    """rule -> its line -> the rule read back"""
    return parse_rule(rule_line(rule).text, NAMES)


class ParseTest(unittest.TestCase):
    def test_every_shipped_rule_reads_back(self):
        # spec: Testing, 5
        n = 0
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            for name, m in f.modules.items():
                for i, r in enumerate(m.rules):
                    n += 1
                    with self.subTest(lang=lang, module=name, rule=i):
                        self.assertEqual(words(parse_rule(rule_line(r).text, names)), words(r))
        self.assertEqual(n, 15564)

    def test_operand_kinds_read_back(self):
        for ins in (Instr(0x40, [PhRef(A)]), Instr(0x4C, [PhRef(SPACE)]), Instr(0x4C, [PhRef(None)]),
                    Instr(0x4C, [PhRef(A, 4)]), Instr(0x4C, [0x7000]), Instr(0xC4, [0x0010, 0, 0x0110, 0]),
                    Instr(0xBC, [0x010C, PhRef(A)]), Instr(0xBC, [0x0314] + [PhRef(A)] * 23),
                    Instr(0x150, [0xFFFB]), Instr(0x188, [3, 2]), Instr(0x100, [0x0005, 0x0000]),
                    Instr(0x108, [0x4005, 0xFC80]), Instr(0x10C, [0x80AC]), Instr(0x100, [0x0005, 0x0203]),
                    Instr(0x68, [0x8004, PhRef(PLUS)]), Instr(0x6C, [0x037C, 0x400A, 0x0001, 0, 0x0003, 0]),
                    Instr(0x68, [0xA00A, PhRef(PLUS), 0xC018, 10, 0x0500]),
                    Instr(0x68, [0x2006, 0x05C6, 0x0533]), Instr(0x68, [0xA004, PhRef(PLUS)]),
                    Instr(0xBC, [0x0130, PhRef(A), PhRef(NAMES['⁏']), PhRef(PLUS), PhRef(SPACE)]),
                    Instr(0xCC, []), Instr(0x10, [7])):
            for iterative in (False, True):
                rule = Rule(iterative, [ins, Instr(0x10, [32])])
                with self.subTest(line=rule_line(rule).text):
                    self.assertEqual(words(again(rule)), words(rule))

    def test_refusals(self):
        for text in ('', 'RULE ', 'RULE  SEEK A:', 'RULE SEEK A: ', 'RULE SEEK', 'RULE SEEK Q',
                     'RULE SEEK A: A:', 'RULE NEXT', 'RULE NEXT #70000', 'RULE A= 70000', 'RULE ?1234',
                     'RULE FROB', 'RULE REPL del 0 ->' + ' A:' * 64, 'RULE REPL del 300 -> A:', 'RULE REPL #0124 A:',
                     'RULE LD F1', 'RULE LD v80@0', 'RULE LD F1@64', 'RULE LD Q1@0', 'RULE ST vac t=0',
                     'RULE RP 300,0 { + }', 'RULE FOCUSP { #8004 }', 'RULE FOCUSP { <F1 ~ 5> }',
                     'RULE SETF [0010/0110]', 'RULE A= +', 'RULE SEEK A:+x', 'RULE* ', 'rule SEEK +'):
            with self.subTest(text=text):
                with self.assertRaises(ParseError):
                    parse_rule(text, NAMES)

    def test_error_column(self):
        with self.assertRaises(ParseError) as cm:
            parse_rule('RULE SEEK A: ; R Q ; NEXT #1', NAMES)
        self.assertEqual(cm.exception.pos, 17)
        self.assertIn("no phoneme 'Q'", str(cm.exception))

    def test_random_edits(self):
        # spec: Testing, 7: an edited line gives a ParseError or a rule whose bytes read back
        rng = random.Random(7)
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names, ph_id = ph_names(f.phonemes), ph_ids(f)
            rules = [r for m in f.modules.values() for r in m.rules]
            for _ in range(400):
                text = edit(rng, rule_line(rng.choice(rules)).text)
                try:
                    rule = parse_rule(text, names)
                except ParseError:
                    continue
                with self.subTest(lang=lang, text=text):
                    self.assertTrue(reads_back(rule, ph_id))


if __name__
```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest tests.test_listing` from `editor/`
Expected: an import error: `cannot import name 'ParseError' from 'ivxedit.listing'`.

- [ ] **Step 3: Write the parser**

In `editor/ivxedit/listing.py`:

1. In the docstring's first paragraph, replace

```python
rule's words; the parser that reads lines back comes with the rule notation (step 3).
```

   with

```python
rule's words: parse_rule reads the line of any rule a file loads back into that rule. It
refuses an instruction whose words would not decode as that instruction again (a wrong
operand count, an unknown opcode, a phoneme where no id goes), which only hand-built
rules have.
```

2. In the imports, replace

```python
from dataclasses import dataclass, field

from .info import item_name, ph_name, s8
from .model import PhRef
from .rules import FIXED, PH_OPS, REPL, REPL_NEXT, rep_split
```

   with

```python
import re
from dataclasses import dataclass, field

from .binfmt import pack_words
from .info import item_name, ph_name, s8
from .model import PhRef
from .rules import (FIXED, PH_OPS, REPL, REPL_NEXT, Instr, Rule, encode_rule, operand_count,
                    ph_positions, rep_split)
```

3. In the end of the file (after `rule_line`): append the parser, replace

```python
        _instr(o, ins)
    return o.line()
```

   with

```python
        _instr(o, ins)
    return o.line()


# --- reading lines back

class ParseError(ValueError):
    """a line that does not read; pos is the column of the problem"""

    def __init__(self, message, pos=0):
        super().__init__('column %d: %s' % (pos + 1, message))
        self.message, self.pos = message, pos


OPCODES = {m: op for op, m in MNEMONICS.items()}
CAPTURE_IDS = {r: op for op, r in CAPTURE.items()}
COMPARE_IDS = {s: op for op, s in COMPARE.items()}
ITEM_IDS = {item_name(i): i for i in range(0x100)}
TEST = re.compile(r'<(?:#([0-9a-f]{4})|([^ <>.]+)(\.last)?(\.hi)? '
                  r'(?:-> (\w+)|(<=|<|==|!=|>=|>) (-?[0-9]+)))>')
FEAT = re.compile(r'\[([0-9a-f]{8})/([0-9a-f]{8})\]')
RAW = re.compile(r'#([0-9a-f]{4})')
LDST = re.compile(r'([^@ ]+)(?:@(?:#([0-9a-f]{4})|(-?[0-9]+)(\.hi)?))?')
DEC = re.compile(r'-?[0-9]+')


def ph_names(phonemes):
    """{name as ph_text writes it: Phoneme}; of two phonemes with one name, the first (as
    the engine's ph_find)"""
    names = {}
    for ph in phonemes:
        names.setdefault(ph_text(PhRef(ph)), ph)
    return names


def u16(text, pos):
    """a decimal operand -> its word; -32768..65535"""
    v = int(text)
    if not -0x8000 <= v <= 0xFFFF:
        raise ParseError('%s does not fit in 16 bits' % text, pos)
    return v & 0xFFFF


def _tokens(text, pos):
    """an instruction's text -> [(token, column)]: tokens are separated by single spaces,
    and a parameter test <...> is one token"""
    out, i = [], 0
    while i < len(text):
        m = TEST.match(text, i) if text[i] == '<' else None
        j = m.end() if m else (text.find(' ', i) if ' ' in text[i:] else len(text))
        if j == i:
            raise ParseError('two spaces in a row', pos + i)
        out.append((text[i:j], pos + i))
        if j < len(text) and (text[j] != ' ' or j + 1 == len(text)):
            raise ParseError('expected one space', pos + j)
        i = j + 1
    return out


def phoneme(tok, pos, names):
    """a phoneme operand: NAME, NAME+d, <end>, or #hhhh (an id outside the inventory)"""
    if RAW.fullmatch(tok):
        return int(tok[1:], 16)
    if tok == '<end>':
        return PhRef(None)
    if tok in names:
        return PhRef(names[tok])
    for k in (2, 1):
        if tok[k:k + 1] == '+' and tok[:k] in names and re.fullmatch('[0-9]+', tok[k + 1:]):
            return PhRef(names[tok[:k]], int(tok[k + 1:]))
    raise ParseError('no phoneme %r' % tok, pos)


def feat_words(tok, pos):
    """[vvvvvvvv/mmmmmmmm] -> 4 words"""
    m = FEAT.fullmatch(tok)
    if not m:
        raise ParseError('expected [vvvvvvvv/mmmmmmmm]', pos)
    v, mk = int(m.group(1), 16), int(m.group(2), 16)
    return [v >> 16, v & 0xFFFF, mk >> 16, mk & 0xFFFF]


def test_words(tok, pos):
    """a parameter test <...> -> its words"""
    m = TEST.fullmatch(tok)
    if m is None:
        raise ParseError('expected a parameter test <...>', pos)
    if m.group(1):
        return [int(m.group(1), 16)]
    name, last, hi, reg, cmp, k = m.group(2, 3, 4, 5, 6, 7)
    if name not in ITEM_IDS:
        raise ParseError('no parameter or variable %r' % name, pos)
    w = ITEM_IDS[name] << 8 | (0x80 if last else 0) | (0x40 if hi else 0)
    if reg is not None:
        if reg not in CAPTURE_IDS:
            raise ParseError('no register %r' % reg, pos)
        return [w | CAPTURE_IDS[reg]]
    return [w | COMPARE_IDS[cmp], u16(k, pos)]


def _read_pattern(toks, names):
    """the tokens between { and } -> the pattern's words, header first"""
    if toks and RAW.fullmatch(toks[0][0]):
        return [phoneme(t, p, names) for t, p in toks]
    ws, h, k = [], 0, 0
    if k < len(toks) and not FEAT.fullmatch(toks[k][0]) and not TEST.fullmatch(toks[k][0]):
        ws.append(phoneme(*toks[k], names))
        h |= 0x8000
        k += 1
    if k < len(toks) and FEAT.fullmatch(toks[k][0]):
        ws += feat_words(*toks[k])
        h |= 0x4000
        k += 1
    if k < len(toks):
        h |= 0x2000
        for t, p in toks[k:]:
            ws += test_words(t, p)
    if len(ws) + 1 > 0x7FF:
        raise ParseError('the pattern is too long', toks[0][1])
    return [h | 2 * (len(ws) + 1)] + ws


def ldst_words(toks):
    """LD/ST operands (F1@k[.hi], F1@#hhhh or vXX, then t=N) -> [spec] or [spec, index]"""
    tok, p = toks[0]
    m = LDST.fullmatch(tok)
    if not m or m.group(1) not in ITEM_IDS or len(toks) > 2:
        raise ParseError('expected F1@k or vXX', p)
    iid, t = ITEM_IDS[m.group(1)], 0
    if len(toks) == 2:
        tt, tp = toks[1]
        if not (tt.startswith('t=') and DEC.fullmatch(tt[2:]) and -128 <= int(tt[2:]) <= 127
                and int(tt[2:])):
            raise ParseError('expected t=N with N in -128..127, not 0', tp)
        t = int(tt[2:]) & 0xFF
    spec = t << 8 | iid
    if iid >= 0x80:
        if m.group(2) or m.group(3):
            raise ParseError('a variable has no index', p)
        return [spec]
    if m.group(2):
        return [spec, int(m.group(2), 16)]
    if m.group(3) is None:
        raise ParseError('a parameter needs an index @k', p)
    k = int(m.group(3))
    if not -64 <= k <= 63:
        raise ParseError('the index %d is not in -64..63' % k, p)
    return [spec, (2 * k & 0xFF) << 8 | (0x80 if m.group(4) else 0)]


def _shaped_args(op, toks, names):
    """op's operands from toks in its shaped form, or None if toks are not in that form"""
    t = [x for x, _ in toks]
    if op in PH_OPS:
        return [phoneme(*toks[0], names)] if len(t) == 1 else None
    if op in FEAT_OPS:
        return feat_words(*toks[0]) if len(t) == 1 and FEAT.fullmatch(t[0]) else None
    if op == 0x68:
        return _read_pattern(toks[1:-1], names) if len(t) >= 2 and t[0] == '{' and t[-1] == '}' else None
    if op in (0x6C, 0x9C):
        m = re.fullmatch(r'([0-9]+),([0-9]+)', t[0]) if t else None
        if not (m and len(t) >= 3 and t[1] == '{' and t[-1] == '}'):
            return None
        lo, hi = int(m.group(1)), int(m.group(2))
        if lo > 255 or hi > 255:
            raise ParseError('a count is more than 255', toks[0][1])
        return [lo << 8 | hi] + _read_pattern(toks[2:-1], names)
    if op in (REPL, REPL_NEXT):
        if not (len(t) >= 3 and t[0] == 'del' and re.fullmatch('[0-9]+', t[1]) and t[2] == '->'):
            return None
        d, ids = int(t[1]), [phoneme(x, p, names) for x, p in toks[3:]]
        w = d * 256 + 12 * len(ids)
        if w > 0xFFFF or rep_split(w) != (d, len(ids)):
            raise ParseError('del %d with %d phonemes does not fit a REPL word' % (d, len(ids)),
                             toks[1][1])
        return [w] + ids
    if op in LDST_OPS:
        return ldst_words(toks) if t and not RAW.fullmatch(t[0]) else None
    if op == 0x188:
        m = re.fullmatch(r'(-?[0-9]+)/2\^(-?[0-9]+)', t[0]) if len(t) == 1 else None
        return [u16(m.group(1), toks[0][1]), u16(m.group(2), toks[0][1])] if m else None
    if op in (0x10, 0xC8):
        m = re.fullmatch(r'#([0-9]+)', t[0]) if len(t) == 1 else None
        return [u16(m.group(1), toks[0][1])] if m else None
    if op in FIXED and len(t) == FIXED[op] and all(DEC.fullmatch(x) for x in t):
        return [u16(x, p) for x, p in toks]
    return None


def _read_instr(text, pos, names):
    toks = _tokens(text, pos)
    if not toks:
        raise ParseError('expected an instruction', pos)
    (m, mp), rest = toks[0], toks[1:]
    if m not in OPCODES:
        raise ParseError('no instruction %r' % m, mp)
    op = OPCODES[m]
    args = _shaped_args(op, rest, names)
    if args is None:
        args = [phoneme(x, p, names) for x, p in rest]
    ins = Instr(op, args)
    check_shape(ins, mp)
    return ins


def check_shape(ins, pos):
    """refuse an instruction whose words would not read back as this instruction: the
    operand count its opcode and operands give, and phonemes only where ids go"""
    ws = [a if isinstance(a, int) else 0 for a in ins.args]
    n = operand_count(pack_words([ins.op] + ws + [0, 0]), 0)
    if n != len(ws):
        raise ParseError('%s has %d operand words here, not %d' % (MNEMONICS[ins.op], n, len(ws)), pos)
    try:
        ids = set(ph_positions(ins))
    except IndexError:
        ids = set()
    if any(isinstance(a, PhRef) and k not in ids for k, a in enumerate(ins.args)):
        raise ParseError('a phoneme where %s has no phoneme id' % MNEMONICS[ins.op], pos)


def words(rule):
    """the rule's words, each phoneme reference as (id of its Phoneme, delta): two rules with
    equal words are the same rule"""
    return encode_rule(rule, lambda ref: (id(ref.ph), ref.delta))


def check_length(rule):
    """refuse a rule longer than its length word holds"""
    if 2 + 2 * sum(1 + len(i.args) for i in rule.instrs) > 0xFFFF:
        raise ParseError('the rule is longer than 65535 bytes')
    return rule


def parse_rule(text, names):
    """a line in instruction form -> Rule; names from ph_names(); raises ParseError"""
    head, sep, body = text.partition(' ')
    if head not in ('RULE', 'RULE*'):
        raise ParseError('a line starts with RULE or RULE*', 0)
    instrs, pos = [], len(head) + 1
    if sep:
        for part in body.split(' ; '):
            instrs.append(_read_instr(part, pos, names))
            pos += len(part) + 3
    return check_length(Rule(head == 'RULE*', instrs))
```


- [ ] **Step 4: Run the tests**

Run: `python -m unittest tests.test_listing -v` from `editor/`
Expected: all pass (16 tests), including `test_every_shipped_rule_reads_back` over the 15,564 rules.

Run: `python -m unittest discover -s editor/tests -t editor` from the repository root
Expected: OK (the GUI tests are unchanged).

- [ ] **Step 5: Commit**

```bash
git add editor/ivxedit/listing.py editor/tests/fuzz.py editor/tests/test_listing.py
git commit -m "ivxedit: read the instruction form back (listing.parse_rule)

Every shipped rule's line reads back to the same words; edited lines give a
ParseError or a rule whose bytes decode the same (spec: Testing, 5 and 7).

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 2: The rule notation

**Files:**
- Create: `editor/ivxedit/notation.py`
- Create: `editor/ivxedit/NOTATION.md`
- Create: `editor/tests/test_notation.py`

**Interfaces:**
- Consumes (Task 1): `listing.ParseError`, `ph_names`, `parse_rule`, `words`, `check_length`, `ldst_words`, `u16`, `_read_instr`, `ITEM_IDS`, `CAPTURE_IDS`; (existing) `listing.CAPTURE`, `rule_line`, `_Out`, `_ldst`, `_shaped`; `tests/fuzz.py`; `speak.speak_model(f, text, exe=...)`; `tests.util.ivxsay`, `ROOT`.
- Produces (Task 3 relies on these):
  - `notation_line(rule, names) -> listing.Line | None`: the rule in notation, with spans of the kinds `op`, `ph` (obj = the `PhRef`), `num`, `feat`, `param`; `None` when the notation cannot say the rule exactly.
  - `parse_notation(text, names) -> Rule` (raises `ParseError`).
  - `Elem`, `Group`, `Form`, `decode(rule) -> Form`, `write(form) -> Line`, `read(text, names) -> Form`, `encode(form) -> Rule`.

The grammar is in `NOTATION.md` (Step 4); read it before the code. The code's path: `decode` (instructions → `Form`) and `write` (`Form` → text) print; `lex`, `_Reader`, `read` (text → `Form`) and `encode` (`Form` → instructions) parse. `notation_line` runs all four and compares `words`.

- [ ] **Step 1: Write the failing tests**

Create `editor/tests/test_notation.py`:

```python
"""Rule notation (spec: Rule notation; Testing, 6-8)."""
import random
import sys
import unittest

from ivxedit.listing import MNEMONICS, ParseError, parse_rule, ph_names, rule_line, words
from ivxedit.model import Phoneme, PhRef, load
from ivxedit.notation import notation_line, parse_notation
from ivxedit.rules import Instr, Rule, rep_carries
from ivxedit.speak import speak_model
from tests.fuzz import edit, ph_ids, reads_back
from tests.util import LANGS, ROOT, ivx_bytes, ivxsay

# rules in notation over the 12 files, per module: the test fails below these (raise them
# as coverage grows)
MINIMUM = {'M0': 5691, 'M1': 1404, 'M2': 3881, 'M3': 878, 'M4': 608, 'M5': 3064}
SAY = ivxsay()

PH = {n: Phoneme(n.encode().rjust(2, b'\0'), 0, 0, []) for n in ('A', 'B', 'S', '+', '1', ' ', '[', '#', '*')}
NAMES = ph_names(PH.values())


def r(text):
    """an instruction-form line over the phonemes in PH"""
    return parse_rule(text, NAMES)


def nota(text):
    return notation_line(r(text), NAMES).text


def sw():
    f = load(ivx_bytes('sw'))
    return f, ph_names(f.phonemes)


class ExamplesTest(unittest.TestCase):
    def test_spec_examples(self):
        # spec: Rule notation, the swrules.ivx examples
        f, names = sw()
        texts = {rule_line(x).text: getattr(notation_line(x, names), 'text', None) for x in f.modules['M0'].rules}
        self.assertEqual(texts['RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32'], '#32 + B → S')
        self.assertEqual(texts['RULE SCAN 1 ; FOCUS A ; REPL del 1 -> A ; SETF [00100000/01100000] ; NEXT #32'],
                         '#32 A → A [+b20 −b24] / 1 _')
        self.assertEqual(texts['RULE SEEK A ; R D ; R R ; R E ; R S ; R S ; CPREV ; LP 3,0 { [00010000/00010000] } ;'
                               ' REPL del 0 -> # ; NEXT #818'], '#818 ∅ → # / [+b16]{3} ⟵ _ A D R E S S')

    def test_notation_md_examples(self):
        # NOTATION.md's examples: each notation line, then its instruction form
        f, names = sw()
        doc = (ROOT / 'editor' / 'ivxedit' / 'NOTATION.md').read_text(encoding='utf-8')
        lines = doc.split('```text\n')[1].split('```')[0].splitlines()
        self.assertEqual(len(lines), 20)
        for text, instr in zip(lines[::2], lines[1::2]):
            with self.subTest(text=text):
                self.assertEqual(notation_line(parse_rule(instr.strip(), names), names).text, text)

    def test_elements(self):
        self.assertEqual(nota('RULE SEEK A ; R? B ; RF [00010000/00030000] ; REPL del 1 -> S ; NEXT #1'),
                         '#1 A → S / _ (B) [+b16 −b17]')
        self.assertEqual(nota('RULE SEEK A ; RP 0,127 { B } ; RP 2,0 { + } ; RP 1,5 { 1 } ; REPL del 1 -> S ; NEXT #1'),
                         '#1 A → S / _ B* +{2} 1{1,5}')
        self.assertEqual(nota('RULE SEEK A ; RP 1,0 { B [00010000/00010000] <F1 < 50> <DR -> R1> } ; RP 1,0 { } ;'
                              ' REPL del 1 -> S ; NEXT #1'), '#1 A → S / _ B[+b16]<F1 < 50><DR → R1> ·')
        self.assertEqual(nota('RULE SEEK A ; RF [00010000/00000000] ; REPL del 1 -> S ; NEXT #1'),
                         '#1 A → S / _ [V=00010000 M=00000000]')
        self.assertEqual(nota('RULE SEEK ␠ ; R [ ; R # ; R * ; REPL del 4 -> + ; NEXT #1'), '#1 ␠ ‹[› # ‹*› → +')

    def test_every_encoding_has_its_own_spelling(self):
        # spec: Rule notation, "Every encoding choice has its own spelling"
        pairs = [
            ('RULE SEEK A ; R B ; REPL+ del 1 -> S ; NEXT #1', '#1 B →⁺ S / A _'),
            ('RULE SCAN A ; FOCUS B ; REPL del 1 -> S ; NEXT #1', '#1 B → S / A _'),
            ('RULE FOCUS A ; REPL del 1 -> S ; NEXT #1', '#1 A → S / ^ _'),
            ('RULE SEEK A ; REPL del 1 -> S ; NEXT #1', '#1 A → S'),
            ('RULE FOCUSP { A } ; REPL del 1 -> S ; NEXT #1', '#1 A{1} → S / ^ _'),
            ('RULE SEEK A ; RP 1,0 { B } ; REPL del 1 -> S ; NEXT #1', '#1 A → S / _ B{1}'),
            ('RULE SEEK A ; R B ; REPL del 1 -> S ; NEXT #1', '#1 A → S / _ B'),
            ('RULE SEEK A ; SCAN B ; REPL del 1 -> S ; NEXT #1', '#1 A → S / _ … B'),
            ('RULE SCAN B ; SEEK A ; REPL del 1 -> S ; NEXT #1', '#1 A → S / B … _'),
            ('RULE SEEK A ; UNWRAP 1 ; NEXT #1', '#1 A → ∅ ↺'),
            ('RULE SEEK A ; REPL del 1 -> ; NEXT #1', '#1 A → ∅'),
            ('RULE SEEK A ; SETF [00010000/00010000] ; NEXT #1', '#1 A → [+b16]'),
            ('RULE SEEK A ; R B ; SETF+ [00010000/00010000] ; NEXT #1', '#1 B →⁺ [+b16] / A _'),
            ('RULE SEEK A ; R B ; REPL del 1 -> S ; SETF+ [00010000/00010000] ; NEXT #1',
             '#1 A → S ; B →⁺ [+b16]'),
            ('RULE* SEEK A ; REPL del 1 -> S ; NEXT #1', '*#1 A → S'),
            ('RULE SEEK A ; REPL del 1 -> S', 'A → S once'),
            ('RULE* SEEK A ; REPL del 1 -> S', '* A → S once'),
            ('RULE SEEK A ; A= 5 ; ST vac ; NEXT #1', '#1 A → { vac := 5 }'),
            ('RULE SEEK A ; A= 5 ; STu F1@0 ; ST vac ; NEXT #1', '#1 A → { F1@0 :=u 5; vac := A }'),
            ('RULE SEEK A ; LD F1@-1.hi t=2 ; A=B ; A*=frac 3/2^2 ; STs F1@0 ; NEXT #1',
             '#1 A → { B = F1@-1.hi t=2; A=B; A*=frac 3/2^2; F1@0 :=s A }'),
            ('RULE SEEK A ; CPREV ; L B ; L? 1 ; LP 0,127 { + } ; REPL del 1 -> S ; NEXT #1',
             '#1 A → S / +* (1) B ⟵ _'),
        ]
        for text, want in pairs:
            with self.subTest(text=text):
                self.assertEqual(nota(text), want)
                self.assertEqual(words(parse_notation(want, NAMES)), words(r(text)))
        self.assertEqual(len({w for _, w in pairs}), len(pairs))

    def test_not_in_notation(self):
        for text in ('RULE GEMIN #1', 'RULE SEEK A ; CHEAD ; R B ; REPL del 1 -> S ; NEXT #1',
                     'RULE SEEK A ; R <end> ; REPL del 1 -> S ; NEXT #1',
                     'RULE SEEK A ; REPL del 2 -> ' + 'S ' * 22 + '; NEXT #1',     # 12*n carries
                     'RULE SEEK A ; REPL del 2 -> S ; NEXT #1',          # deletes more than it matched
                     'RULE SEEK A ; L B ; NEXT #1', 'RULE SEEK A ; NEXT #1', 'RULE R A ; R B ; NEXT #1'):
            with self.subTest(text=text):
                self.assertIsNone(notation_line(r(text), NAMES))

    def test_never_raises(self):
        # hand-built rules, whatever their operands: a Line or None
        rng = random.Random(1)
        ops = list(MNEMONICS)
        for _ in range(3000):
            instrs = [Instr(rng.choice(ops), [rng.choice([rng.randrange(0x10000), PhRef(PH['A']), PhRef(None), -1])
                                              for _ in range(rng.randrange(6))]) for _ in range(rng.randrange(1, 5))]
            notation_line(Rule(False, instrs), NAMES)

    def test_refusals(self):
        for text in ('', '#1', '#1 A', '#1 A →', '#1 A → S /', '#1 A → S / _ _', '#1 A → Q',
                     'A → S', '#1 A → S once', '#1 A B → [+b1]', '#1 A → [+b32]', '#1 A → [+b1 −b1]',
                     '#1 A(B) → S', '#1 (A) → S', '#1 A* → S', '#1 A → S / ^ … B _', '#1 ∅ → S',
                     '#1 A → S / … B _', '#1 B[+b1] → S', '#1 A → S ; B → S', '#1 A →⁺ S',
                     '#1 A → S / B ⟵ C _', '#1 A → { A = 5 }', '#1 A → { LD F1@0 }', '#1 A → { F1 := 5 }',
                     '#1 A → S / _ B{300}', '#99999 A → S', '#1 A → ∅ ↺ [+b1]', '#1 ∅ → ∅ ↺ / _ A',
                     '#1 A → ' + 'S ' * 22):
            with self.subTest(text=text):
                with self.assertRaises(ParseError):
                    parse_notation(text, NAMES)


class ShippedTest(unittest.TestCase):
    def test_coverage(self):
        # spec: Testing, 6: every rule shown in notation reads back; coverage per module
        done, total = dict.fromkeys(MINIMUM, 0), dict.fromkeys(MINIMUM, 0)
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            for name, m in f.modules.items():
                for i, rule in enumerate(m.rules):
                    total[name] += 1
                    line = notation_line(rule, names)
                    if line is not None:
                        done[name] += 1
                        with self.subTest(lang=lang, module=name, rule=i):
                            self.assertEqual(words(parse_notation(line.text, names)), words(rule))
        print('\nrules in notation: ' + ', '.join('%s %d/%d' % (n, done[n], total[n]) for n in sorted(total)),
              file=sys.stderr)
        for name in MINIMUM:
            self.assertGreaterEqual(done[name], MINIMUM[name], name)

    def test_what_is_not_in_notation(self):
        # the 38 shipped rules in instruction form: GEMIN, a REPL word whose count carries,
        # a reference to the inventory's terminator, CHEAD or CTAIL
        why = {}
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names = ph_names(f.phonemes)
            for m in f.modules.values():
                for rule in m.rules:
                    if notation_line(rule, names) is None:
                        ops = {i.op for i in rule.instrs}
                        reason = ('GEMIN' if 0xC8 in ops else 'CHEAD/CTAIL' if ops & {0xD0, 0xD4} else
                                  'carry' if any(i.op in (0xB8, 0xBC) and rep_carries(i.args[0]) for i in rule.instrs)
                                  else 'terminator' if any(isinstance(a, PhRef) and a.ph is None
                                                           for i in rule.instrs for a in i.args) else 'other')
                        why[reason] = why.get(reason, 0) + 1
        self.assertEqual(why, {'GEMIN': 23, 'CHEAD/CTAIL': 2, 'carry': 5, 'terminator': 8})

    def test_random_edits(self):
        # spec: Testing, 7: an edited line gives a ParseError or a rule whose bytes read back
        rng = random.Random(8)
        for lang in LANGS:
            f = load(ivx_bytes(lang))
            names, ph_id = ph_names(f.phonemes), ph_ids(f)
            rules = [x for m in f.modules.values() for x in m.rules]
            n = 0
            while n < 400:
                line = notation_line(rng.choice(rules), names)
                if line is None:
                    continue
                n += 1
                text = edit(rng, line.text)
                try:
                    rule = parse_notation(text, names)
                except ParseError:
                    continue
                with self.subTest(lang=lang, text=text):
                    self.assertTrue(reads_back(rule, ph_id))


@unittest.skipIf(SAY is None, 'ivxsay is not built: run make (or set IVXSAY)')
class EndToEndTest(unittest.TestCase):
    def test_a_rule_added_in_notation(self):
        # spec: Testing, 8: an M0 respelling changes what ivxsay reports
        f, names = sw()

        def phonemes(word):
            return [e.value for e in speak_model(f, word, exe=SAY).phonemes]
        self.assertEqual(phonemes('pilo'), ['', 'Ph', 'I:', 'L', 'O', ',', '.'])
        f.modules['M0'].rules.insert(0, parse_notation('#1 L → M / _ O', names))
        self.assertEqual(phonemes('pilo'), ['', 'Ph', 'I:', 'M', 'O', ',', '.'])
        self.assertEqual(phonemes('pila'), ['', 'Ph', 'I:', 'L', 'A', ',', '.'])


if __name__ == '__main__':
    unittest.main()
```

- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest tests.test_notation` from `editor/`
Expected: `ModuleNotFoundError: No module named 'ivxedit.notation'`.

- [ ] **Step 3: Write `notation.py`**

Create `editor/ivxedit/notation.py`:

```python
"""Rules in rule notation, A → B / C _ D (spec: Rule notation; the grammar is NOTATION.md).

notation_line(rule, names) writes a rule in notation, or returns None when the notation
cannot say it. It reads its own text back with parse_notation and keeps it only if that
gives the rule's words exactly, so a rule shown in notation is that rule. Every other rule
is shown in instruction form (listing.py).

Text and instructions meet in Form, the rule's parts in reading order: the left context,
the groups (each a left-hand side, an arrow and its actions), the right context.
"""
import re
from dataclasses import dataclass, field

from .info import item_name, ph_name
from .listing import (CAPTURE, CAPTURE_IDS, ITEM_IDS, ParseError, _ldst, _Out, _read_instr,
                      _shaped, check_length, ldst_words, rule_line, u16, words)
from .model import PhRef
from .rules import NEXT, REPL, REPL_NEXT, Instr, Rule, rep_split

FOCUSP, RP, LP, UNWRAP, SETF_NEXT, SETF, CPREV, LD, A_SET = (
    0x68, 0x6C, 0x9C, 0xB4, 0xC0, 0xC4, 0xCC, 0x100, 0x150)
ST_OPS = {0x104: ':=s', 0x108: ':=u', 0x10C: ':='}
COMPUTE = set(range(0x150, 0x1D0, 4)) | {LD} | set(ST_OPS)
# role -> (opcode for a phoneme, opcode for a feature bundle)
OPS = {'seek': (0x40, 0x54), 'focus': (0x44, 0x58), 'scan': (0x48, 0x5C), 'r': (0x4C, 0x60),
       'r?': (0x50, 0x64), 'l': (0x7C, 0x90), 'l?': (0x80, 0x94)}
ROLE = {op: (role, k == 1) for role, pair in OPS.items() for k, op in enumerate(pair)}
COMPARE = {0x18: '<', 0x1E: '≤', 0x24: '=', 0x2C: '≠', 0x34: '≥', 0x3C: '>'}   # value OP k
COMPARE_IDS = {s: op for op, s in COMPARE.items()}
SAFE = 'A-Za-z0-9:$%\'@!"`.,=|~\\\\+\\-?#&'   # phoneme names of these are written bare


@dataclass(eq=False)
class Elem:
    """one segment of the match: a phoneme, a feature bundle, parameter tests, or several"""
    ph: object = None        # PhRef
    feat: tuple = None       # (V, M), 32 bits each
    tests: list = None       # parameter-test words
    rep: tuple = None        # (min, opt) of a pattern; None for a plain element
    opt: bool = False        # (x): R?, L?
    search: bool = False     # … x: SCAN, SEEK

    def simple(self):
        """one phoneme or one bundle, no tests: matched by a plain or a feature opcode"""
        return (self.ph is None) != (self.feat is None) and not self.tests


@dataclass(eq=False)
class Group:
    """LHS → RHS: a REPL (ids, [] for ∅), an UNWRAP, or the focus kept; then SETFs and
    statements in order. plus: the group starts one segment after the focus (→⁺)"""
    lhs: list = field(default_factory=list)
    plus: bool = False
    repl: list = None
    unwrap: bool = False
    mods: list = field(default_factory=list)     # ('setf', (V, M)) or ('stmts', [[Instr]])
    n: int = 0                     # decoding: the number of segments on the left-hand side


@dataclass(eq=False)
class Form:
    iterative: bool = False
    label: int = None              # the NEXT operand; None: the rule has no NEXT (once)
    groups: list = field(default_factory=list)
    left: list = field(default_factory=list)     # left context, reading order
    right: list = field(default_factory=list)    # right context
    back: list = None              # left context matched after CPREV (⟵), reading order
    anchored: bool = False         # ^: the match starts at the start position, no search

    def lhs(self):
        return [e for g in self.groups for e in g.lhs]


class _NoForm(Exception):
    """a rule the notation does not write"""


# --- instructions -> Form

def _feat(ws):
    return ws[0] << 16 | ws[1], ws[2] << 16 | ws[3]


def _elem_of(ins):
    """a match instruction -> (role, Elem)"""
    op, a = ins.op, ins.args
    if op in ROLE:
        role, isfeat = ROLE[op]
        if isfeat and len(a) == 4:
            e = Elem(feat=_feat(a))
        elif not isfeat and len(a) == 1 and isinstance(a[0], PhRef):
            e = Elem(ph=a[0])
        else:
            raise _NoForm
        if role.endswith('?'):
            e.opt, role = True, role[0]
        return role, e
    if op == FOCUSP:
        return 'focus', _pattern_elem(a, (1, 0))
    if op in (RP, LP) and a and isinstance(a[0], int):
        return ('r' if op == RP else 'l'), _pattern_elem(a[1:], (a[0] >> 8, a[0] & 0xFF))
    raise _NoForm


def _pattern_elem(ws, rep):
    if not ws or not isinstance(ws[0], int):
        raise _NoForm
    h, e, i = ws[0], Elem(rep=rep), 1
    if h & 0x1001 or (h & 0x0FFE) // 2 != len(ws):
        raise _NoForm
    if h & 0x8000:
        if i >= len(ws) or not isinstance(ws[i], PhRef):
            raise _NoForm
        e.ph, i = ws[i], i + 1
    if h & 0x4000:
        if i + 4 > len(ws) or not all(isinstance(w, int) for w in ws[i:i + 4]):
            raise _NoForm
        e.feat, i = _feat(ws[i:i + 4]), i + 4
    if h & 0x2000:
        e.tests = list(ws[i:])
        if not e.tests or not all(isinstance(w, int) for w in e.tests):
            raise _NoForm
    elif i != len(ws):
        raise _NoForm
    return e


def _groups(acts):
    """the actions -> [Group] with each group's LHS length in .n"""
    groups, k = [], 0
    while k < len(acts):
        op, a = acts[k].op, acts[k].args
        if op in (REPL, REPL_NEXT) and (op == REPL_NEXT or not groups):
            if not a or not isinstance(a[0], int) or not all(isinstance(x, PhRef) for x in a[1:]):
                raise _NoForm
            d, n = rep_split(a[0])
            if n != len(a) - 1 or d * 256 + 12 * n != a[0] or 12 * n > 0xFF:
                raise _NoForm             # 12*n carries into the high byte: the engine misreads it
            g = Group(plus=op == REPL_NEXT, repl=list(a[1:]))
            g.n = d
        elif op == UNWRAP and not groups and len(acts) == 1:
            if len(a) != 1 or not 1 <= a[0] <= 0xFF:
                raise _NoForm
            g = Group(unwrap=True)
            g.n = a[0]
        elif op == SETF_NEXT and len(a) == 4:
            g = Group(plus=True, mods=[('setf', _feat(a))])
            g.n = 1
        elif not groups and (op == SETF or op in COMPUTE):
            g = Group()
            g.n = 1
            groups.append(g)
            continue
        elif groups and op == SETF and len(a) == 4:
            groups[-1].mods.append(('setf', _feat(a)))
            k += 1
            continue
        elif groups and op in COMPUTE:
            fused = op == A_SET and k + 1 < len(acts) and acts[k + 1].op in ST_OPS
            stmt = acts[k:k + 2] if fused else acts[k:k + 1]
            mods = groups[-1].mods
            if mods and mods[-1][0] == 'stmts':
                mods[-1][1].append(stmt)
            else:
                mods.append(('stmts', [stmt]))
            k += len(stmt)
            continue
        else:
            raise _NoForm
        groups.append(g)
        k += 1
    if not groups:
        raise _NoForm
    return groups


def decode(rule):
    """Rule -> Form; raises _NoForm for a rule the notation does not write"""
    ins = list(rule.instrs)
    if not all(_shaped(i) for i in ins):
        raise _NoForm                     # operands not in their opcode's shape (hand-built)
    form = Form(iterative=rule.iterative)
    if ins and ins[-1].op == NEXT:
        if len(ins[-1].args) != 1 or not isinstance(ins[-1].args[0], int):
            raise _NoForm
        form.label = ins.pop().args[0]
    fwd, back, k = [], None, 0
    while k < len(ins) and (ins[k].op in ROLE or ins[k].op in (FOCUSP, RP, LP, CPREV)):
        if ins[k].op == CPREV:
            if back is not None or ins[k].args:
                raise _NoForm
            back = []
        else:
            role, e = _elem_of(ins[k])
            if (role == 'l') != (back is not None):
                raise _NoForm
            (fwd if back is None else back).append((role, e))
        k += 1
    focus = [i for i, (role, _) in enumerate(fwd) if role in ('seek', 'focus')]
    if len(focus) != 1:
        raise _NoForm
    form.anchored = fwd[0][0] not in ('seek', 'scan')
    for i, (role, e) in enumerate(fwd):
        e.search = i > 0 and role in ('seek', 'scan')
    if back is not None:
        form.back = [e for _, e in reversed(back)]
    form.groups = _groups(ins[k:])
    elems = [e for _, e in fwd]
    start = focus[0] + (1 if form.groups[0].plus else 0)
    for g in form.groups:
        if start + g.n > len(elems):
            raise _NoForm
        g.lhs = elems[start:start + g.n]
        start += g.n
    form.left = elems[:focus[0] + (1 if form.groups[0].plus else 0)]
    form.right = elems[start:]
    return form


# --- Form -> text

def ph_word(ref):
    """a phoneme operand as notation writes it; _NoForm for one it does not write"""
    if not isinstance(ref, PhRef) or ref.ph is None or ref.delta:
        raise _NoForm
    name = ph_name(ref.ph.name)
    if name == ' ':
        return '␠'
    if re.fullmatch('[%s]+' % SAFE, name) and not re.match(r'#[0-9]', name):
        return name
    if not name or '›' in name:
        raise _NoForm
    return '‹%s›' % name


def bundle(v, m):
    """[+b20 −b24] (V ⊆ M), or [V=hhhhhhhh M=hhhhhhhh]"""
    if v & ~m:
        return '[V=%08x M=%08x]' % (v, m)
    return '[%s]' % ' '.join(('+' if v >> b & 1 else '−') + 'b%d' % b
                             for b in range(32) if m >> b & 1)


def _tests(ws):
    out, i = [], 0
    while i < len(ws):
        w = ws[i]
        name = item_name(w >> 8) + ('.last' if w & 0x80 else '') + ('.hi' if w & 0x40 else '')
        if w & 0x3F in CAPTURE:
            out.append('<%s → %s>' % (name, CAPTURE[w & 0x3F]))
            i += 1
        elif w & 0x3F in COMPARE and i + 1 < len(ws):
            k = ws[i + 1]
            out.append('<%s %s %d>' % (name, COMPARE[w & 0x3F], k - 0x10000 if k & 0x8000 else k))
            i += 2
        else:
            raise _NoForm
    return out


def _rep(e):
    """the repetition suffix; nothing for a pattern that matches once and needs the pattern
    form anyway (several parts, tests, or none)"""
    if e.rep is None or (e.rep == (1, 0) and not e.simple()):
        return ''
    lo, hi = e.rep
    if (lo, hi) == (0, 127):
        return '*'
    return '{%d}' % lo if hi == 0 else '{%d,%d}' % (lo, hi)


def _elem(o, e, lead=True):
    """an element, with a space before it if lead"""
    if e.search:
        o.tok('…', 'op')
    if lead:
        o.put(' ')
    if e.opt:
        o.put('(')
    if e.ph is not None:
        o.put(ph_word(e.ph), 'ph', e.ph)
    if e.feat is not None:
        o.put(bundle(*e.feat), 'feat')
    if e.ph is None and e.feat is None and not e.tests:
        o.put('·', 'op')
    for t in _tests(e.tests or []):
        o.put(t, 'param')
    if _rep(e):
        o.put(_rep(e), 'num')
    if e.opt:
        o.put(')')


def _stmt(group):
    """X := n, X := A, B = X, or an accumulator instruction in instruction form"""
    a, b = group[0], group[-1]
    if b.op in ST_OPS or a.op == LD:
        o = _Out()
        _ldst(o, b.args)
        target = o.line().text[1:]
        if a.op == LD:
            return 'B = ' + target
        value = 'A' if len(group) == 1 else str(a.args[0] - 0x10000 if a.args[0] & 0x8000 else a.args[0])
        return '%s %s %s' % (target, ST_OPS[b.op], value)
    return rule_line(Rule(False, [a])).text[len('RULE '):]


def write(form):
    """Form -> Line; raises _NoForm"""
    o = _Out()
    head = ('*' if form.iterative else '') + ('' if form.label is None else '#%d' % form.label)
    if head:
        o.put(head, 'num')
        o.put(' ')
    for gi, g in enumerate(form.groups):
        if gi:
            o.tok(';', 'op')
            o.put(' ')
        for k, e in enumerate(g.lhs):
            search, e.search = e.search, False
            _elem(o, e, lead=k > 0)
            e.search = search
        if not g.lhs:
            o.put('∅', 'op')
        o.tok('→⁺' if g.plus else '→', 'op')
        if g.unwrap:
            o.tok('∅', 'op')
            o.tok('↺', 'op')
        if g.repl is not None:
            for ref in g.repl:
                o.tok(ph_word(ref), 'ph', ref)
            if not g.repl:
                o.tok('∅', 'op')
        for kind, val in g.mods:
            if kind == 'setf':
                o.tok(bundle(*val), 'feat')
            else:
                o.tok('{ %s }' % '; '.join(_stmt(s) for s in val), 'param')
    lhs = form.lhs()
    if form.left or form.right or form.back is not None or form.anchored or (lhs and lhs[0].search):
        o.tok('/', 'op')
        if form.back is not None:
            for e in form.back:
                _elem(o, e)
            o.tok('⟵', 'op')
        if form.anchored:
            o.tok('^', 'op')
        for e in form.left:
            _elem(o, e)
        if lhs and lhs[0].search:
            o.tok('…', 'op')
        o.tok('_', 'op')
        for e in form.right:
            _elem(o, e)
    if form.label is None:
        o.tok('once', 'op')
    return o.line()


# --- text -> Form

_LEX = re.compile(r'''
    (?P<ws>\s+)
  | (?P<esc>‹[^›]*›)
  | (?P<space>␠)
  | (?P<any>·)
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


@dataclass
class Tok:
    kind: str
    text: str
    pos: int
    attached: bool      # no space before it


def lex(text):
    out, i, gap = [], 0, True
    m = _HEAD.match(text)
    if m:
        out.append(Tok('head', m.group(), 0, False))
        i = m.end()
    while i < len(text):
        m = _LEX.match(text, i)
        if not m:
            raise ParseError('unexpected %r' % text[i], i)
        if m.lastgroup == 'ws':
            gap = True
        else:
            out.append(Tok(m.lastgroup, m.group(), i, not gap))
            gap = False
        i = m.end()
    return out


class _Reader:
    def __init__(self, text, names):
        self.toks, self.k, self.names, self.end = lex(text), 0, names, len(text)

    def peek(self, kind=None, text=None, at=0):
        k = self.k + at
        if k < len(self.toks):
            t = self.toks[k]
            if (kind is None or t.kind == kind) and (text is None or t.text == text):
                return t
        return None

    def take(self, kind=None, text=None, what=None):
        t = self.peek(kind, text)
        if t is None:
            raise ParseError('expected %s' % (what or text or kind), self.pos())
        self.k += 1
        return t

    def pos(self):
        return self.toks[self.k].pos if self.k < len(self.toks) else self.end

    def sym(self, s):
        """take symbol s if it comes next"""
        if self.peek('sym', s):
            self.k += 1
            return True
        return False

    def phoneme(self, t):
        name = {'space': ' ', 'esc': t.text[1:-1], 'name': t.text}[t.kind]
        key = {' ': '␠', ';': '⁏'}.get(name, name)
        if key not in self.names:
            raise ParseError('no phoneme %r' % name, t.pos)
        return PhRef(self.names[key])

    def at_elem(self):
        t = self.peek()
        return t is not None and (t.kind in ATOM or (t.kind == 'sym' and t.text in ('(', '…')
                                                     and self.peek('sym', '_', 1) is None))

    def elem(self):
        """[…] atom, or (atom)"""
        e = Elem(search=self.sym('…'))
        if self.sym('('):
            self.atom(e)
            t = self.take('sym', ')')
            if not t.attached:
                raise ParseError('no space before )', t.pos)
            if not e.simple() or e.rep is not None:
                raise ParseError('(x) holds one phoneme or one bundle', t.pos)
            e.opt = True
        else:
            self.atom(e)
        return e

    def atom(self, e):
        """name, bundle, tests and repetition, written together without spaces, in that order"""
        t, stage = self.take(what='a phoneme, ·, [features] or <test>'), 0
        while True:
            if t.kind in ('name', 'esc', 'space') and stage == 0:
                e.ph, stage = self.phoneme(t), 1
            elif t.kind == 'any' and stage == 0:
                stage = 2
            elif t.kind == 'bundle' and stage <= 1:
                e.feat, stage = read_bundle(t), 2
            elif t.kind == 'test' and stage <= 3:
                e.tests, stage = (e.tests or []) + read_test(t), 3
            elif t.kind == 'rep' and stage >= 1:
                e.rep, stage = read_rep(t), 4
            else:
                raise ParseError('unexpected %r in an element' % t.text, t.pos)
            nxt = self.peek()
            if nxt is None or not nxt.attached or nxt.kind not in ATOM + ('rep',):
                break
            t = self.take()
        if e.rep is None and not e.simple():
            e.rep = (1, 0)


def read_bundle(t):
    body = t.text[1:-1]
    m = re.fullmatch(r'V=([0-9a-f]{8}) M=([0-9a-f]{8})', body)
    if m:
        v, mk = int(m.group(1), 16), int(m.group(2), 16)
        if not v & ~mk:
            raise ParseError('V is inside M: write the bits as [+bN −bN]', t.pos)
        return v, mk
    v = mk = 0
    for part in body.split(' ') if body else []:
        m = re.fullmatch(r'([+−])b([0-9]+)', part)
        if not m or int(m.group(2)) > 31 or mk >> int(m.group(2)) & 1:
            raise ParseError('expected +bN or −bN, N in 0..31, each bit once', t.pos)
        mk |= 1 << int(m.group(2))
        v |= (m.group(1) == '+') << int(m.group(2))
    return v, mk


def read_test(t):
    name, last, hi, reg, cmp, k = _NTEST.fullmatch(t.text).groups()
    if name not in ITEM_IDS:
        raise ParseError('no parameter or variable %r' % name, t.pos)
    w = ITEM_IDS[name] << 8 | (0x80 if last else 0) | (0x40 if hi else 0)
    if reg is not None:
        if reg not in CAPTURE_IDS:
            raise ParseError('no register %r' % reg, t.pos)
        return [w | CAPTURE_IDS[reg]]
    return [w | COMPARE_IDS[cmp], u16(k, t.pos)]


def read_rep(t):
    if t.text == '*':
        return 0, 127
    lo, _, hi = t.text[1:-1].partition(',')
    if int(lo) > 255 or int(hi or 0) > 255:
        raise ParseError('a count is more than 255', t.pos)
    return int(lo), int(hi or 0)


def read_block(t, names):
    """{ statement; ... } -> [[Instr]]"""
    out, pos = [], t.pos
    for s in t.text[1:-1].split(';'):
        s, pos = s.strip(), pos + 1
        m = re.fullmatch(r'(.+?) (:=s|:=u|:=) (A|-?[0-9]+)', s)
        if m:
            target = ldst_words([(x, pos) for x in m.group(1).split(' ')])
            st = {v: k for k, v in ST_OPS.items()}[m.group(2)]
            pre = [] if m.group(3) == 'A' else [Instr(A_SET, [u16(m.group(3), pos)])]
            out.append(pre + [Instr(st, target)])
        elif s.startswith('B = '):
            out.append([Instr(LD, ldst_words([(x, pos) for x in s[4:].split(' ')]))])
        elif s:
            ins = _read_instr(s, pos, names)
            if ins.op not in COMPUTE or ins.op == LD or ins.op in ST_OPS:
                raise ParseError('%r is not a statement' % s, pos)
            out.append([ins])
        else:
            raise ParseError('an empty statement', pos)
        pos += len(s)
    return out


def _group(r, first):
    g = Group()
    if not r.sym('∅'):
        while not r.peek('arrow'):
            if not r.at_elem():
                raise ParseError('expected →', r.pos())
            g.lhs.append(r.elem())
    arrow = r.take('arrow')
    g.plus = arrow.text == '→⁺'
    if not first and not g.plus:
        raise ParseError('after the first group, each acts after the one before: →⁺', arrow.pos)
    if r.sym('∅'):
        if first and not g.plus and r.sym('↺'):
            g.unwrap = True
            return g
        g.repl = []
    elif r.peek('name') or r.peek('esc') or r.peek('space'):
        g.repl = []
        while r.peek('name') or r.peek('esc') or r.peek('space'):
            g.repl.append(r.phoneme(r.take()))
    while r.peek('bundle') or r.peek('block'):
        t = r.take()
        g.mods.append(('setf', read_bundle(t)) if t.kind == 'bundle' else ('stmts', read_block(t, r.names)))
    if g.repl is None and not g.mods:
        raise ParseError('expected phonemes, ∅, [features] or { statements }', r.pos())
    if g.repl is None and g.plus and g.mods[0][0] != 'setf':
        raise ParseError('→⁺ without phonemes starts with [features] (SETF+)', r.pos())
    return g


def read(text, names):
    """notation text -> Form; raises ParseError"""
    r = _Reader(text, names)
    form = Form()
    t = r.peek('head')
    if t:
        r.k += 1
        form.iterative = t.text.startswith('*')
        if '#' in t.text:
            form.label = int(t.text.lstrip('*#'))
            if form.label > 0xFFFF:
                raise ParseError('a label is at most 65535', t.pos)
    form.groups.append(_group(r, True))
    while r.sym(';'):
        form.groups.append(_group(r, False))
    if r.sym('/'):
        _env(r, form)
    if r.peek('once'):
        if form.label is not None:
            raise ParseError('a rule with a label #N is not once', r.pos())
        r.k += 1
    elif form.label is None:
        raise ParseError('expected once: a rule without a label #N', r.pos())
    if r.peek() is not None:
        raise ParseError('unexpected %r' % r.peek().text, r.pos())
    return form


def _env(r, form):
    elems = []
    while not r.sym('_'):
        if r.peek('sym', '⟵'):
            if form.back is not None or form.anchored:
                raise ParseError('⟵ comes once, before ^', r.pos())
            r.k += 1
            form.back, elems = elems, []
        elif r.peek('sym', '^'):
            if form.anchored or elems:
                raise ParseError('^ comes before the first element of the match', r.pos())
            r.k += 1
            form.anchored = True
        elif r.peek('sym', '…') and r.peek('sym', '_', 1):
            lhs = form.lhs()
            if not lhs or form.groups[0].plus:
                raise ParseError('… _ searches for the focus, the first element on the left-hand side',
                                 r.pos())
            r.k += 1
            lhs[0].search = True
        elif r.at_elem():
            elems.append(r.elem())
        else:
            raise ParseError('expected _', r.pos())
    if form.back is not None and elems:
        raise ParseError('⟵ is not used with other left context', r.pos())
    form.left = elems
    while r.at_elem():
        form.right.append(r.elem())


# --- Form -> instructions

def _feat_words(f):
    v, m = f
    return [v >> 16, v & 0xFFFF, m >> 16, m & 0xFFFF]


def _pattern_words(e):
    ws, h = [], 0
    if e.ph is not None:
        ws.append(e.ph)
        h |= 0x8000
    if e.feat is not None:
        ws += _feat_words(e.feat)
        h |= 0x4000
    if e.tests:
        ws += e.tests
        h |= 0x2000
    if len(ws) >= 0x7FF:
        raise ParseError('the pattern is too long')
    return [h | 2 * (len(ws) + 1)] + ws


def _match(role, e):
    """an element in its role ('seek', 'focus', 'scan', 'r', 'l') -> Instr"""
    if role in ('seek', 'scan') and not (e.simple() and e.rep is None and not e.opt):
        raise ParseError('a search (the first element, or after …) is for one phoneme or one bundle')
    if role == 'focus' and (e.opt or e.rep not in (None, (1, 0))):
        raise ParseError('the focus is one segment: no (x), {n} or *')
    if e.rep is None:
        op = OPS[role + ('?' if e.opt else '')][e.ph is None]
        return Instr(op, [e.ph] if e.ph is not None else _feat_words(e.feat))
    if role == 'focus':
        return Instr(FOCUSP, _pattern_words(e))
    return Instr(RP if role == 'r' else LP, [e.rep[0] << 8 | e.rep[1]] + _pattern_words(e))


def encode(form):
    """Form -> Rule; raises ParseError"""
    g0 = form.groups[0]
    for g in form.groups:
        if g.repl is None and not g.unwrap and len(g.lhs) != 1:
            raise ParseError('a group that keeps its segment has one element on the left')
        if g.unwrap and not g.lhs:
            raise ParseError('↺ deletes the left-hand side: not ∅')
        if g.repl is not None and (len(g.lhs) > 255 or len(g.repl) > 21):
            raise ParseError('a REPL deletes at most 255 and inserts at most 21 segments')
    if g0.plus and (not form.left or form.back is not None):
        raise ParseError('→⁺ acts after the focus, the last element of the left context (not with ⟵)')
    lhs = form.lhs()
    seq = form.left + lhs + form.right
    f = len(form.left) - (1 if g0.plus else 0)
    if f >= len(seq):
        raise ParseError('no focus: ∅ on the left needs right context')
    for e in lhs:
        if e.opt or e.rep not in (None, (1, 0)) or (e.search and e is not seq[f]):
            raise ParseError('the left-hand side is single segments: no (x), {n}, * or …')
    if seq[0].search:
        raise ParseError('the first element is searched for already: no …')
    instrs = []
    for i, e in enumerate(seq):
        search = e.search or (i == 0 and not form.anchored)
        role = ('seek' if search else 'focus') if i == f else ('scan' if search else 'r')
        instrs.append(_match(role, e))
    if form.back is not None:
        instrs.append(Instr(CPREV, []))
        for e in reversed(form.back):
            if e.search:
                raise ParseError('no … before ⟵')
            instrs.append(_match('l', e))
    for g in form.groups:
        mods = list(g.mods)
        if g.repl is not None:
            instrs.append(Instr(REPL_NEXT if g.plus else REPL, [len(g.lhs) * 256 + 12 * len(g.repl)] + g.repl))
        elif g.unwrap:
            instrs.append(Instr(UNWRAP, [len(g.lhs)]))
        elif g.plus:
            instrs.append(Instr(SETF_NEXT, _feat_words(mods.pop(0)[1])))
        for kind, val in mods:
            if kind == 'setf':
                instrs.append(Instr(SETF, _feat_words(val)))
            else:
                instrs += [Instr(i.op, list(i.args)) for s in val for i in s]
    if form.label is not None:
        instrs.append(Instr(NEXT, [form.label]))
    return check_length(Rule(form.iterative, instrs))


# --- the API

def parse_notation(text, names):
    """a rule in notation -> Rule; names from listing.ph_names(); raises ParseError"""
    return encode(read(text, names))


def notation_line(rule, names):
    """the rule in notation as a listing.Line, or None if the notation cannot say it"""
    try:
        line = write(decode(rule))
        again = parse_notation(line.text, names)
    except (_NoForm, ParseError):
        return None
    return line if words(again) == words(rule) else None
```

- [ ] **Step 4: Write `NOTATION.md`**

Create `editor/ivxedit/NOTATION.md` (the examples block is checked by `test_notation_md_examples`; keep each notation line followed by its indented instruction form):

````markdown
# Rule notation

The editor shows a rule as `A → B / C _ D` whenever this notation says exactly what the
rule's bytecode does, and in instruction form (`listing.py`) otherwise. `notation.py`
writes a rule, reads the text back, and keeps the text only if it gives the same words.
In the 12 shipped files that holds for 15,526 of the 15,564 rules. The other 38 are the
23 `GEMIN` rules, the 2 rules with `CHEAD`/`CTAIL`, the 8 rules that name the inventory's
terminator (REFERENCE §5.6) and the 5 `REPL` words whose count carries.

Two rules hold throughout:
- **Every encoding has its own spelling.** Two instruction sequences that look alike but
  behave differently (REFERENCE §5.6: the `inserted` flag, where `NEXT` resumes) are
  written differently. No two texts give the same rule unless they differ only in spacing
  or in the optional spellings listed under "Equivalent spellings".
- **Unmarked means the default encoding.** A rule written without markers (`^`, `…`, `⁺`,
  `{1}`) gets the encoding listed under "Elements and their opcodes".

The interpreter's semantics are in REFERENCE §5.6 and `src/cwtest.c`, `src/cwelem.c`.

## A line

```
[head] group [; group ...] [/ context] [once]
```

- **head**: `#N` is the rule's label, the operand of its closing `NEXT` (never read by the
  interpreter; the rule's number in the original source). `*` before it marks `RULE*`
  (iterative). A rule without `NEXT` has no label and ends with `once`; if it is
  iterative its head is `*` alone.
- **group**: `lhs → rhs`. The left-hand sides of the groups, in order, are consecutive
  segments of the match, starting at the focus.
- **context**: `left _ right`: what the match requires around the left-hand side.
  Without context the `/` part is left out.

Tokens are separated by spaces, except inside an element (below), whose parts are written
together.

## Elements

An element matches one segment, or a run of segments when it has a repetition:

| Written | Matches |
|---|---|
| `A`, `E0`, `+` | the phoneme of that name |
| `‹[›`, `‹*›` | a phoneme whose name holds a character other than letters, digits and `: $ % ' @ ! " `` ` . , = \| ~ \ + - ? # &`, or starts with `#` and a digit |
| `␠` | the phoneme named ` ` (a space) |
| `[+b20 −b24]` | a feature bundle: bits of the 32-bit `feat` (`+`: set, `−`: clear; the bits named are the care mask M). `[]` matches every segment. When V has bits outside M: `[V=00100000 M=01100000]` |
| `A[+b20]` | phoneme and bundle |
| `<F1 < 50>` | a parameter test: the segment's item (or its phoneme's default) compared with k: `<` `≤` `=` `≠` `≥` `>` (value OP k). `.last` takes the last payload word, `.hi` its signed high byte |
| `<F1 → R1>` | a capture into R0–R3 or V0–V9 |
| `·` | any segment (a pattern with no parts) |
| `X*`, `X{3}`, `X{2,125}` | repetition: `{min}` or `{min,opt}`, min required matches then up to opt more (`*` = `{0,127}`) |
| `(X)` | optional: one phoneme or one bundle, matched if present |
| `… X` | found by searching to the right (`SCAN`, or `SEEK` for the focus) |

Parts come in the order name, bundle, tests, repetition: `A[+b16]<F1 < 50>{2}`. Item names
are the parameters `DR ST … F0 XX YY ZZ`, `p29`…`p127` and the variables `v80`…`vff`.

## Elements and their opcodes

The **focus** is the first element of the left-hand side (or, when the left-hand side is
`∅`, the first element of the right context). With `→⁺` on the first group it is the last
element of the left context instead.

| Element | Opcode |
|---|---|
| the first element of the match, focus | `SEEK`, `SEEKF` (search) |
| the first element, not the focus | `SCAN`, `SCANF` (search) |
| `^` before the first element | no search: the match starts at the start position: `FOCUS`/`FOCUSF`/`FOCUSP` for the focus, `R`/`RF`/`RP` for another element |
| the focus, not first | `FOCUS`, `FOCUSF`; `FOCUSP` when it has tests, both a name and a bundle, no parts (`·`), or `{1}` |
| `… _`: the focus, not first, searched | `SEEK`, `SEEKF` |
| any other element | `R`, `RF`; `R?`, `RF?` for `(X)`; `RP` for a repetition, tests, both a name and a bundle, or `·` (with `{1}` written when the element would otherwise be plain) |
| `… X`, not first | `SCAN`, `SCANF` |
| context before `⟵` | after the rest of the match: `CPREV`, then each element right to left with `L`, `LF`, `L?`, `LF?`, `LP` |

A search (the first element, or `…`) takes one phoneme or one bundle. The left-hand side
holds single segments: no `(X)`, no repetition other than `{1}`, no `…` (except the focus,
as `… _`). `⟵` does not combine with other left context or with `→⁺`.

## Groups and actions

| Group | Instructions |
|---|---|
| `B C → S T` | `REPL del 2 -> S T`: delete the left-hand side, insert the phonemes |
| `∅ → S` | `REPL del 0 -> S`: insert before the focus |
| `B → ∅` | `REPL del 1 ->`: delete |
| `B C → ∅ ↺` | `UNWRAP 2`: delete and restart the rule (the only action of its rule) |
| `B → [+b20]` | `SETF`: set the bundle's bits on the focus, keep it |
| `B → { … }` | statements on the focus |
| `→⁺` | the group starts one segment after the focus: `REPL+` (with phonemes or `∅`), or `SETF+` (with a bundle first) |

After the phonemes (or `∅`) of a group come its bundles (`SETF`) and statement blocks, in
order. A second or later group always has `→⁺`: it acts on the segments after the ones
the group before it left. A group with no phonemes and no `∅` has one element on its
left-hand side. `REPL` inserts at most 21 phonemes and deletes at most 255 segments.

Statements, in `{ }` and separated by `;`, work on the focus segment's items and the
accumulator (REPLs and SETFs before them may have moved the focus):

| Statement | Instructions |
|---|---|
| `vac := 5`, `F1@0 :=u 5`, `F0@0.hi :=s -3` | `A= 5`, then `ST`/`STu`/`STs` |
| `vac := A` | `ST` (`:=u` `STu`, `:=s` `STs`) |
| `B = F1@0` | `LD` |
| `A=B`, `A+= -10`, `A*=frac 3/2^2`, `B=R1` | the accumulator instruction in instruction form |

Item operands are as in instruction form: `F1@k` (payload word k, `.hi` for the high byte,
`F1@#hhhh` for an index word with other bits), `vXX`, then ` t=N` for a nonzero high byte
of the spec word.

## Equivalent spellings

These give the same words, and the editor writes the first:
- `A[+b20]` and `A[+b20]{1}` (an element that is a pattern anyway);
- `{ vac := 5 }` and `{ A= 5; vac := A }`;
- `{ x; y }` and `{ x } { y }`.

## Examples

Each notation line is followed by its instruction form (`swrules.ivx`; checked by
`tests/test_notation.py`):

```text
#32 + B → S
    RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32
#32 A → A [+b20 −b24] / 1 _
    RULE SCAN 1 ; FOCUS A ; REPL del 1 -> A ; SETF [00100000/01100000] ; NEXT #32
#818 ∅ → # / [+b16]{3} ⟵ _ A D R E S S
    RULE SEEK A ; R D ; R R ; R E ; R S ; R S ; CPREV ; LP 3,0 { [00010000/00010000] } ; REPL del 0 -> # ; NEXT #818
#818 O → O [−b20 +b27] ; M A →⁺ M A [+b20 +b24 +b25 +b26] ; N →⁺ N / [+b0 −b1 +b16] [+b1 +b16]* _
    RULE SCANF [00010001/00010003] ; RP 0,127 { [00010002/00010002] } ; FOCUS O ; R M ; R A ; R N ; REPL del 1 -> O ; SETF [08000000/08100000] ; REPL+ del 2 -> M A ; SETF [07100000/07100000] ; REPL+ del 1 -> N ; NEXT #818
#32 1 → ∅ ↺
    RULE SEEK 1 ; UNWRAP 1 ; NEXT #32
#7 B →⁺ S / A _
    RULE SEEK A ; R B ; REPL+ del 1 -> S ; NEXT #7
#7 B → S / A _
    RULE SCAN A ; FOCUS B ; REPL del 1 -> S ; NEXT #7
#7 A{1} → S / ^ _ (B) … ‹*›
    RULE FOCUSP { A } ; R? B ; SCAN * ; REPL del 1 -> S ; NEXT #7
#7 A → { vac := 5; F1@0 :=u A; B = F0@-1.hi t=2; A*=frac 3/2^2 } / ␠ … _ ·
    RULE SCAN ␠ ; SEEK A ; RP 1,0 { } ; A= 5 ; ST vac ; STu F1@0 ; LD F0@-1.hi t=2 ; A*=frac 3/2^2 ; NEXT #7
* E[+b16]<F1 ≥ 50> → [+b17] / ^ _ once
    RULE* FOCUSP { E [00010000/00010000] <F1 >= 50> } ; SETF [00020000/00020000]
```
````

- [ ] **Step 5: Run the tests**

Run: `python -m unittest tests.test_notation -v` from `editor/`
Expected: all 11 pass, and `test_coverage` prints `rules in notation: M0 5691/5714, M1 1404/1404, M2 3881/3885, M3 878/882, M4 608/608, M5 3064/3071`. `test_a_rule_added_in_notation` runs only with `build/ivxsay.exe` (run `make` first, or it skips with a message).

Run: `python -m unittest discover -s editor/tests -t editor` from the repository root
Expected: OK.

- [ ] **Step 6: Commit**

```bash
git add editor/ivxedit/notation.py editor/ivxedit/NOTATION.md editor/tests/test_notation.py
git commit -m "ivxedit: rule notation A -> B / C _ D, read and written (notation.py, NOTATION.md)

15,526 of the 15,564 shipped rules read back from notation word for word; the
others (GEMIN, CHEAD/CTAIL, terminator references, carrying REPL words) stay in
instruction form. A rule added in notation changes what ivxsay says.

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

### Task 3: Notation in the window

**Files:**
- Modify (whole file): `editor/ivxedit/gui/rulesview.py`
- Modify: `editor/ivxedit/gui/mainwindow.py`
- Modify: `editor/tests/test_gui.py`
- Modify: `AGENTS.md`, `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`

**Interfaces:**
- Consumes (Task 2): `notation.notation_line(rule, names)`; (Task 1) `listing.ph_names`.
- Produces:
  - `RulesView.set_module(name, module, notation)` (`notation`: per rule, its notation `Line` or `None`), `RulesView.set_instruction_form(on)`, `RulesView.notation`, `RulesView.lines`; module constants `FALLBACK` (the tint `QColor`) and `PREFIX`.
  - `MainWindow.instr_action` (checkable, View ▸ Instruction form, Ctrl+I), `MainWindow.coverage` (`QLabel`), `MainWindow.set_instruction_form(on)`, `MainWindow.names`, `MainWindow.notation`.

- [ ] **Step 1: Write the failing tests**

In `editor/tests/test_gui.py`:

1. In `test_rules_view_shows_instruction_form`: replace it with two tests, replace

```python
    def test_rules_view_shows_instruction_form(self):
        w = self.win
        w.open_file(ivx_path('sw'))
        w.show_rule('M0', 0)
        text = w.rule_views['M0'].toPlainText().splitlines()
        self.assertEqual(len(text), len(w.f.modules['M0'].rules))
        self.assertIn('RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32', '\n'.join(text))

```

   with

```python
    def test_rules_view_shows_notation(self):
        from ivxedit.gui.rulesview import FALLBACK, PREFIX
        from ivxedit.listing import ph_names
        from ivxedit.notation import notation_line
        w = self.win
        w.open_file(ivx_path('sw'))
        w.show_rule('M0', 0)
        view, rules = w.rule_views['M0'], w.f.modules['M0'].rules
        names = ph_names(w.f.phonemes)
        shown = [notation_line(r, names) is not None for r in rules]
        self.assertEqual(w.coverage.text(), 'M0: %s / %s rules in notation' % (
            format(sum(shown), ','), format(len(rules), ',')))
        text = view.toPlainText().splitlines()
        self.assertEqual(len(text), len(rules))
        self.assertEqual(text[0], PREFIX % 0 + '#32 + B → S')
        # the rules notation cannot say: instruction form, on a tinted background
        self.assertIn(False, shown)
        for i, t in enumerate(text):
            tinted = view.document().findBlockByNumber(i).blockFormat().background().color() == FALLBACK
            with self.subTest(rule=i):
                self.assertEqual(tinted, not shown[i])
                self.assertEqual(t.startswith(PREFIX % i + 'RULE'), not shown[i])
        # View ▸ Instruction form, for the open views and the ones made later
        w.instr_action.setChecked(True)
        self.assertEqual(view.toPlainText().splitlines()[0], PREFIX % 0 + 'RULE SEEK + ; R B ; REPL del 2 -> S ; NEXT #32')
        w.show_rule('M1', 0)
        self.assertTrue(all(t.startswith(PREFIX % i + 'RULE')
                            for i, t in enumerate(w.rule_views['M1'].toPlainText().splitlines())))
        w.instr_action.setChecked(False)
        self.assertEqual(view.toPlainText().splitlines()[0], PREFIX % 0 + '#32 + B → S')

    def test_rules_table_counts_notation(self):
        from ivxedit.listing import ph_names
        from ivxedit.notation import notation_line
        w = self.win
        w.open_file(ivx_path('fr'))
        w.tree.setCurrentItem(next(t for t in w.items() if t.text(0) == 'Rules'))
        model = w.table_view.model
        self.assertEqual(model.headers, ['Module', 'Rules', 'In notation', 'RULE*', 'Lead word'])
        names = ph_names(w.f.phonemes)
        for row in model.rows:
            rules = w.f.modules[row[0]].rules
            self.assertEqual(row[2], str(sum(notation_line(r, names) is not None for r in rules)))
        self.assertEqual(w.coverage.text(), '')

```


- [ ] **Step 2: Run the tests to see them fail**

Run: `python -m unittest tests.test_gui` from `editor/` (with `QT_QPA_PLATFORM=offscreen` set)
Expected: errors in the two new tests (`ImportError: cannot import name 'FALLBACK'`, and the Rules table's headers have no 'In notation').

- [ ] **Step 3: Rewrite `rulesview.py`**

Replace `editor/ivxedit/gui/rulesview.py` with:

```python
"""The rules view: one module, one rule per line, in rule notation where it says the rule
exactly and in instruction form otherwise (those lines tinted); highlighted; double-click
a phoneme to jump to it."""
from PySide6.QtGui import (QColor, QSyntaxHighlighter, QTextBlockFormat, QTextCharFormat, QTextCursor,
                           QTextFormat)
from PySide6.QtWidgets import QPlainTextEdit, QTextEdit

from ..listing import rule_line
from .views import mono

PREFIX = '%5d  '          # the rule index, as in the Problems panel's "M0 rule 12"
COLOURS = {'op': '#1f5fbf', 'ph': '#a0400a', 'num': '#2a7f2a', 'feat': '#7a2a9a',
           'param': '#8a6d00', 'raw': '#c00000'}
FALLBACK = QColor('#f1ece0')   # background of a rule shown in instruction form for want of notation


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
        self.document().setUndoRedoEnabled(False)
        self.lines = []                   # the Line shown for each rule
        self.module = None
        self.notation = []                # per rule: its Line in notation, or None
        self.instruction_form = False     # show every rule in instruction form
        self._hl = _Highlighter(self.document(), self)

    def set_module(self, name, module, notation):
        """show module; notation: per rule, its notation Line or None (notation.notation_line)"""
        self.module, self.notation = module, notation
        self._fill()

    def set_instruction_form(self, on):
        if on != self.instruction_form:
            self.instruction_form = on
            self._fill()

    def _fill(self):
        rules = self.module.rules if self.module is not None else []
        self.lines = [n if n is not None and not self.instruction_form else rule_line(r)
                      for r, n in zip(rules, self.notation)]
        self.setPlainText('\n'.join(PREFIX % i + ln.text for i, ln in enumerate(self.lines)))
        tint = QTextBlockFormat()
        tint.setBackground(FALLBACK)
        for i, n in enumerate(self.notation):
            if n is None:
                QTextCursor(self.document().findBlockByNumber(i)).setBlockFormat(tint)

    def ref_at(self, rule, col):
        """the PhRef at column `col` of rule line `rule`, or None"""
        if not 0 <= rule < len(self.lines):
            return None
        col -= len(PREFIX % rule)
        for s, e, kind, obj in self.lines[rule].spans:
            if kind == 'ph' and s <= col < e:
                return obj
        return None

    def goto_rule(self, i):
        if not 0 <= i < len(self.lines):
            return
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

- [ ] **Step 4: Edit `mainwindow.py`**

In `editor/ivxedit/gui/mainwindow.py`:

1. In the imports, replace

```python
from ..info import a1_text, char_rows, flags_text, header_fields, hexdump, ph_name, unknown_text
from ..model import PNAMES, load
```

   with

```python
from ..info import a1_text, char_rows, flags_text, header_fields, hexdump, ph_name, unknown_text
from ..listing import ph_names
from ..model import PNAMES, load
from ..notation import notation_line
```

2. In `__init__`: the per-file notation state, replace

```python
        self.rule_views = {}          # module name -> RulesView, per open file
```

   with

```python
        self.rule_views = {}          # module name -> RulesView, per open file
        self.names = {}               # ph_names of the open file
        self.notation = {}            # module name -> [notation Line or None] per rule
```

3. In `__init__`: the coverage label, replace

```python
        self.problems = QLabel()
        self.statusBar().addWidget(self.problems)
```

   with

```python
        self.problems = QLabel()
        self.statusBar().addWidget(self.problems)
        self.coverage = QLabel()      # the shown module's rules in notation
        self.statusBar().addWidget(self.coverage)
```

4. In `_menus`: the View menu, replace

```python
            a.triggered.connect(slot)
            m.addAction(a)
```

   with

```python
            a.triggered.connect(slot)
            m.addAction(a)
        self.instr_action = QAction('&Instruction form', self)
        self.instr_action.setCheckable(True)
        self.instr_action.setShortcut(QKeySequence('Ctrl+I'))
        self.instr_action.toggled.connect(self.set_instruction_form)
        self.menuBar().addMenu('&View').addAction(self.instr_action)
```

5. In `open_file`: the names and a fresh notation cache, replace

```python
        self.f, self.path = f, Path(path)
        self.uses = uses
```

   with

```python
        self.f, self.path = f, Path(path)
        self.uses = uses
        self.names, self.notation = ph_names(f.phonemes), {}
```

6. In `show_item`: clear the coverage label, replace

```python
        kind, obj = item.data(0, ROLE)
        f = self.f
```

   with

```python
        kind, obj = item.data(0, ROLE)
        f = self.f
        self.coverage.setText('')
```

7. In `show_item`: the Rules table and the module view, replace

```python
            rows = [[n, len(m.rules), sum(r.iterative for r in m.rules), '0x%04x' % m.lead]
                    for n, m in sorted(f.modules.items())]
            self._table('Rule modules (REFERENCE §5.5)', ['Module', 'Rules', 'RULE*', 'Lead word'],
                        rows, [('module', n) for n in sorted(f.modules)])
        elif kind == 'module':
            self.stack.setCurrentWidget(self._rules_view(obj))
```

   with

```python
            rows = [[n, len(m.rules), self._in_notation(n), sum(r.iterative for r in m.rules),
                     '0x%04x' % m.lead] for n, m in sorted(f.modules.items())]
            self._table('Rule modules (REFERENCE §5.5)',
                        ['Module', 'Rules', 'In notation', 'RULE*', 'Lead word'],
                        rows, [('module', n) for n in sorted(f.modules)])
        elif kind == 'module':
            self.stack.setCurrentWidget(self._rules_view(obj))
            self.coverage.setText('%s: %s / %s rules in notation' % (
                obj, format(self._in_notation(obj), ','), format(len(f.modules[obj].rules), ',')))
```

8. In `_rules_view`: replace it with the notation cache, the view and the switch, replace

```python
    def _rules_view(self, name):
        if name not in self.rule_views:
            v = RulesView(self.show_phoneme)
            v.set_module(name, self.f.modules[name])
            self.stack.addWidget(v)
            self.rule_views[name] = v
        return self.rule_views[name]
```

   with

```python
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
```


- [ ] **Step 5: Run the tests**

Run: `python -m unittest tests.test_gui -v` from `editor/` (with `QT_QPA_PLATFORM=offscreen`)
Expected: all 8 pass.

Run: `make editor-test` from the repository root (with `export PATH=/c/winlibs/mingw32/bin:$PATH`)
Expected: OK, no test skipped for want of `ivxsay` or PySide6.

- [ ] **Step 6: Look at it**

Run `python editor/ivxedit.pyw bin/swrules.ivx`, open Rules ▸ M0: rule 0 reads `#32 + B → S`, rule 215 (`RULE GEMIN #5712`) is on a beige background, the status bar says `M0: 578 / 579 rules in notation`; View ▸ Instruction form switches every line to `RULE …` and back. (Skip this step when no display is available; the tests cover the same.)

- [ ] **Step 7: Update the documentation**

1. In `AGENTS.md`, the repository-layout row for `editor/`: steps done: replace

```text
Steps 1 and 2 are done.
```

   with

```text
Steps 1–3 are done.
```

2. In `AGENTS.md`, the same row: the rules: replace

```text
shows every section, with the rules in instruction form (`ivxedit.listing`).
```

   with

```text
shows every section, with the rules in the notation `A → B / C _ D` (`ivxedit.notation`, grammar in `editor/ivxedit/NOTATION.md`; 15,526 of the 15,564 shipped rules) and the rest in instruction form (`ivxedit.listing`, whose `parse_rule` reads it back).
```

3. In `AGENTS.md`, the Editor section: replace

```text
This starts the editor (read-only in step 2).
```

   with

```text
This starts the editor (read-only until step 4; View ▸ Instruction form shows every rule in instruction form).
```

4. In `docs/superpowers/specs/2026-10-03-ivx-editor-design.md`, the end of the "Rule notation" section: replace

```text
The status bar shows the coverage per module, e.g. "M0: 5,214 / 5,714 in rule notation".
```

   with

```text
The status bar shows the coverage per module, e.g. "M0: 5,214 / 5,714 in rule notation".

**As built in step 3** (plan `docs/superpowers/plans/2026-10-04-ivxedit-step3-rule-notation.md`): the grammar is `editor/ivxedit/NOTATION.md`. 15,526 of the 15,564 shipped rules are in notation; the other 38 are the 23 `GEMIN` rules, 2 with `CHEAD`/`CTAIL`, the 8 that name the inventory's terminator and the 5 carrying `REPL` words. Several actions are groups separated by `;` (`A → B [+b20] ; C →⁺ D`), each after the first starting one segment further on (`REPL+`, `SETF+`); the `⁺` marker is on the arrow, for `SETF+` too. Left context matched after `CPREV` is written before `⟵` (`#818 ∅ → # / [+b16]{3} ⟵ _ A D R E S S`). `·` is a pattern with no parts (any segment), `A → ∅ ↺` is `UNWRAP`, and phoneme names that would read as syntax are written `‹name›`. The rules view tints the rules left in instruction form; View ▸ Instruction form shows every rule that way. The instruction-form parser is `listing.parse_rule`.
```


- [ ] **Step 8: Commit**

```bash
git add editor/ivxedit/gui/rulesview.py editor/ivxedit/gui/mainwindow.py editor/tests/test_gui.py AGENTS.md docs/superpowers/specs/2026-10-03-ivx-editor-design.md
git commit -m "ivxedit gui: rules in notation, the rest tinted in instruction form; View > Instruction form; coverage per module

Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>"
```

---

## Self-review against the spec

- Rule notation table: every row has a spelling (left context, `…`, LHS, `_ x`, `_ (x)`, `CPREV` → `⟵`, `^`, bundles both ways, `{3}`/`{2,127}`/`*`, tests and captures, `SETF`, `∅`, `⁺`, statements, `;`, `*`/`once`/`#N`) — Task 2, `NOTATION.md`.
- Rules for the grammar: own spelling per encoding (`test_every_encoding_has_its_own_spelling`), default encoding (`encode`), `b0`…`b31`, `NOTATION.md` checked by a test.
- Status-bar coverage per module — Task 3 (`coverage` label, "In notation" column).
- Rules view: syntax highlighting, fallback rules tinted, phoneme jumps (spans of kind `ph` in both forms) — Task 3.
- Testing 5 (Task 1), 6 and 8 (Task 2), 7 (Tasks 1 and 2).
- Deferred to later steps (not this plan): editing (step 5), typing aids such as ASCII `->` for `→` (step 5), named features (out of scope), `GEMIN` in notation.
