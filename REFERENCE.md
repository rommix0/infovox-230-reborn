# Infovox 230 (Ivx230nt.dll) — Technical Reference

This file collects:

1. The useful facts from the research papers in `docs/`.
2. What static analysis of `bin/Ivx230nt.dll` has confirmed so far.
3. A cross-reference that says where each paper claim shows up in the binary, or doesn't.

Confidence tags used below:

- **[CONFIRMED]**: read directly from the disassembly or data.
- **[LIKELY]**: strong circumstantial evidence.
- **[HYPOTHESIS]**: plausible but unverified. Check it in Ghidra.
- **[NOT FOUND]**: looked for it and it isn't in the DLL. It may live in the `.ivx` rule bytecode instead.

All addresses are virtual addresses with image base `0x10000000`, unless the text says "file offset".

---

## 1. Lineage and what each paper is good for

| Paper (docs/) | What it is | Relevance to Ivx230nt.dll |
|---|---|---|
| `barbe89_eurospeech.pdf`: Barber, Carlson, Cosi, Di Benedetto, Granström, Vagges, *A rule-based Italian TTS system*, Eurospeech '89 | The RULSYS/INFOVOX pipeline: DIG → LEX → GRAF → FON | **High.** Describes the rule-module architecture the `.ivx` files encode. |
| `carlson90_scst.pdf`: Carlson, Granström, Karlsson, *Experiments with voice modelling in speech synthesis*, ESCA SCST 1990 | OVE III synth vs. the new GLOVE synth; LF source; parameter names | **High.** The DLL's parameter set and filter topology are OVE III-style (see §3). |
| `infovox_230_pitch_system_overview.pdf`: K. J. Kohler, *Parametric Control of Prosodic Variables by Symbolic Input in TTS Synthesis* (book ch. 37, pp. 459–475) | KIM (Kiel Intonation Model) in RULSYS/INFOVOX **German** | **High for the German rules and phonetic notation.** The F0 algorithm is rule-level, so expect it in `gerules.ivx`, not in DLL code. |
| `ljungqvist93_eurospeech.pdf`: Ljungqvist (Infovox) and Fujisaki, *Generating intonation for Swedish TTS using a quantitative model for the F0 contour*, Eurospeech '93 | Fujisaki phrase/accent model for Swedish | **Medium.** The DLL has no floating-point F0 model, so if this model is used anywhere, it's in the rule bytecode. |
| `ljungqvist94_icslp.pdf`: Ljungqvist, Lindström, Gustafson, *A new system for TTS conversion…*, ICSLP '94 | Infovox's *successor* to RULSYS: multi-level streams, GLOVE or PSOLA back ends | **Background.** It describes rule formalism ideas. Ivx230 is still the RULSYS-generation engine (see §2). |
| `lindstrom94_icslp.pdf`: Lindström and Ljungqvist, *Text processing WITHIN a speech synthesis system*, ICSLP '94 | Text-processing modules of the new system | **Low.** Text normalisation concepts only. |
| `horne93_prosody.pdf`: Horne, Filipsson, Johansson, Ljungqvist, Lindström, *Improving the prosody in TTS systems… New vs Given*, ESCA Prosody '93 | Referent tracking for focus assignment | **Low.** Research prototype. Useful fact: the default Swedish rule puts focal accent on the **last content word** of the phrase. |
| `lf_model_revisited.pdf`: G. Fant, *Frequency domain analysis of glottal flow: The LF-model revisited* (STL-QPSR, c. 1995) | LF / Rd voice-source equations | **Low for this DLL.** The DLL's source is an impulse train, not LF (see §3.4). Keep it for GLOVE-era comparisons. |

---

## 2. Binary overview (Ivx230nt.dll) [CONFIRMED]

- PE32 i386 DLL, MSVC 5.0 (linker 5.0), timestamp 1998-11-11. Image base `0x10000000`, entry point `0x10030730`.
- VERSIONINFO: "Infovox Text-to-Speech Engine Object", internal name `IvxTTS`, "Release version 1.11". The About box says "Version 1.12 (November 1998)", © Telia Promotor AB 1998.
- Embedded banner: `Infovox 230 Text-to-Speech Engine for Win32 (c)Telia Promotor AB, 1996.`
- **COM / Microsoft SAPI 4 TTS engine.** Exports are only `DllGetClassObject`, `DllCanUnloadNow`, `DllRegisterServer` and `DllUnregisterServer`. It implements `ITTSCentral`, `ITTSAttributes`, `ITTSDialogs`, `ITTSEnum`, `ILexPronounce`, and `IAudio*` / `IAudioDest` (IID name strings start at VA 0x100448e4). Registry keys it touches:
  - `Software\Voice\TextToSpeech\Engine`
  - `Software\Telia Promotor\Infovox 230\1.1` (values `LexiconDir`, `LanguageDir`, `LicenseDir`, …)
- Copy protection and licensing:
  - Rainbow Sentinel SuperPro dongle, called through `SX32W.dll` (`RNBOspro*`).
  - Crypkey software licensing (`cryp95a.dll` / `cryput.dll`, `IVX230.LIC`, site codes).
  - Demo modes `DEVELOP`, `NTHDEMO`, `STHDEMO`.
  - These are out of scope for the speech research. The copy in `bin/` has already had its protection cracked so it runs on modern systems.
- Languages exposed: swedish, norwegian, danish, finnish, icelandic, german, french, italian, castilian spanish, british english, and american english.
- Per-mode registry values: `LanguageFile`, `LibraryFile`, `PhSymFile`, `LanguageID`, `ModeGUID`, `SpeakerName`, `SpeakerStyle`, `FormantNo`, `Aspiration`, `Dynamic`, `PhoneticChar`, `Sample Rate/1`, `Sample Rate/2`.
- Log files: `IVX230_%8.8lx.LOG`, `_TXT.LOG` and `_MSG.LOG`. The message-tag prefixes (`CORE :`, `ENGINE:`, `RESMGR:`, `MODE :`, `DONGLE:`, …) are handy for naming functions.

### 2.1 Embedded RULSYS source IDs (SCCS `@(#)` strings) [CONFIRMED]

These strings are not referenced by code. Their order in `.data` (file offset 0x46e40 onward) probably mirrors link order:

```
opbatch.c  v.1.0 961114      ttsctrl.c  v.4.2 950630      tpchar.c  v.4.2 950628
opsmooth.c v.4.2 950628      opstart.c  v.4.2 950628      tpelem.c  v.4.0 941107
tpstr.c    v.4.2 950628      contwork.c v.4.2 950628      cwflow.c  v.4.2 950628
cwtest.c   v.4.2 950628      cwelem.c   v.4.0 941107      cwmath.c  v.4.2 950628
cwvars.c   v.4.0 941107      cwmisc.c   v.4.0 941107      (build date "Nov  9 1998")
```

The prefixes are guesses [HYPOTHESIS]:

- `tp*`: text processing.
- `cw*` / `contwork`: the "control work" rule-bytecode interpreter.
- `op*`: output parameters, meaning the frame generation and smoothing that feed the synth.

`ttsctrl.c` is the public control API described in §2.2.

### 2.2 Core call surface [CONFIRMED]

| Address | Name | Notes |
|---|---|---|
| `0x10019f50` | `tts_ctrl(h, cmd, arg)` | Tail-dispatch through the function table at `0x10048460` (commands 0x00–0x21; all of them in §2.2.1). Command 0x00 (`0x10019f70`) is create: it allocates a 0xEB0-byte core struct. The engine calls it with 0x1c, 0x16, 0x11, 0x10, 0x12, 0x14 (callback 0x10010e40), 0x1a (callback 0x10011270), 0x1f (callback 0x10010fe0), 0x13, 0x18, 0x19 (arg 500), 0x05 (get/set flags), and 0x1e. |
| `0x10022cc5` | `synt_ctrl(h, cmd, arg)` | Synthesizer control, cmds 0x00–0x13 (table at `0x10022e4b`/`0x10022e7b`). See §3.6. |
| `0x10019780` | `opb_init(src, &out)` | Clones a Core's configuration into a new Core. With `opb_save`/`opb_restore` (op batch), it snapshots and replays segment lists. |
| `0x100201b0` | `cw_run(ctx, code)` | Rule-bytecode interpreter entry. Sets IP at `ctx+0x48`, then loops on `cw_step`. It also calls `Core.hook` (`+0x168`, set by `tts_ctrl` 0x18) every `hook_interval` steps; the SAPI engine sets it to its own `0x10011400` (`0x10010e07`), not to `norm` [CONFIRMED]. |
| `0x10020240` | `cw_step(ctx)` | Fetches a 16-bit opcode from `*(ctx+0x48)` and advances 2 bytes. Opcodes are **multiples of 4** in 0x00–0x1F8. A compare tree handles ops below 0x100; a byte-index jump table (`0x100206f8` → `0x10020658`) handles ops at 0x100 and up. Handlers sit at `0x10020800`–`0x10022ca0`. The default/error handler is `0x10020820`, and op 0 ends execution. `tools/ops.py` prints the full opcode→handler map. |
| `0x10016cc0` | load language library | `LoadLibraryA(LibraryFile)` followed by `GetProcAddress(h, ordinal 1)`. For Danish this resolves `darules.dll!norm`. The function is kept at +0x110 of the 0x114-byte library object (`new` at `0x100174fa`, constructor `0x10016c70`), `0x10016d40` returns it, and the engine passes it to `tts_ctrl` 0x15 as the **exception dictionary** callback, or 0 when the voice has no `LibraryFile` (`0x10010330`–`0x10010354`) [LIKELY]. |

**The complete address→name table is `tools/ghidra/symbols.tsv`**, applied to Ghidra by `IvxApply.java` (§2.3). Add new names there, not here. Only the entry points are repeated in this table.

### 2.2.1 Control interface (`ttsctrl.c`) and its C port [CONFIRMED bit-exact]

`tts_ctrl(h, cmd, arg)` (`0x10019f50`) calls `tts_cmd_table[cmd](h, arg)` with no bounds check. For every command below except 0x00 and 0x21, `h` is the Core. A getter/setter returns its current value when `arg` is −1, and 0 after a set.

| Cmd | Function | Action |
|---|---|---|
| 0x00 | `tts_create` | `h` = `Core **`. Allocates the Core (0xEB0) and `npool` × 0x38 pool nodes; if the pool allocation fails, the Core leaks and the result is −1. Sets `ntotal` = 0x20 and `V_init[0..9]` = 50, fills user voices 5–9 with 32 words of voice 0, then runs reset 0. |
| 0x01 | `tts_c01_destroy` | Frees the pool and the Core. |
| 0x02 | `tts_c02_reset` | Arg 0 resets everything. It relinks the lists and pool (`core_init_lists`) and sets `esc_phon` 2, `esc_mark` 1, `f6d9` 0x20, `mode` 8, `charset` 0 and voice 0. It also sets **`xlat_hi` = NULL**, so a language's own transliteration table is ignored until the next bind [CONFIRMED `0x1001a133`]. Arg 2 clears `fead` and reloads the parameter tables. Arg 1 empties the text (`tp_reset_text`) and clears `word_pos`. |
| 0x03 | `tts_c03_bind` | Runs `ivx_bind`, empties the text and reloads the parameter tables. If the bind fails, the old language's tables are reloaded. |
| 0x04 | `tts_c04_charset` | Bits 0 and 1 pick the national table used for 8-bit chars (§5.5.1). Without bit 4, chars 0x20–0x7E of class 0x1000 become 0x1020 (punctuation) and those of class 0x2000 become 0x2100 (symbol). **With bit 4 they become letters**: 0x600 in `a`–`}`, 0xA00 in `A`–`]`, else 0x200. These are the national letters of 7-bit charsets (`[\]{|}`). |
| 0x05 | `tts_c05_mode` | 0 means 8. Bit 2 implies bit 1. Any of 0x1F0 without bit 4 implies bit 8. Bit 0x80 sets `f6dc`. **Bit 0x100 starts in phonetic input** (`tp_state` = 1); otherwise `tp_state` is 0. Clears `pending`. |
| 0x06 | `tts_c06_set_voice` | `voice_next` = built-in voice 0–4 (`tts_voice_defaults`, 29 words each) or stored voice 5–9, copying `ntotal` words; any other voice returns −1. Sets `voice_dirty`. Recomputes the UI values: `ui_rate` = DR < 10 ? 5·DR : 2·DR + 30, `ui_fd` = (2·FD + 300)/6, `ui_pitch` = 2·F0/5. |
| 0x07 | `tts_c07_store_voice` | Stores `voice_next` as voice 5–9. |
| 0x08 / 0x09 / 0x0a | `tts_c08_fd` / `c09_rate` / `c0a_pitch` | UI value 0–100. `tts_ui_adjust` treats −2/−3 as ±1, −4/−5 as ±2 and −6/−7 as ±5 (a 16-bit compare), and clamps to 0–100. The result maps to `voice_next`: FD = 3·(v − 50); DR = v/5 below 50, else (v − 50)/2 + 10; F0 = 5v/2, a scale where 64 is unity. All three return the new UI value. |
| 0x0b, 0x0c | `tts_c0b_nop` | Return 0. |
| 0x0d | `tts_c0d_var_init` | `V_init[arg >> 16] = (short)arg`, with no range check. |
| 0x0e, 0x0f, 0x10 | | `esc_mark` (0x7F and backspace switch it off), `esc_phon`, `out_flags` |
| 0x11–0x16, 0x18, 0x1a, 0x1f | | Callback and pointer setters (see `symbols.tsv`) |
| 0x17 | | Returns `frames_left` as a signed short. |
| 0x19, 0x1b, 0x1c, 0x1e | | `hook_interval`, `word_pos`, `count_init` = `count` = 0, `feae` |
| 0x1d | `tts_c1d_out_mode` | Sets `out_mode` and returns `&pdesc_n`, the parameter description block `{−1 − nparams, "  ", names…}`. |
| 0x20 | `tts_c20_mark` | Increments the mark. Its low word goes to `mark_hi` and bits 16–23 to the low byte of `mark_lo`; the high byte of `mark_lo` is kept. |
| 0x21 | `tts_c21_ivx_swap` | `h` is unused. Byte-swaps a body in place (§5.1). |

**`tts_c09_rate` (`TTS_RATE`, `ui_rate`/DR) does not change speaking speed** [CONFIRMED]: it only ever writes `voice_next[0]`, the DR slot of the per-voice defaults. `op_next_frame` (`src/opsmooth.c`, the `voice[0] != 0` block) reads that slot solely to lower the AC/AN amplitudes and raise AH — a breathiness-like voice-quality balance, not a duration. Segment durations come only from the rule bytecode read by `op_seg_durations` (`src/opstart.c`). Evidence: in the `infovox230` library's `api_test` (`test_settings_effect`), `ivx_set(e, IVX_RATE, 90)` against the default voice gives the *same* sample count (180,369) with 96,042 of those samples differing in value between the two runs. Actual speaking speed is the synth frame rate (`synt_ctrl` 0x0e, §3.5, exposed as `IVX_FRAME_RATE`).

