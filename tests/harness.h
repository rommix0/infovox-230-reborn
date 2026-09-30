/* Shared pieces of the differential tests that drive bin/Ivx230nt.dll (32-bit).
 *
 * Images: a test copies the DLL's Core and node pool into its own memory ("image"),
 * relocating every pointer into the Core or the pool, runs the DLL on the original and
 * the port on the image, and compares the two word by word. Pointer fields match when
 * they point at corresponding addresses (a Map of region pairs); everything else must
 * be equal. Relocation is typed (the Core's pointer fields, node links, Seg.ext of the
 * segments on the lists) because item data can look like a pool address.
 */
#ifndef HARNESS_H
#define HARNESS_H

#include <stddef.h>
#include <stdint.h>
#include "op.h"

/* ---- the DLL ---- */
extern char *dll_base;
int dll_load(const char *bindir);
void *dll_va(uint32_t va);
int patch_jump(uint32_t va, void *target);
/* callable copy of the first n bytes of the function at va (whole instructions without
 * relative operands) followed by a jump back to va + n; use before patch_jump */
void *trampoline(uint32_t va, int n);

typedef int(__cdecl *tts_ctrl_fn)(void *, int, void *);
typedef int(__cdecl *tp_putc_fn)(Core *, unsigned char);
typedef short *(__cdecl *next_frame_fn)(Core *);
extern tts_ctrl_fn dll_tts_ctrl;
extern tp_putc_fn dll_tp_putc;
extern next_frame_fn dll_next_frame;

/* ---- test input ---- */
typedef struct {
    const char *file;
    const char *text;
} Lang;
extern const Lang langs[];
extern const int nlangs;
unsigned char *read_file(const char *path, long *size);
/* byte-swap and bind bindir\l->file into a new Core with npool nodes (the engine uses
 * 900; fewer force the out-of-nodes paths), charset 2 as the engine */
Core *lang_open(const char *bindir, const Lang *l, int npool, unsigned char **buf);
void lang_close(Core *c, unsigned char *buf);

/* ---- address maps ---- */
typedef struct {
    uintptr_t a, b;
    size_t n;
} Region;
typedef struct {
    Region r[12];
    int n;
} Map;
void map_add(Map *m, const void *a, const void *b, size_t n);
int map_addr(const Map *m, uint32_t v, uint32_t *out);   /* a-side address -> b-side */
uint32_t map_or_same(const Map *m, uint32_t v);
int same_word(const Map *m, uint32_t av, uint32_t bv);
int core_ptr_field(size_t off);                            /* dword at off is a pointer */

/* ---- images ---- */
typedef struct {
    Core *core;
    char *pool;
    size_t pool_bytes;
    Map map;        /* DLL (source) -> image */
} Image;
/* copy src (the DLL's Core, or another image's) into img, relocating through img->map */
void image_take(Image *img, const Core *src);
void image_free(Image *img);

/* compare Core a with Core b and their pools (b = a mapped through m). Returns 0 when
 * equal; else fills what/off/av/bv with the first difference. */
typedef struct {
    char what[64];
    size_t off;
    uint32_t av, bv;
} Diff;
int compare_core_pool(const Map *m, const Core *a, const Core *b, size_t pool_bytes, Diff *d);
/* n bytes as dwords, every word may be a pointer */
int compare_words(const Map *m, const void *a, const void *b, size_t n, const char *what, Diff *d);

#endif
