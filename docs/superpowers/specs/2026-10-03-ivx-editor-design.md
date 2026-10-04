# ivxedit: a GUI editor for `.ivx` language files

Date: 2026-10-03. Status: approved in conversation, awaiting review of this document.

## Goal

A desktop editor that can open any of the 12 `bin/*.ivx` files, show **everything** in them in readable form, edit it, save a working `.ivx`, and play the result at once. The rules are shown and edited in the phonological rule notation `A → B / C _ D` wherever that notation reproduces the bytecode exactly, and in instruction form everywhere else. It is a Python 3.13 + PySide6 application in a new top-level `editor/` folder. `src/`, `lib/` and `cli/` do not change.

## Facts this design rests on

Measured on the 12 shipped files with `tools/ivx.py`, `tools/ivxdasm.py` and capstone on 2026-10-03, unless a section of `REFERENCE.md` is cited.

- **There is no 68k machine code in the files** [CONFIRMED]. The rule sections disassemble as 68000 code to junk (`ori.b #$8,(a0)`, an invalid word every few instructions, a five-instruction pattern repeating), while `ivxdasm.py` decodes all 15,564 rules to exactly their length prefixes. The files are big-endian and the opcodes are multiples of 4 (jump-table offsets), so the original compiler and interpreter probably ran on a 68k machine [LIKELY]; the content is RULSYS bytecode (REFERENCE §5.6).
- **Section order is the same in every file** [CONFIRMED]: inventory at body 0x170, then M3 (H[5]), M4 (H[6]), M0 (H[9]), M1 (H[10]), M5 (H[11]), M2 (H[12]), H[14] transliterations, H[13] class codes, H[3] lexicon. Absent sections are skipped.
- **Every gap between a section's content and the next section is zero bytes**, 0–0x12 bytes long, consistent with 16-byte alignment of section starts [LIKELY; the exact rule is checked in step 1].
- **A second, indexed lexicon follows the class codes in the 10 files without H[3]** [CONFIRMED that the data exists; format HYPOTHESIS]. At H[13] + 0x80 there is a 128-word index (one entry per first byte; the DLL byte-swaps it, REFERENCE §5.1), then entries such as `!` → `'UM#GE0K'E:R` (German) and `ABR'IR INTERROGATHI'ON` (Spanish). Every index value is below the region's size (German: max 0xc566, region 0xc590), so the values are relative to the region [LIKELY] and the region can move as one block. REFERENCE §5.7's "the other languages rely on rules" is incomplete.
- **Danish H[2] = 0xDB00 points into that region** [CONFIRMED]; its meaning is unknown.
- **Phoneme ids are byte offsets from H[8]**: `REPL` reads the new segment's record at `phinv + id/2` words (`src/cwelem.c:167`) [CONFIRMED]. Engine code looks phonemes up by name (`ph_find`, `src/tpelem.c:284`), so the rule bytecode is the only user of the offsets [LIKELY; step 1 searches the Core for other phoneme pointers].
- **8 phoneme ids in the rules point at the inventory's 0 terminator**: 1 in `frrules.ivx` (0x1bae) and 7 in `itrules.ivx` (0xe3c) [CONFIRMED]. They name a phoneme that does not exist; probably a compiler artifact [HYPOTHESIS].
- **The `NEXT` operand is never read**: `cw_op_NEXT` resets `ip` to the rule start (`src/cwflow.c:45`) [CONFIRMED]. It is almost certainly the rule's number or line in the original source [LIKELY], and the editor shows it as the rule's label.
- **Rule shapes** [CONFIRMED, all 15,564 rules]: 15,114 (97 %) are a match part with exactly one focus, then actions, then `NEXT`. Of those, 11,540 only rewrite (`REPL`, `SETF`, `UNWRAP`, `GEMIN`), 3,148 only compute (`LD`/`ST`/arithmetic, mostly M2) and 426 do both. The other 450 move the match cursor mid-match (`CPREV`/`CHEAD`/`CTAIL`), mostly `CPREV ; L…` (left context relative to the focus).
- **Matching is one left-to-right scan** [CONFIRMED, `src/cwtest.c`]: `SCAN x` searches right for `x` without taking a focus (left context), `SEEK x` searches right for the focus, `FOCUS x` requires the next segment to be the focus, `R`/`R?` require/allow the next segment, `L`/`L?` step left (after `CPREV` sets `next = focus->prev`), `RP`/`LP` repeat a pattern `min` times then up to `opt` more. `REPL d → ids` deletes `d` segments from the focus and inserts `ids`; `REPL+` (B8), `SETF+` (C0) act one segment after the focus.
- **Encodings that look equivalent are not**: the `inserted` flag (`FOCUS` fails while it is set, `SEEK` clears it) and where `NEXT` resumes after a `REPL` differ between, for example, `SEEK x ; R y ; REPL+ …` and `SCAN x ; FOCUS y ; REPL …` [CONFIRMED, `src/cwtest.c`, `src/cwelem.c:125`].
- **Five shipped `REPL` words do not encode as `d·256 + 12·n` without a carry**: Danish 0x314, 0x320, 0x32c ×2 and Icelandic 0x1108 [CONFIRMED].
- **`ivxsay -l` accepts an `.ivx` path** and `-E` prints phoneme events with sample positions (`cli/ivxsay.c`) [CONFIRMED]. The library API does not expose synth frames.
- **Environment**: Python 3.13 is 64-bit, so it cannot load the 32-bit `infovox230.dll`; Speak runs `ivxsay.exe` as a separate process. numpy 2.4 is installed; PySide6 and pytest are not.

