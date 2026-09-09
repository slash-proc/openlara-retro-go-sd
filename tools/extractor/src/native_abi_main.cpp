/*
 * The same ABI, linked natively.
 *
 * check.sh runs this under the host's sanitizers to find in ordinary tooling
 * what a wasm trap can only report as "null function or function signature
 * mismatch". It drives exactly the exported entry points a host drives, so
 * anything it finds is a real bug in the module, not in a test double.
 *
 *   native_abi <in.PHD> <out.PKD>
 */
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>

/* Matches abi.cpp's abi_ptr: a u32 offset on wasm, a real pointer here. */
typedef uintptr_t abi_ptr;

extern "C" {
uint32_t abi_version(void);
abi_ptr alloc(uint32_t len);
void input_clear(void);
uint32_t input_add(abi_ptr ptr, uint32_t len);
uint32_t run(uint32_t flags);
uint32_t output_count(void);
abi_ptr output_ptr(uint32_t i);
uint32_t output_len(uint32_t i);
abi_ptr output_name_ptr(uint32_t i);
uint32_t output_name_len(uint32_t i);
abi_ptr error_ptr(void);
uint32_t error_len(void);
abi_ptr warnings_ptr(void);
uint32_t warnings_len(void);
}

static unsigned char* g_alloc;

int main(int argc, char** argv)
{
    if (argc < 3) { fprintf(stderr, "usage: native_abi <in.PHD> <out.PKD>\n"); return 2; }

    FILE* f = fopen(argv[1], "rb");
    if (!f) { perror(argv[1]); return 1; }
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    g_alloc = (unsigned char*)malloc(n);
    if (fread(g_alloc, 1, n, f) != (size_t)n) { fprintf(stderr, "short read\n"); return 1; }
    fclose(f);

    input_clear();
    input_add((abi_ptr)(uintptr_t)g_alloc, (uint32_t)n);
    uint32_t rc = run(0);
    if (warnings_len())
        fprintf(stderr, "warning: %.*s\n", (int)warnings_len(), (const char*)(uintptr_t)warnings_ptr());
    if (rc != 0) {
        fprintf(stderr, "run failed (%u): %.*s\n", rc,
                (int)error_len(), (const char*)(uintptr_t)error_ptr());
        return 1;
    }
    if (output_count() != 1) { fprintf(stderr, "no output\n"); return 1; }
    fprintf(stderr, "output id \"%.*s\", %u bytes\n",
            (int)output_name_len(0), (const char*)(uintptr_t)output_name_ptr(0), output_len(0));

    FILE* o = fopen(argv[2], "wb");
    if (!o) { perror(argv[2]); return 1; }
    fwrite((const void*)(uintptr_t)output_ptr(0), 1, output_len(0), o);
    fclose(o);
    return 0;
}
