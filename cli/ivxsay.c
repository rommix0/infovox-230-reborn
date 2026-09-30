/* ivxsay: text to speech with the infovox230 library (include/infovox230.h).
 *
 * The text comes from the arguments, a file (-i) or stdin, as UTF-8 (or Windows-1252
 * with -e cp1252), and goes to the speaker, or to a WAV file with -o. tools/check_ivxsay.py
 * requires the WAV files to be byte-identical to the DLL-verified audio of tests/tts_cmp.c.
 * -d loads dictionary files, in the same encoding as the text.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <unistd.h>
#endif
#include "infovox230.h"
#include "audio.h"

#define BLOCK 441
#define MAXDICT 16

static FILE *out;
static long nsamples;
static int speaker;
static volatile sig_atomic_t interrupted;

static void usage(void)
{
    fprintf(stderr,
        "usage: ivxsay -l LANG [options] [text ...]\n"
        "  -l LANG   .ivx file, or a language code (sw, no, da, fi, ic, ge, du, fr, it, sp, bl, am)\n"
        "            for bin/<code>rules.ivx, else lang/<code>rules.ivx next to ivxsay\n"
        "  -o FILE   write a WAV file instead of playing through the speaker\n"
        "  -i FILE   read the text from FILE (default: the arguments, else stdin)\n"
        "  -e ENC    text encoding: utf8 (default) or cp1252\n"
        "  -s HZ     sample rate 8000..48000 (default 22050)\n"
        "  -v N      voice 0..9\n"
        "  -r N      rate 0..100 (a voice parameter, not speed; use -F for speed)\n"
        "  -p N      pitch 0..100\n"
        "  -f N      FD 0..100\n"
        "  -m N      text mode flags (default 8 = whole sentences)\n"
        "  -F N      synth frames per second 2..1000 (default 100 = normal speed)\n"
        "  -d FILE   dictionary, one entry per line: [!?]word = replacement\n"
        "            (/.../ phonetic, ! exact case, ? only words the lexicon doesn't know,\n"
        "            # comment); up to 16 files, loaded in order\n"
        "  -E        print events (marks, phonemes, errors, done) to stderr\n");
    exit(2);
}

/* the directory of the running executable, with its trailing separator; "" if unknown */
static void exe_dir(char *buf, size_t n)
{
    size_t len = 0;
#ifdef _WIN32
    DWORD r = GetModuleFileNameA(NULL, buf, (DWORD)n);
    if (r > 0 && r < n)
        len = r;
#else
    ssize_t r = readlink("/proc/self/exe", buf, n - 1);
    if (r > 0)
        len = (size_t)r;
#endif
    while (len > 0 && buf[len - 1] != '/' && buf[len - 1] != '\\')
        len--;
    buf[len] = 0;
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
static void load_dict(IvxEngine *e, const char *path, int enc)
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
        if (n == 1 && enc == IVX_ENC_UTF8 && strlen(line) >= 3
            && memcmp(line, "\xef\xbb\xbf", 3) == 0)
            memmove(line, line + 3, strlen(line + 3) + 1);
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
        if ((flags & IVX_DICT_FALLBACK) && (flags & IVX_DICT_PHONETIC))
            dict_error(path, n, "a fallback (?) cannot be phonetic (/.../)", "");
        r = ivx_dict_add(e, key, rep, flags);
        if (r == IVX_E_RANGE)
            dict_error(path, n, "cannot add the entry: a word cannot be empty or longer than 127 "
                       "bytes, or contain blanks, digits or !#$%&()*+,-.;<=>?^_ (a fallback "
                       "replacement follows the same rule); a text or phonetic replacement cannot "
                       "be empty or longer than 255 bytes", "");
        else if (r != 0)
            dict_error(path, n, "cannot add the entry: ", ivx_strerror(r));
    }
    fclose(f);
}

