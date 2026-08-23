#include "video_scratch.h"
#include "gw_malloc.h"
#include <stdio.h>

uint8_t *g_scratch;

bool video_scratch_acquire(void)
{
    if (g_scratch)
        return true;
    g_scratch = ram_malloc(VIDEO_SCRATCH_SIZE);
    printf("fm: scratch %p need=%u free=%lu\n",
           (void *)g_scratch, (unsigned)VIDEO_SCRATCH_SIZE,
           (unsigned long)ram_get_free_size());
    return g_scratch != NULL;
}

void video_scratch_release(void)
{
    /* ram_malloc has no free — keep the 256 KiB buffer for the next cutscene. */
}
