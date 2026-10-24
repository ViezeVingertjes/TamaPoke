#pragma once
typedef int gpio_num_t;
#define GPIO_INTR_LOW_LEVEL 4
static inline int gpio_wakeup_enable(gpio_num_t, int) { return 0; }
#define GPIO_INTR_NEGEDGE 2
static inline int gpio_wakeup_disable(gpio_num_t) { return 0; }
static inline int gpio_set_intr_type(gpio_num_t, int) { return 0; }
