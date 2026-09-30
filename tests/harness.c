/* Shared pieces of the differential tests (see harness.h). */
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "harness.h"

#define VA_TTS_CTRL      0x10019f50
#define VA_TP_PUTC       0x1001b050
#define VA_OP_NEXT_FRAME 0x1001c4f0

char *dll_base;
tts_ctrl_fn dll_tts_ctrl;
tp_putc_fn dll_tp_putc;
next_frame_fn dll_next_frame;

void *dll_va(uint32_t va)
{
    return dll_base + (va - 0x10000000u);
}

int dll_load(const char *bindir)
{
    char dll[512];
    HMODULE h;

    snprintf(dll, sizeof dll, "%s\\Ivx230nt.dll", bindir);
    h = LoadLibraryA(dll);
    if (h == NULL) {
        printf("cannot load %s (error %lu)\n", dll, GetLastError());
        return 0;
    }
    dll_base = (char *)h;
    dll_tts_ctrl = (tts_ctrl_fn)dll_va(VA_TTS_CTRL);
    dll_tp_putc = (tp_putc_fn)dll_va(VA_TP_PUTC);
    dll_next_frame = (next_frame_fn)dll_va(VA_OP_NEXT_FRAME);
    return 1;
}

int patch_jump(uint32_t va, void *target)
{
    unsigned char *p = dll_va(va);
    DWORD old;
    int32_t rel = (int32_t)((char *)target - (char *)(p + 5));

    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old))
        return 0;
    p[0] = 0xe9;
    memcpy(p + 1, &rel, 4);
    VirtualProtect(p, 5, old, &old);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return 1;
}

