#pragma once
#include <stdint.h>
// light sleep: en el PC no hace nada (el bucle principal no se ejecuta en el renderer)
static inline int esp_sleep_enable_timer_wakeup(uint64_t) { return 0; }
static inline int esp_sleep_enable_gpio_wakeup() { return 0; }
static inline int esp_light_sleep_start() { return 0; }