`ivx_bind` (`0x1001ab40`) [CONFIRMED]:

- It refuses a NULL body or a body whose build tag starts "XX".
- It frees `p160` and **clears `cb_excdict`**, so the engine must set the exception dictionary again after every bind.
- It sets `pdesc_n` = −1 − `nparams`.
- **Parameter names:**
  - If A0[1] is two chars above 0x20, it copies `nparams` − 1 of A0's own names, reversed, with CRT `strncpy(…, 2)` (`0x1002fcf0`).
  - Otherwise it copies the 24 built-in names F1…ZZ. No shipped file has its own names.
- **Pointers:** it sets the `ptab_*` pointers, one every (H[8] − 0x70)/8 words, and `phinv`, `mod[]`, `xlat_hi`/`class_lo` and the lexicon (§5.2).
- It then runs `ivx_build_class_flags` (§5.2).

**C port:** `src/ttsctrl.c` holds all of `0x10019f50–0x1001b04f`. The plain setters are inlined into the `tts_ctrl` switch, and each case names its DLL function. The constants (`tp_class80`, `tts_param_names`, `tts_voice_defaults`) are generated by `tools/gen_tp_data.py`. **With this, the whole path from `.ivx` file and text to samples runs without the DLL**: `tts_ctrl` → `tp_putc` → `op_next_frame` → `synt_generate`.

**Test** (`tests/tts_cmp.c`, run by `make test`) has three parts:

1. **Swap.** Each of the 12 `.ivx` files is swapped by the DLL and by the port, and the two results must be byte-identical.
2. **Commands.** A DLL Core and a port Core are created side by side (pools of 1, 2, 60, 900 and 1000 nodes). They receive the engine's command sequence, then about 4,300 pseudo-random commands per language with edge-case arguments:
   - queries
   - voices −2 to 10
   - step codes, including a step code in the low 16 bits only
   - out-of-range UI values
   - binds to a body with printable A0 names (one containing a NUL), to a body tagged "XX", and to NULL
   - commands issued before any bind

   Identical random values are also written into the fields that only a reset clears or a query reads. The return values, the whole Core and the pool are compared after every call, with pointers mapped between Core, pool and bodies.
3. **Text to audio.** The DLL pipeline (its `tts_ctrl`, `tp_putc`, `op_next_frame` and synth) and the all-port pipeline speak each language's text in three configurations:
   - the engine's (mode 8, 22,050 Hz)
   - mode 4 at 16,000 Hz, which uses the 16 kHz sample loop
   - mode 12 at 11,025 Hz

   The last two change rate, pitch, FD and voice through each side's own `tts_ctrl` at every word, including stored voices and step codes. Every sample is compared. The Core and pool are compared after every char and after a reset 2 and a reset 0 at the end. `tts_cmp bin wav` also writes the port's audio to `build/wav/<lang>_<config>.wav`.

**The engine's `tp_putc` protocol** (`0x10012345`) [CONFIRMED]:

- **Return 0:** send the next char.
- **Return 1:** the char is consumed. Play the utterance, then send the next char.
- **Return 2 (synth busy):** play the audio to the end, then **send the same char again**. The next call starts the waiting utterance (`op_start_utterance`), then either continues with the pending char or returns 1 without appending anything.
- **End of text:** the engine then sends 0x17.

`tts_cmp` follows this protocol (1,004 returns of 1 and 142 returns of 2 in its runs).

`tp_cmp` instead clears `synth_state` after a busy return. That is the same on both sides, so it is still a valid differential test, but it drops the waiting utterance. Changing `tts_cmp` from the `tp_cmp` method to the engine's protocol raised its audio from 16.49 M to 17.40 M samples.

This protocol now lives once in `lib/infovox230.c`'s read loop (§2.2.2); `cli/ivxsay.c` is a client of that library, not a direct driver of the port. Originally, and still accurately, it was described as a text-to-WAV tool that used only the port:

- It runs the engine's sequence: swap, create (900 nodes), charset 2, bind, mode.
- Then the optional voice/rate/pitch/FD commands.
- It creates a mode-0 synth pulling `op_next_frame`, at 22,050 Hz by default.
- It feeds the text with the protocol above, then 0x17.
- It turns LF into CR, so a blank line ends a sentence.

`tools/check_ivxsay.py` (run by `make test`) speaks the 12 test texts with it and requires **byte-identical WAV files** to `tts_cmp`'s DLL-verified port audio. All 12 are identical. With the end-of-text 0x17 removed, all 12 fail.

**Result:** 54,336 commands, 7,663 `tp_putc` calls and **17.40 M samples** (14.11 M non-zero), with 169,036 checks and **0 failures**. **Mutations:** of 41 deliberate bugs, 39 are caught, three of them as port-side crashes. The 2 not caught are equivalent: `swap16`'s rounding up to even when every swapped length is even, and `ui_rate < 50` against `<= 50`, where both formulas give 10 at 50.

### 2.2.2 The infovox230 library [CONFIRMED bit-exact]

`include/infovox230.h` and `lib/infovox230.c` package the port (§2.2.1, §3.7, §5.3.1, §5.5.1, §5.6.1) as a
shared library: `build/infovox230.dll` (plus `libinfovox230.dll.a`/`infovox230.def`) on Windows, or
`build/linux/libinfovox230.so` (soname `libinfovox230.so.0`) on Linux. Design:
`docs/superpowers/specs/2026-09-28-infovox230-library-api-design.md`.

**Stop sequence** [CONFIRMED `0x100119b1`–`0x100119e1`, also `0x10011c67`]: the SAPI engine's cancel/reset
path is `tts_ctrl` reset 1 (arg 1, empty the text), reset 2 (arg 2, clear `fead` and reload the parameter
tables), then `synt_ctrl` 0x0f (drop `frame_left`). `ivx_cancel` reproduces exactly this sequence.
`tests/tts_cmp.c`'s "stops 22050" configuration fires it at random points mid-utterance (after a random 0–29
audio blocks) and compares the whole Core, pool and every sample, DLL versus port: **47 stops**, 20,804,175
samples compared (16,897,906 non-zero), 184,936 checks, 0 failures.

**Bookmarks** [LIKELY, `0x10011304`, `0x10012a85`, callback `0x10010fe0`]:
- The SAPI engine keeps a 16-bit serial at `eng+0x108` and passes it to `TTS_MARK` inside its text loop;
  `tts_c20_mark` stores serial + 1.
- `op_emit_bookmark` reports `(mark_lo & 0xff) << 16 | (short)mark_hi`, so the serial is its low 16 bits.
- A zero mark is never reported.
- Marks with no segments between them share one segment's serial: `cb_bookmark` releases every queued mark
  up to and including the reported serial in one call (a `do…while`), not just the newest.
- The end of text (0x17) always manufactures a final "@." sentence-end segment (`ctl_word_end`,
  `src/contwork.c:117-119`, stamps `mark_lo`/`mark_hi` onto `segs.tail.prev` right before calling
  `op_start_utterance`), so a mark queued immediately before a flush is always claimed by that segment's
  `cb_bookmark` report at flush time, not left pending until `DONE`. The library's own idle-cleanup loop in
  `ivx_read` (`while (e->marks.len != 0) push_event(..., IVX_EV_MARK, ...)`, guarded by `e->flushed`) only
  fires for a mark whose bookmark segment was never reported at all [CONFIRMED by an instrumented build]:
  - It never fires in `tests/api_test.c` (0 times in the whole run), whose texts are ordinary sentences.
  - It does fire when the Core drops the words that carry the marks: a single unbroken token too long for the
    Core. In Swedish a 51-letter "word" gives no audio at all (0 samples, also through `ivxsay`), so marks
    queued inside it are all released at `DONE`; tokens of a few hundred letters give partial audio and
    release the rest (30–70 of 150–300 marks). Out-of-nodes drops with small pools reach the same loop, and
    `tests/cw_cmp.c`/`tests/tp_cmp.c` cover that path against the DLL.
  - It no longer fires through the idle-only calls. Before the final fixes, text read into the Core without a
    flush left the engine "idle", so a language load (`tp_reset_text`) dropped that text and its queued mark
    stayed in the map until the `DONE` of a later, unrelated utterance. The engine now counts unflushed text
    as busy (`in_text`, below), and a successful load also empties the mark map.

**Marks don't change the audio** [CONFIRMED]:
- `tests/tts_cmp.c`'s reference WAVs have `TTS_MARK` at every word, and `ivxsay` (which never calls
  `ivx_mark`) matches them byte for byte.
- `tests/api_test.c` (`test_marks`, `test_marks_coalesced`, `test_phonemes`) compares the same text with and
  without marks — including marks queued back to back before any text and immediately before a flush — and
  finds identical audio in every case.

**Engine init** [CONFIRMED addresses; effect NOT FOUND]: the SAPI engine also sets `feae` = 1
(`0x10010703`), `hook_interval` = 500 (`0x100105d7`) and a user-dictionary callback backed by its lexicon object (`0x10010e40`, returns 1 = text on a hit, else 0; never 2); the library has its own dictionary instead (below). The library, like `tts_cmp`'s part-3 text-to-audio comparison, leaves `feae` at 0 and still gets
bit-exact audio against the DLL; whether `feae` = 1 changes anything observable is open.

**The library in brief** (`lib/infovox230.c`): `ivx_feed`/`ivx_feed_phonetic`/`ivx_mark`/`ivx_flush` and the
queued settings append to one input queue; nothing runs until `ivx_read` is called. `ivx_read` copies a
staged 441-sample `synt_generate` block if one is ready, else generates one, else processes one queue item (a
char through `tp_putc`'s 0/1/2 resend protocol, a mark through `tts_ctrl` `TTS_MARK`, a queued setting
through `tts_ctrl`/`synt_ctrl` 0x0e, or a flush's `tp_putc(0x17)`), else — once idle — emits a `DONE` event if
a flush was processed since the last one. The block size is fixed at 441 samples so the audio never depends
on the caller's read size, matching `tts_cmp`'s DLL-verified WAVs including their zero padding to a whole
block. Events are stamped with the exact sample position: the frame callback records `frame_pos` (the output
position where the frame about to be returned starts) before each `op_next_frame` call and advances it by
`Synth.frame_left` after, and `cb_bookmark`/`cb_phonemes` stamp with that `frame_pos`, `cb_status` with the
current `gen_pos`. `ivx_next_event` releases events only once `ivx_read` has delivered audio up to their
sample, in queue order. Sample rate, text mode, charset, input encoding, event mask and language loading are
idle-only (`IVX_E_BUSY` otherwise). Idle means nothing but settings queued, no char pending, no audio playing
or staged, and no unflushed text in the Core: `in_text` is set when a char or mark goes into the Core and
cleared when a flush's 0x17 has gone in (or by `ivx_cancel`), since the Core holds a sentence's words until
its end and a load's `tp_reset_text` would silently drop them; voice, rate, pitch, FD (§2.2.1's `ui_rate`/DR note applies to
`IVX_RATE`) and frame rate are queued and apply in stream order. `ivx_cancel` empties the queue/pending-
char/staging-block/events/mark map, runs the stop sequence above, and rebases `next_frame_pos` to the
delivered position; it also clears `in_text` and a pending out-of-memory error. `ivx_synthesize` ends at its
`DONE`, or when a read returns 0 without one (its callback called `ivx_cancel`), after handing the last
read's events to the callback. Text is UTF-8 by default (each cp1252 code point maps to its byte, 0x80–0x9F through the
cp1252 table; other, invalid or split sequences become a space), with an `IVX_ENC_CP1252` passthrough option.

**Dictionaries** (`ivx_dict_add`/`ivx_dict_remove`/`ivx_dict_clear` in `lib/infovox230.c`, over the
table and engine callbacks in `lib/ivxdict.c`, API 1.1; design:
`docs/superpowers/specs/2026-09-29-infovox230-dictionaries-design.md`) [CONFIRMED on the port]:
- Precedence per word: exact-case override, case-insensitive override, the built-in lexicon, exact-case
  FALLBACK, case-insensitive FALLBACK, the letter-to-sound rules. Overrides are the engine's user
  dictionary (`tts_ctrl` 0x14: TEXT returns 1, PHONETIC returns 2), fallbacks its exception dictionary
  (`tts_ctrl` 0x15, reply transliterated like `tp_word`, `src/tpstr.c:201-230`).
- The callbacks are installed only while entries need them, so with no entries the audio is the
  DLL-verified audio (`ivxsay` 12/12); 0x15 only with FALLBACK entries, because an installed exception
  callback changes the out-of-nodes retry even when it returns NULL (§5.5.1). The load reinstalls 0x15
  after every bind attempt: a bind that gets past its checks clears it, and reinstalling it is harmless
  when it didn't.
- A TEXT replacement is final: its words are not looked up again. The library records each replacement
  as a span of `Core.text` (offset plus a copy of its bytes). Before every `tp_putc` with
  `text_consumed` set it records the compaction offset `text_done − text` and shifts the spans by it when
  the next callback runs, or after the call; spans past `text_end` are dropped, and a span counts only
  while its bytes are still at its offset. The shift matters when the engine reads a replacement's words
  again after compacting (seen with random bytes in `sp`); `api_test` fails without it.
- Keys are rejected only for bytes that end a word in every language (blanks, control characters, digits,
  `! # $ % & ( ) * + , - . ; < = > ? ^ _`); which other characters belong to a word depends on the
  language and charset (§5.5.1), and a key the loaded language would split never matches.

**Crash guards: the port's only divergences from the DLL** [CONFIRMED]. Input that is not text crashes the
DLL (or makes it write outside its buffers) in four places, and two built-in voices make its synth read
outside its tables (a fifth). The port guards each one, marked `GUARD, not in the DLL` in the source. Every
guard changes only cases where the DLL crashes, livelocks, writes past a buffer or reads outside its tables,
so all differential tests are unchanged (`make test`: 0 failures, `ivxsay` 12/12).
- **`dur_point`** (`src/opstart.c`): a segment with neither its own DR item nor a phoneme default. The DLL
  reads `NULL + 2` in `op_seg_durations` (`0x1001e56e`, §5.3.1): seen in `tests/tp_cmp.c` with small pools,
  and with 20,000 random German bytes at the engine's 900 nodes (3 of 3 DLL runs crash there). The port gives
  the segment duration 0.
- **Text buffer end** (`tp_putc`, `src/tpchar.c`): nothing bounds `Core.text` (800 bytes at `+0x170`). The
  buffer-full check in `tp_st00_start` runs only at a token start, so one token of about 800 bytes runs into
  `text_limit`, `xlat_hi` and the fields after them (the DLL crashed in 4 of 6 runs at `0x1001b516` with a
  3,300-letter word and gave varying output in the other two). The port turns a char into a blank 16 bytes
  before the end, which ends the token; the next token start then finds the buffer full and ends the
  sentence. From 8 bytes before the end, chars are dropped (never reached in testing). 0x17 always goes in.
  Only a token that runs to within 16 bytes of the end is affected: more than 100 chars, at or past the
  128-char word limit.