## Delivery steps

Each step is usable on its own.

1. **Model and writer.** `ivxedit` reads a file into the model and writes it back. No GUI. Includes the step-1 investigations listed under "Open questions".
2. **Read-only GUI.** Every section shown, rules in instruction form, Speak with waveform, spectrogram and phoneme strip, A/B against the original file.
3. **Rule notation, read-only.** Rules shown in `A → B / C _ D` form where it round-trips, coverage per module.
4. **Voice-data editing.** Header, parameter tables, phonemes, characters, lexicon. Undo, the Problems panel, Save As.
5. **Rule editing.** In notation and in instruction form, per rule.
6. **Later: parameter plots.** A frame-dump option in `ivxsay` (a C-side addition), drawn as more rows on the Speak panel's time axis.

## Package layout

```
editor/
  ivxedit/
    binfmt.py      big-endian helpers, section directory, raw blobs
    model.py       IvxFile and the data classes below
    rules.py       instruction decoding/encoding (REFERENCE §5.6 operand formats)
    writer.py      model → bytes: layout, padding, directory, phoneme-id fixups, checks
    notation.py    rule notation ↔ instructions                 (step 3)
    speak.py       runs ivxsay, reads the WAV and the -E events   (step 2)
    gui/           PySide6 application                            (step 2+)
  tests/           unittest
  requirements.txt PySide6, numpy
  ivxedit.pyw      launcher
```

`tools/ivx.py` and `tools/ivxdasm.py` stay unchanged. `ivxedit` does not import them; the tests use them as an independent decoder.

## Model (`model.py`)

**As built in step 1** (plan `docs/superpowers/plans/2026-10-03-ivxedit-step1-model-writer.md`): the model keeps raw words where the list below describes typed fields — `tables` (4 × 32 words, with the `param(i)` accessor), `directory`, `header` (with `version`), `head`, the character tables as `bytes`, `ixlex` as bytes, `Module.end` as words, and `Instr.args` as flat operand words with `PhRef`s at the phoneme positions. The typed views below (feature pairs, patterns, LD/ST specs, REPL del/ids, parameter entries) are accessors to add over those words in the steps that edit them; edits go through them so `check()` stays meaningful.

- **`IvxFile`**
  - `header`: flags, version string (0x80), copyright string (0xC0), lexicon name (0x10), the dword at 0x1C, and the rest of the 0x200 bytes as raw bytes.
  - `head`: body 0x40..0x70 (build tag, then the words at 0x6A, 0x6C, 0x6E), copied as found.
  - `end_pad`: zero bytes after H[15] (am, it, no, sp); `pads`: extra zero padding after a section (16 after the inventory in 6 files).
  - `params`: 29 entries in parameter order (`DR ST FS CS FD F1 … F0 XX YY ZZ`), each with `a0` (raw word: the names slot), `a1` (type code), `a2` (rate), `a3` (offset). The writer stores them reversed, slot j ↔ parameter 29−j, in the 32-word arrays; the three unused slots per array are kept raw.
  - `phonemes`: list of `Phoneme`. The first is the defaults record (`is_defaults = True`) and stays first.
  - `modules`: dict `M0`…`M5` → `Module`, or absent when the language has none.
  - `classes`: 128 class codes for 0x00–0x7F (H[13]), plus, when H[14] exists, 128 two-char transliterations and 128 class codes for 0x80–0xFF.
  - `lexicon`: list of `(orth, transcription)` (H[3]) and the leading bytes before the first entry (REFERENCE §5.7) as raw.
  - `lexicon2`: the region after the H[13] class codes in files without H[3], raw until decoded.
  - `unknown`: H[0], H[1], H[2], H[7] and anything else not modelled, raw, written back in place.
