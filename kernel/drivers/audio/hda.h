/* DevOS — Intel HD Audio driver (hda.h) */
#pragma once
#include "../../include/types.h"

bool hda_init(void);
int  hda_play_beep(uint32_t freq_hz, uint32_t duration_ms);
