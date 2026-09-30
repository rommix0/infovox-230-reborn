/* Differential test: the rule interpreter port (src/cw*.c, contwork.c, tpelem.c) against
 * bin/Ivx230nt.dll, on real text in every language.
 *
 * The DLL's run_module (0x1001fe90) is patched to jump to hook_run_module. On every module
 * run the hook
 *   1. copies the DLL's Core and its node pool into an image (harness.c),
 *   2. runs the module in the DLL (its own cw_run / seg_free_all),
 *   3. runs the same module with the port on the image,
 *   4. compares the status and every dword of the Core and the pool (pointers compared
 *      through the relocation), and
 *   5. returns the DLL's status so the DLL pipeline carries on unchanged.
 * Text is fed through the DLL's tp_putc and the frames are drained with op_next_frame.
 */
#include <stdio.h>
#include <stdlib.h>
#include "harness.h"

#define VA_CW_RUN        0x100201b0
#define VA_SEG_FREE_ALL  0x1001e970
#define VA_RUN_MODULE    0x1001fe90
#define SMALL_POOL       60

typedef short(__cdecl *cw_run_fn)(Core *, unsigned short *);
typedef void(__cdecl *seg_free_all_fn)(Core *);

static cw_run_fn dll_cw_run;
static seg_free_all_fn dll_seg_free_all;

static const char *cur_lang;
static long runs, run_steps[8], failures;
static long no_nodes;       /* runs that ended out of nodes (status 0x4x) */
static int max_reports = 20;
static Image shadow;

static void report(Core *d, int idx, const Diff *df)
{
    failures++;
    if (max_reports-- > 0)
        printf("  FAIL %s M%d run %ld: %s +0x%x: dll %08x port %08x (status dll %x)\n",
               cur_lang, idx, runs, df->what, (unsigned)df->off, (unsigned)df->av,
               (unsigned)df->bv, d->status);
}

/* item-storage statistics: ItemBlks in use and the fullest one seen */
static long blk_seen, blk_full;
static int blk_max_len, chain_max;

static void item_stats(Core *d)
{
    SegList *lists[3] = {&d->segs, &d->held, &d->out};
    Seg *s;
    ItemBlk *b;
    int i, n;

    for (i = 0; i < 3; i++)
        for (s = lists[i]->head.next; s != (Seg *)&lists[i]->tail; s = s->next) {
            for (n = 0, b = s->ext; b != NULL; b = b->next, n++) {
                blk_seen++;
                if (b->len > blk_max_len)
                    blk_max_len = b->len;
                if (b->len >= 0x2c)
                    blk_full++;
            }
            if (n > chain_max)
                chain_max = n;
        }
}

/* replaces the DLL's run_module (cdecl, short idx) */
static short __cdecl hook_run_module(Core *d, short idx)
{
    short st_d, st_p;
    Diff df;
    int i;

    image_take(&shadow, d);
    /* the DLL's own run_module body, calling its own cw_run / seg_free_all */
    if (idx == 2)
        for (i = 0; i < 10; i++)
            d->V[i] = d->V_init[i];
    st_d = dll_cw_run(d, d->mod[idx]);
    if (st_d != 0)
        dll_seg_free_all(d);
    no_nodes += (st_d & 0xf0) == 0x40;

    item_stats(d);
    st_p = run_module(shadow.core, idx);
    if (st_d != st_p) {
        Diff s = {"status", 0, (uint16_t)st_d, (uint16_t)st_p};
        report(d, idx, &s);
    } else if (compare_core_pool(&shadow.map, d, shadow.core, shadow.pool_bytes, &df)) {
        report(d, idx, &df);
    }
    runs++;
    if (idx >= 0 && idx < 8)
        run_steps[idx]++;
    return st_d;
}

/* ---- driving the DLL ---- */

static long frames;

static void drain(Core *c)
{
    while (dll_next_frame(c) != NULL)
        frames++;
}

static void speak(Core *c, const char *text)
{
    const unsigned char *p;
    int r;

    for (p = (const unsigned char *)text; *p; p++) {
        if (p == (const unsigned char *)text || p[-1] == ' ')
            dll_tts_ctrl(c, 0x20, (void *)(intptr_t)(0x10001 * (int)(p - (const unsigned char *)text + 1)));
        r = dll_tp_putc(c, *p);
        if (r < 0 || r > 2)
            printf("  tp_putc returned %d at offset %d\n", r, (int)(p - (const unsigned char *)text));
        drain(c);
    }
    dll_tp_putc(c, 0x17);   /* end of text, as the engine sends */
    drain(c);
}