- **`Phoneme`**: `name` (2 chars), `feat` (u32), `mask` (u32), `items`: a list of `ParamItem(pid, points=[(time:s8, value:u8)…])` and `VarItem(vid, value:s16)`, in file order (ascending id in records, REFERENCE §5.6.1).
- **`Module`**: `lead` word, `rules: list[Rule]`, and the module's trailing words after its last rule (`END`, …) as decoded instructions.
- **`Rule`**: `label` (the `NEXT` operand, or `None` when the rule has no `NEXT`), `iterative` (`RULE*`), `instrs: list[Instr]`.
- **`Instr`**: opcode plus typed operands: `PhRef`, feature pair `(V, M)`, pattern, LD/ST spec and index words, `REPL` (`del`, `ids`, and the raw word when it does not encode as `d·256 + 12·n`), numbers.
- **`PhRef`**: a reference to a `Phoneme` object plus a byte `delta`; `PhRef(None)` is the inventory terminator (the 8 ids above), encoded as the new terminator offset.

## Writer (`writer.py`)

**Layout** (always the order all files share):

```
0x000  file header (0x200)          body length at 0x08 updated
body:  directory H[0..15]            rebuilt; H[0], H[1], H[2], H[7] from the model
       0x40 build tag, 0x6A..0x6E    words copied or derived (see Model)
       0x70 A0..A3                   reversed, 32 words each
       0x170 phoneme inventory       records, then the 0 terminator
       M3, M4, M0, M1, M5, M2        rule modules present in the model
       H[14] transliterations + classes 0x80–0xFF   (when present)
       H[13] classes 0x00–0x7F, then lexicon2      (lexicon2 in files without H[3])
       H[3] lexicon                  (sw, bl)
```

Each section starts at the alignment step 1 confirms (16 bytes expected), padded with zeros. H[15] and the header's body length come from the result. Danish H[2] is relocated with lexicon2 if step 1 shows it points into it, and kept otherwise.

**Phoneme ids, two passes.** Pass 1 lays out the inventory and gives each `Phoneme` its new offset. Pass 2 encodes the modules; every `PhRef` is written as `offset(phoneme) + delta`. Ids are written from typed operands (`SEEK`/`FOCUS`/`SCAN`/`R`/`R?`/`L`/`L?`, `REPL`/`REPL+` id lists, pattern headers with bit 15), so no location can be missed.

**Checks before saving.** Any failure writes nothing and reports the location (section, rule, phoneme):
- the inventory, including its terminator, is at most 0x8000 bytes (phoneme ids are signed shorts), and each rule's length fits its length word;
- each `REPL` word's low byte is exactly 12·n for its n ids (so at most 21), unless it is an unedited carrying word from the original file (the five shipped ones);
- M0, M1, M2 and M5 exist, and M3 only with M4;
- every `PhRef` points at a phoneme in the model; deleting a phoneme that rules use is refused, with the list of rules;
- lexicon entries are in the order `lex_lookup` binary-searches (REFERENCE §5.5.1);
- parameter times and values fit s8/u8, variables s16, class codes and transliterations their bytes;
- the target path is not inside `bin/`.

`save()` also re-loads the bytes it is about to write and refuses if they do not load and re-encode identically.

**Saving.** Save As writes a temporary file in the target folder and renames it over the target; overwriting an existing file first keeps `<name>.bak`. Speak writes to a temporary file in the system temp folder.

## Rule notation (`notation.py`, step 3)

The translator works rule by rule. A rule is shown in notation only if `notation → instructions → bytes` reproduces its original bytes; otherwise it is shown in instruction form, the `ivxdasm.py` syntax made unambiguous, which also round-trips. Both forms are editable in step 5, in the same module view.

**Notation:**

