#ifndef CONVAI_PLATFORM_WIN_H
#define CONVAI_PLATFORM_WIN_H

#include "convai/convai_platform.h"

#ifdef __cplusplus
extern "C" {
#endif

int convai_platform_win_init(void);

uint64_t win_get_time_ms(void);

/**
 * Get device id (fixed "goldieos-sim" for the simulator).
 * Returns string length on success, -1 on failure.
 */
int win_device_id(char *buf, size_t len);

#ifdef __cplusplus
}
#endif

#endif /* CONVAI_PLATFORM_WIN_H */
