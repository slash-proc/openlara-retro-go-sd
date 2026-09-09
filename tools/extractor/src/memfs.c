/*
 * The packer's filesystem: two files, both in linear memory.
 *
 * out_GBA::convertGBA reads its level through a FileStream and writes the
 * .PKD through another, and FileStream is fopen/fread/fwrite/fseek/ftell/
 * fclose over a real FILE*. There is no real filesystem here and there
 * cannot be one -- the module imports nothing -- so those six calls are
 * intercepted with wasm-ld's --wrap and served from two buffers the ABI owns.
 *
 * --wrap rather than redefining fopen, because libc's own stdio has to keep
 * working for everything else: the packer prints progress with printf and
 * asserts through fprintf(stderr). A handle we did not create goes straight
 * to __real_*, so those behave exactly as they otherwise would, which with
 * the WASI stubs in place means they go nowhere.
 *
 * Two details that are not incidental:
 *
 *   1. ftell() must be exact. Every offset in the .PKD header is a getPos()
 *      result, and align4() pads by the difference between a position and
 *      its rounding. A position that is off by one byte moves every lump.
 *   2. fputc and fputs are wrapped even though FileStream never calls them.
 *      At -O1 and above clang rewrites fwrite(p, 1, 1, f) into fputc, and
 *      FileStream::write(uint8) is exactly that shape. Unwrapped, the call
 *      reached libc's real fputc with one of our fake handles, which walked
 *      a FILE structure that does not exist and trapped inside __overflow.
 *      A compiler is entitled to that substitution; the fix is to intercept
 *      what it can produce, not only what the source spells out.
 *   3. The output length is a HIGH-WATER MARK, not the final position.
 *      convertGBA seeks back to offset 0 and rewrites the header last, so
 *      the position at fclose() is the size of the header, not of the file.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern FILE *__real_fopen(const char *path, const char *mode);
extern size_t __real_fread(void *p, size_t size, size_t n, FILE *f);
extern size_t __real_fwrite(const void *p, size_t size, size_t n, FILE *f);
extern int __real_fclose(FILE *f);
extern int __real_fseek(FILE *f, long off, int whence);
extern long __real_ftell(FILE *f);
extern int __real_fputc(int c, FILE *f);
extern int __real_fputs(const char *s, FILE *f);

/* The names abi.cpp hands the packer. Matched exactly, nothing else. */
#define INPUT_NAME  "input.PHD"
#define OUTPUT_NAME "output.PKD"

typedef struct {
    int used;
    int writing;
    unsigned char *data;   /* input: borrowed. output: owned, realloc'd */
    size_t size;           /* bytes that exist (high-water mark) */
    size_t cap;            /* output only */
    size_t pos;
} memfile;

/* Two slots: one level in, one .PKD out. Static, so a handle's address is
   stable and is_ours() is a range check rather than bookkeeping. */
static memfile g_files[2];

static const unsigned char *g_input;
static size_t g_input_len;

static unsigned char *g_output;
static size_t g_output_len;

/*
 * Set when a read of the input asks for more bytes than are left.
 *
 * A .PHD is a chain of counted arrays, so the only way to validate one is to
 * parse it, and the parse cannot be unwound out of -- there are no exceptions
 * and no setjmp on this target. This flag is how a truncated or wrong-format
 * file becomes an ordinary error return instead of a trap: the parse runs to
 * the end over zeros, and abi.cpp checks the flag afterwards. The zero fill
 * below is what keeps the parse harmless, since FileStream::read leaves its
 * result untouched when fread comes up short and would otherwise turn stack
 * garbage into an array length.
 */
static int g_input_truncated;

int pkd_memfs_input_truncated(void) { return g_input_truncated; }

void pkd_memfs_set_input(const unsigned char *data, size_t len) {
    g_input = data;
    g_input_len = len;
}

const unsigned char *pkd_memfs_output(size_t *len) {
    *len = g_output_len;
    return g_output;
}

void pkd_memfs_reset(void) {
    for (unsigned i = 0; i < sizeof g_files / sizeof g_files[0]; i++) {
        if (g_files[i].used && g_files[i].writing) free(g_files[i].data);
        memset(&g_files[i], 0, sizeof g_files[i]);
    }
    free(g_output);
    g_output = NULL;
    g_output_len = 0;
    g_input = NULL;
    g_input_len = 0;
    g_input_truncated = 0;
}

static int is_ours(FILE *f) {
    return (void *)f >= (void *)&g_files[0] &&
           (void *)f < (void *)&g_files[sizeof g_files / sizeof g_files[0]];
}

