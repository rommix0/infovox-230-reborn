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
