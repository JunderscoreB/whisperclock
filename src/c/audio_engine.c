#include <pebble.h>
#include "audio_engine.h"
#include "settings_engine.h"
#include "speaker_engine.h"

AudioQueueItem s_audio_playlist[10];
int s_playlist_size = 0;

// Queue Management Variables
static int s_current_word_index = 0;
static AppTimer *s_queue_timer = NULL;
static bool s_auto_exit = false;

extern WhisperSettings s_settings;

static void queue_audio(const char* filename, const char* display_text) {
  if (s_playlist_size < 10) {
    strncpy(s_audio_playlist[s_playlist_size].filename, filename, 16);
    strncpy(s_audio_playlist[s_playlist_size].display_text, display_text, 16);
    APP_LOG(APP_LOG_LEVEL_INFO, "QUEUED: %s -> %s", filename, display_text);
    s_playlist_size++;
  }
}

void generate_audio_playlist() {
  s_playlist_size = 0;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int hour = t->tm_hour;
  int min = t->tm_min;
  char num_buf[16];
  char text_buf[16];

  // The 3-state check
  bool is_military;
  if (s_settings.clock_mode == 1) {
    is_military = false; // Force 12h
  } else if (s_settings.clock_mode == 2) {
    is_military = true;  // Force 24h
  } else {
    is_military = clock_is_24h_style(); // Auto (Watch Setting)
  }

  // 1. Optional Prefix
  if (s_settings.say_its) {
    queue_audio("its.wav", "It's");
  }

  // ==========================================
  // PATH A: MILITARY TIME (24-Hour Clock)
  // ==========================================
  if (is_military) {
    if (hour == 0 && min == 0) {
      queue_audio("zero.wav", "Zero"); queue_audio("zero.wav", "Zero");
      queue_audio("zero.wav", "Zero"); queue_audio("zero.wav", "Zero");
      return;
    }

    // --- Military Hours ---
    if (hour < 20) {
      if (hour < 10) {
        queue_audio("zero.wav", "Zero");
        if (hour == 0) {
          queue_audio("zero.wav", "Zero");
        } else {
          snprintf(num_buf, sizeof(num_buf), "%d.wav", hour);
          snprintf(text_buf, sizeof(text_buf), "%d", hour);
          queue_audio(num_buf, text_buf);
        }
      } else {
        snprintf(num_buf, sizeof(num_buf), "%d.wav", hour);
        snprintf(text_buf, sizeof(text_buf), "%d", hour);
        queue_audio(num_buf, text_buf);
      }
    } else {
      // Stitched Hours for 20-23
      queue_audio("20.wav", "20");
      int unit = hour % 10;
      if (unit > 0) {
        snprintf(num_buf, sizeof(num_buf), "%d.wav", unit);
        snprintf(text_buf, sizeof(text_buf), "%d", unit);
        queue_audio(num_buf, text_buf);
      }
    }

    // --- Military Minutes ---
    if (min == 0) {
      queue_audio("hundred.wav", "Hundred");
      queue_audio("hours.wav", "Hours");
    } else if (min < 10) {
      queue_audio("zero.wav", "Zero");
      snprintf(num_buf, sizeof(num_buf), "%d.wav", min);
      snprintf(text_buf, sizeof(text_buf), "%d", min);
      queue_audio(num_buf, text_buf);
    } else if (min < 20) {
      snprintf(num_buf, sizeof(num_buf), "%d.wav", min);
      snprintf(text_buf, sizeof(text_buf), "%d", min);
      queue_audio(num_buf, text_buf);
    } else {
      // Stitched Military Minutes (20-59)
      int tens = (min / 10) * 10;
      int units = min % 10;

      snprintf(num_buf, sizeof(num_buf), "%d.wav", tens);
      snprintf(text_buf, sizeof(text_buf), "%d", tens);
      queue_audio(num_buf, text_buf);

      if (units > 0) {
        snprintf(num_buf, sizeof(num_buf), "%d.wav", units);
        snprintf(text_buf, sizeof(text_buf), "%d", units);
        queue_audio(num_buf, text_buf);
      }
    }
  }

  // ==========================================
  // PATH B: STANDARD TIME (12-Hour Clock)
  // ==========================================
  else {
    if (hour == 12 && min == 0) {
      queue_audio("noon.wav", "Noon");
      return;
    }
    if (hour == 0 && min == 0) {
      queue_audio("midnight.wav", "Midnight");
      return;
    }

    int display_hour = hour % 12;
    if (display_hour == 0) display_hour = 12;

    // --- Standard Hour ---
    snprintf(num_buf, sizeof(num_buf), "%d.wav", display_hour);
    snprintf(text_buf, sizeof(text_buf), "%d", display_hour);
    queue_audio(num_buf, text_buf);

    // --- Standard Minutes ---
    if (min == 0) {
      queue_audio("oclock.wav", "O'Clock");
    } else if (min < 10) {
      queue_audio("oh.wav", "Oh");
      snprintf(num_buf, sizeof(num_buf), "%d.wav", min);
      snprintf(text_buf, sizeof(text_buf), "%d", min);
      queue_audio(num_buf, text_buf);
    } else if (min < 20) {
      snprintf(num_buf, sizeof(num_buf), "%d.wav", min);
      snprintf(text_buf, sizeof(text_buf), "%d", min);
      queue_audio(num_buf, text_buf);
    } else {
      // Stitched Standard Minutes (20-59)
      int tens = (min / 10) * 10;
      int units = min % 10;

      snprintf(num_buf, sizeof(num_buf), "%d.wav", tens);
      snprintf(text_buf, sizeof(text_buf), "%d", tens);
      queue_audio(num_buf, text_buf);

      if (units > 0) {
        snprintf(num_buf, sizeof(num_buf), "%d.wav", units);
        snprintf(text_buf, sizeof(text_buf), "%d", units);
        queue_audio(num_buf, text_buf);
      }
    }

    // --- AM / PM ---
    if (s_settings.say_ampm) {
      if (hour < 12) {
        queue_audio("am.wav", "AM");
      } else {
        queue_audio("pm.wav", "PM");
      }
    }
  }
}