- **Text compaction** (`tp_putc`, `src/tpchar.c`): `text_cur` can lag behind `text_done` (a token too long
  to speak rewinds it after the sentence was consumed), so `d − tail` lies before `Core.text` and the next
  token writes into the Core fields before it (seen writing `mod[2]` and `out.tail.prev`, random bytes in
  `am`, `du` and `sp`). The port reads from the start of what was kept.
- **REPL count** (`cw_op_REPL`, `src/cwelem.c`): the REPL word is `del·256 + 12·n`, and 12·n carries into
  the high byte for 22 or more phonemes. `darules.ivx` M5 has four such rules (words 0x32c, 0x314, 0x32c, 0x320; rules 20, 21, 48, 49)
  and `icrules.ivx` M5 one (0x1108) [CONFIRMED, `editor/tests/test_rules.py`]. Rules 20 and 21 name the
  quotation marks (`" 1` → *venstre anførselstegn*, 25 phonemes; `" 2` → *højre anførselstegn*, 23 phonemes,
  word 0x314) [LIKELY: the lengths match]; what rules 48 and 49 name is not checked. The DLL
  takes the low byte as the count, so it runs 20 → 8 → 65532…, reads the bytecode after the rule as phoneme
  ids and crashes on their records at `0x10021942` (Danish phonetic input `"2` in text mode 12, 3 of 3 runs).
  Where the reads survive, it fills the pool, and `ctl_word_end`'s out-of-nodes restart fails the same way
  every time (a livelock). The port stops the module with status 0x10 (bad opcode), which drops the word.
  Ordinary Danish text with quotation marks does not reach these rules.
