# infovox230: user and exception dictionaries

Date: 2026-09-29. Status: approved. Revised while planning (same day): the key rule, since which characters belong to a word depends on the language, and the SAPI engine's use of the exception callback.

## Goal

Let callers of the `infovox230` library fix pronunciations: replace a word with other text or with phoneme names, or give a respelling for words the engine's built-in lexicon doesn't know. The engine already has both hooks, the user-dictionary callback (`tts_ctrl` 0x14) and the exception-dictionary callback (`tts_ctrl` 0x15), and the port runs them bit-exact against the DLL (`tests/tp_cmp.c` registers both). This work is library API and `ivxsay` only. `src/` does not change.

## What the engine does (facts this design rests on)

Both callbacks are called from `tp_word` (`0x1001f170`, `src/tpstr.c`), once per word.

- **What a word is.** A run of the characters the language counts as letters. Which those are depends on the language and the charset [CONFIRMED, probe on the port: every byte 0x21–0xFF between letters, 12 languages, charsets 0–3]:
  - A–Z and a–z always, and `@` in every language.
  - The apostrophe in every language but French, which splits "l'homme" into "l" + "homme".
  - Some ASCII punctuation in some languages, as letters of their 7-bit RULSYS alphabets: `[ \ ] { | } ~` in German, `[ \ ] { |` and `"` in Norwegian, `:` and `~` in Spanish, `"` and `/` in Italian, `` ` `` in Finnish and Icelandic.
  - Bytes ≥ 0x80 by charset. With the engine's charset 2: 0xC0–0xFF except Ã Õ ã õ × ÷, plus ´ (0xB4). German and French also split at Ø ø, and French at ß and ´; French keeps Œ œ, and Icelandic ª º. So under charset 2, Š š Ž ž Ÿ split a word ("Škoda" arrives as "koda"). Charsets 0 and 1 differ again.
  - Digits, blanks, control characters and `! # $ % & ( ) * + , - . ; < = > ? ^ _` split words in every language and charset: "e-mail" arrives as "e" + "mail", "R2D2" as "R", "D".
- **The user callback** `cb_userdict(arg, word, &out)` is called first, before the built-in lexicon, with the word as it stands in the text buffer: cp1252 bytes in the original case [CONFIRMED, `tpstr.c:171`].
  - It returns 0 for no entry.
  - It returns 1 to replace the word in the text buffer with the text at `*out`, which the engine then reads again from the word's start.
  - It returns 2 to replace the word with `*out` between two `esc_phon` characters, i.e. as phonetic input.

  If the replacement does not fit (`text_end + k >= text_limit`, `text_limit = text + 0x2a0`), the word fails with status 0x71 (`TP_NO_ROOM`), which the library reports as `IVX_EV_ERROR` [CONFIRMED].
- **The engine calls it with `from == tok_start`**, so the word's position in the text buffer is `Core.tok_start` [CONFIRMED, the three call sites `tpchar.c:242,270,310`].
- **A self-including text entry does not hang.** "cat" → "cat dog" expands 167 times until the text buffer is full, then gives 0x71 twice and a truncated sentence [CONFIRMED, probe on the port; the port is bit-exact with the DLL on this path].
- **The exception callback** `cb_excdict(arg, key, 0)` is called only after the built-in lexicon has no entry, and only when the word is not split as a compound (a compound goes straight to `ctl_word_end`) [CONFIRMED, `tpstr.c:232-245`]. It is called in the same `tp_word` call as the user callback for that word.
  - Its key is a space, the word's segment names, then a space. The names are the word as the engine spells it internally: transliterated to the 7-bit RULSYS alphabet and upper-cased. Examples are `" KALLE "`, `" F]GLAR "` (fåglar), `" STRA~E "` (Straße) and `" 'T'OR'DUR "` (Þórður) [CONFIRMED, probe on the port].
  - Its reply is a **respelling** in that same alphabet, not a transcription. The engine turns it into segments the way it turns a spelling into segments (`tp_text_to_segs(c, 1, …)`) and runs M0, the letter-to-sound rules, on it. The reply is not looked up in the lexicon. A reply equal to the word's own key gives audio identical to the word as text, in 6 of 6 cases in 4 languages. A phonetic reply sounds wrong in Swedish and English [CONFIRMED, probe on the port].
  - **Installing it has a cost even when it returns NULL.** Before the call, the engine writes the key into the word buffer it later uses to rebuild the word when M0 runs out of nodes (`tpstr.c:247,268`). The retry then builds the word from the first *n* bytes of the key, *n* being the transliterated word's length. With one-char names, that is the leading space and all but the last letter. This happens only for a word that reached the exception callback and then ran out of nodes, and it is the DLL's own behaviour [CONFIRMED, `tpstr.c:245-270`].
  - A bind that gets past its checks (a non-NULL body without an "XX" tag) clears it (`ivx_bind`,
    `ttsctrl.c:478`); `tts_ctrl` create clears both callbacks [CONFIRMED].
