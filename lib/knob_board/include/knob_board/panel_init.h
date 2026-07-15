#pragma once

#include <stddef.h>

#include "knob_board/panel_driver.h"

#ifdef __cplusplus
extern "C" {
#endif

const knob_lcd_init_cmd_t* knob_lcd_init_commands(size_t* count);

#ifdef __cplusplus
}
#endif

