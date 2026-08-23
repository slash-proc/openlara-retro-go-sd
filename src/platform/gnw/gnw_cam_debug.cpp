#include "gnw_cam_debug.h"

#if defined(__GNW__) || defined(HOST_BUILD)

int32_t gCamDbgFrame;

#if GNW_CAM_DEBUG

extern "C" {
#include <stdio.h>
#include <stdarg.h>
}

void gnwCamDbgReset(void)
{
    gCamDbgFrame = 0;
}

void gnwCamLog(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
}

#else /* !GNW_CAM_DEBUG */

void gnwCamDbgReset(void)
{
    gCamDbgFrame = 0;
}

void gnwCamLog(const char *fmt, ...)
{
    (void)fmt;
}

#endif /* GNW_CAM_DEBUG */

#endif /* __GNW__ || HOST_BUILD */