static unsigned char *read_all(FILE *f, long *size)
{
    size_t cap = 4096, n = 0, k;
    unsigned char *b = malloc(cap);

    while (b != NULL && (k = fread(b + n, 1, cap - n, f)) > 0) {
        n += k;
        if (n == cap)
            b = realloc(b, cap *= 2);
    }
    if (b == NULL) {
        fprintf(stderr, "ivxsay: out of memory\n");
        exit(1);
    }
    *size = (long)n;
    return b;
}

static void wav_header(int fs, long n)
{
    unsigned char h[44];
    uint32_t v[] = {36 + 2 * (uint32_t)n, 16, 1 | 1 << 16, (uint32_t)fs, 2 * (uint32_t)fs,
                    2 | 16 << 16, 2 * (uint32_t)n};

    memcpy(h, "RIFF", 4);
    memcpy(h + 4, &v[0], 4);
    memcpy(h + 8, "WAVEfmt ", 8);
    memcpy(h + 16, &v[1], 20);
    memcpy(h + 36, "data", 4);
    memcpy(h + 40, &v[6], 4);
    fseek(out, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, out);
}

static void emit(const short *pcm, int n)
{
    if (speaker)
        audio_write(pcm, n);
    else
        fwrite(pcm, 2, (size_t)n, out);
    nsamples += n;
}

static void on_sigint(int sig)
{
    (void)sig;
    interrupted = 1;
}

static void print_event(const IvxEvent *ev, int fs)
{
    static const char *const names[] = {"mark", "phoneme", "error", "done"};

    fprintf(stderr, "%10.3f %-7s ", (double)ev->sample / fs,
            ev->type >= 0 && ev->type < 4 ? names[ev->type] : "?");
    if (ev->type == IVX_EV_PHONEME)
        fprintf(stderr, "%c%c\n", (int)(ev->value & 0xff), ev->value >> 8 ? (int)(ev->value >> 8) : ' ');
    else
        fprintf(stderr, "%lu\n", (unsigned long)ev->value);
}

static int set(IvxEngine *e, int param, int value, const char *what)
{
    int r = ivx_set(e, param, value);

    if (r != 0)
        fprintf(stderr, "ivxsay: %s %d: %s\n", what, value, ivx_strerror(r));
    return r;
}

static int opt_int(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc)
        usage();
    return atoi(argv[++*i]);
}

