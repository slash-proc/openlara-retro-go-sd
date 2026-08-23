/*
 * Freestanding helpers for OpenLara fixed engine on Retro-Go SD (__GNW__).
 * Pulled in from patched third_party/OpenLara/src/fixed/common.h.
 */
#ifndef GNW_COMPAT_H
#define GNW_COMPAT_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>
/* Do not include <stdio.h>/<math.h>/<cmath> here — OpenLara #defines sin(). */

#ifdef __cplusplus
extern "C" {
#endif

/* OpenLara uses a sin() macro; provide abs() without pulling <cmath>. */
static inline int gnw_abs_i(int x)
{
    return x < 0 ? -x : x;
}
#ifndef abs
#define abs(x) gnw_abs_i(x)
#endif

static inline char *gnw_itoa(int value, char *str, int base)
{
    char tmp[16];
    int i = 0;
    int n = value;
    unsigned int u;
    (void)base;

    if (!str)
        return str;

    if (n == 0) {
        str[0] = '0';
        str[1] = '\0';
        return str;
    }

    if (n < 0) {
        *str++ = '-';
        u = (unsigned int)(-(n + 1)) + 1u;
    } else {
        u = (unsigned int)n;
    }

    while (u) {
        tmp[i++] = (char)('0' + (u % 10u));
        u /= 10u;
    }
    while (i--)
        *str++ = tmp[i];
    *str = '\0';
    return str;
}

#ifdef __cplusplus
}
#endif

/* Fixed engine includes <math.h> on other platforms; we do not need libm. */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#endif /* GNW_COMPAT_H */