| Bytecode | Notation |
|---|---|
| left context before the focus (`SCAN x`, then `R y`…) | `x y _`. The first element of a match is found by search: `SCAN` when it is left context, `SEEK` when it is the focus. |
| a search later in the match (`SCAN`/`SEEK` after the first element) | `…` before the element searched for: `x … y _` |
| the focus plus the segments `REPL` deletes | left-hand side, `+ B → …` |
| right context `R x`, optional `R? x` | `_ x`, `_ (x)` |
| `CPREV ; L…` | left context in reading order (step 3 decides whether these rules leave the fallback) |
| a match that starts with `FOCUS`/`R` (anchored at the start position) | `^` before the first element |
| feature test `{V, M}` with V ⊆ M | `[+b20 −b24]`; otherwise `[V=00100000 M=01100000]` |
| `RP`/`LP {min, opt}` | `X{3}`, `X{2,127}`, `X*` (opt 0x7F) |
| pattern parameter tests | `X<F1 ≥ 50>`; a capture `X<F1 → R1>` |
| `SETF` | after the right-hand side, `→ A [+b20]` |
| `del 0` / no ids | `∅ → # / …`, `S → ∅` |
| `REPL+`, `SETF+` | a dimmed `⁺` marker on the arrow or bracket |
| `LD`/`ST`/arithmetic | `vac := 5`, `F1 :=u R1` (`STu`), `F0 :=s A` (`STs`), `DR *= 0.8`; other sequences as accumulator statements in `{ }` |
| several actions | separated by `;` |
| `RULE*`, no `NEXT`, label | `*` prefix, `once` suffix, `#818` |

Examples (`swrules.ivx`):

```
#32   + B → S                                    SEEK + ; R B ; REPL del 2 -> S
#32   A → A [+b20 −b24] / 1 _                    SCAN 1 ; FOCUS A ; REPL del 1 -> A ; SETF
#818  ∅ → # / [+b16]{3} _ A D R E S S            SEEK A ; R D R E S S ; CPREV ; LP {3,0} ; REPL del 0 -> #
#50   E0 → { vac := 5; vad := 5; va5 := 5 } / Ex _
```

**Rules for the grammar:**
- Every encoding choice has its own spelling; no two encodings are treated as equivalent.
- A rule typed without markers gets the **default encoding**: the focus by `SEEK` when there is no left context; otherwise the first left-context element by `SCAN`, the rest by `R`, the focus by `FOCUS`; right context by `R`/`R?`; the `F` opcode variants for a feature bundle; `FOCUSP`/`RP`/`LP` when an element has a repetition, parameter tests, or both a phoneme and features; `REPL` and `SETF` (not the `+` forms); `ST` (raw) for `:=`.
- Feature bits are written `b0`…`b31`. Named features (`[+vowel]`) are out of scope until the rule-data track names them.
- The complete grammar is written down in step 3 as `editor/ivxedit/NOTATION.md`, held to these rules and to the round-trip test.

The status bar shows the coverage per module, e.g. "M0: 5,214 / 5,714 in rule notation".

## GUI (`gui/`, steps 2–5)

```
┌ IVX Editor — swrules.ivx * ─────────────────────────────────────────────────┐
│ File  Edit  View                                                            │
├────────────────────┬────────────────────────────────────────────────────────┤
│ ▸ Header           │  (editor for the selected item)                        │
│ ▸ Parameters (29)  │                                                        │
│ ▾ Phonemes         │                                                        │
│ ▾ Rules M0…M5      │                                                        │
│ ▸ Characters (256) │                                                        │
│ ▸ Lexicon          ├────────────────────────────────────────────────────────┤
│ ▸ Lexicon 2 (raw)  │ Speak: [text______________] [▶ Speak] [A/B ☐]          │
│ ▸ Unknown          │ waveform / spectrogram / phoneme strip                 │
├────────────────────┴────────────────────────────────────────────────────────┤
│ Problems (0) · rule-notation coverage                                       │
└─────────────────────────────────────────────────────────────────────────────┘
```

**Editors:**
- **Header**: form (version, copyright, flags, lexicon name).
- **Parameters**: 29 rows: name, A1 type with its handler name (REFERENCE §5.3, e.g. "18: approach → freq"), A2 rate, A3 offset.
- **Phoneme**: name, `feat`/`mask` as a bit grid, items table (`time@value` cells per parameter, variables), and "Used by N rules", which lists the rules and jumps to them.
- **Rules**: one module per view, one rule per line, syntax highlighting, fallback rules on a tinted background, phoneme names that jump to the phoneme.
- **Characters**: 256 rows: byte, cp1252 character, class code with its flag meaning (REFERENCE §5.2), transliteration.
- **Lexicon**: searchable table, edits keep the sort order.
- **Lexicon 2, Unknown**: read-only hex view.

