#pragma once
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif
// Monotonic milliseconds, read on demand by LVGL instead of a 2 ms timer.
uint32_t rr_lvgl_millis(void);
#ifdef __cplusplus
}
#endif
