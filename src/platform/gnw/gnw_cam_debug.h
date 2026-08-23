#ifndef GNW_CAM_DEBUG_H
#define GNW_CAM_DEBUG_H

#include <stdint.h>

/*
 * Camera / trigger diagnostics for __GNW__ and host builds.
 * Logs go to printf (USB serial on device, terminal on host).
 * Set GNW_CAM_DEBUG to 0 in the build to silence.
 */
#if defined(__GNW__) || defined(HOST_BUILD)

#ifndef GNW_CAM_DEBUG
#define GNW_CAM_DEBUG 1
#endif

#ifdef __cplusplus
extern "C" {
#endif

void gnwCamLog(const char *fmt, ...);
void gnwCamDbgReset(void);
extern int32_t gCamDbgFrame;

#ifdef __cplusplus
}
#endif

#if GNW_CAM_DEBUG
#define CAM_LOG(...) gnwCamLog(__VA_ARGS__)
#else
#define CAM_LOG(...) ((void)0)
#endif

#else /* !__GNW__ && !HOST_BUILD */

#define CAM_LOG(...) ((void)0)
static inline void gnwCamDbgReset(void) {}

#endif

#endif /* GNW_CAM_DEBUG_H */