int main(int argc, char **argv)
{
    const char *lang = NULL, *outpath = NULL, *inpath = NULL, *dicts[MAXDICT];
    int fs = 22050, voice = -1, rate = -1, pitch = -1, fd = -1, mode = 8, fps = 0;
    int enc = IVX_ENC_UTF8, events = 0, ndict = 0, err, r, n, i;
    unsigned char *text = NULL;
    long tlen = 0, k;
    char path[512];
    short buf[BLOCK];
    IvxEngine *e;
    IvxEvent ev;
    FILE *f;

    for (i = 1; i < argc && argv[i][0] == '-' && argv[i][1] != 0; i++) {
        switch (argv[i][1]) {
        case 'l': if (i + 1 >= argc) usage(); lang = argv[++i]; break;
        case 'o': if (i + 1 >= argc) usage(); outpath = argv[++i]; break;
        case 'i': if (i + 1 >= argc) usage(); inpath = argv[++i]; break;
        case 'd': if (i + 1 >= argc || ndict == MAXDICT) usage(); dicts[ndict++] = argv[++i]; break;
        case 'e':
            if (i + 1 >= argc) usage();
            i++;
            if (strcmp(argv[i], "utf8") == 0) enc = IVX_ENC_UTF8;
            else if (strcmp(argv[i], "cp1252") == 0) enc = IVX_ENC_CP1252;
            else usage();
            break;
        case 's': fs = opt_int(argc, argv, &i); break;
        case 'v': voice = opt_int(argc, argv, &i); break;
        case 'r': rate = opt_int(argc, argv, &i); break;
        case 'p': pitch = opt_int(argc, argv, &i); break;
        case 'f': fd = opt_int(argc, argv, &i); break;
        case 'm': mode = opt_int(argc, argv, &i); break;
        case 'F': fps = opt_int(argc, argv, &i); break;
        case 'E': events = 1; break;
        default: usage();
        }
    }
    if (lang == NULL)
        usage();

    e = ivx_create(&err);
    if (e == NULL) {
        fprintf(stderr, "ivxsay: %s\n", ivx_strerror(err));
        return 1;
    }
    if (set(e, IVX_SAMPLE_RATE, fs, "sample rate") || set(e, IVX_TEXT_MODE, mode, "text mode")
        || set(e, IVX_INPUT_ENCODING, enc, "encoding")
        || (events && set(e, IVX_EVENT_MASK, IVX_EVMASK_ALL, "event mask")))
        return 1;
    r = ivx_load_language(e, lang);
    if (r == IVX_E_IO && strlen(lang) == 2) {
        snprintf(path, sizeof path, "bin/%srules.ivx", lang);
        r = ivx_load_language(e, path);
        if (r == IVX_E_IO) {    /* a release folder: lang/ next to the executable */
            char dir[400];
            exe_dir(dir, sizeof dir);
            if (dir[0]) {
                snprintf(path, sizeof path, "%slang/%srules.ivx", dir, lang);
                r = ivx_load_language(e, path);
            }
        }
    }
    if (r != 0) {
        fprintf(stderr, "ivxsay: %s: %s\n", lang, ivx_strerror(r));
        return 1;
    }
    if ((voice >= 0 && set(e, IVX_VOICE, voice, "voice")) || (rate >= 0 && set(e, IVX_RATE, rate, "rate"))
        || (pitch >= 0 && set(e, IVX_PITCH, pitch, "pitch")) || (fd >= 0 && set(e, IVX_FD, fd, "FD"))
        || (fps && set(e, IVX_FRAME_RATE, fps, "frame rate")))
        return 1;
    for (k = 0; k < ndict; k++)
        load_dict(e, dicts[k], enc);

    if (inpath != NULL) {
        f = fopen(inpath, "rb");
        if (f == NULL) {
            fprintf(stderr, "ivxsay: cannot read %s\n", inpath);
            return 1;
        }
        text = read_all(f, &tlen);
        fclose(f);
    } else if (i < argc) {
        for (k = i; k < argc; k++)
            tlen += (long)strlen(argv[k]) + 1;
        text = calloc(1, (size_t)tlen + 1);
        for (tlen = 0; i < argc; i++)
            tlen += sprintf((char *)text + tlen, "%s%s", tlen ? " " : "", argv[i]);
    } else {
        text = read_all(stdin, &tlen);
    }

    if (outpath == NULL) {
        speaker = 1;
        if (audio_open(fs) != 0) {
            fprintf(stderr, "ivxsay: cannot open the audio device\n");
            return 1;
        }
    } else {
        out = fopen(outpath, "wb");
        if (out == NULL) {
            fprintf(stderr, "ivxsay: cannot write %s\n", outpath);
            return 1;
        }
        wav_header(fs, 0);
    }
    signal(SIGINT, on_sigint);
    if ((r = ivx_feed(e, (const char *)text, (size_t)tlen)) != 0 || (r = ivx_flush(e)) != 0) {
        fprintf(stderr, "ivxsay: %s\n", ivx_strerror(r));
        return 1;
    }
    for (;;) {
        n = interrupted ? 0 : ivx_read(e, buf, BLOCK);
        if (n < 0) {
            fprintf(stderr, "ivxsay: %s\n", ivx_strerror(n));
            return 1;
        }
        if (n > 0)
            emit(buf, n);
        while (ivx_next_event(e, &ev) == 1)
            if (events)
                print_event(&ev, fs);
        if (n == 0)
            break;
    }
    if (interrupted)
        ivx_cancel(e);
    if (speaker) {
        audio_close(!interrupted);
    } else {
        wav_header(fs, nsamples);
        fclose(out);
        printf("%s: %ld samples, %.2f s at %d Hz\n", outpath, nsamples, (double)nsamples / fs, fs);
    }
    ivx_destroy(e);
    free(text);
    return 0;
}