void *trampoline(uint32_t va, int n)
{
    unsigned char *t = VirtualAlloc(NULL, 64, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    unsigned char *src = dll_va(va);
    int32_t rel;

    if (t == NULL)
        return NULL;
    memcpy(t, src, (size_t)n);
    t[n] = 0xe9;
    rel = (int32_t)((src + n) - (t + n + 5));
    memcpy(t + n + 1, &rel, 4);
    FlushInstructionCache(GetCurrentProcess(), t, 64);
    return t;
}

/* ---- test input ---- */

const Lang langs[] = {
    {"amrules.ivx",
     "Hello, this is a test of the American English rules. The quick brown fox jumps over "
     "the lazy dog on May 21, 1998 at 3:45 p.m. Dr. Smith paid $1,234.56 for 12 books! "
     "Isn't it remarkable? Call 555-1234 or write to 42 Elm St., Springfield. "
     "Pronunciation, synthesizers and phonemes: nothing is impossible."},
    {"blrules.ivx",
     "Good morning. The British English rules are being tested today, the 3rd of June 1997. "
     "Colour, behaviour and neighbourhood are spelt differently. It costs \xa3" "45.99 per week. "
     "Would you like a cup of tea? Mr. Jones lives at 221B Baker Street, London."},
    {"darules.ivx",
     "Goddag. Dette er en pr\xf8ve af den danske tale. R\xf8" "dgr\xf8" "d med fl\xf8" "de. "
     "Klokken er 14.30 den 5. maj 1998, og det koster 250 kroner. Hvad hedder du? "
     "K\xf8" "benhavn og \xc5rhus er store byer i Danmark."},
    {"durules.ivx",
     "Goedemorgen. Dit is een test van de Nederlandse spraaksynthese. Het is vandaag 12 "
     "augustus 1998 en het regent. Hoeveel kost dat? Het kost 35 gulden. De fietsenmaker "
     "woont in Amsterdam, niet ver van het station."},
    {"firules.ivx",
     "Hyv\xe4\xe4 p\xe4iv\xe4\xe4. T\xe4m\xe4 on suomenkielisen puhesynteesin testi. "
     "Kello on 9.15 ja p\xe4iv\xe4m\xe4\xe4r\xe4 on 24.12.1998. Paljonko t\xe4m\xe4 maksaa? "
     "Helsinki on Suomen p\xe4\xe4kaupunki, ja siell\xe4 asuu noin 500000 ihmist\xe4."},
    {"frrules.ivx",
     "Bonjour. Ceci est un essai de la synth\xe8se vocale fran\xe7" "aise. Nous sommes le 14 "
     "juillet 1998 et il fait tr\xe8s beau. Combien co\xfbte ce g\xe2teau? Il co\xfbte 25 "
     "francs. Les enfants \xe9" "coutent la m\xe9t\xe9o \xe0 la radio."},
    {"gerules.ivx",
     "Guten Tag. Dies ist ein Test der deutschen Sprachsynthese. Heute ist der 3. Oktober "
     "1998, und es regnet. Wieviel kostet das? Es kostet 49,95 Mark. Die Donaudampfschiffahrt"
     "sgesellschaft hat \xfc" "ber 1500 Mitarbeiter. Sch\xf6ne Gr\xfc\xdf" "e aus M\xfc" "nchen!"},
    {"icrules.ivx",
     "G\xf3\xf0" "an daginn. \xde" "etta er pr\xf3" "fun \xe1 \xed" "slenskum talgervli. "
     "Klukkan er 10.30 og \xfe" "a\xf0 er 17. j\xfan\xed 1998. Hva\xf0 kostar \xfe" "etta? "
     "Reykjav\xedk er h\xf6" "fu\xf0" "borg \xcd" "slands."},
    {"itrules.ivx",
     "Buongiorno. Questa \xe8 una prova della sintesi vocale italiana. Oggi \xe8 il 25 aprile "
     "1998 e fa bel tempo. Quanto costa questo libro? Costa 15000 lire. La citt\xe0 di Roma "
     "\xe8 molto bella, e anche Firenze."},
    {"norules.ivx",
     "God morgen. Dette er en pr\xf8ve p\xe5 norsk talesyntese. I dag er det 17. mai 1998, og "
     "det er nasjonaldagen. Hva koster det? Det koster 120 kroner. Bl\xe5" "b\xe6r og "
     "multer vokser i fjellet ved Trondheim."},
    {"sprules.ivx",
     "Buenos d\xed" "as. Esta es una prueba de la s\xedntesis de voz en espa\xf1ol. Hoy es el 12 de "
     "octubre de 1998. \xbf" "Cu\xe1nto cuesta? Cuesta 3500 pesetas. El ni\xf1o come "
     "manzanas en Madrid y en Barcelona."},
    {"swrules.ivx",
     "God morgon. Detta \xe4r ett test av svensk talsyntes. I dag \xe4r det den 6 juni 1998 "
     "och solen skiner. Vad kostar det? Det kostar 95 kronor. Sj\xf6sjukdom och "
     "kvarts\xf6" "verdomstol \xe4r l\xe5nga ord. Stockholm ligger vid \xd6stersj\xf6n."},
};
const int nlangs = sizeof(langs) / sizeof(langs[0]);

unsigned char *read_file(const char *path, long *size)
{
    FILE *f = fopen(path, "rb");
    unsigned char *buf;

    if (f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    *size = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = calloc(1, (size_t)*size + 16);   /* zero pad: lex_lookup can read 1 byte past the end */
    if (fread(buf, 1, (size_t)*size, f) != (size_t)*size) {
        fclose(f);
        free(buf);
        return NULL;
    }
    fclose(f);
    return buf;
}

Core *lang_open(const char *bindir, const Lang *l, int npool, unsigned char **bufp)
{
    char path[512];
    unsigned char *buf;
    long size;
    Core *c = NULL;

    snprintf(path, sizeof path, "%s\\%s", bindir, l->file);
    buf = read_file(path, &size);
    if (buf == NULL) {
        printf("%s: cannot read %s\n", l->file, path);
        return NULL;
    }
    dll_tts_ctrl(NULL, 0x21, buf + 0x200);             /* byte-swap the body */
    if (dll_tts_ctrl(&c, 0, (void *)(intptr_t)npool) != 0 || c == NULL) {
        printf("%s: tts_create failed\n", l->file);
        free(buf);
        return NULL;
    }
    dll_tts_ctrl(c, 4, (void *)2);                      /* charset, as the engine */
    if (dll_tts_ctrl(c, 3, buf + 0x200) != 0)
        printf("%s: bind returned non-zero\n", l->file);
    *bufp = buf;
    return c;
}

void lang_close(Core *c, unsigned char *buf)
{
    dll_tts_ctrl(c, 1, NULL);
    free(buf);
}

/* ---- address maps ---- */

void map_add(Map *m, const void *a, const void *b, size_t n)
{
    if (m->n < (int)(sizeof(m->r) / sizeof(m->r[0]))) {
        m->r[m->n].a = (uintptr_t)a;
        m->r[m->n].b = (uintptr_t)b;
        m->r[m->n].n = n;
        m->n++;
    }
}

/* Addresses inside a region map first; failing that, up to MAP_SLACK bytes past a region's
 * end: the DLL keeps end pointers one past a node (an inline item area ending at the
 * last node of an array) and up to 0xa bytes further (Track.p1 = p2 + 0x38). */
#define MAP_SLACK 0x40

int map_addr(const Map *m, uint32_t v, uint32_t *out)
{
    int i, pass;

    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < m->n; i++)
            if (v >= m->r[i].a && v < m->r[i].a + m->r[i].n + (pass ? MAP_SLACK : 0)) {
                *out = (uint32_t)(m->r[i].b + (v - m->r[i].a));
                return 1;
            }
    return 0;
}

uint32_t map_or_same(const Map *m, uint32_t v)
{
    uint32_t t;

    return map_addr(m, v, &t) ? t : v;
}

int same_word(const Map *m, uint32_t av, uint32_t bv)
{
    uint32_t t;

    return av == bv || (map_addr(m, av, &t) && t == bv);
}

int core_ptr_field(size_t off)
{
    if (off >= 0x818 && off < 0x818 + 32 * sizeof(Track))
        return (off - 0x818) % sizeof(Track) < 0x10;       /* Track p0, p1, p2, seg */
    return (off >= 0x40 && off < 0x68)       /* mod_base .. item_blk */
        || (off >= 0xa0 && off < 0x114)      /* phinv, lists, held_mark, out_cur, ivx, mod[], pool */
        || (off >= 0x160 && off < 0x16c)     /* p160, cb_excdict, hook */
        || (off >= 0x490 && off < 0x4b0)     /* text pointers, xlat_hi, class_lo */
        || (off >= 0x6b4 && off < 0x6bc)     /* lex_index, lexicon */
        || (off >= 0x6e4 && off < 0x710);    /* cb_arg, callbacks, ptab_* */
}

/* ---- images ---- */

static void reloc_list_ext(Image *img, const SegList *l, const Core *src)
{
    const Seg *s;
    uint32_t t;
    int guard = src->pool_n + 1;

    for (s = l->head.next; s != (const Seg *)&l->tail && guard-- > 0; s = s->next) {
        if (!map_addr(&img->map, (uint32_t)(uintptr_t)s, &t))
            break;
        ((Seg *)(uintptr_t)t)->ext = (ItemBlk *)(uintptr_t)map_or_same(&img->map, (uint32_t)(uintptr_t)s->ext);
    }
}

void image_take(Image *img, const Core *src)
{
    uint32_t *w;
    size_t i;

    img->pool_bytes = (size_t)src->pool_n * NODE_SIZE;
    if (img->core == NULL)
        img->core = calloc(1, sizeof(Core) + MAP_SLACK);
    /* padded: an end pointer of one region must not land in another region of the image */
    img->pool = realloc(img->pool, img->pool_bytes + MAP_SLACK);
    memcpy(img->core, src, sizeof(Core));
    memcpy(img->pool, src->pool, img->pool_bytes);
    img->map.n = 0;
    map_add(&img->map, src, img->core, sizeof(Core));
    map_add(&img->map, src->pool, img->pool, img->pool_bytes);
    w = (uint32_t *)img->core;
    for (i = 0; i < sizeof(Core) / 4; i++)
        if (core_ptr_field(i * 4))
            w[i] = map_or_same(&img->map, w[i]);
    w = (uint32_t *)img->pool;
    for (i = 0; i < img->pool_bytes / NODE_SIZE; i++) {
        w[i * NODE_SIZE / 4] = map_or_same(&img->map, w[i * NODE_SIZE / 4]);
        w[i * NODE_SIZE / 4 + 1] = map_or_same(&img->map, w[i * NODE_SIZE / 4 + 1]);
    }
    reloc_list_ext(img, &src->segs, src);
    reloc_list_ext(img, &src->held, src);
    reloc_list_ext(img, &src->out, src);
}

void image_free(Image *img)
{
    free(img->core);
    free(img->pool);
    memset(img, 0, sizeof *img);
}

static int diff(Diff *d, const char *what, size_t off, uint32_t av, uint32_t bv)
{
    if (d != NULL) {
        snprintf(d->what, sizeof d->what, "%s", what);
        d->off = off;
        d->av = av;
        d->bv = bv;
    }
    return 1;
}

int compare_core_pool(const Map *m, const Core *a, const Core *b, size_t pool_bytes, Diff *d)
{
    const uint32_t *ac = (const uint32_t *)a, *bc = (const uint32_t *)b;
    const uint32_t *ap = (const uint32_t *)a->pool, *bp = (const uint32_t *)b->pool;
    size_t i;

    for (i = 0; i < sizeof(Core) / 4; i++)
        if (core_ptr_field(i * 4) ? !same_word(m, ac[i], bc[i]) : ac[i] != bc[i])
            return diff(d, "Core", i * 4, ac[i], bc[i]);
    for (i = 0; i < pool_bytes / 4; i++)
        if (!same_word(m, ap[i], bp[i])) {
            char what[32];
            snprintf(what, sizeof what, "pool node %u", (unsigned)(i * 4 / NODE_SIZE));
            return diff(d, what, i * 4 % NODE_SIZE, ap[i], bp[i]);
        }
    return 0;
}

int compare_words(const Map *m, const void *a, const void *b, size_t n, const char *what, Diff *d)
{
    const uint32_t *aw = a, *bw = b;
    size_t i;

    for (i = 0; i < n / 4; i++)
        if (!same_word(m, aw[i], bw[i]))
            return diff(d, what, i * 4, aw[i], bw[i]);
    return 0;
}
