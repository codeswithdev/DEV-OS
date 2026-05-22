#pragma once
#include "../../include/types.h"

void     timer_init(void);
uint64_t timer_ticks(void);
void     timer_sleep_ms(uint32_t ms);
