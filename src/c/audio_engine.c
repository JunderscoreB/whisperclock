/*
 * WhisperClock - Audio Engine Implementation
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#include <pebble.h>
#include "audio_engine.h"
#include "settings_engine.h"
#include "speaker_engine.h"

extern void hide_speaking_graphic(void);

AudioQueueItem s_audio_playlist[10];
int s_playlist_size = 0;

static int s_current_word_index = 0;
static AppTimer *s_queue_timer = NULL;
static bool s_auto_exit = false;

extern WhisperSettings s_settings;

static void queue_audio_ext(const char* filename, const char* display_text, int16_t delay_mod, int16_t trim_mod) {
  if (s_playlist_size < 10) {
    strncpy(s_audio_playlist[s_playlist_size].filename, filename, 16);
    strncpy(s_audio_playlist[s_playlist_size].display_text, display_text, 16);
    s_audio_playlist[s_playlist_size].delay_mod = delay_mod;
    s_audio_playlist[s_playlist_size].trim_mod = trim_mod;
    s_playlist_size++;
  }
}

static void queue_audio(const char* filename, const char* display_text) {
  queue_audio_ext(filename, display_text, 0, 0);
}

static void queue_number_ext(int number, int16_t delay_mod, int16_t trim_mod) {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", number);
  char file_buf[16];
  snprintf(file_buf, sizeof(file_buf), "%d.wav", number);
  queue_audio_ext(file_buf, buf, delay_mod, trim_mod);
}

static void queue_number(int number) {
  queue_number_ext(number, 0, 0);
}

void generate_audio_playlist() {
  s_playlist_size = 0;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  int hour = t->tm_hour;
  int min = t->tm_min;

  bool is_military;
  if (s_settings.clock_mode == 1) is_military = false;
  else if (s_settings.clock_mode == 2) is_military = true;
  else is_military = clock_is_24h_style();

  // Handle the new 3-way prefix mode logic
  if (s_settings.prefix_mode == 1) {
    queue_audio_ext("its.wav", "It's", -s_settings.playback_speed / 2, 40);
  } else if (s_settings.prefix_mode == 2) {
    queue_audio_ext("the-time-is.wav", "The time is", -s_settings.playback_speed / 2, 40);
  }

  int16_t hour_delay = 40;

  // --- NEW: Custom Noon/Midnight Handling for 12-Hour Mode ---
  if (!is_military && min == 0 && hour == 0) {
    queue_audio_ext("midnight.wav", "Midnight", hour_delay, 0);
    return; // Skip AM/PM suffix entirely
  } else if (!is_military && min == 0 && hour == 12) {
    queue_audio_ext("noon.wav", "Noon", hour_delay, 0);
    return; // Skip AM/PM suffix entirely
  }

  int display_hour = hour;
  if (!is_military) {
    display_hour = hour % 12;
    if (display_hour == 0) display_hour = 12;
  }

  // --- FIXED: Military Time Hour Parsing ---
  if (is_military && hour == 0) {
    queue_audio_ext("zero.wav", "Zero", -s_settings.playback_speed / 2, 60);
    if (min == 0) {
      queue_audio_ext("hundred.wav", "Hundred", hour_delay, 0);
    } else {
      // e.g. 00:15 becomes "Zero Zero Fifteen"
      queue_audio_ext("zero.wav", "Zero", hour_delay, 0);
    }
  } else if (is_military && hour < 10) {
    queue_audio_ext("zero.wav", "Zero", -s_settings.playback_speed / 2, 60);
    queue_number_ext(hour, hour_delay, 0);
  } else if (is_military && hour >= 20) {
    if (hour % 10 != 0) {
      queue_number_ext(20, -s_settings.playback_speed, 140);
      queue_number_ext(hour % 10, hour_delay, 0);
    } else {
      queue_number_ext(20, hour_delay, 0);
    }
  } else {
    queue_number_ext(display_hour, hour_delay, 0);
  }

  // --- FIXED: Military Time Minute Parsing ---
  if (min == 0) {
    if (is_military) {
      // Prevent "Zero hundred hundred" by only saying hundred if hour != 0
      if (hour != 0) {
        queue_audio("hundred.wav", "Hundred");
      }
    }
    else queue_audio("oclock.wav", "O'clock");
  } else if (min < 10) {
    if (is_military) {
      queue_audio_ext("zero.wav", "Zero", -s_settings.playback_speed, 100);
    } else {
      queue_audio_ext("oh.wav", "Oh", -s_settings.playback_speed, 100);
    }
    queue_number(min);
  } else if (min >= 10 && min <= 19) {
    queue_number(min);
  } else {
    int tens = (min / 10) * 10;
    int ones = min % 10;

    if (ones > 0) {
      queue_number_ext(tens, -s_settings.playback_speed, 200);
      queue_number(ones);
    } else {
      queue_number(tens);
    }
  }

  // --- SUFFIX ATTACHMENT LOGIC ---
  if (!is_military && s_settings.say_ampm) {
    // Normal spacing restored for AM/PM to avoid rushing.
    if (hour < 12) queue_audio("am.wav", "AM");
    else queue_audio("pm.wav", "PM");
  } else if (is_military && s_settings.say_ampm) {
    // Retained the aggressive compression for the word "Hours" in military time.
    if (s_playlist_size > 0) {
      s_audio_playlist[s_playlist_size - 1].delay_mod = -s_settings.playback_speed;
      s_audio_playlist[s_playlist_size - 1].trim_mod = 60;
    }
    queue_audio("hours.wav", "Hours");
  }
}

void cancel_playback(void) {
  if (s_queue_timer) {
    app_timer_cancel(s_queue_timer);
    s_queue_timer = NULL;
  }
  speaker_cancel();
}

static void exit_app_callback(void *data) {
  s_queue_timer = NULL;
  window_stack_pop_all(true);
}

static void hide_graphic_callback(void *data) {
  s_queue_timer = NULL;
  hide_speaking_graphic();
}

static void play_next_word(void *data) {
  s_queue_timer = NULL;

  if (s_current_word_index >= s_playlist_size) {
    if (s_auto_exit) window_stack_pop_all(true);
    return;
  }

  const char* next_file = s_audio_playlist[s_current_word_index].filename;
  int16_t trim_mod = s_audio_playlist[s_current_word_index].trim_mod;
  int16_t delay_mod = s_audio_playlist[s_current_word_index].delay_mod;

  uint32_t clip_duration_ms = speaker_play_file(next_file, trim_mod);

  s_current_word_index++;

  if (s_current_word_index < s_playlist_size) {
    int32_t time_to_wait = (int32_t)clip_duration_ms + (int32_t)s_settings.playback_speed + (int32_t)delay_mod;
    if (time_to_wait < 1) time_to_wait = 1;

    s_queue_timer = app_timer_register((uint32_t)time_to_wait, play_next_word, NULL);
  } else {
    if (s_auto_exit) {
      s_queue_timer = app_timer_register(clip_duration_ms + 2500, exit_app_callback, NULL);
    } else {
      s_queue_timer = app_timer_register(clip_duration_ms + 2500, hide_graphic_callback, NULL);
    }
  }
}

void trigger_playback(bool auto_exit) {
  s_auto_exit = auto_exit;
  generate_audio_playlist();
  s_current_word_index = 0;

  cancel_playback();
  play_next_word(NULL);
}
