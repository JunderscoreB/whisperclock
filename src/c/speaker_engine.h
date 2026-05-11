#pragma once
#include <pebble.h>

void speaker_init(void);
uint32_t speaker_play_file(const char* filename);
void speaker_cancel(void); // NEW: The hardware kill switch
