/*
 * Stands in for <windows.h> in the OpenLara GBA packer.
 *
 * The packer needs four things from Windows and nothing else: DebugBreak for
 * its ASSERT, and the FindFirstFile family for one directory walk. This header
 * supplies both in a form that compiles for the host compiler and for
 * wasm32-wasip1 alike, so the module and the native oracle in check.sh are
 * built from the very same sources.
 *
 * The directory walk lives in out_GBA::convertTracks, which converts a folder
 * of pre-encoded .ad4 chunks into TRACKS.AD4. The PHD -> PKD path never calls
 * it -- this module converts one level and emits one .PKD -- but the function
 * is a member of the same struct and so must still compile. Rather than
 * pretending to enumerate a filesystem that does not exist inside the module,
 * the search reports "nothing found". That is the truthful answer here: a
 * module that imports nothing has no directory to walk.
 */
#ifndef H_PACKER_COMPAT
#define H_PACKER_COMPAT

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifndef MAX_PATH
#define MAX_PATH 1024
#endif

#define FILE_ATTRIBUTE_DIRECTORY 0x10
#define INVALID_HANDLE_VALUE ((void*)0)

typedef void* HANDLE;

typedef struct {
    char cFileName[MAX_PATH];
    unsigned dwFileAttributes;
} WIN32_FIND_DATA;

static inline void DebugBreak(void)
{
    /*
     * Upstream traps into a debugger. Here an ASSERT means the packer met a
     * level it cannot represent, and there is no debugger and no way to
     * unwind: wasm cannot throw or longjmp without the exception-handling
     * proposal. abort() ends in proc_exit, which src/stubs.c turns into a
     * wasm trap -- the host sees a dead instance and reads the message the
     * ABI wrote before the call. See the comment in abi.cpp.
     */
    abort();
}

static inline HANDLE FindFirstFile(const char* pattern, WIN32_FIND_DATA* fd)
{
    (void)pattern;
    if (fd) memset(fd, 0, sizeof(*fd));
    return INVALID_HANDLE_VALUE;
}

static inline int FindNextFile(HANDLE h, WIN32_FIND_DATA* fd)
{
    (void)h; (void)fd;
    return 0;
}

static inline void FindClose(HANDLE h) { (void)h; }

#endif