FILE *__wrap_fopen(const char *path, const char *mode) {
    if (!path) return NULL;
    int writing = mode && (strchr(mode, 'w') || strchr(mode, 'a'));

    if (!writing && strcmp(path, INPUT_NAME) == 0) {
        if (!g_input) return NULL;
        for (unsigned i = 0; i < sizeof g_files / sizeof g_files[0]; i++) {
            if (g_files[i].used) continue;
            g_files[i].used = 1;
            g_files[i].writing = 0;
            g_files[i].data = (unsigned char *)g_input;
            g_files[i].size = g_input_len;
            g_files[i].pos = 0;
            return (FILE *)&g_files[i];
        }
        return NULL;
    }

    if (writing && strcmp(path, OUTPUT_NAME) == 0) {
        for (unsigned i = 0; i < sizeof g_files / sizeof g_files[0]; i++) {
            if (g_files[i].used) continue;
            g_files[i].used = 1;
            g_files[i].writing = 1;
            g_files[i].data = NULL;
            g_files[i].size = 0;
            g_files[i].cap = 0;
            g_files[i].pos = 0;
            return (FILE *)&g_files[i];
        }
        return NULL;
    }

    /* Any other path is not ours. saveBitmap and convertScreen open files by
       name; neither is on this path, and letting libc fail them is the right
       answer rather than inventing a file. */
    return __real_fopen(path, mode);
}

size_t __wrap_fread(void *p, size_t size, size_t n, FILE *f) {
    if (!is_ours(f)) return __real_fread(p, size, n, f);
    memfile *m = (memfile *)f;
    if (size == 0 || n == 0) return 0;
    size_t want = size * n;
    size_t avail = m->pos < m->size ? m->size - m->pos : 0;
    size_t items = (want <= avail) ? n : (avail / size);
    if (items) {
        memcpy(p, m->data + m->pos, items * size);
        m->pos += items * size;
    }
    if (items != n) {
        memset((unsigned char *)p + items * size, 0, want - items * size);
        if (!m->writing) g_input_truncated = 1;
    }
    return items;
}

static int grow(memfile *m, size_t need) {
    if (need <= m->cap) return 1;
    size_t cap = m->cap ? m->cap : (4u << 20);
    while (cap < need) cap += cap / 2;
    unsigned char *p = (unsigned char *)realloc(m->data, cap);
    if (!p) return 0;
    /* Seeking past the end and then writing must read back as zeros, not as
       whatever realloc handed us: align4() leaves gaps it never fills. */
    memset(p + m->cap, 0, cap - m->cap);
    m->data = p;
    m->cap = cap;
    return 1;
}

size_t __wrap_fwrite(const void *p, size_t size, size_t n, FILE *f) {
    if (!is_ours(f)) return __real_fwrite(p, size, n, f);
    memfile *m = (memfile *)f;
    if (!m->writing || size == 0 || n == 0) return 0;
    size_t bytes = size * n;
    if (!grow(m, m->pos + bytes)) return 0;
    memcpy(m->data + m->pos, p, bytes);
    m->pos += bytes;
    if (m->pos > m->size) m->size = m->pos; /* high-water mark */
    return n;
}

int __wrap_fseek(FILE *f, long off, int whence) {
    if (!is_ours(f)) return __real_fseek(f, off, whence);
    memfile *m = (memfile *)f;
    long base = (whence == SEEK_SET) ? 0 : (whence == SEEK_CUR) ? (long)m->pos
                                                                : (long)m->size;
    long want = base + off;
    if (want < 0) return -1;
    m->pos = (size_t)want;
    return 0;
}

long __wrap_ftell(FILE *f) {
    if (!is_ours(f)) return __real_ftell(f);
    return (long)((memfile *)f)->pos;
}

int __wrap_fputc(int c, FILE *f) {
    if (!is_ours(f)) return __real_fputc(c, f);
    unsigned char b = (unsigned char)c;
    return __wrap_fwrite(&b, 1, 1, f) == 1 ? (int)b : -1;
}

int __wrap_fputs(const char *s, FILE *f) {
    if (!is_ours(f)) return __real_fputs(s, f);
    size_t n = strlen(s);
    return __wrap_fwrite(s, 1, n, f) == n ? (int)n : -1;
}

int __wrap_fclose(FILE *f) {
    if (!is_ours(f)) return __real_fclose(f);
    memfile *m = (memfile *)f;
    if (m->writing) {
        /* Hand the bytes to the ABI. Ownership moves; the slot keeps nothing. */
        free(g_output);
        g_output = m->data;
        g_output_len = m->size;
    }
    memset(m, 0, sizeof *m);
    return 0;
}
