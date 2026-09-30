# infovox230 Dictionaries Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Let callers of the `infovox230` library fix pronunciations with `ivx_dict_add`/`ivx_dict_remove`/`ivx_dict_clear` (text, phonetic and fallback entries), and expose it in `ivxsay -d FILE`.

**Architecture:** A new `lib/ivxdict.c` holds the entry table (a hash on the case-folded cp1252 key), the engine's two dictionary callbacks, and a list of "spans" that keeps the words of a text replacement from being looked up again. `lib/infovox230.c` converts the caller's text to cp1252, installs the callbacks through `tts_ctrl` 0x14/0x15 only while entries need them, and calls the span bookkeeping around every `tp_putc`. `src/` does not change, and with no entries the audio stays byte-identical to the DLL-verified library.

**Tech Stack:** C11, 32-bit gcc 14.2 (winlibs mingw32) on Windows and gcc-multilib on Linux (WSL), GNU make, Python 3 for `tools/check_ivxsay.py`.

**Spec:** `docs/superpowers/specs/2026-09-29-infovox230-dictionaries-design.md` (revised while planning: the key rule and the SAPI engine's use of the exception callback).

## Global Constraints

- Not a git repository: never run git, and make no commits. Each task ends with its tests passing instead of a commit.
- 32-bit toolchain: in Git Bash, run `export PATH=/c/winlibs/mingw32/bin:$PATH` before `make`. Run from the repository root `C:\Users\abart\Desktop\infovox_disasm`.
- WSL (Linux build) is run through the PowerShell tool: `wsl -e bash -lc "cd /mnt/c/Users/abart/Desktop/infovox_disasm && …"`.
- Write C, Python and Markdown with the Write/Edit tools, never with Bash heredocs or `sed`: the Bash tool mangles `\n`, `\0` and `\x` escapes in heredocs, and a heredoc edit once put NUL bytes into `REFERENCE.md`.
- `src/` does not change. Don't modify anything in `bin/` or the papers `docs/*.pdf`.
- C sources and `include/infovox230.h` stay ASCII. Match the surrounding style: 4-space indent, K&R braces, declarations at the top of a block, short lower-case comments.
- With no dictionary entries, the audio must stay byte-identical: `tools/check_ivxsay.py` 12 of 12, and every DLL-differential test in `make test` 0 failures.
- `IVX_VERSION` becomes `0x00010001`.
- Tag every claim added to `REFERENCE.md` or `symbols.tsv` `[CONFIRMED]`/`[LIKELY]`/`[HYPOTHESIS]`/`[NOT FOUND]`, with its VA.
- Names go in `tools/ghidra/symbols.tsv`/`gen_types.py` and are applied with `IvxApply.java`, never by hand in Ghidra.

## Files

| File | Task | Responsibility |
|---|---|---|
| `lib/ivxdict.h` | 1, 2 | Create: `IvxDict`, the entry/callback/span functions |
| `lib/ivxdict.c` | 1, 2 | Create: word rule, case folding, hash table, spans, `dict_user` (1), `dict_exc` and the transliteration (2) |
| `lib/infovox230.c` | 1, 2 | Modify: `U8Dec` decoder split out of the engine, `IvxDict` in `IvxEngine`, hooks around `tp_putc`, cancel/load/destroy, `cb_userdict` (1), `cb_excdict` (2), `ivx_dict_*` |
| `include/infovox230.h` | 1 | Modify: `IVX_VERSION`, the flags, the three calls and their documentation |
| `Makefile` | 1 | Modify: build `lib/ivxdict.c` into the library |
| `tests/api_test.c` | 1, 2 | Modify: the dictionary tests |
| `cli/ivxsay.c` | 3 | Modify: `-d FILE` |
| `tools/check_ivxsay.py` | 3 | Modify: the `-d` case and malformed files |
| `REFERENCE.md`, `AGENTS.md`, `tools/ghidra/symbols.tsv`, `tools/ghidra/gen_types.py`, `docs/superpowers/specs/2026-09-28-infovox230-library-api-design.md` | 3 | Modify: documentation and the `LibraryFile`/0x18 corrections |

---

### Task 1: Dictionary table, text and phonetic entries, and spans

**Files:**
- Create: `lib/ivxdict.h`, `lib/ivxdict.c`
- Modify: `lib/infovox230.c`, `include/infovox230.h`, `Makefile`
- Test: `tests/api_test.c`

**Interfaces:**
- Consumes: `tts_ctrl(core, TTS_CB_USERDICT, fn)` (`src/tts.h`); `Core.text`, `Core.text_end`, `Core.text_done`, `Core.text_consumed`, `Core.tok_start` (`tools/ghidra/ivx_types.h`); `tp_putc` in `step()`.
- Produces (Task 2 relies on these):
  - `include/infovox230.h`: `IVX_DICT_TEXT 0`, `IVX_DICT_PHONETIC 1`, `IVX_DICT_FALLBACK 2`, `IVX_DICT_MATCH_CASE 4`; `int ivx_dict_add(IvxEngine *, const char *word, const char *replacement, int flags)`, `int ivx_dict_remove(IvxEngine *, const char *word, int flags)`, `void ivx_dict_clear(IvxEngine *)`.
  - `lib/ivxdict.h`: `IvxDict` (fields `bucket`, `nbucket`, `count`, `nfallback`, `span`, `nspan`, `shift`, `reply`), `DICT_WORD 127`, `DICT_REP 255`, `DICT_SPANS 64`, and `dict_add`, `dict_remove`, `dict_clear`, `dict_is_word`, `dict_user`, `dict_before_putc`, `dict_after_putc`, `dict_reset`.
  - `lib/ivxdict.c`: `static const DictEntry *lookup(const IvxDict *, const unsigned char *w, size_t n, int fallback)` (`fallback` is 0 or `IVX_DICT_FALLBACK`).
  - `lib/infovox230.c`: `static void dict_install(IvxEngine *e)`, `static int dict_text(...)`, `static int dict_word(...)`.
  - `tests/api_test.c`: `DictDef`, `dict_defs`, `render_dict`, `dict_same`, the `AM` sentence.
- FALLBACK entries are validated and stored in this task but have no effect until Task 2 installs the exception callback.

- [ ] **Step 1: Write the failing tests**

In `tests/api_test.c`, insert this block after the end of `test_robust()` (just before `int main(`):

```c
/* ---- dictionaries ---- */

static const char AM[] = "This is a test of the speech synthesizer. The time is 12.30 and the weather is fine.";

typedef struct {
    const char *word, *rep;
    int flags;
} DictDef;

/* add entries to an engine */
static void dict_defs(IvxEngine *e, const DictDef *d, int nd)
{
    int k;

    for (k = 0; k < nd; k++)
        CHECK(ivx_dict_add(e, d[k].word, d[k].rep, d[k].flags) == 0, "ivx_dict_add \"%s\" \"%s\" %d",
              d[k].word, d[k].rep, d[k].flags);
}

/* all events; text is fed whole */
static void render_dict(const char *code, int enc, const DictDef *d, int nd, const char *text, Out *o)
{
    IvxEngine *e = engine(code, enc);

    memset(o, 0, sizeof *o);
    CHECK(ivx_set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0, "event mask");
    dict_defs(e, d, nd);
    CHECK(ivx_feed(e, text, strlen(text)) == 0 && ivx_flush(e) == 0, "feed");
    drain(e, 441, o);
    ivx_destroy(e);
}

/* text with the entries sounds exactly like expect without them (same events too), and
 * with differs = 1, unlike text without them */
static void dict_same(const char *code, int enc, const DictDef *d, int nd, const char *text,
                      const char *expect, int differs, const char *what)
{
    Out a, b, c;

    render_dict(code, enc, d, nd, text, &a);
    render_dict(code, enc, NULL, 0, expect, &b);
    CHECK(a.n > 0 && same_audio(&a, &b) && same_events(&a, &b), "%s: %ld samples, expected %ld", what,
          a.n, b.n);
    CHECK(count_events(&a, IVX_EV_ERROR) == 0 && count_events(&a, IVX_EV_DONE) == 1, "%s: %ld errors",
          what, count_events(&a, IVX_EV_ERROR));
    if (differs) {
        render_dict(code, enc, NULL, 0, text, &c);
        CHECK(!same_audio(&a, &c), "%s: the entry changed nothing", what);
        out_free(&c);
    }
    out_free(&a);
    out_free(&b);
}

static void test_dict_text(void)
{
    static const DictDef usa[] = {{"USA", "U S A", IVX_DICT_TEXT}};
    static const DictDef och[] = {{"och", "eller", IVX_DICT_TEXT}};
    static const DictDef the[] = {{"the", "a", IVX_DICT_TEXT}};

    dict_same("sw", IVX_ENC_CP1252, usa, 1, "Jag bor i USA nu.", "Jag bor i U S A nu.", 1, "sw USA");
    dict_same("am", IVX_ENC_CP1252, usa, 1, "I live in the USA now.", "I live in the U S A now.", 1, "am USA");
    dict_same("sw", IVX_ENC_CP1252, och, 1, "Kalle och Lisa kommer.", "Kalle eller Lisa kommer.", 1, "sw och");
    dict_same("am", IVX_ENC_CP1252, the, 1, AM,
              "This is a test of a speech synthesizer. a time is 12.30 and a weather is fine.", 1, "am the");
}

static void test_dict_phonetic(void)
{
    static const DictDef hej[] = {{"hej", PHON_SYM, IVX_DICT_PHONETIC}};
    IvxEngine *e;
    Out a, b;

    render_dict("sw", IVX_ENC_CP1252, hej, 1, "Jag sa hej till dig.", &a);
    e = engine("sw", IVX_ENC_CP1252);
    memset(&b, 0, sizeof b);
    CHECK(ivx_set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL) == 0, "event mask");
    CHECK(ivx_feed(e, "Jag sa ", 7) == 0 && ivx_feed_phonetic(e, PHON_SYM, strlen(PHON_SYM)) == 0
          && ivx_feed(e, " till dig.", 10) == 0 && ivx_flush(e) == 0, "phonetic feed");
    drain(e, 441, &b);
    ivx_destroy(e);
    CHECK(a.n > 0 && same_audio(&a, &b) && same_events(&a, &b), "phonetic entry: %ld samples, expected %ld",
          a.n, b.n);
    out_free(&a);
    out_free(&b);
}

static void test_dict_case(void)
{
    static const DictDef usa[] = {{"usa", "U S A", IVX_DICT_TEXT}};
    static const DictDef us[] = {{"US", "United States", IVX_DICT_MATCH_CASE}};
    static const DictDef both[] = {{"us", "you", IVX_DICT_TEXT}, {"US", "United States", IVX_DICT_MATCH_CASE}};
    static const DictDef aa[] = {{"\xe5sa", "Lisa", IVX_DICT_TEXT}};                 /* "asa", a-ring */
    static const DictDef oe[] = {{"\x9cuvre", "chose", IVX_DICT_TEXT}};             /* "oeuvre", oe ligature */
    static const DictDef u8[] = {{"h\xc3\xa4r", "d\xc3\xa4r", IVX_DICT_TEXT}};      /* "har" -> "dar", UTF-8 */
    static const DictDef cp[] = {{"h\xe4r", "d\xe4r", IVX_DICT_TEXT}};              /* the same in cp1252 */
    Out a, b;

    dict_same("am", IVX_ENC_CP1252, usa, 1, "USA and Usa and usa.", "U S A and U S A and U S A.", 1,
              "case-insensitive");
    dict_same("am", IVX_ENC_CP1252, us, 1, "The US and us.", "The United States and us.", 1, "exact case");
    dict_same("am", IVX_ENC_CP1252, both, 2, "The US and us and Us.", "The United States and you and you.", 1,
              "exact before case-insensitive");
    dict_same("sw", IVX_ENC_CP1252, aa, 1, "Hej \xc5SA och \xc5sa.", "Hej Lisa och Lisa.", 1, "fold a-ring");
    /* French keeps the oe ligature (0x8c 0x9c) inside a word */
    dict_same("fr", IVX_ENC_CP1252, oe, 1, "Une \x8cuvre et une \x9cuvre.", "Une chose et une chose.", 1,
              "fold oe");
    render_dict("sw", IVX_ENC_UTF8, u8, 1, SW_UTF8, &a);
    render_dict("sw", IVX_ENC_CP1252, cp, 1, SW, &b);
    CHECK(a.n > 0 && same_audio(&a, &b) && same_events(&a, &b), "UTF-8 entry differs from cp1252");
    out_free(&a);
    out_free(&b);
}

static void test_dict_final(void)
{
    static const DictDef cat[] = {{"cat", "cat dog", IVX_DICT_TEXT}};
    static const DictDef chain[] = {{"one", "two", IVX_DICT_TEXT}, {"two", "three", IVX_DICT_TEXT}};
    static const DictDef stop[] = {{"cat", "cat. The dog", IVX_DICT_TEXT}, {"dog", "puppy", IVX_DICT_TEXT}};
    static const char S1[] = "The cat sat on the mat with another cat and a big cat. ";
    static const char S2[] = "The cat dog sat on the mat with another cat dog and a big cat dog. ";
    static char text[4000], expect[6000];
    int k;

    dict_same("am", IVX_ENC_CP1252, cat, 1, "I see a cat now.", "I see a cat dog now.", 1, "cat -> cat dog");
    dict_same("am", IVX_ENC_CP1252, chain, 2, "I have one and two.", "I have two and three.", 1,
              "one -> two -> three");
    /* many sentences: the engine's text buffer is compacted many times, and the spans must
     * follow their text or "cat dog" would grow again */
    text[0] = expect[0] = 0;
    for (k = 0; k < 40; k++) {
        strcat(text, S1);
        strcat(expect, S2);
    }
    dict_same("am", IVX_ENC_CP1252, cat, 1, text, expect, 1, "spans across compaction");
    /* a replacement that ends a sentence: the engine compacts its buffer while the rest of
     * the replacement is unread, and "dog" must stay final */
    dict_same("am", IVX_ENC_CP1252, stop, 2, "I see a cat now. The dog is here.",
              "I see a cat. The dog now. The puppy is here.", 1, "span cut by compaction");
}

static void test_dict_limits(void)
{
    static char big[257], word[130], u8word[260];
    DictDef d[1];
    IvxEngine *e = engine("sw", IVX_ENC_CP1252);
    Out o;
    int k;

    /* three 255-byte replacements (one word and blanks, few rule nodes) in one sentence
     * overflow the engine's text buffer (0x2a0 bytes) */
    memset(big, ' ', 255);
    big[0] = 'l';
    big[1] = 'a';
    d[0].word = "x";
    d[0].rep = big;
    d[0].flags = IVX_DICT_TEXT;
    render_dict("sw", IVX_ENC_CP1252, d, 1, "Ett x x x.", &o);
    for (k = 0; k < o.nev && !(o.ev[k].type == IVX_EV_ERROR && o.ev[k].value == 0x71); k++)
        ;
    CHECK(k < o.nev, "a replacement that does not fit: no error 0x71");
    CHECK(count_events(&o, IVX_EV_DONE) == 1 && o.ev[o.nev - 1].type == IVX_EV_DONE, "no fit: DONE");
    out_free(&o);

    CHECK(ivx_dict_add(NULL, "a", "b", 0) == IVX_E_ARG, "add NULL engine");
    CHECK(ivx_dict_add(e, NULL, "b", 0) == IVX_E_ARG, "add NULL word");
    CHECK(ivx_dict_add(e, "a", NULL, 0) == IVX_E_ARG, "add NULL replacement");
    CHECK(ivx_dict_remove(NULL, "a", 0) == IVX_E_ARG, "remove NULL engine");
    CHECK(ivx_dict_remove(e, NULL, 0) == IVX_E_ARG, "remove NULL word");
    ivx_dict_clear(NULL);
    CHECK(ivx_dict_add(e, "", "b", 0) == IVX_E_RANGE, "empty word");
    CHECK(ivx_dict_add(e, "e-mail", "b", 0) == IVX_E_RANGE, "e-mail");
    CHECK(ivx_dict_add(e, "R2D2", "b", 0) == IVX_E_RANGE, "R2D2");
    CHECK(ivx_dict_add(e, "two words", "b", 0) == IVX_E_RANGE, "two words");
    CHECK(ivx_dict_add(e, "a.b", "b", 0) == IVX_E_RANGE, "a.b");
    CHECK(ivx_dict_add(e, "don't", "do not", 0) == 0, "apostrophe");
    CHECK(ivx_dict_add(e, "\x8akoda", "bil", 0) == 0, "0x8a (a letter in some charsets)");
    CHECK(ivx_dict_add(e, "a@b", "b", 0) == 0, "@ (inside a word in every language)");
    memset(word, 'a', 128);
    word[128] = 0;
    CHECK(ivx_dict_add(e, word, "b", 0) == IVX_E_RANGE, "128 letters");
    word[127] = 0;
    CHECK(ivx_dict_add(e, word, "b", 0) == 0, "127 letters");
    CHECK(ivx_dict_add(e, "a", "", 0) == IVX_E_RANGE, "empty replacement");
    CHECK(ivx_dict_add(e, "a", big, 0) == 0, "255-byte replacement");
    big[255] = 'a';
    CHECK(ivx_dict_add(e, "a", big, 0) == IVX_E_RANGE, "256-byte replacement");
    CHECK(ivx_dict_add(e, "a", "H[J", IVX_DICT_PHONETIC) == 0, "phonetic");
    CHECK(ivx_dict_add(e, "a", "H[J H[J", IVX_DICT_PHONETIC) == IVX_E_RANGE, "phonetic blank");
    CHECK(ivx_dict_add(e, "a", "H\x01J", IVX_DICT_PHONETIC) == IVX_E_RANGE, "phonetic escape");
    CHECK(ivx_dict_add(e, "a", "two words", IVX_DICT_FALLBACK) == IVX_E_RANGE, "fallback two words");
    CHECK(ivx_dict_add(e, "a", "b", IVX_DICT_FALLBACK | IVX_DICT_PHONETIC) == IVX_E_RANGE, "fallback phonetic");
    CHECK(ivx_dict_add(e, "a", "b", 8) == IVX_E_RANGE, "unknown flag");
    CHECK(ivx_dict_remove(e, "a", IVX_DICT_MATCH_CASE) == IVX_E_RANGE, "remove: other identity");
    CHECK(ivx_dict_remove(e, "A", IVX_DICT_PHONETIC) == 0, "remove (PHONETIC ignored, case folded)");
    CHECK(ivx_dict_remove(e, "a", 0) == IVX_E_RANGE, "remove twice");
    CHECK(ivx_dict_remove(e, "zzz", 0) == IVX_E_RANGE, "remove missing");
    CHECK(ivx_dict_remove(e, "e-mail", 0) == IVX_E_RANGE, "remove bad word");
    CHECK(ivx_dict_remove(e, "a", 8) == IVX_E_RANGE, "remove unknown flag");
    ivx_destroy(e);
    /* 127 letters after conversion, 254 bytes of UTF-8 */
    e = engine("sw", IVX_ENC_UTF8);
    for (k = 0; k < 127; k++) {
        u8word[2 * k] = (char)0xc3;
        u8word[2 * k + 1] = (char)0xa4;
    }
    u8word[254] = 0;
    CHECK(ivx_dict_add(e, u8word, "b", 0) == 0, "127 UTF-8 letters");
    ivx_destroy(e);
}

static void test_dict_clear(void)
{
    static const DictDef d[] = {{"och", "eller", 0}, {"test", "prov", IVX_DICT_MATCH_CASE}};
    IvxEngine *e;
    Out ref, o;
    int round;

    render("sw", IVX_ENC_CP1252, SW, 0, 441, &ref);
    /* round 0: ivx_dict_clear; round 1: removing the last entry */
    for (round = 0; round < 2; round++) {
        e = engine("sw", IVX_ENC_CP1252);
        dict_defs(e, d, round ? 1 : 2);
        if (round)
            CHECK(ivx_dict_remove(e, "och", 0) == 0, "remove the last entry");
        else
            ivx_dict_clear(e);
        memset(&o, 0, sizeof o);
        speak(e, SW, 0);
        ivx_flush(e);
        drain(e, 441, &o);
        CHECK(same_audio(&ref, &o) && same_events(&ref, &o), "an emptied dictionary changed the audio (%d)",
              round);
        out_free(&o);
        ivx_destroy(e);
    }
    out_free(&ref);
}

/* random bytes (as test_robust) with overrides and phonetic entries installed. sp needs the
 * span shift (dict_before_putc/apply_shift): without it the "o" replacement is read again
 * after a compaction and grows until the sentence after it is cut short */
static void test_dict_robust(void)
{
    static const DictDef d[] = {
        {"och", "eller", 0}, {"the", "a", 0}, {"und", "oder", 0}, {"de", "la", 0}, {"e", "et", 0},
        {"A", "H[J", IVX_DICT_PHONETIC | IVX_DICT_MATCH_CASE}, {"o", "o o o o o o o o", 0}};
    static char buf[20000];
    IvxEngine *e;
    Out o;
    size_t i;
    int k;

    for (k = 0; k < 12; k++) {
        lcg = (uint32_t)k * 31u + 7u;
        for (i = 0; i < sizeof buf; i++)
            buf[i] = (char)rnd8();
        e = engine(codes[k], IVX_ENC_CP1252);
        dict_defs(e, d, (int)(sizeof d / sizeof d[0]));
        memset(&o, 0, sizeof o);
        CHECK(ivx_feed(e, buf, sizeof buf) == 0 && ivx_flush(e) == 0, "%s: feed", codes[k]);
        drain(e, 4096, &o);
        CHECK(count_events(&o, IVX_EV_DONE) == 1, "%s dictionary, random bytes: %ld DONE", codes[k],
              count_events(&o, IVX_EV_DONE));
        o.base += (uint64_t)o.n;
        o.n = 0;
        o.nev = 0;
        speak(e, k == 11 ? SW : AM, 0);   /* am and sp say little of the Swedish sentence */
        ivx_flush(e);
        drain(e, 4096, &o);
        CHECK(o.n > 22050 && count_events(&o, IVX_EV_DONE) == 1, "%s dictionary: no speech after it (%ld samples)",
              codes[k], o.n);
        out_free(&o);
        ivx_destroy(e);
    }
}
```

In `main()`, after `test_robust();`, add:

```c
    test_dict_text();
    test_dict_phonetic();
    test_dict_case();
    test_dict_final();
    test_dict_limits();
    test_dict_clear();
    test_dict_robust();
```

- [ ] **Step 2: Run the build to verify it fails**

Run (Git Bash): `export PATH=/c/winlibs/mingw32/bin:$PATH; make build/api_test.exe`
Expected: FAIL: `implicit declaration of function 'ivx_dict_add'` and `'IVX_DICT_TEXT' undeclared`.

- [ ] **Step 3: Add the API to `include/infovox230.h`**

Change the version line:

```c
#define IVX_VERSION 0x00010001   /* major << 16 | minor */
```

After the `ivx_synthesize` declaration (before `ivx_strerror`), insert:

```c

/* Dictionary: fixes the pronunciation of single words. For each word in the text, in order:
 * an exact-case entry, a case-insensitive entry, the language's built-in lexicon, an
 * exact-case FALLBACK entry, a case-insensitive FALLBACK entry, the letter-to-sound rules.
 *
 * word: one word, 1-127 bytes after conversion to cp1252, without blanks, control chars,
 * digits or !#$%&()*+,-.;<=>?^_ (these split words in every language, so "e-mail", "R2D2"
 * and "two words" are IVX_E_RANGE). Which other characters belong to a word depends on the
 * language and IVX_CHARSET: French splits at the apostrophe, charset 2 at S/Z/Y with caron
 * or diaeresis (0x8A 0x9A 0x8E 0x9E 0x9F). An entry whose word the loaded language would
 * split never matches. Matching ignores case (a-z, 0xE0-0xFE and 0x9A 0x9C 0x9E 0xFF against
 * their capitals) unless IVX_DICT_MATCH_CASE. word and a TEXT or FALLBACK replacement are in
 * the input encoding (IVX_INPUT_ENCODING), converted when added. Replacements:
 *   IVX_DICT_TEXT      1-255 bytes of text, read by the engine in place of the word (control
 *                      chars become blanks). Its words are not looked up again: "cat" ->
 *                      "cat dog" says "cat dog".
 *   IVX_DICT_PHONETIC  1-255 bytes of phoneme names as for ivx_feed_phonetic, one phonetic
 *                      word: no blank or control char (0x01, 0x02 and 0x17 included).
 *   IVX_DICT_FALLBACK  a respelling, one word under the same rule as word, pronounced by the
 *                      letter-to-sound rules (not the lexicon); used only for words the
 *                      lexicon doesn't know and the engine doesn't split as compounds, of at
 *                      most 42 letters as the language spells them internally (the engine's
 *                      exception dictionary, tts_ctrl 0x15). Not with IVX_DICT_PHONETIC.
 *                      While FALLBACK entries exist, a word that runs out of rule nodes is
 *                      retried from its spelling minus its last letter (the engine's own
 *                      behaviour).
 *   IVX_DICT_MATCH_CASE  the entry matches only this exact case.
 * An entry is its word plus the FALLBACK and MATCH_CASE bits; adding one again replaces it.
 * Changes apply at once to every word the engine has not processed yet, including text fed
 * but not read. Entries stay across language loads and ivx_cancel. A replacement too long
 * for the engine's text buffer gives IVX_EV_ERROR 0x71. */
enum {
    IVX_DICT_TEXT       = 0,
    IVX_DICT_PHONETIC   = 1,
    IVX_DICT_FALLBACK   = 2,
    IVX_DICT_MATCH_CASE = 4
};
IVX_API int IVX_CALL ivx_dict_add(IvxEngine *e, const char *word, const char *replacement, int flags);
/* flags: the entry's FALLBACK and MATCH_CASE bits (PHONETIC is ignored); IVX_E_RANGE if
 * there is no such entry */
IVX_API int IVX_CALL ivx_dict_remove(IvxEngine *e, const char *word, int flags);
IVX_API void IVX_CALL ivx_dict_clear(IvxEngine *e);
```

- [ ] **Step 4: Create `lib/ivxdict.h`**

```c
/* The infovox230 dictionary: entries, the engine's two dictionary callbacks and the text
 * replacements' spans. Design: docs/superpowers/specs/2026-09-29-infovox230-dictionaries-design.md
 *
 * Everything here is cp1252; lib/infovox230.c converts the caller's text and installs the
 * callbacks (tts_ctrl 0x14, 0x15) while entries exist.
 */
#ifndef IVXDICT_H
#define IVXDICT_H

#include <stddef.h>
#include "tts.h"

#define DICT_WORD  127   /* longest key or fallback respelling */
#define DICT_REP   255   /* longest text or phonetic replacement */
#define DICT_SPANS 64    /* text replacements tracked in Core.text at once */

typedef struct DictEntry DictEntry;

/* a text replacement in Core.text: its words are not looked up again */
typedef struct {
    int off, len;
    unsigned char rep[DICT_REP];
} DictSpan;

typedef struct {
    DictEntry **bucket;          /* by case-folded key */
    size_t nbucket, count, nfallback;
    DictSpan span[DICT_SPANS];
    int nspan;
    int shift;                   /* compaction tp_putc may do: Core.text_done - Core.text */
    char reply[DICT_REP + 1];    /* what a callback returns */
} IvxDict;

/* word: 1..DICT_WORD bytes passing dict_is_word; rep: checked by the caller
 * (lib/infovox230.c). IVX_OK, IVX_E_NOMEM */
int dict_add(IvxDict *d, const unsigned char *word, size_t wlen, const unsigned char *rep,
             size_t rlen, int flags);
/* IVX_OK, IVX_E_RANGE (no such entry) */
int dict_remove(IvxDict *d, const unsigned char *word, size_t wlen, int flags);
void dict_clear(IvxDict *d);
/* 0 if a byte ends a word in every language: a blank, a control char, a digit or
 * !#$%&()*+,-.;<=>?^_ */
int dict_is_word(const unsigned char *s, size_t n);

/* the user dictionary callback, and the calls around every tp_putc */
int dict_user(IvxDict *d, Core *c, const char *word, char **out);
void dict_before_putc(IvxDict *d, const Core *c);
void dict_after_putc(IvxDict *d, const Core *c);
/* the Core's text was dropped (cancel, language load) */
void dict_reset(IvxDict *d);

#endif
```

- [ ] **Step 5: Create `lib/ivxdict.c`**

```c
/* The infovox230 dictionary (ivxdict.h). The engine side is tp_word (0x1001f170,
 * src/tpstr.c): the user callback gets each word as it stands in Core.text before the
 * lexicon, the exception callback gets " " + the word's symbol names + " " when the lexicon
 * has no entry and the word is not a compound, and takes a respelling.
 */
#include <stdlib.h>
#include <string.h>
#include "infovox230.h"
#include "ivxdict.h"

struct DictEntry {
    DictEntry *next;
    int flags;                   /* IVX_DICT_* */
    size_t klen, rlen;
    unsigned char *key;          /* exact with IVX_DICT_MATCH_CASE, else case-folded */
    unsigned char *rep;
    unsigned char data[];        /* key NUL rep NUL */
};

/* ---- words and case folding ---- */

/* ASCII punctuation that ends a word in every language and charset. The rest, ' " / : @
 * [ \ ] ` { | } ~ and 0x80-0xff, is part of a word in some languages (their 7-bit RULSYS
 * alphabets) or charsets, so whether a key can match depends on the loaded language */
#define DICT_BREAK "!#$%&()*+,-.;<=>?^_"

int dict_is_word(const unsigned char *s, size_t n)
{
    size_t k;
    unsigned char b;

    for (k = 0; k < n; k++) {
        b = s[k];
        if (b <= ' ' || b == 0x7f || (b >= '0' && b <= '9') || (b < 0x80 && strchr(DICT_BREAK, b) != NULL))
            return 0;
    }
    return 1;
}

static unsigned char fold(unsigned char b)
{
    if ((b >= 'a' && b <= 'z') || (b >= 0xe0 && b <= 0xfe && b != 0xf7))
        return (unsigned char)(b - 0x20);
    if (b == 0x9a || b == 0x9c || b == 0x9e)
        return (unsigned char)(b - 0x10);
    if (b == 0xff)
        return 0x9f;
    return b;
}

static void fold_copy(unsigned char *d, const unsigned char *s, size_t n)
{
    size_t k;

    for (k = 0; k < n; k++)
        d[k] = fold(s[k]);
    d[n] = 0;
}

/* FNV-1a of the folded key */
static size_t hash(const unsigned char *s, size_t n)
{
    uint32_t h = 2166136261u;
    size_t k;

    for (k = 0; k < n; k++)
        h = (h ^ fold(s[k])) * 16777619u;
    return h;
}

/* ---- the table ---- */

static int grow(IvxDict *d)
{
    size_t n = d->nbucket ? 2 * d->nbucket : 64, k;
    DictEntry **b = calloc(n, sizeof *b), *x, *next;

    if (b == NULL)
        return IVX_E_NOMEM;
    for (k = 0; k < d->nbucket; k++)
        for (x = d->bucket[k]; x != NULL; x = next) {
            next = x->next;
            x->next = b[hash(x->key, x->klen) & (n - 1)];
            b[hash(x->key, x->klen) & (n - 1)] = x;
        }
    free(d->bucket);
    d->bucket = b;
    d->nbucket = n;
    return IVX_OK;
}

/* the link to the entry with this identity (key in stored form, the FALLBACK and
 * MATCH_CASE bits), or to the NULL at the end of its bucket */
static DictEntry **link_of(IvxDict *d, const unsigned char *key, size_t n, int id)
{
    DictEntry **p = &d->bucket[hash(key, n) & (d->nbucket - 1)];

    while (*p != NULL && !(((*p)->flags & (IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE)) == id
                           && (*p)->klen == n && memcmp((*p)->key, key, n) == 0))
        p = &(*p)->next;
    return p;
}

int dict_add(IvxDict *d, const unsigned char *word, size_t wlen, const unsigned char *rep,
             size_t rlen, int flags)
{
    unsigned char f[DICT_WORD + 1];
    const unsigned char *key = word;
    int id = flags & (IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE);
    DictEntry **p, *x;

    if (d->count >= d->nbucket && grow(d) != IVX_OK)
        return IVX_E_NOMEM;
    if (!(flags & IVX_DICT_MATCH_CASE)) {
        fold_copy(f, word, wlen);
        key = f;
    }
    x = malloc(sizeof *x + wlen + rlen + 2);
    if (x == NULL)
        return IVX_E_NOMEM;
    x->flags = flags;
    x->klen = wlen;
    x->rlen = rlen;
    x->key = x->data;
    x->rep = x->data + wlen + 1;
    memcpy(x->key, key, wlen);
    x->key[wlen] = 0;
    memcpy(x->rep, rep, rlen);
    x->rep[rlen] = 0;
    p = link_of(d, key, wlen, id);
    if (*p != NULL) {             /* same identity: replace it */
        x->next = (*p)->next;
        free(*p);
    } else {
        x->next = NULL;
        d->count++;
        if (flags & IVX_DICT_FALLBACK)
            d->nfallback++;
    }
    *p = x;
    return IVX_OK;
}

int dict_remove(IvxDict *d, const unsigned char *word, size_t wlen, int flags)
{
    unsigned char f[DICT_WORD + 1];
    const unsigned char *key = word;
    DictEntry **p, *x;

    if (d->nbucket == 0)
        return IVX_E_RANGE;
    if (!(flags & IVX_DICT_MATCH_CASE)) {
        fold_copy(f, word, wlen);
        key = f;
    }
    p = link_of(d, key, wlen, flags & (IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE));
    if (*p == NULL)
        return IVX_E_RANGE;
    x = *p;
    *p = x->next;
    d->count--;
    if (x->flags & IVX_DICT_FALLBACK)
        d->nfallback--;
    free(x);
    return IVX_OK;
}

void dict_clear(IvxDict *d)
{
    size_t k;
    DictEntry *x, *next;

    for (k = 0; k < d->nbucket; k++)
        for (x = d->bucket[k]; x != NULL; x = next) {
            next = x->next;
            free(x);
        }
    free(d->bucket);
    d->bucket = NULL;
    d->nbucket = d->count = d->nfallback = 0;
}

/* the entry for a word from the text: an exact-case one, else a case-insensitive one */
static const DictEntry *lookup(const IvxDict *d, const unsigned char *w, size_t n, int fallback)
{
    unsigned char f[DICT_WORD + 1];
    const DictEntry *x, *any = NULL;

    if (d->nbucket == 0 || n == 0 || n > DICT_WORD)
        return NULL;
    fold_copy(f, w, n);
    for (x = d->bucket[hash(f, n) & (d->nbucket - 1)]; x != NULL; x = x->next) {
        if ((x->flags & IVX_DICT_FALLBACK) != fallback || x->klen != n)
            continue;
        if (x->flags & IVX_DICT_MATCH_CASE) {
            if (memcmp(x->key, w, n) == 0)
                return x;
        } else if (memcmp(x->key, f, n) == 0) {
            any = x;
        }
    }
    return any;
}

/* ---- spans ---- */

/* the compaction tp_putc did (tpchar.c:86-105): [text_done, text_end) moved to Core.text */
static void apply_shift(IvxDict *d)
{
    int k, j = 0, cut;
    DictSpan *s;

    for (k = 0; k < d->nspan; k++) {
        s = &d->span[k];
        if (s->off + s->len <= d->shift)
            continue;
        if (s->off < d->shift) {
            cut = d->shift - s->off;
            memmove(s->rep, s->rep + cut, (size_t)(s->len - cut));
            s->len -= cut;
            s->off = 0;
        } else {
            s->off -= d->shift;
        }
        if (j != k)
            d->span[j] = *s;
        j++;
    }
    d->nspan = j;
    d->shift = 0;
}

void dict_before_putc(IvxDict *d, const Core *c)
{
    d->shift = d->nspan != 0 && c->text_consumed ? (int)(c->text_done - c->text) : 0;
}

/* a callback ran, so tp_putc got past its compaction; after the call, text_consumed still
 * set with text_done in place means it returned before it (the content check covers the
 * rare case of a new sentence ending at the same offset) */
void dict_after_putc(IvxDict *d, const Core *c)
{
    int k, j = 0, end;

    if (d->shift != 0) {
        if (!c->text_consumed || (int)(c->text_done - c->text) != d->shift)
            apply_shift(d);
        d->shift = 0;
    }
    end = (int)(c->text_end - c->text);
    for (k = 0; k < d->nspan; k++)
        if (d->span[k].off + d->span[k].len <= end) {
            if (j != k)
                d->span[j] = d->span[k];
            j++;
        }
    d->nspan = j;
}

void dict_reset(IvxDict *d)
{
    d->nspan = 0;
    d->shift = 0;
}

static int in_span(const IvxDict *d, const Core *c, int pos)
{
    int k, end = (int)(c->text_end - c->text);
    const DictSpan *s;

    for (k = 0; k < d->nspan; k++) {
        s = &d->span[k];
        if (pos >= s->off && pos < s->off + s->len && s->off + s->len <= end
            && memcmp(c->text + s->off, s->rep, (size_t)s->len) == 0)
            return 1;
    }
    return 0;
}

/* the word at pos (wlen bytes) is about to become rep: later spans move with the text */
static void add_span(IvxDict *d, int pos, int wlen, const unsigned char *rep, int rlen)
{
    int k;

    for (k = 0; k < d->nspan; k++)
        if (d->span[k].off >= pos + wlen)
            d->span[k].off += rlen - wlen;
    if (d->nspan == DICT_SPANS) {
        memmove(d->span, d->span + 1, (DICT_SPANS - 1) * sizeof *d->span);
        d->nspan--;
    }
    d->span[d->nspan].off = pos;
    d->span[d->nspan].len = rlen;
    memcpy(d->span[d->nspan].rep, rep, (size_t)rlen);
    d->nspan++;
}

/* ---- the callbacks ---- */

/* cb_userdict: called with from == Core.tok_start (tpchar.c:242,270,310) */
int dict_user(IvxDict *d, Core *c, const char *word, char **out)
{
    size_t n = strlen(word);
    int pos;
    const DictEntry *x;

    if (d->shift != 0)
        apply_shift(d);
    pos = (int)(c->tok_start - c->text);
    if (n == 0 || n > DICT_WORD || in_span(d, c, pos))
        return 0;
    x = lookup(d, (const unsigned char *)word, n, 0);
    if (x == NULL)
        return 0;
    memcpy(d->reply, x->rep, x->rlen + 1);
    *out = d->reply;
    if (x->flags & IVX_DICT_PHONETIC)
        return 2;
    add_span(d, pos, (int)n, x->rep, (int)x->rlen);
    return 1;
}
```

- [ ] **Step 6: Wire it into `lib/infovox230.c`**

Make these edits (each "replace" names text that occurs once in the file):

1. After `#include "synth.h"` add `#include "ivxdict.h"`.

2. Before the comment `/* a growable ring of fixed-size elements */` insert:

```c
/* a UTF-8 decoder (Unicode table 3-7) */
typedef struct {
    uint32_t cp;            /* code point so far */
    int need;               /* continuation bytes still expected */
    unsigned char lo, hi;   /* allowed range of the next continuation byte */
} U8Dec;

```

3. In `struct IvxEngine`, replace

```c
    uint32_t u8cp;              /* UTF-8 decoder: code point so far */
    int u8need;                 /* continuation bytes still expected */
    unsigned char u8lo, u8hi;   /* allowed range of the next continuation byte */
```

with

```c
    U8Dec u8;                   /* caller text in UTF-8 */
```

and after the field `int oom;                    /* an allocation failed inside a callback */` add

```c
    IvxDict dict;               /* lib/ivxdict.c */
```

4. After the forward declaration `static void cb_status(void *arg, int st);` add `static void dict_install(IvxEngine *e);`.

5. In `ivx_destroy`, after `free(e->ev.a);` add `dict_clear(&e->dict);`.

6. In `ivx_load_language_mem`, add `int r;` to the declarations after `size_t k;`, and replace

```c
    if (tts_ctrl(e->core, TTS_BIND, b + 0x200) != 0) {   /* the old language stays bound */
        free(b);
        return IVX_E_BADFILE;
    }
```

with

```c
    r = tts_ctrl(e->core, TTS_BIND, b + 0x200);
    dict_install(e);   /* the bind cleared the exception dictionary callback */
    if (r != 0) {      /* the old language stays bound */
        free(b);
        return IVX_E_BADFILE;
    }
    dict_reset(&e->dict);
```

7. In `ivx_set`, `case IVX_INPUT_ENCODING:`, and in `ivx_cancel`, replace `e->u8need = 0;` with `e->u8.need = 0;` (two places). In `ivx_cancel`, after the loop that resets `qcount`/`val` (just before `tts_ctrl(e->core, TTS_RESET, (void *)1);`) add `dict_reset(&e->dict);`.

8. Replace the whole of `put_cp` and `put_utf8` (from `static int put_cp(IvxEngine *e, uint32_t cp)` to the closing brace of `put_utf8`, including the comment above `put_utf8`) with:

```c
/* a code point as cp1252; a blank outside it */
static unsigned char cp1252_of(uint32_t cp)
{
    int k;

    if (cp < 0x80 || (cp >= 0xa0 && cp <= 0xff))
        return (unsigned char)cp;
    for (k = 0; k < 32; k++)
        if (cp1252_hi[k] == cp)
            return (unsigned char)(0x80 + k);
    return ' ';
}

/* one byte of UTF-8; returns how many code points it completes (0-2): a bad or
 * unfinished sequence becomes one blank, and the byte that ended it starts afresh */
static int u8_byte(U8Dec *d, unsigned char b, uint32_t cp[2])
{
    int n = 0;

    if (d->need != 0) {
        if (b >= d->lo && b <= d->hi) {
            d->cp = d->cp << 6 | (b & 0x3f);
            d->lo = 0x80;
            d->hi = 0xbf;
            if (--d->need != 0)
                return 0;
            cp[0] = d->cp;
            return 1;
        }
        d->need = 0;
        cp[n++] = ' ';
    }
    d->lo = 0x80;
    d->hi = 0xbf;
    if (b < 0x80) {
        cp[n++] = b;
    } else if (b >= 0xc2 && b <= 0xdf) {
        d->need = 1;
        d->cp = b & 0x1f;
    } else if (b >= 0xe0 && b <= 0xef) {
        d->need = 2;
        d->cp = b & 0x0f;
        if (b == 0xe0)
            d->lo = 0xa0;
        if (b == 0xed)
            d->hi = 0x9f;
    } else if (b >= 0xf0 && b <= 0xf4) {
        d->need = 3;
        d->cp = b & 0x07;
        if (b == 0xf0)
            d->lo = 0x90;
        if (b == 0xf4)
            d->hi = 0x8f;
    } else {
        cp[n++] = ' ';
    }
    return n;
}

static int put_utf8(IvxEngine *e, unsigned char b)
{
    uint32_t cp[2];
    int n = u8_byte(&e->u8, b, cp), k, r = IVX_OK;

    for (k = 0; k < n && r == IVX_OK; k++)
        r = put_byte(e, cp1252_of(cp[k]));
    return r;
}
```

9. In `end_utf8`, replace both `e->u8need` with `e->u8.need`.

10. In `step()`, replace `    r = tp_putc(e->core, e->pending_ch);` with

```c
    dict_before_putc(&e->dict, e->core);
    r = tp_putc(e->core, e->pending_ch);
    dict_after_putc(&e->dict, e->core);
```

11. At the end of the file, after `cb_status`, append:

```c

/* tp_word's user dictionary callback (tts_ctrl 0x14; lib/ivxdict.c) */
static int cb_userdict(void *arg, char *word, char **out)
{
    IvxEngine *e = arg;

    return dict_user(&e->dict, e->core, word, out);
}

/* ---- dictionary ---- */

/* the callback only while there are entries: with none the engine runs exactly as the
 * DLL-verified loop does */
static void dict_install(IvxEngine *e)
{
    tts_ctrl(e->core, TTS_CB_USERDICT, e->dict.count != 0 ? (void *)cb_userdict : NULL);
}

/* caller text in the input encoding to cp1252, control chars as blanks; the length, or -1
 * if it is longer than cap (out has cap + 1 bytes) */
static int dict_text(const IvxEngine *e, const char *s, unsigned char *out, int cap)
{
    const unsigned char *p = (const unsigned char *)s;
    unsigned char b[2];
    uint32_t cp[2];
    U8Dec d;
    int n = 0, m, k;

    memset(&d, 0, sizeof d);
    for (;; p++) {
        if (e->val[IVX_INPUT_ENCODING] == IVX_ENC_CP1252) {
            if (*p == 0)
                break;
            b[0] = *p;
            m = 1;
        } else {
            if (*p == 0 && d.need == 0)
                break;
            if (*p == 0) {   /* an unfinished sequence at the end */
                cp[0] = ' ';
                m = 1;
            } else {
                m = u8_byte(&d, *p, cp);
            }
            for (k = 0; k < m; k++)
                b[k] = cp1252_of(cp[k]);
        }
        for (k = 0; k < m; k++) {
            if (n == cap)
                return -1;
            out[n++] = b[k] < 0x20 ? ' ' : b[k];
        }
        if (*p == 0)
            break;
    }
    out[n] = 0;
    return n;
}

/* a key or respelling: 1..DICT_WORD bytes that can be one word (dict_is_word), else -1 */
static int dict_word(const IvxEngine *e, const char *word, unsigned char *out)
{
    int n = dict_text(e, word, out, DICT_WORD);

    return n > 0 && dict_is_word(out, (size_t)n) ? n : -1;
}

#define DICT_FLAGS (IVX_DICT_PHONETIC | IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE)

int ivx_dict_add(IvxEngine *e, const char *word, const char *replacement, int flags)
{
    unsigned char key[DICT_WORD + 1], rep[DICT_REP + 1];
    const unsigned char *p = (const unsigned char *)replacement;
    int n, m, r;

    if (e == NULL || word == NULL || replacement == NULL)
        return IVX_E_ARG;
    if ((flags & ~DICT_FLAGS) != 0 || ((flags & IVX_DICT_PHONETIC) && (flags & IVX_DICT_FALLBACK))
        || (n = dict_word(e, word, key)) < 0)
        return IVX_E_RANGE;
    if (flags & IVX_DICT_PHONETIC) {
        for (m = 0; p[m] != 0; m++)
            if (m == DICT_REP || p[m] <= ' ')   /* blanks and control chars (0x01 0x02 0x17) */
                return IVX_E_RANGE;
        memcpy(rep, p, (size_t)m);
    } else if (flags & IVX_DICT_FALLBACK) {
        m = dict_word(e, replacement, rep);
    } else {
        m = dict_text(e, replacement, rep, DICT_REP);
    }
    if (m <= 0)
        return IVX_E_RANGE;
    r = dict_add(&e->dict, key, (size_t)n, rep, (size_t)m, flags);
    dict_install(e);
    return r;
}

int ivx_dict_remove(IvxEngine *e, const char *word, int flags)
{
    unsigned char key[DICT_WORD + 1];
    int n, r;

    if (e == NULL || word == NULL)
        return IVX_E_ARG;
    if ((flags & ~DICT_FLAGS) != 0 || (n = dict_word(e, word, key)) < 0)
        return IVX_E_RANGE;
    r = dict_remove(&e->dict, key, (size_t)n, flags);
    dict_install(e);
    return r;
}

void ivx_dict_clear(IvxEngine *e)
{
    if (e == NULL)
        return;
    dict_clear(&e->dict);
    dict_install(e);
}
```

The engine is `calloc`ed in `ivx_create`, so `e->dict` starts empty and needs no init.

- [ ] **Step 7: Build `lib/ivxdict.c` into the library (`Makefile`)**

Replace the `LIBOBJ` line with

```make
LIBOBJ    = $(patsubst src/%.c,$(B)/lib/%.o,$(SRC)) $(B)/lib/infovox230.o $(B)/lib/ivxdict.o
```

and replace the `$(B)/lib/infovox230.o` rule with these two rules (recipe lines start with a TAB):

```make
$(B)/lib/infovox230.o: lib/infovox230.c lib/ivxdict.h include/infovox230.h $(HDR)
	@mkdir -p $(B)/lib
	$(CC) $(LIBCFLAGS) -c $< -o $@

$(B)/lib/ivxdict.o: lib/ivxdict.c lib/ivxdict.h include/infovox230.h $(HDR)
	@mkdir -p $(B)/lib
	$(CC) $(LIBCFLAGS) -c $< -o $@
```

- [ ] **Step 8: Run the tests to verify they pass**

Run: `export PATH=/c/winlibs/mingw32/bin:$PATH; make build/api_test.exe build/ivxsay.exe 2>&1 | grep -i warning; build/api_test.exe bin | tail -2`
Expected: no warnings; `0 failures` (the prototype printed `api_test: 1067085 checks`).

- [ ] **Step 9: Check that the robustness test guards the span shift**

Temporarily change the body of `dict_before_putc` in `lib/ivxdict.c` to `d->shift = 0; (void)c;`, rebuild and run `build/api_test.exe bin | grep -E "FAIL|failures"`.
Expected: `FAIL ... sp dictionary: no speech after it (20286 samples)` and `1 failures`. Restore the original line, rebuild, and confirm `0 failures` again.

- [ ] **Step 10: Run the whole suite**

Run: `make test 2>&1 | grep -E "failures|identical|languages"`
Expected: every test prints `0 failures`, and `ivxsay: 12 of 12 languages identical to the DLL-verified audio`. This checks that the audio is unchanged with no entries.

---

### Task 2: Fallback entries (the exception dictionary)

**Files:**
- Modify: `lib/ivxdict.h`, `lib/ivxdict.c`, `lib/infovox230.c`
- Test: `tests/api_test.c`

**Interfaces:**
- Consumes (from Task 1): `IvxDict`, `lookup(d, w, n, fallback)`, `dict_install`, `DictDef`/`dict_defs`/`dict_same`/`render_dict`, `test_dict_robust`'s entry list; `TP_CHARSET1`, `TP_CHARSET2`, `tp_xlat_char(Core *, unsigned char)` and `Core.charset` (already used by `src/tpstr.c:201-230`, declared through `tts.h`); `tts_ctrl(core, TTS_CB_EXCDICT, fn)`.
- Produces: `char *dict_exc(IvxDict *d, Core *c, const char *key)`; `IvxDict.word`, `word_len`, `word_state`.

- [ ] **Step 1: Write the failing tests**

In `tests/api_test.c`, insert after `test_dict_clear()` (before the `test_dict_robust` comment):

```c
static void test_dict_fallback(void)
{
    static const DictDef blorf[] = {{"blorf", "plarf", IVX_DICT_FALLBACK}};
    static const DictDef aa[] = {{"blorf", "sn\xe5rp", IVX_DICT_FALLBACK}};         /* a-ring */
    static const DictDef known[] = {{"och", "plarf", IVX_DICT_FALLBACK}};
    static const DictDef exact[] = {{"Blorf", "plarf", IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE}};
    static const DictDef zorgle[] = {{"zorgle", "zoggle", IVX_DICT_FALLBACK}};
    static const DictDef over[] = {{"blorf", "plarf", IVX_DICT_FALLBACK}, {"blorf", "huset", IVX_DICT_TEXT}};

    dict_same("sw", IVX_ENC_CP1252, blorf, 1, "Det var en blorf i huset.", "Det var en plarf i huset.", 1,
              "sw fallback");
    dict_same("sw", IVX_ENC_CP1252, aa, 1, "Det var en blorf i huset.", "Det var en sn\xe5rp i huset.", 1,
              "sw fallback with a-ring");
    dict_same("am", IVX_ENC_CP1252, zorgle, 1, "I saw a zorgle today.", "I saw a zoggle today.", 1,
              "am fallback");
    /* the lexicon knows "och": a fallback entry changes nothing */
    dict_same("sw", IVX_ENC_CP1252, known, 1, "Kalle och Lisa kommer.", "Kalle och Lisa kommer.", 0,
              "fallback for a known word");
    dict_same("sw", IVX_ENC_CP1252, exact, 1, "Jag och Blorf och blorf.", "Jag och plarf och blorf.", 1,
              "exact-case fallback");
    /* an override comes before the fallback */
    dict_same("sw", IVX_ENC_CP1252, over, 2, "Det var en blorf i huset.", "Det var en huset i huset.", 1,
              "override before fallback");
}

static void test_dict_lifetime(void)
{
    static const DictDef d[] = {{"och", "eller", 0}, {"blorf", "plarf", IVX_DICT_FALLBACK}};
    static const char T[] = "Kalle och en blorf.", X[] = "Kalle eller en plarf.";
    static const char PRE[] = "Nu kommer en ny mening efter avbrottet.";
    IvxEngine *e;
    Out ref, o, pre;
    unsigned char *b;
    long size;
    short buf[441];
    int round, n;

    render("sw", IVX_ENC_CP1252, X, 0, 441, &ref);
    /* round 0: load am then sw again; round 1: a failed load; round 2: entries added after
     * the text was fed */
    for (round = 0; round < 3; round++) {
        e = engine("sw", IVX_ENC_CP1252);
        if (round < 2)
            dict_defs(e, d, 2);
        if (round == 0)
            CHECK(ivx_load_language(e, lang_path("am")) == 0 && ivx_load_language(e, lang_path("sw")) == 0,
                  "language switch");
        if (round == 1) {
            b = read_file(lang_path("ge"), &size);
            b[0x240] = b[0x241] = 'X';
            CHECK(ivx_load_language_mem(e, b, (size_t)size) == IVX_E_BADFILE, "failed load");
            free(b);
        }
        memset(&o, 0, sizeof o);
        speak(e, T, 0);
        ivx_flush(e);
        if (round == 2)
            dict_defs(e, d, 2);
        drain(e, 441, &o);
        CHECK(same_audio(&ref, &o), "entries lost (round %d)", round);
        out_free(&o);
        ivx_destroy(e);
    }
    out_free(&ref);
    /* ivx_cancel: the same cancelled text on two engines, then T with the entries against
     * X without them */
    for (round = 0; round < 2; round++) {
        e = engine("sw", IVX_ENC_CP1252);
        if (round == 0)
            dict_defs(e, d, 2);
        memset(&pre, 0, sizeof pre);
        speak(e, PRE, 0);
        ivx_flush(e);
        do
            n = read_some(e, buf, 441, &pre);
        while (n > 0 && pre.n < 441 * 20);
        ivx_cancel(e);
        memset(round ? &o : &ref, 0, sizeof o);
        (round ? &o : &ref)->base = (uint64_t)pre.n;
        speak(e, round ? X : T, 0);
        ivx_flush(e);
        drain(e, 441, round ? &o : &ref);
        out_free(&pre);
        ivx_destroy(e);
    }
    CHECK(ref.n > 0 && same_audio(&ref, &o), "entries lost by ivx_cancel");
    out_free(&ref);
    out_free(&o);
}
```

In `test_dict_robust`, replace the comment and the entry list:

```c
/* random bytes (as test_robust) with overrides and phonetic entries installed. sp needs the
 * span shift (dict_before_putc/apply_shift): without it the "o" replacement is read again
 * after a compaction and grows until the sentence after it is cut short */
static void test_dict_robust(void)
{
    static const DictDef d[] = {
        {"och", "eller", 0}, {"the", "a", 0}, {"und", "oder", 0}, {"de", "la", 0}, {"e", "et", 0},
        {"A", "H[J", IVX_DICT_PHONETIC | IVX_DICT_MATCH_CASE}, {"o", "o o o o o o o o", 0}};
```

with

```c
/* random bytes (as test_robust) with overrides, phonetic entries and fallbacks installed.
 * sp needs the span shift (dict_before_putc/apply_shift): without it the "o" replacement is
 * read again after a compaction and grows until the sentence after it is cut short */
static void test_dict_robust(void)
{
    static const DictDef d[] = {
        {"och", "eller", 0}, {"the", "a", 0}, {"und", "oder", 0}, {"de", "la", 0}, {"e", "et", 0},
        {"A", "H[J", IVX_DICT_PHONETIC | IVX_DICT_MATCH_CASE}, {"o", "o o o o o o o o", 0},
        {"a", "zorg", IVX_DICT_FALLBACK}, {"b", "blorf", IVX_DICT_FALLBACK},
        {"x", "xyzzy", IVX_DICT_FALLBACK}, {"q", "kv", IVX_DICT_FALLBACK | IVX_DICT_MATCH_CASE}};
```

In `main()`, between `test_dict_clear();` and `test_dict_robust();`, add:

```c
    test_dict_fallback();
    test_dict_lifetime();
```

- [ ] **Step 2: Run the tests to verify they fail**

Run: `make build/api_test.exe && build/api_test.exe bin | grep -E "FAIL|failures"`
Expected: FAIL lines for `sw fallback`, `sw fallback with a-ring`, `am fallback`, `exact-case fallback` and `entries lost` (rounds 0–2 and `ivx_cancel`), because the fallback entries have no effect yet. `fallback for a known word` and `override before fallback` already pass.

- [ ] **Step 3: Add the saved word and `dict_exc` to `lib/ivxdict.h`**

In `IvxDict`, after `int shift; ...` add

```c
    unsigned char word[DICT_WORD + 1];   /* the word of the last user callback */
    int word_len, word_state;    /* 0 none, 1 looked up, 2 inside a span (final) */
```

and replace the declarations block

```c
/* the user dictionary callback, and the calls around every tp_putc */
int dict_user(IvxDict *d, Core *c, const char *word, char **out);
```

with

```c
/* the engine callbacks, and the calls around every tp_putc */
int dict_user(IvxDict *d, Core *c, const char *word, char **out);
char *dict_exc(IvxDict *d, Core *c, const char *key);
```

- [ ] **Step 4: Implement the exception callback in `lib/ivxdict.c`**

In `dict_reset`, add `d->word_state = 0;` after `d->shift = 0;`.

After the line `/* ---- the callbacks ---- */` insert:

```c

/* tp_word's transliteration (tpstr.c:201-230): the charset table, tp_xlat_char, then
 * upper case. Returns the length, or -1 at 128 chars or more */
static int translit(Core *c, const unsigned char *s, size_t n, unsigned char *out)
{
    unsigned char *d = out, *q, b;

    for (; n != 0; s++, n--) {
        b = *s;
        if (b & 0x80) {
            if (c->charset & 1)
                b = TP_CHARSET1[b & 0x7f];
            else if (c->charset & 2)
                b = TP_CHARSET2[b & 0x7f];
            if (b == 0)
                continue;
            if ((signed char)b < 0) {
                *d = tp_xlat_char(c, b);
                if (*d != 0)
                    d++;
                *d = tp_xlat_char(c, b & 0x7f);
                if (*d != 0)
                    d++;
                if (d - out >= 0x80)
                    return -1;
                continue;
            }
        }
        *d++ = b;
        if (d - out >= 0x80)
            return -1;
    }
    *d = 0;
    for (q = out; *q != 0; q++)
        if ((signed char)*q >= 'a' && (signed char)*q <= '}')
            *q &= 0x5f;
    return (int)(d - out);
}
```

Replace the whole `dict_user` function with:

```c
int dict_user(IvxDict *d, Core *c, const char *word, char **out)
{
    size_t n = strlen(word);
    int pos;
    const DictEntry *x;

    if (d->shift != 0)
        apply_shift(d);
    d->word_state = 0;
    if (n == 0 || n > DICT_WORD)
        return 0;
    memcpy(d->word, word, n + 1);
    d->word_len = (int)n;
    pos = (int)(c->tok_start - c->text);
    if (in_span(d, c, pos)) {
        d->word_state = 2;
        return 0;
    }
    d->word_state = 1;
    x = lookup(d, d->word, n, 0);
    if (x == NULL)
        return 0;
    memcpy(d->reply, x->rep, x->rlen + 1);
    *out = d->reply;
    if (x->flags & IVX_DICT_PHONETIC)
        return 2;
    add_span(d, pos, (int)n, x->rep, (int)x->rlen);
    return 1;
}

/* cb_excdict: the key is " " + the word's symbol names + " " (op_emit_phonemes into a
 * 0x2d buffer, so at most 44 chars); the reply is a respelling in the same form */
char *dict_exc(IvxDict *d, Core *c, const char *key)
{
    unsigned char t[DICT_WORD + 4];
    int state = d->word_state, n;
    const DictEntry *x;

    d->word_state = 0;
    if (state != 1)
        return NULL;
    n = translit(c, d->word, (size_t)d->word_len, t + 1);
    if (n < 0)
        return NULL;
    t[0] = ' ';
    t[n + 1] = ' ';
    t[n + 2] = 0;
    if (strcmp((const char *)t, key) != 0)
        return NULL;
    x = lookup(d, d->word, (size_t)d->word_len, IVX_DICT_FALLBACK);
    if (x == NULL || translit(c, x->rep, x->rlen, (unsigned char *)d->reply) <= 0)
        return NULL;
    return d->reply;
}
```

(Keep the comment `/* cb_userdict: called with from == Core.tok_start (tpchar.c:242,270,310) */` above `dict_user`.)

- [ ] **Step 5: Install it from `lib/infovox230.c`**

Replace the comment `/* tp_word's user dictionary callback (tts_ctrl 0x14; lib/ivxdict.c) */` with `/* tp_word's dictionary callbacks (tts_ctrl 0x14, 0x15; lib/ivxdict.c) */`, and after `cb_userdict` add:

```c

static char *cb_excdict(void *arg, char *key, int unused)
{
    IvxEngine *e = arg;

    (void)unused;
    return dict_exc(&e->dict, e->core, key);
}
```

Replace `dict_install` and its comment with:

```c
/* the callbacks only while entries need them: with none the engine runs exactly as the
 * DLL-verified loop does, and an exception callback changes the out-of-nodes retry
 * (tpstr.c:247,268) */
static void dict_install(IvxEngine *e)
{
    tts_ctrl(e->core, TTS_CB_USERDICT, e->dict.count != 0 ? (void *)cb_userdict : NULL);
    tts_ctrl(e->core, TTS_CB_EXCDICT, e->dict.nfallback != 0 ? (void *)cb_excdict : NULL);
}
```

- [ ] **Step 6: Run the tests to verify they pass**

Run: `make build/api_test.exe build/ivxsay.exe 2>&1 | grep -i warning; build/api_test.exe bin | tail -2`
Expected: no warnings; `0 failures` (the prototype printed `api_test: 1071349 checks`).

- [ ] **Step 7: Run the whole suite**

Run: `make test 2>&1 | grep -E "failures|identical|languages"`
Expected: every test `0 failures`, `ivxsay: 12 of 12 languages identical to the DLL-verified audio`.

---

### Task 3: `ivxsay -d`, its check, the Linux run and the documentation

**Files:**
- Modify: `cli/ivxsay.c`, `tools/check_ivxsay.py`
- Modify (docs): `REFERENCE.md`, `AGENTS.md`, `tools/ghidra/symbols.tsv`, `tools/ghidra/gen_types.py` (and the generated `tools/ghidra/ivx_types.h`, `ivx_types_check.c`), `docs/superpowers/specs/2026-09-28-infovox230-library-api-design.md`

**Interfaces:**
- Consumes: `ivx_dict_add` and the `IVX_DICT_*` flags (Task 1), `ivx_strerror`.
- Produces: `ivxsay -d FILE` (up to 16, loaded in order after the language and settings).

- [ ] **Step 1: Write the failing check (`tools/check_ivxsay.py`)**

Replace the docstring's lines

```
tests/harness.c) with ivxsay and requires byte-identical WAV files.
Run from the repository root after tts_cmp:  python tools/check_ivxsay.py
```

with

```
tests/harness.c) with ivxsay and requires byte-identical WAV files. It also requires
'ivxsay -d' to say the same as the text with the dictionary's replacements made, and to
reject malformed dictionary lines with 'file:line:'.
Run from the repository root after tts_cmp:  python tools/check_ivxsay.py
```

Before `def main():` insert:

```python
DICT = ('# test dictionary\n'
        '\n'
        'USA = U S A\n'
        '!Kalle = Karl        # exact case: "kalle" stays\n'
        '?blorf = pl\u00e5rf        # fallback: only words the lexicon does not know\n'
        'h\u00e4r = d\u00e4r\n'
        'hej = /H[J/          # phonetic\n')
DICT_TEXT = 'Kalle och kalle bor i USA. Det var en blorf h\u00e4r.'
DICT_SAID = 'Karl och kalle bor i U S A. Det var en pl\u00e5rf d\u00e4r.'
BAD_DICTS = [('USA U S A\n', 1), ('ok = fine\n= nothing\n', 2), ('x = /H[J\n', 1),
             ('x = /H[J/ y\n', 1), ('two words = x\n', 1), ('?a = two words\n', 1)]


def check_dict(say, tmp):
    """ivxsay -d: the same WAV as the text with the replacements made; bad lines rejected"""
    def run(args, text):
        with open(os.path.join(tmp, 'text.txt'), 'w', encoding='utf-8') as f:
            f.write(text)
        return subprocess.run([say, '-l', os.path.join('bin', 'swrules.ivx'), '-i', os.path.join(tmp, 'text.txt')]
                              + args, capture_output=True)

    path = os.path.join(tmp, 'dict.txt')
    a, b = os.path.join(tmp, 'a.wav'), os.path.join(tmp, 'b.wav')
    with open(path, 'w', encoding='utf-8') as f:
        f.write(DICT)
    ok = run(['-d', path, '-o', a], DICT_TEXT).returncode == 0 and run(['-o', b], DICT_SAID).returncode == 0
    if ok:
        with open(a, 'rb') as fa, open(b, 'rb') as fb:
            ok = fa.read() == fb.read()
    print('%-12s %s' % ('-d', 'identical' if ok else 'DIFFERENT'))
    bad = not ok
    for text, line in BAD_DICTS:
        with open(path, 'w', encoding='utf-8') as f:
            f.write(text)
        r = run(['-d', path, '-o', a], DICT_TEXT)
        if r.returncode != 1 or ('%s:%d:' % (path, line)).encode() not in r.stderr:
            print('-d %r: exit %d, %r' % (text, r.returncode, r.stderr))
            bad += 1
    return bad
```

In `main()`, replace

```python
            bad += not same
            print('%-12s %s' % (name, 'identical' if same else 'DIFFERENT'))
    print('\nivxsay: %d of %d languages identical to the DLL-verified audio\n%d failures'
          % (len(langs) - bad, len(langs), bad))
```

with

```python
            bad += not same
            print('%-12s %s' % (name, 'identical' if same else 'DIFFERENT'))
        nlang = len(langs) - bad
        bad += check_dict(say, tmp)
    print('\nivxsay: %d of %d languages identical to the DLL-verified audio\n%d failures'
          % (nlang, len(langs), bad))
```

- [ ] **Step 2: Run it to verify it fails**

Run: `python tools/check_ivxsay.py | tail -4`
Expected: FAIL: `-d           DIFFERENT` (ivxsay prints its usage and exits 2 on `-d`) and a non-zero failure count.

- [ ] **Step 3: Add `-d` to `cli/ivxsay.c`**

In the header comment, after the line `* requires the WAV files to be byte-identical to the DLL-verified audio of tests/tts_cmp.c.` add ` * -d loads dictionary files, in the same encoding as the text.`

After `#define BLOCK 441` add `#define MAXDICT 16`.

In `usage()`, replace

```c
        "  -F N      synth frames per second 2..1000 (default 100 = normal speed)\n"
        "  -E        print events (marks, phonemes, errors, done) to stderr\n");
    exit(2);
}
```

with

```c
        "  -F N      synth frames per second 2..1000 (default 100 = normal speed)\n"
        "  -d FILE   dictionary, one entry per line: [!?]word = replacement\n"
        "            (/.../ phonetic, ! exact case, ? only words the lexicon doesn't know,\n"
        "            # comment); up to 16 files, loaded in order\n"
        "  -E        print events (marks, phonemes, errors, done) to stderr\n");
    exit(2);
}

static void dict_error(const char *path, int line, const char *msg, const char *arg)
{
    fprintf(stderr, "%s:%d: %s%s\n", path, line, msg, arg);
    exit(1);
}

/* s..end without blanks at either end, NUL-terminated */
static char *trim(char *s, char *end)
{
    while (s < end && (*s == ' ' || *s == '\t'))
        s++;
    while (end > s && (end[-1] == ' ' || end[-1] == '\t' || end[-1] == '\r' || end[-1] == '\n'))
        end--;
    *end = 0;
    return s;
}

/* "[!?]word = replacement" per line: ! exact case, ? fallback, /.../ phonetic, # comment */
static void load_dict(IvxEngine *e, const char *path)
{
    char line[1024], *eq, *hash, *key, *rep, *close;
    int n = 0, flags, r;
    FILE *f = fopen(path, "rb");

    if (f == NULL) {
        fprintf(stderr, "ivxsay: cannot read %s\n", path);
        exit(1);
    }
    while (fgets(line, sizeof line, f) != NULL) {
        n++;
        if (strchr(line, '\n') == NULL && !feof(f))
            dict_error(path, n, "line too long", "");
        eq = strchr(line, '=');
        hash = strchr(line, '#');
        if (eq == NULL || (hash != NULL && hash < eq)) {   /* blank or comment */
            if (*trim(line, hash != NULL ? hash : line + strlen(line)) != 0)
                dict_error(path, n, "expected [!?]word = replacement", "");
            continue;
        }
        key = trim(line, eq);
        flags = IVX_DICT_TEXT;
        for (;; key++)
            if (*key == '!')
                flags |= IVX_DICT_MATCH_CASE;
            else if (*key == '?')
                flags |= IVX_DICT_FALLBACK;
            else
                break;
        rep = trim(eq + 1, eq + 1 + strlen(eq + 1));
        if (*rep == '/') {
            close = strchr(rep + 1, '/');
            if (close == NULL)
                dict_error(path, n, "unterminated /.../", "");
            hash = trim(close + 1, close + 1 + strlen(close + 1));
            if (*hash != 0 && *hash != '#')
                dict_error(path, n, "text after /.../", "");
            *close = 0;
            rep++;
            flags |= IVX_DICT_PHONETIC;
        } else if ((hash = strchr(rep, '#')) != NULL) {
            rep = trim(rep, hash);
        }
        if ((r = ivx_dict_add(e, key, rep, flags)) != 0)
            dict_error(path, n, "cannot add the entry: ", ivx_strerror(r));
    }
    fclose(f);
}
```

In `main()`:
- replace `const char *lang = NULL, *outpath = NULL, *inpath = NULL;` with `const char *lang = NULL, *outpath = NULL, *inpath = NULL, *dicts[MAXDICT];`
- replace `int enc = IVX_ENC_UTF8, events = 0, err, r, n, i;` with `int enc = IVX_ENC_UTF8, events = 0, ndict = 0, err, r, n, i;`
- after the `case 'i': ...` line add `        case 'd': if (i + 1 >= argc || ndict == MAXDICT) usage(); dicts[ndict++] = argv[++i]; break;`
- after the settings block that ends `|| (fps && set(e, IVX_FRAME_RATE, fps, "frame rate")))` / `return 1;`, add

```c
    for (k = 0; k < ndict; k++)
        load_dict(e, dicts[k]);
```

(`k` is the existing `long k`; the entries are added after `IVX_INPUT_ENCODING` is set, so a file is read in the `-e` encoding.)

- [ ] **Step 4: Run the check to verify it passes**

Run: `make build/ivxsay.exe 2>&1 | grep -i warning; python tools/check_ivxsay.py | tail -5`
Expected: no warnings; `-d           identical`, `ivxsay: 12 of 12 languages identical to the DLL-verified audio`, `0 failures`.

- [ ] **Step 5: Run the whole suite on Windows**

Run: `make test 2>&1 | grep -E "checks|failures|identical|languages"`
Expected: all five DLL-differential tests `0 failures`; `api_test: 1071349 checks` (or whatever Task 2 printed) and `0 failures`; `-d identical`; 12 of 12; `0 failures`. Note the `api_test` count for the docs.

- [ ] **Step 6: Run on Linux (WSL), plain and with sanitizers**

Run (PowerShell tool):

```
wsl -e bash -lc "cd /mnt/c/Users/abart/Desktop/infovox_disasm && make test 2>&1 | tail -20"
```

Expected: no compiler warnings; `api_test: <same count as Windows> checks`, `0 failures`; `-d identical`; `12 of 12`; `0 failures`.

Then:

```
wsl -e bash -lc "cd /mnt/c/Users/abart/Desktop/infovox_disasm && mkdir -p /tmp/asan && gcc -std=gnu11 -g -O1 -m32 -msse2 -mfpmath=sse -fsanitize=address,undefined -fno-omit-frame-pointer -Isrc -Itools/ghidra -Iinclude -Ilib -DIVX_BUILD src/*.c lib/*.c tests/api_test.c -o /tmp/asan/api_test -lm && /tmp/asan/api_test bin 2>&1 | tail -3"
```

Expected: `0 failures` and no `runtime error` or `AddressSanitizer` report. If the link needs the i386 sanitizer runtimes and they are missing, report that rather than installing anything (system changes outside the tree need the user's approval).

- [ ] **Step 7: `REFERENCE.md`**

Use the Edit tool. In the texts to find and insert below (Steps 7–10), `` \` `` stands for a literal backtick. Afterwards check for NUL bytes: `python -c "print(open('REFERENCE.md','rb').read().count(b'\x00'))"` must print `0`.

1. §2.2 table, row `0x100201b0` (`cw_run`): replace `It also calls an external hook at \`ctx+0x168\` (probably the \`LibraryFile\` \`norm\` function).` with `It also calls \`Core.hook\` (\`+0x168\`, set by \`tts_ctrl\` 0x18) every \`hook_interval\` steps; the SAPI engine sets it to its own \`0x10011400\` (\`0x10010e07\`), not to \`norm\` [CONFIRMED].`

2. §2.2 table, row `0x10016cc0`: after `For Danish this resolves \`darules.dll!norm\`.` add ` The function is kept at +0x110 of the 0x114-byte library object (\`new\` at \`0x100174fa\`, constructor \`0x10016c70\`), \`0x10016d40\` returns it, and the engine passes it to \`tts_ctrl\` 0x15 as the **exception dictionary** callback, or 0 when the voice has no \`LibraryFile\` (\`0x10010330\`–\`0x10010354\`) [LIKELY].`

3. §2.2.2, paragraph **Engine init**: replace `and a user-dictionary callback that the library does not
reproduce.` with `and a user-dictionary callback backed by its lexicon object (\`0x10010e40\`, always returning 1 = text); the library has its own dictionary instead (below).`

4. §2.2.2: insert a new paragraph before **Crash guards: the port's only divergences from the DLL**:

```markdown
**Dictionaries** (`lib/ivxdict.c`, `ivx_dict_add`/`ivx_dict_remove`/`ivx_dict_clear`, API 1.1; design:
`docs/superpowers/specs/2026-09-29-infovox230-dictionaries-design.md`) [CONFIRMED on the port]:
- Precedence per word: exact-case override, case-insensitive override, the built-in lexicon, exact-case
  FALLBACK, case-insensitive FALLBACK, the letter-to-sound rules. Overrides are the engine's user
  dictionary (`tts_ctrl` 0x14: TEXT returns 1, PHONETIC returns 2), fallbacks its exception dictionary
  (`tts_ctrl` 0x15, reply transliterated like `tp_word`, `src/tpstr.c:201-230`).
- The callbacks are installed only while entries need them, so with no entries the audio is the
  DLL-verified audio (`ivxsay` 12/12); 0x15 only with FALLBACK entries, because an installed exception
  callback changes the out-of-nodes retry even when it returns NULL (§5.5.1). The load reinstalls 0x15
  after every bind, which clears it.
- A TEXT replacement is final: its words are not looked up again. The library records each replacement
  as a span of `Core.text` (offset plus a copy of its bytes). Before every `tp_putc` with
  `text_consumed` set it records the compaction offset `text_done − text` and shifts the spans by it when
  the next callback runs, or after the call; spans past `text_end` are dropped, and a span counts only
  while its bytes are still at its offset. The shift matters when the engine reads a replacement's words
  again after compacting (seen with random bytes in `sp`); `api_test` fails without it.
- Keys are rejected only for bytes that end a word in every language (blanks, control characters, digits,
  `! # $ % & ( ) * + , - . ; < = > ? ^ _`); which other characters belong to a word depends on the
  language and charset (§5.5.1), and a key the loaded language would split never matches.
```

5. §2.2.2 **Tests**, first bullet: replace `**784,014 checks, 0 failures** (773,634 before the synth guard, 434,392 before the
  crash guards)` with `**<N> checks, 0 failures** (784,014 before the dictionary tests, 773,634 before the synth guard, 434,392 before the
  crash guards)`, where `<N>` is the count from Step 5, and append to the end of that bullet: ` Dictionaries (\`test_dict_*\`): TEXT and PHONETIC entries sound exactly like the replaced input (Swedish, English), case rules (exact before case-insensitive, å/Å, French œ/Œ, UTF-8 = cp1252), finality ("cat" → "cat dog", a → b → c) across 40 sentences of buffer compaction and a replacement that ends a sentence, 0x71 for a replacement that cannot fit, every \`IVX_E_*\` case, clear/remove giving the plain audio, FALLBACK entries (unknown words only, respelling with å, exact case, override first), entries surviving a language switch, a failed load and \`ivx_cancel\` and applying to text already fed, and the random bytes of \`test_robust\` with overrides, phonetic entries and fallbacks installed in all 12 languages.`

6. §2.2.2 **Tests**, `tools/check_ivxsay.py` bullet: append ` It also checks \`ivxsay -d\`: a Swedish dictionary file (TEXT, exact case, FALLBACK, UTF-8 keys, a phonetic line) gives the same WAV as the text with the replacements made, and six malformed files exit 1 with \`file:line:\`.`

7. §2.2.2 **Tests**, Linux bullet: replace `\`api_test\`: 784,014 checks with the five guards (773,634 with four` with `\`api_test\`: <N> checks with the dictionary tests (784,014 with the five guards, 773,634 with four`, using the WSL count from Step 6 (it must equal the Windows count).

8. §5.5.1 **Words**, item 5: replace `and returns a transcription or NULL.` with `and returns a respelling or NULL. The reply goes through \`tp_text_to_segs(c, 1, …)\` like a spelling and then M0; it is not looked up in the lexicon, and a reply equal to the word's own key gives the word's own audio [CONFIRMED, probe on the port].`

9. §5.5.1: after the line `Every path ends in \`ctl_word_end\`.` insert:

```markdown
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
  (the leading space and all but the last letter) (`tpstr.c:245-270`). Every bind clears it
  (`ttsctrl.c:478`).
- The SAPI engine installs the voice's `LibraryFile` function (Danish: `darules.dll!norm`) as `cb_excdict`
  (§2.2, `0x10016cc0`) [LIKELY], and backs `cb_userdict` with its lexicon object (`0x10010e40`).
```

- [ ] **Step 8: `AGENTS.md`**

1. `lib/infovox230.c` row: after `events stamped with exact sample positions, and cancel.` add ` The dictionary (\`lib/ivxdict.c\`: \`ivx_dict_add\`/\`remove\`/\`clear\` over the engine's user and exception dictionary callbacks, spans that keep replaced words final).`
2. `cli/ivxsay.c` row: after `an event trace (\`-E\`)` add `, dictionary files (\`-d FILE\`: \`[!?]word = replacement\`, \`/…/\` phonetic)`; after `(\`tools/check_ivxsay.py\`, run by \`make test\`)` add ` and \`-d\` must match the text with its replacements made`.
3. `tests/` row, the `api_test.c` sentence: after `and speaks with voices 2 and 4 in every language, to cover the crash guards.` add ` Its \`test_dict_*\` tests cover the dictionaries: TEXT/PHONETIC/FALLBACK entries against the replaced input, case rules, finality across buffer compaction, limits, lifetime and random bytes with entries installed.`
4. Architecture diagram: replace `│     optional LibraryFile hook (darules.dll!norm)` with `│     optional LibraryFile function (darules.dll!norm) as the exception dictionary (tts_ctrl 0x15)`.
5. Next steps, the **User and exception dictionaries** bullet: replace it with `     - ~~**User and exception dictionaries**~~ **done:** \`ivx_dict_add\`/\`ivx_dict_remove\`/\`ivx_dict_clear\` (API 1.1, \`lib/ivxdict.c\`, spec \`docs/superpowers/specs/2026-09-29-infovox230-dictionaries-design.md\`) and \`ivxsay -d\` (REFERENCE §2.2.2).`
6. The **Danish `LibraryFile` hook** bullet: replace `(\`darules.dll!norm\`, cmd 0x18/0x19)` with `(\`darules.dll!norm\`, which the SAPI engine installs as the exception dictionary, cmd 0x15 [LIKELY]; it would chain after the FALLBACK entries in the library's \`cb_excdict\`)`.

- [ ] **Step 9: `tools/ghidra/symbols.tsv` and `gen_types.py`**

In `symbols.tsv` (TAB-separated; use the Edit tool):
1. `10010e40` row: replace the comment `callback passed to tts_ctrl 0x14 (Core.cb_userdict) [LIKELY]` with `callback passed to tts_ctrl 0x14 (Core.cb_userdict): looks the word up in the engine's lexicon object, always returns 1 (text) [CONFIRMED]`.
2. `10016cc0` row: append to its comment `; the proc is kept at +0x110 and installed as the exception dictionary (tts_ctrl 0x15) [LIKELY]`.
3. Insert after the `10016cc0` row: `10016d40<TAB>func<TAB>eng_library_proc<TAB>-<TAB>returns the LibraryFile object's ordinal-1 function (+0x110); 0x10010334 passes it to tts_ctrl 0x15, the exception dictionary, or 0 without one [LIKELY]`.
4. `1001a7b0` row (`tts_c18_hook`): replace the comment `cmd 0x18: LibraryFile norm hook` with `cmd 0x18: Core.hook, called every hook_interval steps by cw_run; the SAPI engine passes 0x10011400 (0x10010e07) [CONFIRMED]`.

In `tools/ghidra/gen_types.py`, replace the comment `'cmd 0x18: LibraryFile norm() hook, called every hook_interval steps'` with `'cmd 0x18: engine callback called every hook_interval steps (SAPI: 0x10011400)'`, then regenerate and check:

```bash
cd tools/ghidra && python gen_types.py && gcc -std=c11 -m32 -fsyntax-only ivx_types_check.c && cd ../..
```

Expected: no errors. If the ghidra-mcp tools are connected, copy `IvxApply.java` to `%USERPROFILE%\ghidra_scripts\`, run it with `run_ghidra_script` (program `Ivx230nt.dll`), expect `errors: 0`, and save the program; otherwise say that `IvxApply.java` still has to be run.

- [ ] **Step 10: The library API spec**

In `docs/superpowers/specs/2026-09-28-infovox230-library-api-design.md`, **Out of scope for v1**: replace `- user and exception lexicons (\`cb_userdict\`, \`cb_excdict\`)` with `- user and exception lexicons (\`cb_userdict\`, \`cb_excdict\`): added in 1.1, see \`2026-09-29-infovox230-dictionaries-design.md\``, and replace `- the \`darules.dll\` norm hook` with `- the \`darules.dll\` norm hook (the SAPI engine installs it as the exception dictionary; see the dictionaries spec)`.

- [ ] **Step 11: Final check**

Run: `make test 2>&1 | grep -E "failures|identical|languages"` and `python -c "import sys; [print(p, open(p,'rb').read().count(b'\x00')) for p in ('REFERENCE.md','AGENTS.md')]"`
Expected: every test `0 failures`, 12 of 12, `-d identical`; `0` NUL bytes in both files.