- **Synth table indices** (`synt_set_resonator` `0x100237a0` and the `amp_db` lookups, `src/synth.c`): the
  frame's frequency, bandwidth and amplitude values index the 128-entry `res_b`/`res_c`, 32-entry
  `bw_b_ratio`/`bw_c_ratio` and 32-entry `amp_db` tables unchecked. Voices 2 and 4 drive K1/K2 to −6 for a
  few frames per sentence in 9 of the 12 languages (not `da`, `fi`, `sp`; voices 0, 1, 3 and 5–9 never), so
  the synth reads `res_b[-6]` and `res_c[-6]`. In the DLL that is heap history: its CRT's small-block heap
  packs the tables back to back without headers, so the read normally gets `amp_db[26]` (18426) and
  `bw_b_ratio[26]` (25031), but gave 0 for `res_c` when it landed in a reused block (1 of 4 creations,
  probe on the DLL). In the port, whose tables are separate `calloc` blocks, a table at the start of a page
  makes the read fault: `api_test` crashed with 0xC0000005 in `test_voice` once a larger `IvxEngine`
  moved the heap (run from Git Bash; natively and under gdb it passed). Every such frame has AC = 0, so
  `g_fric = amp_db[0]/4 = 0`: the frication branch that uses K1/K2 is off and its state cleared, and the
  coefficients are rebuilt each frame, so the out-of-range values never reach the audio (instrumented port,
  all 12 languages × 10 voices; audio identical with the DLL's usual values and with the clamp). The port
  clamps the indices to the tables. No out-of-range bandwidth or amplitude index was seen; they are clamped
  the same way.

**Fuzzing** (scratch probes, not in `make test`): 36 runs of 20,000 random bytes (12 languages × 3 seeds),
and 960 runs mixing random chunks, phonetic input, marks, flushes, reads, cancels, both encodings and eight
text modes (0, 1, 2, 4, 8, 12, 0x88, 0x100), all end without a crash or hang with pointer checks on the text
state after every step. Before the guards, the first set crashed at its first Dutch run (the overrun `xlat_hi`),
and with only the `dur_point` guard at `am` seed 2 (the compaction); the second set crashed at run 387 (the
REPL count) and then, with an out-of-nodes status in that guard, livelocked there.

**Port fix** [CONFIRMED]: the hyphen token `tp_hy` (`0x10048998`) is `"hy\0\0"` in the DLL, and
`tp_symbol_to_seg` reads the byte after the `'y'` as the second char of a 2-char name. The port had it as a
2-byte array, so it read whatever followed in its own data (found by AddressSanitizer); it now has the
DLL's 4 bytes. Not a guard: the DLL reads its own zero there.

**Sanitizers:** `api_test` built with `-fsanitize=address,undefined` (WSL, i386) reports nothing. Before the
synth guard and the `tp_hy` fix it reported the `res_b`/`res_c` reads, the `tp_hy` overread and a left
shift of a negative `signed char` in `tp_symbol_to_seg` (now a multiplication, same value).

**Tests:**
- `tests/api_test.c`: **1,071,349 checks, 0 failures** (784,014 before the dictionary tests, 773,634 before the synth guard, 434,392 before the
  crash guards) (buffer sizes 1/7/441/4096, `ivx_synthesize` vs.
  feed+flush+read, marks in order/non-decreasing/never released early, mark coalescing both before any text
  and immediately before a flush, phoneme events, every `IVX_E_*` error path, UTF-8/cp1252/embedded-control-
  character handling, 20 random-point cancel trials, and the `IVX_VOICE`→rate/pitch/FD cascade). Final-review
  additions: `ivx_cancel` from inside the `ivx_synthesize` callback (used to hang), unflushed text in the Core
  keeping the engine busy (and a cancel ending that), phonetic input in KIM notation (`"H[J"`: 18,081 samples
  against 10,143 for an empty flush, PHONEME events H, [, J, and different from the same bytes as text), and
  the two-engine interleave with marks and all events. Crash guards (`test_robust`): 20,000 random bytes in each of the 12
  languages (German reaches the `dur_point` guard; `am`, `du` and `sp` the compaction guard), a 3,300-letter
  word in each language (the text buffer guard), each followed by a sentence that must still be spoken in
  full, Danish phonetic `"2` in text mode 12 (the REPL guard, error event 0x10), and voices 2 and 4 in
  every language (the synth guard; whether its absence crashes depends on the heap layout). Removing any
  one of the first four guards makes `api_test` crash (three of them) or fail 24 checks (the text buffer
  guard). Dictionaries (`test_dict_*`): TEXT and PHONETIC entries sound exactly like the replaced input
  (Swedish, English), case rules (exact before case-insensitive, å/Å, French œ/Œ, UTF-8 = cp1252), finality
  ("cat" → "cat dog", a → b → c) across 40 sentences of buffer compaction and a replacement that ends a
  sentence, 0x71 for a replacement that cannot fit, every `IVX_E_*` case, clear/remove giving the plain
  audio, FALLBACK entries (unknown words only, respelling with å, exact case, override first), entries
  surviving a language switch, a failed load and `ivx_cancel` and applying to text already fed, and the
  random bytes of `test_robust` with overrides, phonetic entries and fallbacks installed in all 12
  languages.
- `tools/check_ivxsay.py`: **12 of 12 languages byte-identical** to the DLL-verified audio, run through
  `ivxsay` against the shared library. It also checks `ivxsay -d`: a Swedish dictionary file (TEXT, exact
  case, FALLBACK, UTF-8 keys, a phonetic line) gives the same WAV as the text with the replacements made,
  and six malformed files exit 1 with `file:line:`.
- **Linux** (Task 7, WSL Ubuntu 24.04, gcc 13.3.0, glibc 2.39): `build/linux/libinfovox230.so` (with a
  `libinfovox230.so.0` soname symlink, added via the Makefile's `LIBPOST` because only the unversioned file
  is built but `-Wl,-soname` records the versioned name as the runtime `DT_NEEDED` entry), `build/linux/
  api_test` and `build/linux/ivxsay` all build with zero warnings. `api_test`: 1,071,349 checks with the dictionary tests (784,014 with the five guards, 773,634 with four, 434,392 after the final fixes, 433,665 before), 0 failures —
  the same count as Windows. `check_ivxsay.py`: 12 of 12 languages byte-identical to the *Windows*
  DLL-verified reference WAVs, so the synth's table-building math (`pow`/`sqrt`/`exp`/`cos`) is bit-exact
  under glibc as well as mingw, given `-msse2 -mfpmath=sse`. `nm -D --defined-only` confirms only `ivx_*`
  symbols are exported (`-fvisibility=hidden`). The ALSA "default" PCM device is not reachable in this WSL
  image (no i386 PulseAudio ALSA plugin); `ivxsay` reports "cannot open the audio device" and exits 1 — one
  of the two outcomes the design treats as acceptable, so live playback under WSL is unverified.
- `ivxsay`'s speaker output (`cli/audio_win.c`, waveOut) has only been checked for pacing: wall-clock time
  for a spoken sentence closely tracks its audio length (5.658 s wall vs. 5.38 s of audio for one test
  sentence), so playback is paced by the device rather than racing ahead of it. Nobody has listened to the
  actual audio yet.

### 2.3 Code map, data structures and the Ghidra project [CONFIRMED unless tagged]

`.text` regions. Each Ghidra function carries a tag for its region:

| Range | Tag | Contents |
|---|---|---|
| `0x10001000–0x100121af` | `SAPI_COM` | C++ COM classes (CEngine, CAudioDev, …) |
| `0x100121b0–0x1001977f` | `ENGINE` | Registry, languages, lexicon, logging. The licensing functions here are tagged `LICENSE` (0x100145d0, 0x10014830, 0x10014980, 0x10014b50, 0x100150b0, 0x10015ba0). |
| `0x10019780–0x100250d1` | `RULSYS` | The speech engine proper (see the per-file split below) |
| `0x100250d2–0x100250ef` | (none) | comdlg32 import thunks (`GetFileTitleA`, `GetSaveFileNameA`, `GetOpenFileNameA`) |
| `0x100250f0–0x1002cf9f` | `LICENSE` | Statically linked Crypkey and Sentinel client code. It's cracked, so skip it. |
| `0x1002cfa0–0x1003ffff` | `CRT` | MSVC 5 runtime. Ghidra's function-ID only names about 49 of 347, so treat the rest as library code. |

The RULSYS per-source-file split (`src_*` tags) follows the SCCS link order in §2.1. The addresses of the cw* handler groups appear in exactly that order:

| Range | Source file |
|---|---|
| `0x10019780` | `opbatch` [HYPOTHESIS] |
| `0x10019f50` | `ttsctrl`: `tts_ctrl` and its commands, the .ivx swap and bind, the class flags (ported: `src/ttsctrl.c`, §2.2.1) |
| `0x1001b050` | `tpchar`: the text state machine `tp_putc` and `tp_st*` (ported: `src/tpchar.c`, §5.5.1) |
| `0x1001c4f0` | `opsmooth`: `op_next_frame`, the tracks, the `op_t*` handlers |
| `0x1001dc40` | `opstart`: utterance start, durations, track init |
| `0x1001e890` | `tpelem`: segment lists, symbol → segment |
| `0x1001eef0` | `tpstr`: words, lexicon, compounds, sentence end (ported: `src/tpstr.c`, §5.5.1) |
| `0x1001fe90` | `contwork` |
| `0x10020800` | `cwflow` |
| `0x10020900` | `cwtest` |
| `0x100216e0` | `cwelem` |
| `0x10021b30` | `cwmath` |
| `0x10021f80` | `cwvars` |
| `0x10022ca0` | `cwmisc` |
| `0x10022cb0–0x100250d1` | `synth` (ported to C: `src/synth.c`, §3.7) |

**Data structures:** `tools/ghidra/ivx_types.h`, generated by `gen_types.py`. The offsets are checked by `ivx_types_check.c` with 32-bit gcc, and all 17 structs pass.

- **`Core`** (0xEB0) is the tts_ctrl handle. It holds:
  - the interpreter registers (§5.6)
  - `phinv`
  - two doubly-linked lists with head and tail sentinel anchors: `free_list` at +0xA4 and `segs` at +0xB4. A loop runs from `head.next` until it reaches `&tail`.
  - `mod[]`, `pool`, the hook, the class tables and the per-parameter arrays
- **`Seg`** (0x38) is the segment node. Its fields:
  - `next`, `prev`
  - `ext` (overflow item blocks)
  - `items_len`
  - `ph` (the phoneme id)
  - `feat`, `mask`
  - 12 inline item words at +0x20

  All Segs **and** the overflow `ItemBlk`s (`next`, `len` at +8, data at +0xA) come from one pool of `npool × 0x38` nodes allocated by `tts_create`. `seg_alloc` (`0x1001ea20`) pops a node from `free_list`. REPL creates a new segment's feature/mask from its `PhRec` and copies `f10`/`f12` across from the deleted segment.
- **`Synth`** (0x3C) is the synt_ctrl handle. It holds the frame source (`get_frame(get_frame_arg)`), the per-type `frame_fn`/`samples_fn` pointers, `fs`, `frame_rate` and the frame sample counter. `Synth.tab` points to **`SynthTables`** (0x2C), which holds the map and the five coefficient tables. `SynthTables.run` points to **`SynthRun`** (0x24), which holds the flags, the period counters, `coef` and `sstate`.
  - `coef` points to **`FrameCoef0`** (0xA0, mode 0) or **`FrameCoef1`** (0x7C, mode 1). They hold the gains, the `Resonator {c,b,a}` slots of §3.3 and the output low-pass coefficient.
  - `sstate` points to **`SynthState0`** (0x5C): the pitch counter, the noise LFSR, the resonator delay lines and the low-pass state.
  - The map comes from `0x10048ed0`, or from `0x10048fd0` when `map_sel != 0`. It has 32 `SynthMapEntry {const char *name; short frame_index; short def}` entries. Entry k uses the synth order of §3.1.

**Ghidra project:** `ivx_ghidra/` is driven through the ghidra-mcp bridge. After auto-analysis, `tools/ghidra/IvxApply.java` imports the types into `/ivx`, applies `symbols.tsv` (names, prototypes and plate comments), and tags every function. It is idempotent, so re-run it after editing either input.

---

## 3. The synthesizer inside the DLL (OVE III-style, fixed point)

The synth module runs from `0x10022cb0` to about `0x100250f0`. It is compiled differently from the rest: every function has an `ebp` frame, and the sample loops use hand-written inline asm (`imul` plus `rcl edx,1` Q31 multiplies). **This is the classic KTH/OVE III cascade/parallel formant synthesizer**, which Carlson '90 calls the "simplified" old model. It is *not* GLOVE: the map does name `RG RK FA AZ NA NM DI FZ BZ FP BP` (entries 21–31), but nothing reads them, and there is no LF source [CONFIRMED].

### 3.1 Parameter set [CONFIRMED]

There are two tables of 2-character parameter codes.

(a) At `0x10048904`, 4-byte names referenced through the pointer table at `0x10048768`. Listed in index order:

```
DR ST FS CS FD  F1 B1 F2 B2 F3 B3 F4 B4 FH  K1 C1 K2 C2  FN BN  AK AC AH AN A0  F0  (…YY ZZ)
```

(b) At `0x10045374`, MSVC multi-char constants stored little-endian (`"1F1B2F…"`). The order is `F1 B1 F2 B2 F3 B3 F4 B4 FH K1 C1 K2 C2 FN BN AK AC AN AH A0 F0`. Note that AN and AH are swapped relative to (a).

The synth's own parameter map (`SynthTables.map`) has 32 entries of 8 bytes. Each entry is `{const char *name, int16 frame_index, int16 default}`, where `frame_index == -1` means "use the default" [CONFIRMED]. **The synth uses the order of table (a) minus DR/ST/FS/CS/FD.** That is, entry k is:

`F1 B1 F2 B2 F3 B3 F4 B4 FH K1 C1 K2 C2 FN BN AK AC AH AN A0 F0` (then `RG RK FA AZ NA NM DI FZ BZ FP BP`, unused)

The names are the 4-byte strings at `0x10049518` (a second copy at `0x10049598` serves the alt map). In the default map, entries 0–20 have `frame_index` 1–21, so `frame[0]` is the phoneme-name slot that `op_next_frame` writes. `synt_ctrl(0x0b)` re-binds the map by name to a list of big-endian 2-char codes (§3.6).

Meaning by OVE III / GLOVE convention, checked against Carlson '90 Fig. 1:

- **F1–F4, B1–B4**: cascade formants and bandwidths. FH is the higher-pole correction.
- **K1 C1 K2 C2**: fricative-branch filters. The K-parameter is used as the frequency index and the C-parameter as the bandwidth index.
- **FN BN**: nasal resonator.
- **AK**: fricative bypass/mix. **AC**: fricative noise amplitude. **AH**: aspiration. **AN**: nasal amplitude. **A0**: voicing amplitude. **F0**: fundamental frequency.
- **DR / ST / FS / CS / FD** are not sent to the synth [CONFIRMED, §5.3]:
  - DR is the segment duration in frames.
  - ST, FS and CS are meta-parameters that set the smoothing rates of the other tracks.
  - FD shifts another parameter's offset (A3).

### 3.2 Coefficient tables (built at init with CRT pow, sqrt, exp and cos) [CONFIRMED]

Everything else runs in integer arithmetic. The only floating-point math in the whole DLL is here. CRT helper addresses: `pow` 0x10030c19, `sqrt` 0x10030ee4, `exp` 0x10030be0, `cos` 0x10030e34, `_ftol` 0x1002f5b0.

| Builder | Size | Contents |
|---|---|---|
| `0x10024ca0` | 32 × int16 | **Amplitude (dB) table.** `amp[0]=0`, and `amp[i] = 32767·10^((i−31)/20)` otherwise. **Amplitude parameters are 0–31 in 1 dB steps**, with 31 = 0 dB and 0 = off. |
| `0x10024d34` | 128 × int16 | `32767·exp(−π·B(F)/fs)·cos(2π·F/fs)`, i.e. the resonator "B" (r·cos θ) term. |
| `0x10024e2f` | 128 × int16 | `−32767·exp(−2π·B(F)/fs)`, i.e. the resonator "C" (−r²) term. |
| `0x10024efc` | 32 × int16 | Extra-bandwidth ratio for the B term: `exp(−π(B₀+ΔB[i])/fs) / exp(−πB₀/fs)`. |
| `0x10024fe5` | 32 × int16 | The same ratio for the C term (2π). |

- **Frequency index table** at `0x100490d0` holds 128 doubles, `F_k = 200·2^((k−3)/24)` Hz, so k=0 is 183.4 Hz and k=127 is 7183.8 Hz. **All formant/pole frequency parameters are indices on a quarter-tone (24 steps/octave) scale.** The values 400, 800, 1600, 3200 and 6400 Hz fall on k = 27, 51, 75, 99 and 123.
- **Default bandwidth:** `B(F) = √((F/20)² + 50²)` Hz. The constants are 20.0 at 0x10041000 and 2500.0 at 0x10041010.
- **Bandwidth parameters are additive increments ΔB** over that default. They index the 32-entry int16 table at `0x100494d0`:
  `0,32,64,…,512 (steps of 32), 593,674,755,836,917,998,1080,1300,1570,1890,2280,2750,3320,4000,4825` Hz.
  The reference B₀ for the ratio tables is `B(1000 Hz) ≈ 70.7 Hz`.
- `synt_set_resonator(SynthTables *t, Resonator *r, Fidx, Bidx, shift)` is at `0x100237a0`. Note that the first argument is the tables block (`synth->tab`), not the synth. It computes:
  - `r->b = 2·Tb[F]·Tbr[B] >> (15−shift)`
  - `r->c = 2·Tc[F]·Tcr[B] >> (15−shift)`
  - `r->a = (0x7fffffff>>(15−shift)) − 2·b − c`

  The memory order is `{c, b, a}`. This is the Klatt normalisation A = 1 − B − C, with B = 2r·cos θ [CONFIRMED in Ghidra].

### 3.3 Frame update (mode 0) at `0x10023846` [CONFIRMED]

The frame update reads the frame vector through the map. Undecoded fields are left as `?`.

- Amplitude gains, all via the dB table:
  - `A0 → gain_voice`
  - `AH/4 → gain_asp`, the noise added into the voice branch
  - `AN/2 → gain_nasal`
  - `AC/4 → gain_fric`
  - `AK → fric_mix`
- A non-zero A0 or AN sets the synth's "voiced" flag. If A0, AH, AN and AC are all 0, a "silent frame" flag is set.
- Cascade slots, in the order the sample loop runs them (coefficient offset: parameters, shift):

  | Offset | Parameters | Shift |
  |---|---|---|
  | +0x18 | F1/B1 | 14 |
  | +0x24 | FH / ΔB#10 (320 Hz) | 11 |
  | +0x30 | FH / ΔB#25 (1570 Hz) | 12 |
  | +0x3c | F2/B2 | 14 |
  | +0x48 | FH / ΔB#29 (3320 Hz) | 12 |
  | +0x54 | F4/B4 | 13 |
  | +0x60 | F3/B3 | 13 |

  **The higher-pole correction is three resonators at the same FH frequency with fixed extra bandwidths.** Compare Carlson '90: GLOVE instead uses F5, F6 = 1.2·F5 and F7 = 1.4·F5.
- Parallel slots:
  - nasal +0x70 `FN/BN`
  - fricative +0x80 `K2/C2` and +0x8c `K1/C1`
- **F0 source setup:**
  - `F0 < 10 → 10` (also when unvoiced)
  - `period = fs / F0`, then stored ÷2 as `half_period`
  - `samples_per_frame = fs / frame_rate` (stored but not read by the sample loop)
  - **The sample loop fires an impulse every `half_period` samples, so the pitch is 2 × the F0 frame value** [CONFIRMED, `0x1002464c`]. The F0 frame value is in 2 Hz units. `op_t0b_cosine` halves the rule value on the way out (`(A3 + 100 + (v−100)·voice/64)/2`), so the rules' F0 values are plain Hz [LIKELY].
- **Mode 1** (`0x10023fbb`, `FrameCoef1`) is a reduced variant: cascade F1 (shift 14), F2 (13), F3 (12) and one `FH−4`/ΔB#10 pole; nasal FN/BN; one fricative pole K1/C1. There is no F4 and no K2, and it doesn't set the silent flag 0x10 [CONFIRMED]. Its sample function is an empty stub (§3.4).

### 3.4 Sample loop (mode 0) at `0x100245e0`, and at `0x10024940` when fs = 16000 [CONFIRMED]

- **Voice source is a single-sample impulse (0x7FFF) once per pitch period.** It is not an LF pulse or a stored glottal waveform. Spectral shaping comes from the final low-pass.
- **Noise** is a 16-bit Galois LFSR: `x <<= 1; if (x < 0) x ^= 0x1d87`, seeded with `0xfff8`. It is then scaled with `>>2` (16-bit arithmetic shift). The pitch counter starts at 0x200.
- **Resonators:** `y = a·x + c·y2 + 2b·y1`, and the result is shifted left by `15 − shift` (F1 ≪1, FH1 ≪4, FH2 ≪3, F2 ≪1, FH3 ≪3, F4 ≪2, F3 ≪2, nasal ≪1, K2/K1 ≪3).
  - Each product is `imul` followed by `rcl edx,1`. That's `(p ≫ 31)`, except that bit 0 is imul's overflow flag rather than bit 31 of the product.
  - Every sum wraps at 32 bits.
  - This quirk changes the output within the first few samples, so any port must reproduce it [CONFIRMED by `tests/synth_cmp.c`].
- **Voice branch:** `(impulse·A0 + noise·AH)·2 ≫ 4`, fed through the 7-resonator cascade above. When both gains are 0, the 14 state words are cleared.
- **Nasal branch:** `impulse·AN·2 ≫ 4` through the FN resonator, added in parallel.
- **Fricative branch:** `noise·AC·4 ≫ 4` through K2/C2 (output `y1`, added to the sum) and then K1/C1 (output `y2`). The branch then adds `2·(y2 ≫ 16)·AK − y2` [CONFIRMED].
  - AK = 31 (0 dB) cancels the K1 path, and AK = 0 subtracts it fully. So AK sets how much of the second pole is removed.
  - This matches the AC-K2C2-K1C1-AK chain in Carlson '90 Fig. 1.
- **Output:** a one-pole low-pass, `y += c·(4·sum − y)` with a proper Q31 multiply (`shl eax,1; rcl edx,1`). `c = (1 − e^{−2π·200/fs})·(2³¹−1)` in mode 0; the mode-1 variant uses 1000 Hz (built at `0x100233ab`). The value is doubled, clipped to ±2²⁷ and shifted to 16 bits (`≪4 ≫16`).
  - The impulse train plus the 200 Hz one-pole gives the classic −6 dB/oct source-and-radiation slope of OVE-type synthesizers.
- **The fs = 16000 loop** (`0x10024940`) differs in exactly two places: the fricative input is `·2 ≫ 4` instead of `·4 ≫ 4`, and the output gain is 3·sum instead of 4·sum [CONFIRMED by diffing the two loops]. `synt_init_lowpass` selects it only when fs is exactly 16000.
- **Mode 1** (frame function `0x10023fbb`, sample function `0x10023760`): the sample function is an empty stub, so mode 1 produces no audio. Mode 2 returns −1 (unsupported).
- **Driver:** `synt_generate(s, buf, n)` at `0x10022fb4` is called by the engine at `0x10011fbb`.
  - Every `fs/frame_rate` samples (twice that at half rate) it pulls a frame from `get_frame(get_frame_arg)` and runs `frame_fn` on it. The engine's callback `0x10011410` returns `op_next_frame(core)`.
  - A NULL frame produces one frame of zero bytes.
  - It returns the number of synthesized samples.
  - Bytes per sample are `2` when `sample_fmt & 0xf` is non-zero, and `1` otherwise, but the sample loop always writes 16-bit samples.

### 3.5 Rate, volume and pitch from SAPI [CONFIRMED]

- `SpeedSet` (words per minute) is clamped to 15–500. Then `idx = first i with tbl[i] ≥ wpm·100/150` (the conversion is at `0x1000f870`, the table at `0x100453d8`). The engine then calls `synt_ctrl(0x0E, tbl[idx])`, which stores the **frame rate in frames per second** (clamped 2–1000).
  - `tbl` has 100 × int16 in roughly 0.4 dB geometric steps: 10 … 100 (index 50) … 931.
  - **150 wpm = 100 frames/s = 10 ms frames.** Global speaking rate is uniform frame-rate scaling in the synth. Contrast this with KIM's rule-level rate categories (see §4.1).
  - The inverse (frames/s → wpm) is at `0x1000f8b0`.
- `synt_ctrl(0x11, fs)`: in mode 1 with fs ≥ 16000, the synth runs at fs/2 and sets `synth+0x36 = 1`. That is the "Sample Rate/2" registry value.
- Don't confuse this with `tts_ctrl` 0x09 (`ui_rate`/DR, §2.2.1): that's a voice amplitude-balance parameter, not a speed control, and it never touches the frame rate above.

### 3.6 `synt_ctrl` command map [CONFIRMED]

| Cmd | Action |
|---|---|
| 0x00–0x08 | Create synth `0x10022e8f`; cmd = synth type (0 = mode 0, 1 = mode 1, 2 fails, 3–8 create a synth with no frame/sample functions). Defaults: 100 frames/s, **fs = 11025 Hz** (0x2B11). The engine then sets fs = 22050 (0x5622). |
| 0x09 | Set the frame-source callback `get_frame` (error −4 if NULL). The engine passes `0x10011410`. |
| 0x0a | Set the callback argument `get_frame_arg` (error −5 if NULL) |
| 0x0b | `synt_set_param_list` (`0x10022f62`): re-bind the map to a code list `{n, ?, code…}`. It uses `−n` codes if n < 0, and 25 otherwise. |
| 0x0c | Returns −12 (unsupported), as do all commands above 0x13 |
| 0x0d | Set `map_sel`, copy the map (`0x1002335b`) and re-select the functions and low-pass (`0x100233ab`). This resets any 0x0b binding. |
| 0x0e | Frame rate (2–1000) |
| 0x0f | Clear `frame_left`, which drops the rest of the current frame |
| 0x10 | Set `sample_fmt` (default 1) |
| 0x11 | Sample rate, then rebuild the tables and the low-pass. In mode 1 with fs ≥ 16000 it halves fs and sets `half_rate`. |
| 0x12 | Read `produced`, the count from the last `synt_generate` |
| 0x13 | Destroy (`0x10023180`) |

### 3.7 C port [CONFIRMED bit-exact]

`src/synth.c` is a function-by-function port of the whole synth module: all 26 functions, including both inline-asm sample loops rewritten in C. The constant tables are extracted from the DLL by `tools/gen_synth_data.py`, which writes `src/synth_data.c`.

`tests/synth_cmp.c` loads the real `Ivx230nt.dll` and drives its `synt_ctrl`/`synt_generate` and the port with the same pseudo-random frame stream. It covers:

- mode 0 at 8000, 11025, 16000, 22050 and 44100 Hz, and 2–1000 frames/s
- the alt map
- `sample_fmt` 0
- both forms of `0x0b` parameter binding
- mode 1 including half rate

It compares every coefficient table, the map, `FrameCoef*`, `SynthState0`, the run flags and every output sample (about 585k samples, about 507k of them from mode 0). The result is **9,941 checks, 0 failures**. The table builders match when compiled with SSE2 double math (`-msse2 -mfpmath=sse`), which is consistent with MSVC running the x87 at 53-bit precision. Build and run it with `make test` (see `Makefile`).

---

## 4. Prosody papers vs. the DLL

### 4.1 KIM, the Kiel Intonation Model (Kohler), in RULSYS/INFOVOX German

The facts below are from the paper. The numbers are the things to grep for in `gerules.ivx` bytecode constants.

**Symbolic input (German TTS):**

- Upper-case letters are phonemes, lower-case letters are allophones.
- `'` marks primary stress and `"` secondary stress, placed before the vowel.
- `%` is the compound boundary in TTS; KIM itself uses `#`.
- Raw phonetic input goes between `#…#`.
- Sentence-stress digits go before words: `3` reinforced, `2` accented (the default for content words), `1` partially deaccented, `0` deaccented. The `+` suffix marks function words, which default to 0.
- Peak position: `)` early, unmarked medial, `(` late.
- Punctuation:
  - `.` peak/fall, `,` low rise, `?` high rise
  - `,.` or `.,` fall + low rise, `.?` fall + high rise (TTS form: `,,` and `,?`)
  - `=` after `p:` marks a high prehead.
- The boundary marker is written `[rate][F0-endpoint][final-lengthening][pause]p:`.
  - Each digit is 0/1/2: pause 1 = ≤200 ms, 2 = ≥200 ms. Final lengthening 1 = default, 2 = hesitation. F0 descent 0 = none, 1 = intermediate, 2 = full.
  - Rate: 0 fast, 1 medium-reduced, 2 medium (the default), 3 slow.
  - `+` before the digits means pitch reset.
  - Examples: `#2212p:#` and `#+2110p:#`.
- TTS pauses: `,` = 300 ms. `.`, `?` and `!` = 600 ms, and each extra `.` adds 600 ms. `#(2)212p:.#` is equivalent to `.`.

**F0 algorithm (the medial peak is the reference):**

- `TF0` is at the start of the syllable containing the sentence-stressed vowel.
- `T2F0` is near the vowel centre, placed from the intrinsic vowel duration. Aspiration shifts it right: 3/4 of the aspiration is counted as vowel.
- `T3F0` applies only to sentence-final medial peaks, at peak + 150 ms (medium rate).
- Early peak: the maximum is at stressed-syllable onset, TF0 is 100 ms before it, and T3 is where the medial maximum would be.
- Late peak:
  - TF0 is as in the medial peak, and an extra low T2F0 sits at vowel onset.
  - The summit T3F0 is 100 ms after the medial centre, or at the end of the last voiced segment if that is less than 100 ms away.
  - Utterance-final: T4F0 is summit + 100 ms. In monosyllables with no final voiced consonant, T3 must be at least 30 ms before vowel offset.
- Valleys: the left and centre points match TF0/T2F0, and the high right point is at the end of the last voiced segment.
- **Downstep instead of declination:** −6 % peak to peak, starting at 130 Hz for a male voice. It is −18 % from a peak maximum to the following base. Valley low and high points both step down 6 %. The floor is 95 Hz for peak maxima, and downstep is interrupted by reinforcement or reset.
- The male low end is about 60–80 Hz; female voices are about an octave higher.
- **Microprosody:**
  - Close vowels with medial peaks: summit × 1.08.
  - **Cosine interpolation** between points.
  - After voiceless obstruents: +15 Hz at vowel onset, decaying to 0 at T2F0.
  - Voiced plosives −10 Hz, other voiced consonants −5 Hz.
  - F0 is masked in voiceless stretches.
- Durations follow a Klatt-style (1979) model with factors for stress, position, syllable count and rate. Vowels and consonants are scaled separately.

**In the DLL:** there is no native KIM code. **[CONFIRMED] KIM is implemented as rule bytecode in `gerules.ivx`, module M2** (see §5.6). Listing: `python tools/ivxdasm.py bin/gerules.ivx 12`. Examples, with rule addresses as body offsets:

| Rule | Bytecode, simplified | KIM equivalent |
|---|---|---|
| `145f4` | `F0@0200 += F0@0200 · (−(va6−1)) · 983/2^14` | Downstep of **6 %** per preceding peak (`va6` counts peaks) |
| `1465c` | pattern tests `va4` against 0x5F, then `F0@0200 = 95` | **95 Hz** floor on peak maxima |
| `150dc`, `15102` | close-vowel feature match → `F0 ·= 1105/2^10 (1.079)` | Close vowels in medial peaks: summit **× 1.08** |
| `14978` | `F0@fc00 = 130·(100 + 6·vac)/100` | **130 Hz** male start value with a 6 % step term |
| `140d4` | `F0@0000=90 ; F0@0080 −= 10 ; F0@fe00=130` | Point placement plus −10 Hz consonant microprosody |

- F0 values are plain Hz in the rules. Time positions are the `@xxxx` specs (§5.6): high byte = signed segment point, low byte 0x80 = relative/delta value.
- Still to identify in M2: the 18 % peak-to-base drop, the +15 Hz after voiceless obstruents, and the 150/100/30 ms point offsets.
- **Cosine interpolation [CONFIRMED]** is in the DLL, not in the rules. `op_t0b_cosine` (`0x1001d210`) interpolates between successive target points with the half-cosine table at `0x100489d0`. It is used by the **F0 track in every shipped file** (A1 type 11, or 32 in `blrules`) [CONFIRMED from the A1 tables].
- German says "Adapted to MBROLA synthesis", but M2 still writes formant parameters (B1/B2/B3, F2, AH…). How MBROLA output is chosen (`GetDiphoneResource`, `0x10016fe3`) is still open.

### 4.2 Fujisaki model for Swedish (Ljungqvist and Fujisaki '93)

The model is `ln F0(t) = ln Fb + Σ Ap·Gp(t−T0) + Σ Aa·[Ga(t−T1) − Ga(t−T2)]`, where:

- `Gp(t) = α²·t·e^{−αt}`
- `Ga(t) = min[1 − (1+βt)·e^{−βt}, γ]`, with γ = 0.9
- Typical parameter values: Fb = 78 Hz (male), α ≈ 1.7–2.0 s⁻¹, β ≈ 20–25 s⁻¹, Ap about 0.6–0.7 (initial) and 0.14–0.46 (later), Aa from −0.26 to +0.72.

TTS rules from the paper:

- Phrase commands go 300 ms before major syntactic boundaries.
- The focal accent's "return phase" coincides with the next stressed syllable's accent command.
- Accent 1 and Accent 2 have different command timing (Fig. 4 of the paper).

**In the DLL:** **[NOT FOUND]**. There are no exp/log calls outside synth-table init and no α/β/γ constants. **[CONFIRMED] Swedish (`swrules.ivx` M2) also uses KIM-like target points in Hz**, not Fujisaki phrase/accent commands. For example, rule `19ab6` does `F0@0000=130 ; F0@0080 −= 8 ; F0@fe00=100`, and `va2` counts accents. That is the Bruce/Granström point-based model.

### 4.3 RULSYS pipeline (Barber et al. '89, Carlson et al.)

The pipeline is `TEXT → DIG` (number grammar) `→ LEX` (exception lexicon) `→ GRAF` (grapheme-to-phoneme, word stress first) `→` phonetic transcription `→ FON` (phoneme defaults, coarticulation and prosodic rules) `→` synthesizer parameters.

- DIG and LEX hits skip GRAF.
- FON rules cover:
  - nasalisation spreading, lip-rounding assimilation, velar/labial coarticulation
  - diphthong transition lengthening
  - duration rules: stressed-vowel and geminate lengthening, cluster shortening, shortening before tense consonants, clause-final lengthening
- The parameter track in Fig. 2 of that paper uses 10 ms frames. This matches 100 frames/s at the default speed (§3.5).

**In the DLL:**

- [CONFIRMED] Each `.ivx` file holds up to six compiled rule modules (M0–M5), a phoneme/symbol inventory, character tables and a lexicon (§5). Mapping modules to DIG/GRAF/FON is **[HYPOTHESIS]**: M2 is FON (parameters, durations, F0), and M0/M3/M4 do symbol normalisation, stress and compounds (§5.5).
- [CONFIRMED] The DLL is the generic interpreter (`cw*`), the parameter/frame generator (`op*`), and the synth (§3).

### 4.4 Voice source papers (Carlson '90 GLOVE, Fant LF)

- **GLOVE parameters** (not present in this DLL):
  - RK = (Te−Tp)/Tp and RG = T0/(2Tp), both in %.
  - EE in dB. FA in Hz, above which an extra −6 dB/oct applies.
  - NM (flow-modulated noise), NA (pitch-synchronous aspiration noise), DI (diplophonia: every second pulse attenuated and delayed).
  - B1 varies with glottal opening. The nasal branch gets 2 poles and 1 zero. Higher poles: F6 = 1.2·F5, F7 = 1.4·F5, with a 6.3 kHz output low-pass.
  - Frames are 10 ms, linearly interpolated at each pitch-synchronous update. Rule smoothing is specified as "target value + time to reach it".
- **LF / Rd (Fant):**
  - Ra = Ta/T0, Rg = T0/(2Tp), Rk = (Te−Tp)/Tp, OQ = (1+Rk)/(2Rg), Fa = 1/(2πTa)
  - Rd = (U0/Ee)(F0/110)
  - Rap = (−1+4.8Rd)/100, Rkp = (22.4+11.8Rd)/100
  - Rd ≈ (1/0.11)(0.5+1.2Rk)(Rk/(4Rg)+Ra)
  - H1−H2 = −7.6 + 11.1·Rd dB
  - Typical male: Fa 700 Hz, Rk 0.30, Rg 1.20. Typical female: Fa 400 Hz, Rk 0.30, Rg 1.0.
- **In the DLL:** **[NOT FOUND]**. The source is an impulse train with a 200 Hz one-pole low-pass. Nothing in it models RK, RG or FA. The registry/UI knobs `Aspiration`, `Dynamic` (slider "Dy&namics") and `FormantNo` are the only voice-quality controls. Trace where they feed in (`0x10015993`–`0x10015a39`) [HYPOTHESIS: Aspiration scales AH, Dynamic scales the amplitude range].

---

## 5. Rule files (`bin/*.ivx`): format [CONFIRMED unless tagged]

All multi-byte values are **big-endian**, which suggests the files were compiled on 68000-era tooling. The DLL converts them in place when loading (§5.1). The files hold **no 68k machine code** [CONFIRMED]: the rule sections disassemble as 68000 code to junk (capstone: `ori.b #$8,(a0)`, an invalid word every few instructions), while the RULSYS decoder reads every rule to its exact length. The opcodes are multiples of 4 (jump-table offsets), so the original compiler and interpreter probably ran on a 68k machine [LIKELY]. Tools:

- `tools/ivx.py`: parse the header and dump phoneme records.
- `tools/ivxdasm.py`: validate (no args) or list the rule bytecode.
- `editor/ivxedit`: a model of the whole file that writes all 12 files back byte-identical and relocates phoneme ids (`python -m unittest discover -s editor/tests -t editor`).

### 5.1 Loading (DLL side)

- `CLangResource::Load` (`0x10016490`) reads the whole file with the CRT, then calls `tts_ctrl(NULL, 0x21, buf+0x200)` (`0x1001a8f0`). That call byte-swaps the body with two helpers:
  - `swap16(p,n)` at `0x1001aff0` swaps the bytes of each 16-bit word.
  - `swap_halves32(p,n)` at `0x1001b020` swaps the two halves of each dword.
- It converts [CONFIRMED, port `src/ttsctrl.c` byte-identical on all 12 files]:
  - the body header (H[0..15]) as dwords, then the three words at 0x6A
  - the per-parameter tables A1–A3 as words, (H[8] − 0x70)/8 words each. A0 is not converted, because it holds names.
  - each phoneme record as words: the length word first, then the rest of the record
  - the rule sections H[5], H[6], H[9], H[10], H[11] and H[12] as words. Each runs up to the next section: H[6] up to H[9], and H[12] up to H[14] (or H[13] when H[14] is 0).
  - in files without a lexicon (H[3] = 0), the 128-word lexicon index at H[13] + 0x80
- Character tables and the lexicon stay as bytes.
- `tts_ctrl` cmd 3 (`0x1001ab40`) then binds the sections into the core struct (the `core+` offsets in §5.2).
- **What an edited file must satisfy to load** [CONFIRMED, port source and `ivxsay` probes during the ivxedit step-1 review]: phoneme ids are signed shorts (`Seg.ph`), so the inventory including its terminator is at most 0x8000 bytes; M0, M1, M2 and M5 must exist (bound unconditionally, `src/ttsctrl.c:501-503`, or swapped up to the next section, `src/ttsctrl.c:389-398`) and M3 needs M4; an edited REPL word's low byte must be exactly 12·n (`cw_op_REPL`, `src/cwelem.c`); a build tag starting "XX" is refused by `ivx_bind` (`src/ttsctrl.c:472`). `editor/ivxedit/checks.py` enforces the first three.

### 5.2 File layout

```
0x000  file header, 0x200 bytes
   0x00 BE dword  flags (0x8000; 0 in amrules)     0x08 BE dword  body length (= file size - 0x200)
   0x10 char[12]  lexicon name "SWULEX.IVX"        0x1C BE dword  1
   0x80 version string   0xC0 "Copyright (C) Solna 1988,1989 INFOVOX AB"
0x200  body (all offsets below are body-relative)
   0x00  H[0..15] BE dwords (section directory)
   0x40  build tag "SWRU981118.P2"
   0x6A  word = 2*25 (nParams incl. DR..A0 → core+0x810)   0x6C word 116   0x6E word = 2*29 (total params → core+0x812)
   0x70  4 arrays x 32 words, per-parameter tables (§5.3)  → core+0x708/0x700/0x704/0x70C
   H[8]=0x170  phoneme/symbol inventory: chain of length-prefixed records, 0-terminated (§5.4) → core+0xA0
```

| Dir | Contents | Core field |
|---|---|---|
| H[0]=0x12F0, H[1]=0x4000 | Constant in every file. Probably runtime buffer sizes [HYPOTHESIS]. | — |
| H[2] | Non-zero only in Danish (0xDB00) [unknown] | — |
| H[3] | Lexicon, or 0 (§5.7) | `core+0x6B8` |
| H[5], H[6] | Rule modules M3, M4 (0 in fi/sp) | `core+0xFC`, `+0x100` |
| H[7] | Not an offset: words 0x004A, 0x006A | — |
| H[8] | Phoneme inventory (always 0x170) | `core+0xA0` |
| H[9] | Rule module M0 | `core+0xF0` |
| H[10] | Rule module M1 | `core+0xF4` |
| H[11] | Rule module M5 (may be empty) | `core+0x104` |
| H[12] | Rule module M2 | `core+0xF8` |
| H[13] | 128 class codes for ASCII 0x00–0x7F | `core+0x4AC` |
| H[14] | 128 × 2-char transliterations of bytes 0x80–0xFF, then 128 class codes for 0x80–0xFF (so H[13] = H[14]+0x180). 0 in older files. Those use the DLL's built-in transliterations (`tp_xlat_char`, §5.5.1) and the built-in class codes for 0x80–0xFF at `0x100486E8` (`tp_class80`). | `core+0x4A8` |
| H[15] | End of the data. Equal to the body length, except in am, it, no and sp, where 80, 64, 32 and 48 zero bytes follow it [CONFIRMED, `editor/tests/test_roundtrip.py`]. | — |

**Section order and padding** [CONFIRMED on all 12 files, `editor/ivxedit` round trip]: the inventory at 0x170, then M3, M4, M0, M1, M5, M2, H[14], H[13], H[3]; absent sections are skipped. Every section starts on a 16-byte boundary, with zero padding. In da, fr, ge, it, no and sw the inventory is followed by 16 more zero bytes. Each rule module is a lead word, its rules and the words `04 00`; the lead words vary per file (`9401`, `1023`, `8888`…) and may be a date or version stamp [HYPOTHESIS]. In files without H[3], H[13] is followed by the indexed lexicon (§5.7). Danish H[2] points 0x1800 bytes into that region, past the last index target (0x16d0); what it addresses is unknown.

The DLL converts class codes into char-class flag words at `core+0x4B2` (`0x1001ada0`). Codes −6 to 5 select, through jump table `0x1001aeb4`, one of 0x08/0x01/0x20/0x40/0x10/0x80/0x100/0x200/0x600 (lower-case letter)/0xA00 (upper-case letter)/0x1020/0x2100. Any other code makes a letter: 0x600 for `a`–`}`, 0xA00 for `A`–`]`, else 0x200 (all of 0x80–0xFF). The DLL then adds these fixed flags [CONFIRMED]:

- TAB and space: blank (|= 2)
- `.` and 0x17: sentence end (|= 0x10)
- LF, FF and CR: line break (|= 4)

### 5.3 Per-parameter tables (body 0x70)

There are four 32-word arrays: A0 at 0x70, A1 at 0xB0, A2 at 0xF0 and A3 at 0x130. They are stored **reversed** (file slot j ↔ parameter 29−j). The DLL copies them into 29-entry core arrays (`0x1001dc40`): A1 → `core+0x750`, A2 → `core+0x790` (×16 if A1 < 0x48), A3 → `core+0x7D0`.

- **A0:** if it holds printable 2-char codes, those are the parameter *names*. Otherwise (as in all shipped files) the DLL's names are used, from the pointer table `0x10048768`: `DR ST FS CS FD F1 B1 F2 B2 F3 B3 F4 B4 FH K1 C1 K2 C2 FN BN AK AC AH AN A0 F0 XX YY ZZ` (indices 0–28).
- **A3** is a per-parameter offset. In Swedish: F1 3, F2 35, F3 59, F4 75, FH 75, K1 51, K2 75, FN 3, ST 10, FS 32, CS 48, FD 8, F0 −3.
- **A1 is a conversion-type code**, not a scale [CONFIRMED, `op_param_dispatch` 0x1001cbb0]. Each frame, `A1[i]/4` selects the handler for track i (`Core.tracks[i]`, `Track` struct, `ivx_types.h`), and `A2[i]` is that handler's rate. `voice[i]` (`core+0xCDE`) is the active voice parameter: an additive offset for frequencies and amplitudes, and a scale (64 = unity) for rates.

  | A1/4 | Handler | Behaviour |
  |---|---|---|
  | 1, 2, 3 | `op_t01/02/03_*_rates` | **Meta-parameters.** The value sets the `A2` rates of other parameters: 21–24, then 7/9/11/13, then 5. |
  | 13, 14, 15 | `op_t0d/0e/0f_*` | The same, with the value scaled by voice/64. |
  | 4–7, 10, 16 | `op_t04_smooth` | Rate-limited approach to the target, then an output mapping: 4, 5 `op_out_freq`; 6 `op_out_div16`; 7 `op_out_amp2`; 10 `op_out_amp`; 16 `op_out_offset` [CONFIRMED, jump table `0x1001d0a4`]. |
  | 8, 9 | `op_t08/09_amp_decay` | Amplitude approach, clamped 0–31. |
  | 11, 32 | `op_t0b_cosine` (`0x1001d210`) | **Cosine interpolation** between points, using table `0x100489d0` = `16384·(1−cos(πi/128))`. Output `out = clamp15..255((A3 + 100 + ((v−100)·voice >> 6)) / 2)`. |
  | 12 | `op_t0c_linear` | Linear interpolation. Output `clamp(…)/2`, up to 0x1FF. |
  | 18–21, 28 | `op_t12_approach` | First-order approach. The step is `Δ·tbl[A2] >> 7`, with `tbl` = `0x10048ad8` = 256/(2i), so the approach takes about A2 frames. Output: 18, 19 `op_out_freq`; 20 `op_out_amp`; 21 `op_out_offset`; 28 `op_out_amp2` [CONFIRMED, jump table `0x1001d760`]. |
  | 22, 23 | `op_t16/17_amp_approach` | The same, for amplitudes (0–31). |
  | 24, 25, 26 | `op_t18/19/1a_*` | Copy the value into other parameters' A2 (and their track state). |
  | 17, 27, 29–31 | none | The track is advanced but never output. The older-family files give XX/YY/ZZ type 17 [CONFIRMED, jump table `0x1001ccec`]. |

  The output mappings used by the smoothing handlers:
  - `op_out_freq`: `v/4 + voice + A3`, clamped 0–127. This is the quarter-tone synth index, **confirming the `A3 + v/4` formula** [CONFIRMED].
  - `op_out_amp`: `voice + v/8`, clamped 0–31.
  - `op_out_amp2`: `(v + voice)/8`.
  - `op_out_div16`: `(v + voice)/16`.
  - `op_out_offset`: writes `A3` of another parameter.
- **Type codes per file** (`A1/4`, printed by the snippet in AGENTS.md) [CONFIRMED]:
  - Two families. **Older** (am, da, fi, ge, it, sp) uses: formants 4/5, bandwidths 10, AK 7, AC/AH 8, AN/A0 9, FD 16. **Newer** (bl, du, fr, ic, no, sw) uses: formants 18/19, bandwidths 20, AK 28, AC/AH 22, AN/A0 23, FD 21.
  - **ST, FS and CS are meta-parameters that set smoothing rates**: types 13/14/15 (older) or 24/26/25 (newer). So rules control coarticulation speed per segment.
  - FD writes `A3` of another parameter, making it a dynamic formant offset [LIKELY].
  - F0 is 11 (32 in `bl`).
  - DR is 0: it isn't output, and is read as the segment duration by `op_seg_durations`.
- If `A1[i] == 0x80`, the interpolated value first goes through the `short[260]` table at `0x10048B60`.
- The results go to `core+0xC98` (`outval[]`). The synth map's `frame_index` values index the frame that `op_next_frame` (`0x1001c4f0`) returns [CONFIRMED]:
  - With `out_mode` 0 (the default), it returns `&outval[4]`, and `outval[4]` (the frame's word 0) holds the flags `outval[0]`: 2, or 3 when an amplitude handler takes a new point. Parameter 4 (FD) is parked in `out_save0` and restored on the next call.
  - With `out_mode` ≠ 0 (`tts_ctrl` 0x1d), it returns `&outval[3]`. Word 0 is the byte-swapped frame number and word 1 the byte-swapped phoneme name of the segment on its first frame (else `"  "`).

### 5.3.1 Frame generation (`op*.c`) and its C port [CONFIRMED bit-exact]

**Utterance start** (`op_start_utterance`, `0x1001dd40`) moves `segs` to `Core.out` and then does four things:
- `op_seg_durations` sets each `Seg.dur` to its DR value byte (frames) and `frames_left` to their 16-bit sum.
- The tracks are cleared, initialised and primed.
- `op_mark_param` runs for F0 (parameter 25, hard-coded).
- It returns 0x20 when there is nothing to say: no segments, or no F0 point anywhere in the utterance.

With `feae` set (`tts_ctrl` 0x1e) it leaves the list in `segs` and only updates `synth_state`.

**Tracks.** Track i reads parameter i's points from the segment's own item (`p0` walks it), else from the phoneme record's default (`p1` walks it, `p0 == NULL`).
- **Search order:** segment items are sorted by descending signed id, so `op_tracks_init` searches from ntotal−1 down and hands its search pointers to track i−1. The last track is searched twice. Phoneme defaults are then searched upwards in the same way.
- **"Has points" bit:** bit i of `Seg.feat` (of `Seg.mask` for i ≥ 32) is set on every segment by `op_seg_durations` and cleared for segments where parameter i has no points. `op_tracks_init` clears it on the first segment, and `op_tracks_prime`, `op_track_next_seg` and `op_mark_param` clear it on later ones. A track skips flagged-off segments and adds their durations to its countdown. So `feat`/`mask` are reused as scratch once the rules are done.
- **Point times:** a track waits `time(next) − time(current)` frames between points and carries the segment duration over to the next segment (§5.4).

**One frame** (`op_next_frame`):
1. A pending voice (`voice_dirty`) becomes `voice[]`, and every track's target and delta drop by 1.
2. Track 0 counts down the segment. On a segment's first frame it reports the bookmark (`op_emit_bookmark`) and, with `out_flags` bit 1, the phoneme name through `cb_phonemes`. It toggles `feat` bit 0 when the segment ends.
3. `op_frame_gen` gives tracks 1..ntotal−1 their new points.
4. `op_param_dispatch` runs the A1 handler of every track whose delta ≠ 0. `feac` forces delta to 1 first; nothing in the DLL sets `feac`.
5. With `voice[0]` ≠ 0, AC and AN are lowered by `voice[0]`, and AH is raised to at least `level − 12 + voice[0]` of either, up to 31.

**Out of nodes.** The text side handles out-of-nodes:
- `tp_text_to_segs` retries once with retry bit 0, then frees `segs` and `held`.
- `seg_copy_held` frees `segs` and returns 1.

With pools of about 25 nodes, the DLL itself crashes in `op_seg_durations` at `0x1001e56e`. A segment without any DR value reaches output, and the DLL reads through `NULL + 2`. The engine always uses 900 nodes. Random bytes reach it with 900 nodes too; the port gives such a segment duration 0 (a guard, §2.2.2).

**Quirks the port reproduces:**
- When `p0` crosses into the next ItemBlk, `Track.p1` is set to `p2 + 0x38`, past the node. Only the point count bounds the reads.
- `op_track_has_item` clears `p0` when a lower id stops the search, but `op_track_find_item` leaves it there.
- `opb_restore` sets the first restored segment's `prev` to `&segs.tail`, and empties `Core.out` without freeing its nodes; `opb_save` does the same.
- `opb_save` keeps the original `prev` in the first copied ItemBlk. With an empty list it leaves `list.tail.prev` uninitialised; the port uses the list head instead.
- `seg_copy_held` does not copy `Seg.dur`.
- `opb_init` copies the Tracks raw, pointers included, but not `user_voice`.

**Port.** The frame generator is `src/opsmooth.c`, `src/opstart.c` and `src/opbatch.c`, all 44 functions (28 + 10 + 6), with `op.h`. `src/tpelem.c` is now complete (all 14 functions of `0x1001e890–0x1001eeef`). The lookup tables are one generated array, `src/op_data.c` (`tools/gen_op_data.py`): 0x100489d0–0x10048ecf, holding the cosine ramp, the rate table and the value remap. The DLL indexes them without bounds checks, for example `rate[A2]` with A2 up to 255, so the port keeps the block as one array.

**Test.** `tests/op_cmp.c` patches the DLL's `op_start_utterance`, `ivx_load_param_tables` and the tpelem entry points the text pipeline calls: `tp_text_to_segs`, `seg_hold`, `seg_unhold`, `seg_copy_held`, `seg_free_held`, `seg_free_segs_held` and `op_emit_phonemes`. The hooks call the originals through trampolines. It also checks every `op_next_frame` call. Each check compares:
- the return value (for frames, which word the frame starts at)
- every dword of the Core and the node pool (`tests/harness.c`, typed pointer relocation)
- the bookmark and phoneme callback output

At each utterance start, the segment list also goes through `opb_save`, `opb_init`, `opb_restore` and all of its frames, on two clones of the DLL state. The text is the same as `cw_cmp`'s, with this setup:
- Rate, pitch, FD and voice change between words, so `voice_dirty` fires.
- Bookmarks and the phoneme callback are on.
- A second pass runs with `out_mode` 1, and forces `feac` on for about 5 % of frames.
- A third pass uses a 60-node pool, which reaches the out-of-nodes paths.

Result: **0 failures in all 12 languages.**
- 86,429 `op_next_frame` calls plus 96,370 frames through `opb_restore`.
- All 23 A1 types that occur, and every tpelem entry point, including the out-of-nodes returns of `seg_copy_held` (12) and `tp_text_to_segs` (20).

Mutation check: 13 deliberate bugs were introduced into the port.
- 9 were caught as mismatches: rounding, the wrap test, clamps, bookmark clearing, the mode-0 flags word, the cosine divisor, has_item's `p0` reset, a missing `mark_hi` and a missing `word_pos`.
- 1 was caught as a crash.
- 3 have no effect: two are equivalent, and nothing in the replay reads the `prev` changed by the third.
- One batch run also crashed on two mutants. That did not reproduce when they were rerun, and the unmodified test is stable over repeated runs.

### 5.4 Phoneme / symbol inventory (H[8])

This is a chain of records `[BE len][name char[2]][features u32][feature-defined mask u32][items…]`, terminated by len 0.

- Names are two bytes; one-character names have a **leading** NUL (`00 2B` = `+`) [CONFIRMED].
- Items are in ascending **signed** id order, so variables (ids ≥ 0x80) come first; each id at most once [CONFIRMED, all 1,557 records].
- **The phoneme id used throughout the bytecode is the record's offset relative to H[8].** For example, Swedish id 0x12D2 = record at body 0x1442 = `S`.
- The first record (len 0x30) holds defaults: initial values for variables 0xA2–0xAA.
- The inventory contains phonemes (`A:`, `E0`, `2S` retroflex, …), allophones (lower-case names) **and** input symbols (`+ 1 ' " ` ; = , . # * _ -` digits, and so on). So text symbols, prosodic markers and phonemes share one segment list.
- Features and mask are what the feature-match opcodes test: `((seg.feat ^ V) & seg.mask & M) == 0`.
- **Item:** `u8 id, u8 size, data[size]`.
  - id < 0x80 is a parameter (index into the name list). Its data is `(u8 timecode, u8 value)` pairs.
  - id ≥ 0x80 is a variable (`vXX`) with a BE int16 value.
- **Time codes:** a parameter point is the word `{time:s8, value:u8}`. The time is a **signed frame offset from the start of the segment** [CONFIRMED, `op_frame_gen` 0x1001c810 / `op_track_next_seg`]: a track waits `time(next) − time(current)` frames between points and carries the segment's DR duration over to the next segment. So `0xFF` (−1) is "from the segment start" (the common single point), `0..4` are points inside the segment, and `0xF8–0xFE` (−8…−2) start the movement up to 8 frames early, inside the previous segment.
- Example (Swedish `A:`): `A0=24@0 F1=165@*/165@2 F2=87@*/83@2 … FD=100@* va5=40 vac=110 vad=30`. Amplitudes (A0 23–30, AN, AH, AC) are on the 0–31 dB scale of the synth. FD=100 looks like a duration percentage [HYPOTHESIS].

### 5.5 Rule modules

`run_module(core, i)` at `0x1001fe90` runs `cw_run(core, core->mod[i])`, where `mod[] = {H9, H10, H12, H5, H6, H11}` (M0–M5).

- Call sites, in pipeline order by address:
  - M0 at `0x1001f4c1`/`0x1001f522`
  - M3 and M4 at `0x1001f605`, `0x1001f67e`, `0x1001f86b` (only if both exist)
  - M1 at `0x1001fa8c`
  - M5 at `0x1001fd4d`/`0x1002006a`
  - M2 at `0x1001fdfa`/`0x1002010f`/`0x10020157`
- **The pipeline is now traced in code [CONFIRMED]** (tpchar/tpstr/opstart, see §2.3):
  1. `tp_putc` (`0x1001b050`) buffers text into `Core.text` and runs a 14-state character machine (`tp_st*`). There are two escape characters:
     - `esc_phon` (default ^B) switches to **phonetic input**, where symbols go straight to segments.
     - `esc_mark` (default ^A) takes a 4-byte bookmark.
  2. **Words** go to `tp_word` (`0x1001f170`), which tries, in order:
     - the user-dictionary callback
     - the **lexicon** (`lex_lookup`)
     - the compound path, if M3 and M4 exist (`tp_compound`: **M3**, split at `=`/`-`, lexicon lookups on the parts, **M4**)
     - the exception-dictionary callback
     - **M0**, the letter-to-sound rules on the spelled-out word

     **Other tokens** (digits, symbols) go to `tp_token_m1`, which runs **M1**.
  3. The finished word (`ctl_word_end`) is either held in `Core.held` until the end of the sentence (mode bit 8) or processed at once.
  4. At the end of a sentence (`tp_sentence_end`, `0x1001fb90`), the held words move to `segs` and a punctuation symbol is appended. Then **M5** runs (sentence level, if present), then **M2** (phonetic realisation: durations, formants, F0), then `op_start_utterance` moves the segments to `Core.out` for frame generation.
- Module roles, refined [LIKELY]:
  - **M0**: word letter-to-sound, plus normalisation of 7-bit national letters and stress digits.
  - **M1**: numbers and non-word tokens.
  - **M3/M4**: compound and lexicon decomposition, before and after the split.
  - **M5**: sentence-level markers and variables (pauses, `vbf=100`…).
  - **M2**: **FON**. Durations (`A*=frac` factors 0.7–1.4), formant/amplitude adjustments, and **F0 target points**.
- Out-of-nodes status (`0x4x`) is handled by freeing the held words and re-running the module.
- **Module layout:** `[lead word] [0x0004 BEGIN] rule* [0x0000 END]`.
- **Rule layout:** `0x0008 RULE` (or `0x000C RULE*`, iterative) · `len` (bytes, counted from the len word) · body · normally `0x0010 NEXT <rule-number>`. The trailing word is the source rule number, used for tracing.
- **Validation:** with the opcode table in `tools/ivxdasm.py`, **all 15,564 rules in the 12 files decode to exactly their declared length.**

### 5.5.1 Text processing (`tpchar.c`, `tpstr.c`) and its C port [CONFIRMED bit-exact]

**Input buffer.** `tp_putc` (`0x1001b050`) appends each char to `Core.text` (800 bytes) and runs the state functions while `text_cur < text_end`. It returns 1 when an utterance is ready, 2 when the synth is busy (`synth_state` 0xFF: the char stays pending and the next call does not append its argument), else 0. Non-zero state statuses go to `cb_status`.
- Accepted chars: printable and 8-bit, 0x08–0x0D except LF, `esc_mark`, `esc_phon` and 0x17 (end of text). LF and other control chars are dropped.
- After a sentence (`text_consumed`), the unread rest is moved to the start of the buffer. When the buffer passes `text_limit` (text+0x2A0), `tp_st00` appends " ." so the sentence ends.
- `tp_putc` calls `setjmp(Core.jmpbuf)`, but nothing in the DLL calls longjmp [CONFIRMED: `0x1001b0b1` is the only use of Core+0x120]. The port leaves the call out.
- Mode 1 (char mode, set together with mode 2): every char is spoken on its own through `tp_speak_char(ch, flush=1)`.

**States** (`Core.tp_state`, jump table `0x1001b434`):

| State | Function | Role |
|---|---|---|
| 0 | `tp_st00_start` | Classify a char. A CR after a CR becomes "." (when words are held). A CR with fewer than 40 free nodes becomes 0x17. |
| 1 | `tp_st01_phonetic` | Phonetic input after `esc_phon`: symbols go straight to segments. 8-bit chars are replaced in the buffer by their 2-char transliteration. |
| 2 | `tp_st02_phonetic_skip` | Phonetic input over 85 chars, or out of nodes: speak it char by char. |
| 3 | `tp_st03_word` | Letters (class 0x200) → `tp_word`. Over 85 chars, or out of nodes: spell it (state 7). |
| 4/5 | `tp_st04_hyphen` | `word-letter` is spoken as two words with the token "hy" (`0x10048998`) between them. `word-<line break>` is joined (mode 0x10 keeps a newline in the word). |
| 6 | `tp_st06_skip_break` | Drop blanks and breaks after `word-<break>`. |
| 7, 10 | `tp_st07_spell`, `tp_st0a_spell` | Spell a word or token by char names. |
| 8, 9 | `tp_st08_token`, `tp_st09_num_sep` | Other tokens → `tp_token_m1` (M1). A class-0x40 separator stays in the token only before a digit (class 0x80), as in `3.14` or `1,000`. |
| 11 | `tp_st0b_spell_sep` | A separator inside a spelled token, spoken before a digit. |
| 12/13 | `tp_st0c_mark` | `esc_mark b1 b2 b3` sets the bookmark: `mark_lo = (mark_lo & 0xFF00) \| (signed)b1`, `mark_hi = b2<<8 \| (signed)b3`. The sign extensions are the DLL's. State 13 returns to phonetic input. |

Char classes (`Core.class_flags`):
- 1 ignored
- 2 blank
- 4 line break (read as a blank)
- 0x10 sentence punctuation
- 0x40 number separator
- 0x80 digit
- 0x100 spoken as a symbol
- 0x200 letter
- 0x400 lower case
- 0x800 prefix "@D" in mode 2
- 0x2000 with 0x100: name "c%"

Chars ≥ 0x80 are first mapped through the national-charset table: `0x10048508` for charset bit 0, `0x10048588` for bit 1 (the engine uses 2).

**Words** (`tp_word`, `0x1001f170`):
1. **User dictionary** (`cb_userdict(arg, word, &out)`): 1 replaces the word in the text buffer by `out`, and 2 by `esc_phon out esc_phon`. The text is then read again from the word start. If the result does not fit, it returns 0x71.
2. The word is transliterated (8-bit chars → 2 chars: `tp_xlat_char`, from `Core.xlat_hi` or built-in tables chosen by the .ivx tag) and upper-cased. 128 chars or more returns 0x70.
3. **Lexicon** (`tp_lex_word`).
4. Symbol segments; then, if both M3 and M4 exist, **`tp_compound`**:
   - M3 runs on a held copy and marks the symbols with `=` (prefix) and/or `-` (suffix).
   - The stem is looked up in the lexicon, trying `prefix+stem` first when both marks are present. `prefix=stem` drops the stem's last char.
   - The segments become prefix symbols + stem transcription + suffix symbols, and M4 runs.
5. **Exception dictionary** (`cb_excdict(arg, names, 0)`): it gets the word's symbol names (`op_emit_phonemes`, at most 44 chars) in the same buffer, and returns a respelling or NULL. The reply goes through `tp_text_to_segs(c, 1, …)` like a spelling and then M0; it is not looked up in the lexicon, and a reply equal to the word's own key gives the word's own audio [CONFIRMED, probe on the port].
6. **M0**.

Every path ends in `ctl_word_end`.

**The dictionary callbacks in practice** [CONFIRMED, probes on the port unless tagged]:
- **What a word is** depends on the language and the charset (every byte 0x21–0xFF between letters, 12
  languages, charsets 0–3). A–Z, a–z and `@` always belong to a word; the apostrophe everywhere but in
  French; `[ \ ] { | } ~` in German, `[ \ ] { |` and `"` in Norwegian, `:` and `~` in Spanish, `"` and `/`
  in Italian, `` ` `` in Finnish and Icelandic (their 7-bit alphabets). Under charset 2: 0xC0–0xFF except
  Ã Õ ã õ × ÷, plus ´, with Ø ø split in German and French, ß and ´ in French, and Œ œ kept in French,
  ª º in Icelandic; Š š Ž ž Ÿ split words. Digits, blanks, control characters and
  `! # $ % & ( ) * + , - . ; < = > ? ^ _` split words everywhere.
- `cb_userdict` gets the word as it stands in `Core.text` (cp1252, original case), and the engine calls it
  with `from == tok_start` (`tpchar.c:242,270,310`). A self-including TEXT reply ("cat" → "cat dog")
  expands 167 times until the buffer is full, then gives 0x71 twice.
- `cb_excdict`'s key is `" " + names + " "`, the word transliterated and upper-cased: `" KALLE "`,
  `" F]GLAR "` (fåglar), `" STRA~E "` (Straße), `" 'T'OR'DUR "` (Þórður). It is called only after a lexicon
  miss and only for a word that is not split as a compound, in the same `tp_word` call as `cb_userdict`.
- Installing `cb_excdict` changes the out-of-nodes retry even when it returns NULL: the key is written
  into the word buffer the retry rebuilds the word from, so the retry takes the first *n* bytes of the key
  (the leading space and all but the last letter) (`tpstr.c:245-270`). A bind that gets past its checks
  (a non-NULL body without an "XX" tag) clears it (`ttsctrl.c:478`).
- The SAPI engine installs the voice's `LibraryFile` function (Danish: `darules.dll!norm`) as `cb_excdict`
  (§2.2, `0x10016cc0`) [LIKELY], and backs `cb_userdict` with its lexicon object (`0x10010e40`).

**`ctl_word_end`** (`0x1001fef0`):
- It stamps the segments with the bookmark and `word_pos = text_cur − tok_start − 1`, which goes to `cb_wordpos`; the DLL passes junk in the high half.
- In mode 8 (the default) the word is held (`seg_hold`) until `tp_sentence_end`. That function appends a punctuation symbol and runs M5, M2 and `op_start_utterance`. The symbol is `",,@@."` for end of text, `" @."` with no punctuation, the punctuation char itself, or `"@@<p>"` in mode 0x80.
- In mode 4, or while retry bit 1 is set, it appends `"@."` and runs M5, M2 and the utterance at once. In mode 12 this runs on a copy of the held words.

**Out of nodes** (status 0x4x). A module that runs out:
- drops the held words and sets retry bit 0; while it is set, the rest of the sentence is skipped;
- or makes the sentence restart from the start of the buffer with retry bit 1, and its words then go out one by one.

Two DLL bugs lie on these paths:
- **Livelock:** a word that does not fit the pool even alone restarts the sentence forever. Only `tp_sentence_end` clears retry bit 1, and it is never reached.
- **Crash:** the known crash at `0x1001e56e` (§5.3.1), which the port guards (§2.2.2).

Both need pools well below the engine's 900.

**`lex_lookup`** (`0x1001f890`), without `lex_index`, binary-searches the 0x7F-separated entries (§5.7). If the search ends on the final 0x7F and the word starts with 0x7F, it compares one byte **past the end of the lexicon** [CONFIRMED with a guard page]. With `lex_index`, each first char has a run of keys without their first char, both strings padded to even length and the run closed by 0x7F.

**Data.** The demo version's nag in `tp_st00` (`0x1001b55f`, "zdemo" every `count_init` sentences) is jumped over by the crack. It is licensing, so it is labelled and not ported. Nothing in the DLL sets `flush_on_input` or `count_init`.

**Port.** The port has these files:
- `src/tpchar.c`: all 14 functions of `0x1001b050–0x1001c4ef`.
- `src/tpstr.c`: the 7 of `0x1001eef0–0x1001fe8f`.
- `ctl_word_end`, in `src/contwork.c`.
- `tp_xlat_char` and `tp_char_class`, in `src/ttsctrl.c` (the rest of `ttsctrl.c` is still open).
- `src/tp.h`.

The char tables `0x10048508–0x100486e7` are one generated array, `src/tp_data.c` (`tools/gen_tp_data.py`).

**Test** (`tests/tp_cmp.c`). It runs the DLL and the port side by side from `tp_putc` down: two Cores, and no hooks. Everything below `tp_putc` runs as DLL or as port (rule modules, `op_start_utterance`, frames). After every char it compares:
- the return value
- every dword of the Core and pool, with pointers through the relocation map
- all callback output

After every char, the frames are drained on both sides and compared.

The 12 language texts plus a stress text run in 12 configurations:
- Settings:
  - the engine's settings (mode 8, charset 2)
  - word at a time (mode 4)
  - mode 12
  - mode 0x90
  - char mode
  - no synthesis (out_flags 4/5)
  - charsets 0 and 1
  - pools of 80 and 120 nodes
- Stress text:
  - in-text bookmarks, one with bytes ≥ 0x80
  - phonetic input, including over-long input and 8-bit chars
  - hyphenation and line breaks
  - numbers with separators
  - symbols and control chars
  - an 85+ letter word, a 170-char transliteration and a 100-digit token
  - a 700-char sentence
- Callbacks:
  - The user and exception dictionaries rewrite some words.
  - The word-position callback makes the synth "busy" now and then.

A vectored exception handler and a watchdog thread turn a DLL crash or livelock into an abandoned run.

**Result: 144 runs, 208,027 `tp_putc` calls and 4.74 M frames match with 0 failures.** Two runs (German at 80 nodes) were abandoned at the DLL's own crash. Every branch counter of the port is reached, including:
- both user-dictionary kinds and "no room"
- the too-long word
- all three compound shapes
- the M0/M1/compound/word/sentence out-of-nodes retries
- the busy/pending path
- the buffer-full " ." append

The test found no port bug, only test-harness issues: an unpadded file buffer (the lexicon over-read above), and the DLL crash and livelock.

**Mutations.** Of 14 deliberate bugs, 9 are caught: 8 as mismatches and 1 as a port-side hang caught by the watchdog. One of those caught is the bookmark sign extension, caught through a high-byte bookmark in the stress text. The other 5 are not caught:
- One is equivalent: in `tp_token_m1`'s retry, `seg_free_held` versus `seg_free_segs_held`, because `run_module` has already freed segs.
- Four need inputs the text lacks:
  - exactly 39 free nodes at a CR
  - symbol names of 44+ chars for the exception dictionary
  - a `prefix=stem` compound whose stem is in a lexicon
  - the French built-in transliteration of 0x81, which is probably shadowed by the file's own table

### 5.6 Rule bytecode instruction set

The bytecode is a pattern-match/rewrite machine over the segment list. Interpreter state: IP `core+0x48`, rule start `+0x44`, cursor `+0x4C`, focus `+0x50`, next `+0x54`, status `+0x68`.

Registers:
- A: 32-bit accumulator, `+0x6C`
- B: 16-bit operand, `+0x70`
- R0–R3: `+0x72…+0x78`
- V[n]: global variables, `+0x7A`
- two 16-entry ring queues: `+0x00`, `+0x20`

Failure modes:
- `fail` (`0x10020980`) jumps to the next rule.
- `retry` (`0x100208d0`) re-runs the rule at the next start position.

Opcodes are words that are multiples of 4. `op` below = operand size in bytes.

| Op | Mnemonic | Operands | Meaning |
|---|---|---|---|
| 00 | END | – | end of module |
| 04 | BEGIN | – | module start (status 0x90) |
| 08/0C | RULE/RULE* | len | start rule; scan from list head (0C = iterative mode) |
| 10 | NEXT | rule# | advance start position and retry. The operand is **never read** (`cw_op_NEXT` resets `ip` to the rule start, `src/cwflow.c:45`) [CONFIRMED]; it is the rule's number in the source [LIKELY] |
| 40 | SEEK | ph | scan right for phoneme `ph`, set focus |
| 44 | FOCUS | ph | current must be `ph`; set focus |
| 48 | SCAN | ph | scan right for `ph` (no focus) |
| 4C / 50 | R / R? | ph | right context required / optional |
| 7C / 80 | L / L? | ph | left context required / optional |
| 54,58,5C,60,64,90,94 | SEEKF, FOCUSF, SCANF, RF, RF?, LF, LF? | V:u32 M:u32 | same as above, matching feature bits |
| 68 | FOCUSP | pattern | focus on the next segment if it matches the pattern |
| 6C / 9C | RP / LP | {min:8, opt:8} pattern | right/left context: `min` (high byte) mandatory matches, then up to `opt` (low byte) optional ones (0x007F = `*`) |
| B0 | FNEXT | – | focus = focus→next (not used by any rule file) |
| B4 | UNWRAP | n | **delete** n segments from the focus (with their item blocks) and restart the rule; n = 0 → status 0x60 [CONFIRMED] |
| B8/BC | REPL+/REPL | w, ids… | replace: delete `d`, insert `n` phonemes, where `w = d·256 + 12·n` (12·n may carry into the high byte). Old nodes are reused first. New segments take feat/mask from their inventory record and the bookmark of the last deleted segment. If the next op is NEXT, matching continues after the new segments. B8 acts one segment later.  A count that is not a multiple of 12 (12·n carried into the high byte) wraps in the DLL; the port stops with status 0x10 (a guard, §2.2.2). Five shipped words carry: Danish 0x314, 0x320, 0x32c ×2, Icelandic 0x1108 [CONFIRMED]. |
| C0/C4 | SETF+/SETF | V M | `feat = feat&~M \| V`, `mask = mask&~V \| M` |
| C8 | GEMIN | (rule#) | find adjacent equal phonemes with feature bits 0x10002, set bit 4 in feat and mask of both, then NEXT. The following word is never read. |
| CC/D0/D4 | CPREV/CHEAD/CTAIL | – | next = focus→prev (retry if the focus is first) / list head / list tail |
| 100 | LD | spec [index] | B = item of the focus segment, or the phoneme's default |
| 104/108/10C | STs/STu/ST | spec [index] | store A: clamped to −127..127, clamped to 0..255, or raw |
| 150…194 | arithmetic | n / – | A=n, A=B, `B=−B, A=B`, ±, abs, ×, ÷ rounded as `(A ± d/4)/d` (away from zero at a quarter), `A=(A·m)>>s` (0x188, 2 words), shifts. 0x18C shifts copy bit 14 into bit 15 (16-bit sign). Divide by 0 → status 0x30–0x33. |
| 198/19C, 1A0/1A4 | PUSHQ1/2, B=POPQ1/2 | – | 16-entry ring queues (indices reset by `cw_run`) |
| 1A8–1C4 | B=Rn / Rn=A | – | scratch registers |
| 1C8/1CC | B=V / V=A | n | global variables (unchecked index; not used by any rule file) |
| 1F0–1F8 | – | (1 word) | `0x10022ca0` skips one operand word. Not used by any rule file. |

**Phoneme ids that point at the inventory's terminator** [CONFIRMED]: 1 operand in `frrules.ivx` (0x1bae) and 7 in `itrules.ivx` (0xe3c) equal the offset of the inventory's 0 terminator, so they name a phoneme that does not exist (no segment can match it). Probably a rule-compiler artifact for an undefined symbol [HYPOTHESIS]. Every other phoneme-id operand in the 12 files is a record start. The rule bytecode is the only place a file stores phoneme offsets: at run time every segment's id comes from `ph_find` (by name, `src/tpelem.c:284`; the segment's id is set from its result at `:274`) or from a rule operand [CONFIRMED, every `phinv` use in `src/`].

Status codes (`Core.status`): 0x10 bad opcode, 0x20 LD without item or default, 0x21 LD of an empty item, 0x22/0x23 invalid ST delete, 0x30–0x33 divide by zero, **0x40 out of pool nodes**, 0x50 bad parameter-test op, 0x60 UNWRAP 0, 0x90 end of module (returned as 0) [CONFIRMED].

Operand formats [CONFIRMED by the C port, §5.6.1]:
- **Items** (on segments and in phoneme records) are `{id:8, bytes:8}` header words, each followed by `bytes/2` payload words. **Ids ≥ 0x80 are single-word variables. Lower ids are synthesis parameters**, whose payload words each hold two bytes: the low byte is read unsigned, and the high byte signed.
- **LD/ST spec word:** `{tspec:8, id:8}` (id in the low byte). For parameters, an **index word** follows: bits 9–15 = word index k, and flag 0x80 = use the high byte.
  - LD: k counts from the first payload word, or from the last one when bit 15 is set, so the high byte is `2k`. The §5.4 time codes `00 02 04` therefore mean words 0, 1, 2. From the end, `FC` is the last word and `FE` is one past it (the append position for an insert).
  - ST with `tspec` 0 overwrites that byte, `tspec > 0` inserts a new word at k, and `tspec < 0` deletes word k. For variables the same `tspec` sign means overwrite, insert or delete of the single value.
- **Pattern** (`68`/`6C`/`9C`): header `H` whose bits 1–11 give the pattern length in words (`H & 0x0FFE` bytes).
  - Bit 15: a phoneme id follows.
  - Bit 14: feature V, M follow.
  - Bit 13: **parameter tests** follow up to the end of the pattern. Each test is one word `{id:8, 0x80, 0x40, op:6}`, optionally followed by a constant:
    - The value is the item `id` of the segment (else its phoneme default). The test fails if neither exists or the item is empty.
    - Flag 0x80 takes the last payload word instead of the first, and flag 0x40 takes its signed high byte instead of the unsigned low byte.
    - `op` 0–12 and 18 **capture** the value into R0, V0–V4, R1, V5–V9, R2 or R3.
    - `op` 0x18/0x1E/0x24/0x2C/0x34/0x3C **compare** the following constant k with the value: pass if `k > v`, `≥`, `==`, `≠`, `≤`, `<`.
    - Any other op sets status 0x50. [CONFIRMED, `0x10021300` and its jump table at `0x100214a0`/`0x100214f4`]

### 5.6.1 C port of the interpreter [CONFIRMED bit-exact]

The interpreter is ported function by function to `src/contwork.c` and `src/cw*.c`, one file per original source file, plus the list helpers in `src/tpelem.c` (now complete, see §5.3.1). That covers all 89 functions of 0x1001fe90–0x10022caf, including `ctl_word_end`, which drives the tp/op pipeline (§5.5.1). The item-storage functions (`cw_find_item`, `cw_item_insert`, `cw_st_*`) were written from the disassembly, because the decompiler mistyped their node arithmetic.

- **Item storage:** a Seg holds 12 inline item words (24 bytes) at +0x20 and continues into a chain of `ItemBlk` nodes (`Seg.ext`, then `ItemBlk.next`; `ItemBlk.prev` points back to the previous block or the Seg). Each block holds 23 words (0x2E bytes) at +0xA.
- **Ordering:** items may straddle node boundaries. They are sorted by descending signed id on segments, but by ascending signed id in phoneme records (§5.4).

`tests/cw_cmp.c` patches the DLL's `run_module` so that every module run is also executed by the port. Before each run it copies the DLL's Core and node pool with typed pointer relocation (`tests/harness.c`). Afterwards it compares the status and every dword of the Core and pool: the Core's pointer fields through the relocation, all other fields exactly.

The text runs twice: with the engine's 900-node pool, and with a 60-node pool that makes modules run out of nodes.

On real text in all 12 languages, **1,765 module runs (4.1 M opcodes; 24 end out of nodes with status 0x40) match with 0 failures**.
- 64 of the 70 opcodes are executed. SETF+ is not reached by the test text. FNEXT, `A−=n`, `B=V`, `V=A` and 1F0–1F8 appear in no rule file.
- All four `cw_item_insert` branches are exercised, including new-block allocation.
- Deliberately introduced bugs are caught, either as mismatches, as a crash, or as a hang.

Example (`swrules.ivx` M0, rule #32): `SEEK + ; R B ; REPL del 2 -> S`, i.e. the sequence `+ B` is rewritten to `S`.

### 5.7 Lexicon (H[3])

- `BE u32 length`, then ASCII-sorted entries `0x7F orth 0x00 transcription 0x00`. `lex_lookup` binary-searches them (§5.5.1). The first bytes after the length (`20 00 "es" 00`) sit where the search's lower bound starts, so they are read as an ordinary first entry, key " " → "es" [LIKELY].
- Transcriptions use the RULSYS notation (`'` `"` stress, `#` compound boundary, `:` length, `E0` schwa; Swedish ISO-646-SE `[`=Ä, `\`=Ö, `]`=Å). They often end in lower-case class/POS tags: `a b kn v s r …` [HYPOTHESIS].
- Examples: `%` → `PROS'ENT`, `!` → `"U:TROPS#T'EKE0N`.
- The size dword counts itself. The entries end with one or more 0x7F bytes, up to a 16-byte boundary [CONFIRMED, sw 2, bl 3]. The order is non-decreasing: sw has 6 and bl 10 repeated headwords (`BORT` twice with different transcriptions, `LOSE` twice the same) [CONFIRMED].
- Only Swedish and British English have H[3]. **The other 10 files have an indexed lexicon instead** [CONFIRMED, `ivx_bind` `src/ttsctrl.c:517-525`, `lex_lookup` `src/tpstr.c:415`]: a 128-word index at H[13] + 0x80 (one entry per first byte, offsets relative to H[13] + 0x180), then the entries at H[13] + 0x180. Keys lack their first byte, strings are padded to even length, and each first-byte group ends with 0x7F. Examples: German `!` → `'UM#GE0K'E:R`, Spanish `ABR'IR INTERROGATHI'ON`. Danish also has H[2] (§5.2) and the external `darules.dll`.

### 5.8 Versions

| File | Version string |
|---|---|
| amrules.ivx | American English v2.0 880113 "Compiled with NEW op-codes" |
| blrules.ivx | British English v3.1 930930 "LARGE lexicon" |
| darules.ivx | Danish v1.2 940217 (plus `darules.dll`, exporting `norm` at ordinal 1: text normaliser with embedded word/place-name data) |
| durules.ivx | Dutch v1.0 980408 |
| firules.ivx | Finnish v1.0 931214 |
| frrules.ivx | French v2.1 960429 |
| gerules.ivx | German v2.31 980604 "Adapted to MBROLA synthesis" |
| icrules.ivx | Icelandic v1.1 980919 |
| itrules.ivx | Italian v1.1 910508 "NEW op-codes" |
| norules.ivx | Norwegian v2.2 940221 |
| sprules.ivx | Castilian Spanish v1.0 880517 "NEW op-codes" |
| swrules.ivx | Swedish v3.2 981118 "extended lexicon" |

The same instruction set decodes every file, including the "NEW op-codes" ones.

---

## 6. Cross-reference summary

| Paper claim | DLL evidence | Status |
|---|---|---|
| OVE III cascade formant synth, simple glottal pulse (Carlson '90) | Impulse source, F1–F4 + FH cascade, nasal + fricative parallel branches, 200 Hz one-pole | **CONFIRMED** |
| Parameter names F1..F4 B1..B4 AH AN AC AK A0 K1 C1 K2 C2 FN BN F0 (GLOVE diagram) | Name tables at 0x10048904 and 0x10045374 | **CONFIRMED** |
| 10 ms parameter frames (Barber '89 Fig. 2; Carlson '90) | 100 frames/s at the default 150 wpm | **CONFIRMED** |
| GLOVE higher poles F6 = 1.2·F5, F7 = 1.4·F5 | Instead: 3 poles at the same FH with fixed ΔB (320/1570/3320 Hz) | **DIFFERENT** (older OVE III scheme) |
| LF source (RK/RG/EE/FA), NA/NM noise, DI | The synth map names `RG RK FA AZ NA NM DI FZ BZ FP BP` (entries 21–31, `0x10049518`), but no frame or sample function reads them | **NOT FOUND** (names only) |
| RULSYS modular pipeline DIG/LEX/GRAF/FON | Generic bytecode interpreter plus per-language `.ivx` files (6 rule modules, phoneme inventory, lexicon) and an optional language DLL (`norm`). All 15,564 rules decode. | **CONFIRMED** (module↔stage names are HYPOTHESIS) |
| KIM German prosody (downstep 6 %/18 %, 130/95 Hz, cosine interpolation, microprosody) | `gerules.ivx` M2: 6 % downstep (×0.060 per peak), 95 Hz floor, ×1.079 close-vowel summit, 130 Hz start, −10 Hz microprosody | **CONFIRMED in .ivx bytecode.** Cosine interpolation is CONFIRMED in the DLL (`op_t0b_cosine`). 18 % and +15 Hz are not yet located. |
| KIM phonetic notation (`'` `"` stress, upper-case phonemes, `#…#` phonetic input) | Seen in `.ivx` lexicon strings and `tpchar.c` tables | **CONFIRMED** |
| Fujisaki F0 model (Swedish) | No exp/log outside synth init. Swedish M2 uses Hz target points instead. | **NOT FOUND** |
| Speech rate via rule blocks (KIM) | The SAPI rate path is uniform frame-rate scaling | **DIFFERENT** (the rule-level rate digit may still exist in the rules) |