static int test_lang(const char *bindir, const Lang *l, int npool)
{
    unsigned char *buf;
    long runs0 = runs, fail0 = failures, frames0;
    Core *c;
    int i;

    cur_lang = l->file;
    c = lang_open(bindir, l, npool, &buf);
    if (c == NULL) {
        failures++;
        return 1;
    }
    for (i = 0; i < 8; i++)
        run_steps[i] = 0;
    frames0 = frames;
    speak(c, l->text);
    printf("%-12s pool %3d: %4ld module runs (M0 %ld, M1 %ld, M2 %ld, M3 %ld, M4 %ld, M5 %ld), %ld frames: %s\n",
           l->file, npool, runs - runs0, run_steps[0], run_steps[1], run_steps[2], run_steps[3],
           run_steps[4], run_steps[5], frames - frames0, failures == fail0 ? "ok" : "MISMATCH");
    lang_close(c, buf);
    return failures != fail0;
}

int main(int argc, char **argv)
{
    const char *bindir = argc > 1 ? argv[1] : "bin";
    int i;

    setvbuf(stdout, NULL, _IONBF, 0);
    if (!dll_load(bindir))
        return 2;
    dll_cw_run = (cw_run_fn)dll_va(VA_CW_RUN);
    dll_seg_free_all = (seg_free_all_fn)dll_va(VA_SEG_FREE_ALL);
    if (!patch_jump(VA_RUN_MODULE, (void *)hook_run_module)) {
        printf("cannot patch run_module\n");
        return 2;
    }

    /* the engine's pool size, then a small pool that runs out of nodes */
    for (i = 0; i < nlangs; i++)
        test_lang(bindir, &langs[i], 900);
    for (i = 0; i < nlangs; i++)
        test_lang(bindir, &langs[i], SMALL_POOL);

#ifdef CW_COVERAGE
    {
        extern unsigned long cw_op_count[0x80];
        static const unsigned short ops[] = {
            0x004, 0x008, 0x00c, 0x010, 0x040, 0x044, 0x048, 0x04c, 0x050, 0x054, 0x058, 0x05c,
            0x060, 0x064, 0x068, 0x06c, 0x07c, 0x080, 0x090, 0x094, 0x09c, 0x0b0, 0x0b4, 0x0b8,
            0x0bc, 0x0c0, 0x0c4, 0x0c8, 0x0cc, 0x0d0, 0x0d4, 0x100, 0x104, 0x108, 0x10c, 0x150,
            0x154, 0x158, 0x15c, 0x160, 0x164, 0x168, 0x16c, 0x170, 0x174, 0x178, 0x17c, 0x180,
            0x184, 0x188, 0x18c, 0x190, 0x194, 0x198, 0x19c, 0x1a0, 0x1a4, 0x1a8, 0x1ac, 0x1b0,
            0x1b4, 0x1b8, 0x1bc, 0x1c0, 0x1c4, 0x1c8, 0x1cc, 0x1f0, 0x1f4, 0x1f8};
        unsigned long total = 0;
        unsigned k;

        printf("\nopcode coverage (executions in the port):\n");
        for (k = 0; k < sizeof(ops) / sizeof(ops[0]); k++) {
            total += cw_op_count[ops[k] >> 2];
            printf("  %03x:%-9lu%s", ops[k], cw_op_count[ops[k] >> 2], k % 6 == 5 ? "\n" : "");
        }
        printf("\n  %lu opcodes executed\n", total);
        {
            extern unsigned long cw_insert_count[4];
            printf("  cw_item_insert: inline %lu, inline+new block %lu, block %lu, block+new block %lu\n",
                   cw_insert_count[0], cw_insert_count[1], cw_insert_count[2], cw_insert_count[3]);
        }
    }
#endif
    printf("\nitem blocks: %ld seen, longest chain %d, fullest %d bytes, %ld with >= 0x2c bytes\n",
           blk_seen, chain_max, blk_max_len, blk_full);
    printf("\n%ld module runs compared (%ld ended out of nodes), %ld failures\n", runs, no_nodes, failures);
    return failures != 0;
}