Editing is enabled per editor in step 4 (all but Rules) and step 5 (Rules). Every edit is a `QUndoCommand` on the model; the title shows `*` when there are unsaved changes; the Problems panel runs the writer's checks after each edit.

**Speak (`speak.py`):**
- Writes the model to a temporary `.ivx`, writes the text to a temporary UTF-8 file, and runs `ivxsay -l tmp.ivx -i text.txt -o tmp.wav -E` with `-v -r -p -f -F -s` from the panel's options.
- `ivxsay` is looked up at `build/ivxsay.exe` from the repo root, overridable in the settings (`QSettings`). If it is missing, the panel says to run `make`.
- Plays the WAV (Qt Multimedia) and draws, on one zoomable time axis: the waveform, a spectrogram (numpy STFT painted into a `QImage`, no matplotlib) and the phoneme strip from the `-E` events.
- **A/B**: the same text through the file as it was opened, stacked above the edited result.

## Testing

`unittest`, run by `python -m unittest discover -s editor/tests -t editor`; a new `make editor-test` target, also run by `make test`. Tests that need `build/ivxsay.exe` or PySide6 skip with a message when they are missing.

Model and writer (step 1):
1. **Round trip**: all 12 files read and written without edits are byte-identical to the originals.
2. **Cross-check**: rule count (15,564), phonemes per file and section sizes match `tools/ivx.py`/`tools/ivxdasm.py`.
3. **Relocation**: insert an unused phoneme (a name no rule or text produces) after the defaults record, shifting every id; save; `ivxsay` on the 12 languages' test texts gives WAVs identical to the original file's. Control: changing one formant value in a used phoneme makes the WAV differ.
4. **Checks**: one test per refusal in "Checks before saving", including a path inside `bin/` and an edited Danish M5 `REPL` with a carry.

Rule notation (steps 3, 5):

5. **Instruction form** round-trips 100 % of the rules in all 12 files.
6. **Notation** round-trips every rule it shows; coverage per module is printed and must not fall below a stored minimum, raised as coverage grows.
7. **Parser robustness**: random edits of real rule lines give a parse error or a rule that encodes, never an exception or bad bytes.
8. **End to end**: a rule added in notation (an M0 respelling) changes the phonemes `ivxsay -E` reports for a test word as expected.

GUI (steps 2–4, `QT_QPA_PLATFORM=offscreen`):

9. **Smoke**: open all 12 files, select every tree node, open every editor, no exceptions.
10. **Speak**: on the original file, the WAV equals running `ivxsay` directly, and the phoneme strip equals the `-E` events.
11. **Undo**: a script of edits, then undo of all of them, saves byte-identical to the original.

## Open questions (investigated in step 1)

Answered while planning step 1 (plan `docs/superpowers/plans/2026-10-03-ivxedit-step1-model-writer.md`, "Facts found while planning"): sections are 16-byte aligned (plus 16 after the inventory in 6 files, and zero padding after H[15] in 4); lexicon2 is the engine's indexed lexicon with region-relative offsets; Danish H[2] points into it; no engine code stores phoneme offsets; the words at 0x6A/0x6E are kept raw. The save-time checks are in `checks.py`.

Each answer is recorded in `REFERENCE.md` with its tag. If one contradicts this design (for example, absolute offsets inside lexicon2), the writer refuses size-changing edits that would break it until the format is decoded, and the spec is revised.

## Documentation

`REFERENCE.md` gets the new findings with confidence tags: no 68k code in the files (§5), the second lexicon (§5.7), the `NEXT` operand that is never read (§5.6), the 8 stray phoneme ids (§5.6), the section order and alignment (§5.2). `AGENTS.md` gets `editor/` in the repository layout and its commands. `editor/ivxedit/NOTATION.md` documents the rule notation.

## Out of scope

- Writing files in a format other than the shipped `.ivx` layout, or converting between languages.
- Named features (`[+vowel]`) and named variables; the notation keeps `b20` and `vac` until the rule-data track names them.
- Decoding lexicon2 beyond what step 1 needs for safe relocation; editing it.
- A decompiler to a reconstructed original RULSYS source language.
- The frame-by-frame parameter plots (step 6, a later spec).
- Loading the 32-bit DLL from Python, a 32-bit Python build, or any change to `src/`, `lib/` or `cli/` (until step 6).
