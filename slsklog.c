// LD_PRELOAD shim for SoulseekQt (Qt6): mirrors text appended to log widgets into a file.
// Build: gcc -shared -fPIC -O2 -o slsklog.so slsklog.c -ldl -lpthread
//
// Environment variables:
//   SLSK_HOOK_LOG       output file (default /data/.SoulseekQt/transfers.log)
//   SLSK_HOOK_MAX_SIZE  rotate when the file reaches this size, e.g. 500K, 50M, 1G or plain
//                       bytes. 0 = never rotate. Default 50M. On rotation the file is renamed
//                       to <file>.1 (replacing any older .1) and a new empty file is started.
//   SLSK_HOOK_VERBOSE   if set, prefix each line with epoch-ms, hook name and widget pointer
#define _GNU_SOURCE
#include <ctype.h>
#include <dlfcn.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_LOG "/data/.SoulseekQt/transfers.log"
#define DEFAULT_MAX (50LL * 1024 * 1024)

// Qt6 QString == QArrayDataPointer<char16_t> { d, ptr, size }
typedef struct { void *d; uint16_t *ptr; intptr_t size; } QString6;

static FILE *out;
static int inited, verbose;
static const char *path;
static char rotated_path[4096];
static long long max_size, cur_size;
static pthread_mutex_t mu = PTHREAD_MUTEX_INITIALIZER;

// "50M" -> 52428800. Accepts K/M/G with optional "B"/"iB" suffix, any case. Bad input -> default.
static long long parse_size(const char *s) {
    if (!s || !*s) return DEFAULT_MAX;
    char *end;
    double n = strtod(s, &end);
    if (end == s || n < 0) return DEFAULT_MAX;
    while (isspace((unsigned char)*end)) end++;
    long long mult = 1;
    switch (toupper((unsigned char)*end)) {
        case 'K': mult = 1024LL; end++; break;
        case 'M': mult = 1024LL * 1024; end++; break;
        case 'G': mult = 1024LL * 1024 * 1024; end++; break;
    }
    if (toupper((unsigned char)*end) == 'I') end++;
    if (toupper((unsigned char)*end) == 'B') end++;
    if (*end) return DEFAULT_MAX;
    return (long long)(n * mult);
}

static void open_out(void) {
    out = fopen(path, "a");
    if (!out) return;
    setvbuf(out, NULL, _IOLBF, 0);
    fseek(out, 0, SEEK_END);
    cur_size = ftell(out);
}

static void init(void) {
    inited = 1;
    verbose = getenv("SLSK_HOOK_VERBOSE") != NULL;    path = getenv("SLSK_HOOK_LOG");
    if (!path || !*path) path = DEFAULT_LOG;
    snprintf(rotated_path, sizeof rotated_path, "%s.1", path);
    max_size = parse_size(getenv("SLSK_HOOK_MAX_SIZE"));
    open_out();
}

static void rotate(void) {
    fclose(out);
    out = NULL;
    rename(path, rotated_path);  // replaces any previous .1
    open_out();
}

static void write_utf8(FILE *f, const uint16_t *s, intptr_t n) {
    for (intptr_t i = 0; i < n; i++) {
        uint32_t c = s[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < n) { c = 0x10000 + ((c - 0xD800) << 10) + (s[++i] - 0xDC00); }
        if (c == '\n' || c == '\r') { fputs("\\n", f); continue; }
        if (c < 0x80) fputc(c, f);
        else if (c < 0x800) { fputc(0xC0 | c >> 6, f); fputc(0x80 | (c & 63), f); }
        else if (c < 0x10000) { fputc(0xE0 | c >> 12, f); fputc(0x80 | (c >> 6 & 63), f); fputc(0x80 | (c & 63), f); }
        else { fputc(0xF0 | c >> 18, f); fputc(0x80 | (c >> 12 & 63), f); fputc(0x80 | (c >> 6 & 63), f); fputc(0x80 | (c & 63), f); }
    }
}

static void emit(const char *tag, void *self, const QString6 *s) {
    pthread_mutex_lock(&mu);
    if (!inited) init();
    if (out && s && s->ptr) {
        struct timespec ts; clock_gettime(CLOCK_REALTIME, &ts);
        // default output is byte-identical to what you'd copy out of Diagnostics > Logs;
        // SLSK_HOOK_VERBOSE=1 adds epoch-ms, hook name and widget pointer columns
        if (verbose) fprintf(out, "%ld.%03ld\t%s\t%p\t", (long)ts.tv_sec, ts.tv_nsec / 1000000, tag, self);
        write_utf8(out, s->ptr, s->size);
        fputc('\n', out);
        cur_size = ftell(out);
        // rotate only between lines, so no line is ever split across files
        if (max_size > 0 && cur_size >= max_size) rotate();
    }
    pthread_mutex_unlock(&mu);
}

#define HOOK1(mangled, tag)                                                     \
    void mangled(void *self, const QString6 *s) {                               \
        static void (*real)(void *, const QString6 *);                          \
        if (!real) real = dlsym(RTLD_NEXT, #mangled);                           \
        emit(tag, self, s);                                                     \
        if (real) real(self, s);                                                \
    }

HOOK1(_ZN14QPlainTextEdit15appendPlainTextERK7QString, "plain.append")
HOOK1(_ZN9QTextEdit6appendERK7QString, "text.append")