- **The SAPI engine** backed the user callback with its lexicon object, returning 1 (text) on a hit,
  else 0, and never 2 (`0x10010e40`) [CONFIRMED]. It set the exception callback to the voice's
  `LibraryFile` function when there is one, and to 0 otherwise (`0x10010330`–`0x10010354`) [LIKELY]:
  - 0x10016cc0 loads the `LibraryFile` DLL and stores `GetProcAddress(h, 1)` at +0x110 of a 0x114-byte object (`new` at 0x100174fa, constructor 0x10016c70).
  - 0x10016d40 returns that field, and 0x10010334 calls it on `[edi+4]` and passes the result to `tts_ctrl` 0x15.
  - For Danish that function is `darules.dll!norm`, so the Danish text normaliser is an exception dictionary. The hook that `tts_ctrl` 0x18 sets is the engine's own `0x10011400` (0x100105c2 sets 0, 0x10010e0c sets 0x10011400), not `norm`.

## Public API (`include/infovox230.h`)

```c
/* flags for ivx_dict_add / ivx_dict_remove */
enum {
    IVX_DICT_TEXT       = 0,   /* replacement is text, spoken by the normal rules          */
    IVX_DICT_PHONETIC   = 1,   /* replacement is phoneme names, as for ivx_feed_phonetic   */
    IVX_DICT_FALLBACK   = 2,   /* only for words the built-in lexicon doesn't know;
                                  replacement is a respelling (text only, one word)        */
    IVX_DICT_MATCH_CASE = 4    /* match the word's exact case (default: case-insensitive)  */
};
IVX_API int  IVX_CALL ivx_dict_add(IvxEngine *e, const char *word, const char *replacement, int flags);
IVX_API int  IVX_CALL ivx_dict_remove(IvxEngine *e, const char *word, int flags);
IVX_API void IVX_CALL ivx_dict_clear(IvxEngine *e);
```

`IVX_VERSION` becomes `0x00010001` (1.1).

### Keys and replacements

- The key and a text or fallback replacement are in the engine's current input encoding (`IVX_INPUT_ENCODING`: UTF-8 by default, or cp1252), converted to cp1252 when added. A phonetic replacement is raw bytes, as for `ivx_feed_phonetic`.
- **Key:** 1–127 cp1252 bytes after conversion, none of them a blank, a control character (< 0x20, 0x7F), a digit or one of `! # $ % & ( ) * + , - . ; < = > ? ^ _`. These split words in every language, so a key containing one ("e-mail", "R2D2", "two words") is rejected rather than stored.
  - Every other byte is allowed, because whether it belongs to a word depends on the loaded language and charset (see the engine facts above). A key the loaded language would split, such as "don't" in French or "Škoda" under charset 2, is stored but never matches.
- **TEXT replacement:** 1–255 bytes after conversion. Any text is allowed: several words, numbers, punctuation. Control characters become spaces.
- **PHONETIC replacement:** 1–255 bytes of phoneme names, with no blank, control character (< 0x20) or the escape characters 0x01, 0x02, 0x17. The engine's phonetic input ends at a blank, so a replacement is one phonetic word.
- **FALLBACK replacement:** 1–127 bytes after conversion under the same rule as keys, since it is one respelled word. `IVX_DICT_FALLBACK | IVX_DICT_PHONETIC` is rejected.

### Entries, identity and precedence

- An entry's identity is its key plus its FALLBACK and MATCH_CASE bits. For case-insensitive entries the key is case-folded, and for exact entries it is the exact bytes. So an exact `US` and a case-insensitive `us` are two entries. Adding an existing identity replaces its replacement and its PHONETIC bit. `ivx_dict_remove` takes the same bits (PHONETIC is ignored).
- **Case folding:** cp1252 lower to upper for a–z, 0xE0–0xFE except 0xF7, š→Š, œ→Œ, ž→Ž and ÿ→Ÿ. ß (0xDF) has no upper case and folds to itself.
- **Precedence for each word:**
  1. exact-case override
  2. case-insensitive override
  3. the engine's built-in lexicon (and its compound split)
  4. exact-case fallback
  5. case-insensitive fallback
  6. the engine's letter-to-sound rules
- **A replacement is final.** The words a TEXT replacement puts into the text are not looked up in the dictionary again, neither as overrides nor as fallbacks. So "cat → cat dog" says "cat dog", and with a → b and b → c, "a" says "b".