// ==========================================
// CALLBACKS AND TIMER LOGIC
// ==========================================

// Safe Exit Callback
static void exit_app_callback(void *data) {
  window_stack_pop_all(true);
}

// Graphic Dismiss Callback (ensures the window stays open while the last word finishes)
static void hide_graphic_callback(void *data) {
  hide_speaking_graphic();
}

// ==========================================
// SMART AUDIO PLAYBACK QUEUE
// ==========================================

static void play_next_word(void *data) {
  if (s_current_word_index >= s_playlist_size) {
    if (s_auto_exit) {
      window_stack_pop_all(true);
    }
    return;
  }

  const char* next_file = s_audio_playlist[s_current_word_index].filename;
  uint32_t clip_duration_ms = speaker_play_file(next_file);

  s_current_word_index++;

  if (s_current_word_index < s_playlist_size) {
    uint32_t time_to_wait = clip_duration_ms + s_settings.playback_speed;
    s_queue_timer = app_timer_register(time_to_wait, play_next_word, NULL);
  } else {
    // We are on the last word. We wait for it to finish plus a tiny buffer, then trigger cleanup.
    if (s_auto_exit) {
      s_queue_timer = app_timer_register(clip_duration_ms + 500, exit_app_callback, NULL);
    } else {
      s_queue_timer = app_timer_register(clip_duration_ms + 500, hide_graphic_callback, NULL);
    }
  }
}

void trigger_playback(bool auto_exit) {
  s_auto_exit = auto_exit;

  if (s_queue_timer) {
    app_timer_cancel(s_queue_timer);
    s_queue_timer = NULL;
  }

  generate_audio_playlist();

  s_current_word_index = 0;
  play_next_word(NULL);

  }

  void cancel_playback(void) {
    if (s_queue_timer) {
      app_timer_cancel(s_queue_timer);
      s_queue_timer = NULL;
    }
    s_current_word_index = s_playlist_size;
    speaker_cancel();

    if (s_auto_exit) {
      window_stack_pop_all(true);
    } else {
      hide_speaking_graphic();
    }
  }
