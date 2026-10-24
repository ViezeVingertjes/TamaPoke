#pragma once
#include <stdint.h>
// light sleep: does nothing on the PC (the main loop does not run in the renderer)
static inline int esp_sleep_enable_timer_wakeup(uint64_t) { return 0; }
static inline int esp_sleep_enable_gpio_wakeup() { return 0; }
static inline int esp_light_sleep_start() { return 0; }