### Timing and lifetime

- `ivx_dict_*` may be called at any time the engine is not inside a call (the header's one-thread-per-engine rule). A change applies to every word the engine has not yet processed, including text already fed but not yet read.
- The dictionary belongs to the engine. It starts empty, survives `ivx_load_language`/`ivx_load_language_mem` (whether they succeed or fail) and `ivx_cancel`, and is freed by `ivx_destroy`.

### Return values

| Call | Returns |
|---|---|
| `ivx_dict_add` | `IVX_OK`; `IVX_E_ARG` (NULL engine, word or replacement); `IVX_E_RANGE` (a key, replacement or flag combination outside the rules above; unknown flag bits); `IVX_E_NOMEM` |
| `ivx_dict_remove` | `IVX_OK`; `IVX_E_ARG`; `IVX_E_RANGE` (no such entry, or an invalid key or flag bits) |
| `ivx_dict_clear` | nothing; NULL is allowed |

A replacement that does not fit the engine's text buffer when used gives `IVX_EV_ERROR` with value 0x71, and the utterance carries on (the engine's behaviour).

## Library internals (`lib/infovox230.c`)

### Callbacks, installed only when needed

- With no entries, no dictionary callback is installed, so the audio stays byte-identical to the library without dictionaries (and `tools/check_ivxsay.py` keeps applying).
- With at least one entry, `cb_userdict` is installed.
- With at least one FALLBACK entry, `cb_excdict` is installed too. The header documents that FALLBACK entries switch on the engine's exception path with its out-of-nodes retry quirk (see the engine facts above).
- `ivx_dict_add`, `ivx_dict_remove` and `ivx_dict_clear` install or remove callbacks as the counts change.
- `ivx_load_language_mem` reinstalls `cb_excdict` after every bind attempt: a bind that gets past its checks clears the exception callback, and reinstalling it is harmless when it didn't.

### The user callback

1. It saves the word (cp1252, original case, up to 127 bytes) as the engine's *current word*, with a flag that the current word is valid and one that it is final.
2. If `Core.tok_start` lies inside a live replaced span (below), it marks the current word final and returns 0.
3. It looks up an exact-case override, then a case-insensitive one.
   - TEXT: it copies the replacement into the engine's reply buffer, records a span `[tok_start − text, tok_start − text + len)` with a copy of the replacement's bytes, sets `*out`, and returns 1.
   - PHONETIC: it copies the replacement to the reply buffer, sets `*out`, and returns 2. Phonetic input is never looked up, so no span is needed.
4. Otherwise it returns 0.

### The exception callback

1. It returns NULL if there is no valid current word or the current word is final.
2. It returns NULL unless the key equals `" " + transliterate(current word) + " "`. This is a safety check that the call belongs to the saved word.
3. It looks up an exact-case fallback, then a case-insensitive one, transliterates the respelling into the reply buffer, and returns it. Otherwise it returns NULL.

The current word is invalidated after the exception callback, and by cancel.

`transliterate` copies `tp_word`'s transliteration loop (`tpstr.c:201-230`):

- bytes ≥ 0x80 go through `TP_CHARSET1`/`TP_CHARSET2` by `Core.charset`, then through `tp_xlat_char` (one or two chars; 0 drops the char);
- then `'a'..'}'` are upper-cased with `& 0x5f`.

The result is limited to 127 bytes. A longer one means no match, or no reply. Because a respelling is transliterated when used, fallback entries follow whichever language is loaded.

### Replaced spans

A small list of spans, each an offset range in `Core.text` plus a copy of the replacement bytes, kept valid as the engine moves its text:

- **Compaction.** Before each `tp_putc`, if `Core.text_consumed` is set, the engine will move `[text_done, text_end)` to `Core.text` (`tpchar.c:86-105`). Every span shifts by `text_done − text`, and spans whose end falls at or before that point are dropped.
- **Emptying or cutting back.** After each `tp_putc`, spans that end beyond `text_end − text` are dropped.
- **Content check.** At lookup a span counts only while `memcmp` finds its bytes still at its offset.
- **Restarts** that re-read the sentence from `Core.text` (`contwork.c:83`, out of nodes) keep the spans, so the replacement's words stay final.
- `ivx_cancel` clears all spans.

### Storage

- A hash table keyed on the case-folded cp1252 key. Each entry holds the exact key, the flags and the replacement (cp1252 for TEXT and FALLBACK, raw for PHONETIC), copied when added.
- Counts of all entries and of FALLBACK entries drive the callback installation.
- One reply buffer per engine (256 bytes) holds what a callback returns. The engine copies or consumes it before the next callback, so it can be reused.

