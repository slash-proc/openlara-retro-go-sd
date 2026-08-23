#pragma once

/* No resume for OpenLara cutscenes — always start from frame 0. */
static inline int video_resume_get(const char *path)
{
    (void)path;
    return 0;
}

static inline void video_resume_put(const char *path, int frame, int total)
{
    (void)path;
    (void)frame;
    (void)total;
}
