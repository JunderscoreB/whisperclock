#pragma once
#include <pebble.h>

typedef struct {
  char filename[16];
  char display_text[16];
} AudioQueueItem;

void generate_audio_playlist(void);
void trigger_playback(bool auto_exit);
void cancel_playback(void); // NEW: Exported so the UI can trigger it