## `ivxsay -d FILE`

- `-d` can be given more than once, and the files load in order. A file is read in the `-e` encoding (UTF-8 by default).

  ```
  # comment; blank lines are ignored
  USA      = U S A
  tomato   = /T'OMA:TOU/        # phonetic: between slashes
  ?xyzzy   = ZIZZY              # fallback: ? before the word
  !US      = United States      # exact case: ! before the word (!? combines)
  ```

- Each line is `[!?]word = replacement`. Blanks around `=` and at the line ends are trimmed. A `#` begins a comment, except inside a phonetic `/…/` replacement.
- A replacement wholly between slashes is phonetic.
- A malformed line, or one that `ivx_dict_add` rejects, prints `file:line: <message>` to stderr and exits 1.
- The parser lives in `ivxsay`. The library keeps only the three calls.

## Testing

**`api_test`, a new `test_dict`:** no DLL, runs unchanged on Linux. Audio comparisons are sample for sample against input with the replacement already made, in the same sentence context.

1. **Text:** `USA` → `U S A` sounds the same as "U S A", in Swedish and English. A word the lexicon knows ("och", "the") is overridden.
2. **Phonetic:** `hej` → `H[J` sounds the same as `ivx_feed_phonetic("H[J")` in the same sentence.
3. **Case:**
   - a case-insensitive entry matches USA, Usa and usa;
   - a `MATCH_CASE` `US` leaves "us" alone;
   - an exact entry beats a case-insensitive one;
   - folding works for å/Å (Swedish) and œ/Œ (French, which keeps them in words);
   - a UTF-8 key and the same key in cp1252 mode behave the same.
4. **Fallback:**
   - an unknown word with a respelling sounds the same as the respelling as text;
   - a word the lexicon knows is unchanged by a fallback entry;
   - a Swedish respelling with å is transliterated correctly;
   - a `MATCH_CASE` fallback matches only its case.
5. **Finality:**
   - "cat → cat dog" sounds the same as "cat dog", with no error events;
   - with a → b and b → c, "a" says "b".
6. **Spans across compaction:** a multi-sentence text long enough to compact the engine's buffer several times, with many replacements, sounds the same as the text with the replacements already made.
7. **Limits:**
   - a replacement that cannot fit gives `IVX_EV_ERROR` 0x71 and then DONE;
   - every `IVX_E_RANGE` and `IVX_E_ARG` case in the table above;
   - removing a missing entry gives `IVX_E_RANGE`.
8. **Lifetime:**
   - entries (a fallback included) survive a language switch, a failed load and `ivx_cancel`;
   - an entry added after `ivx_feed` but before `ivx_read` applies.
9. **No dictionary:** after `ivx_dict_clear`, the audio is byte-identical to an engine that never had entries.
10. **Robustness:** the existing random-byte runs repeat with a dictionary of common words installed, overrides and fallbacks both. No crash or hang, and DONE arrives.

**`tools/check_ivxsay.py`:** one added case. `ivxsay -d` with a small dictionary file writes the same WAV as `ivxsay` given the text with the replacements already made.

## Documentation

- **`include/infovox230.h`:** the flags and functions, the precedence, the final-replacement rule and the FALLBACK retry note.
- **This spec,** plus a pointer from `2026-09-28-infovox230-library-api-design.md`.
- **`REFERENCE.md`:** §5.5.1 gets the engine facts above with their tags and addresses. §2.2.2 describes the library's dictionary.
- **`AGENTS.md`:** dictionaries move from open to done. The `lib`, `cli/ivxsay.c` and `tests/` (`api_test`) rows are updated.
- **`tools/ghidra/symbols.tsv`:** the existing label `eng_cb_userdict` (`0x10010e40`) goes from [LIKELY] to [CONFIRMED], and its comment gets the behaviour (lexicon lookup, returns 1 on a hit else 0, never 2). The `LibraryFile` function as the exception callback (`0x10016d40`, `0x10010344`) is noted, and the wrong "`LibraryFile` norm hook" comment on `tts_c18_hook` is corrected, in `symbols.tsv`, `REFERENCE.md` (§2.2 and the `cw_run` row) and `AGENTS.md`. Labels only, as for the rest of SAPI; re-run `IvxApply.java`.

## Out of scope

- The Danish `LibraryFile` hook (`darules.dll!norm`), a separate open item. The SAPI engine installs it as the exception callback (see the engine facts), so when it is added it shares `cb_excdict` with FALLBACK entries: fallback entries first, then `norm`.
- A raw-callback API. It can come later if a caller needs dynamic lookups.
- Dictionary file loading in the library itself (`ivxsay` has the parser).
