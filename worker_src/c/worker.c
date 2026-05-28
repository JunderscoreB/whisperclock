/*
 * WhisperClock
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 *
 * AI Disclosure: Portions of this file, including system architecture, 
 * audio upsampling algorithms, and preprocessor UI toggles, were 
 * generated and optimized with the assistance of generative AI 
 * (Google Gemini).
 */

#include <pebble_worker.h>

#define MAX_BUFFER_SIZE 40 
#define GESTURE_PERSIST_KEY 2 
#define SETTINGS_PERSIST_KEY 3 
#define WAKE_REASON_PERSIST_KEY 4 

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))

typedef struct __attribute__((__packed__)) {
  bool say_its;
  bool say_ampm;
  int16_t playback_speed;
  int16_t gesture_buffer_size;
  uint8_t clock_mode;
  uint8_t volume;
  int16_t clip_trim;
  bool respect_quiet_time;
  uint8_t trigger_mode; 
  uint8_t tap_count;    
  uint8_t quiet_start_hour; 
  uint8_t quiet_end_hour; 
  bool enable_beta_features; 
  uint16_t z_multiplier; 
  uint16_t xy_multiplier; 
} WhisperSettings;

WhisperSettings s_worker_settings; 

typedef struct {
  int16_t x; int16_t y; int16_t z;
} CustomAccelData;

typedef struct {
  AccelData data[MAX_BUFFER_SIZE];
  uint8_t index;
  bool is_full;
} AccelBuffer;

static AccelBuffer s_buffer;
static int s_cooldown = 0; 
static bool s_has_trained_gesture = false; 
static int s_current_buffer_size = 25; 

static CustomAccelData s_gesture_template[MAX_BUFFER_SIZE]; 
static int32_t s_prev_row[MAX_BUFFER_SIZE];
static int32_t s_curr_row[MAX_BUFFER_SIZE];
static AccelData s_flat_live_data[MAX_BUFFER_SIZE];

static CustomAccelData s_centered_live[MAX_BUFFER_SIZE];
static CustomAccelData s_centered_saved[MAX_BUFFER_SIZE];

static int s_current_taps = 0;
static uint32_t s_absolute_tick = 0; 
static uint32_t s_last_tap_tick = 0;

static int16_t s_last_accel_x = 0;
static int16_t s_last_accel_y = 0;
static int16_t s_last_accel_z = 0;
static bool s_accel_primed = false;

static int32_t get_distance(CustomAccelData live_point, CustomAccelData template_point) {
  return abs(live_point.x - template_point.x) + 
         abs(live_point.y - template_point.y) + 
         abs(live_point.z - template_point.z);
}

static int32_t calculate_dtw_cost(uint32_t length) {
  s_prev_row[0] = get_distance(s_centered_live[0], s_centered_saved[0]);
  for (uint32_t j = 1; j < length; j++) {
    s_prev_row[j] = s_prev_row[j-1] + get_distance(s_centered_live[0], s_centered_saved[j]);
  }
  for (uint32_t i = 1; i < length; i++) {
    s_curr_row[0] = s_prev_row[0] + get_distance(s_centered_live[i], s_centered_saved[0]);
    for (uint32_t j = 1; j < length; j++) {
      int32_t cost = get_distance(s_centered_live[i], s_centered_saved[j]);
      int32_t cheapest_path = MIN3(s_prev_row[j], s_curr_row[j-1], s_prev_row[j-1]);
      s_curr_row[j] = cost + cheapest_path;
    }
    for (uint32_t k = 0; k < length; k++) {
      s_prev_row[k] = s_curr_row[k];
    }
  }
  return s_prev_row[length - 1]; 
}

static bool is_custom_quiet_time() {
  if (!s_worker_settings.respect_quiet_time) return false;
  
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int hour = t->tm_hour;
  
  if (s_worker_settings.quiet_start_hour == s_worker_settings.quiet_end_hour) return false;
  
  if (s_worker_settings.quiet_start_hour < s_worker_settings.quiet_end_hour) {
    return (hour >= s_worker_settings.quiet_start_hour && hour < s_worker_settings.quiet_end_hour); 
  } else {
    return (hour >= s_worker_settings.quiet_start_hour || hour < s_worker_settings.quiet_end_hour); 
  }
}

static void software_tap_handler(int32_t z_sq, int32_t xy_sq) {
  if (s_worker_settings.trigger_mode == 0) return; // 0 is Gesture Mode. Ignore taps!
  if (is_custom_quiet_time()) return; 

  uint32_t ticks_since_last = s_absolute_tick - s_last_tap_tick;
  uint32_t ms_gap = ticks_since_last * 40; 
  
  if (s_last_tap_tick > 0 && ticks_since_last < 3) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, ">>> TAP REJECTED (Too Fast / Recoil): %lu ms", ms_gap);
      return;
  }

  if (s_last_tap_tick == 0) {
      s_current_taps = 1;
      APP_LOG(APP_LOG_LEVEL_INFO, ">>> TAP ACCEPTED! Seq: 1/%d (Initial Tap) | Z: %ld", s_worker_settings.tap_count, z_sq);
  } else if (ticks_since_last > 75) { 
      s_current_taps = 1;
      APP_LOG(APP_LOG_LEVEL_INFO, ">>> SEQUENCE RESET (Gap: %lu ms > 3000 ms limit). Starting at 1.", ms_gap);
      APP_LOG(APP_LOG_LEVEL_INFO, ">>> TAP ACCEPTED! Seq: 1/%d | Z: %ld", s_worker_settings.tap_count, z_sq);
  } else {
      s_current_taps++;
      APP_LOG(APP_LOG_LEVEL_INFO, ">>> TAP ACCEPTED! Seq: %d/%d | Gap: %lu ms | Z: %ld", s_current_taps, s_worker_settings.tap_count, ms_gap, z_sq);
  }
  
  s_last_tap_tick = s_absolute_tick;
  
  if (s_current_taps >= s_worker_settings.tap_count) {
    s_current_taps = 0; 
    s_cooldown = 50; 
    s_last_tap_tick = 0; 
    
    APP_LOG(APP_LOG_LEVEL_INFO, ">>> TARGET REACHED! Waking app...");
    uint8_t reason = 1; 
    persist_write_data(WAKE_REASON_PERSIST_KEY, &reason, sizeof(reason));
    worker_launch_app();
  }
}

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  for (uint32_t i = 0; i < num_samples; i++) {
      s_absolute_tick++;

      if (s_cooldown > 0) {
          s_cooldown--;
          if (s_cooldown == 0) APP_LOG(APP_LOG_LEVEL_INFO, "=== READY FOR TAPS/GESTURES ===");
          continue; 
      }

      if (!s_accel_primed) {
          s_last_accel_x = data[i].x; s_last_accel_y = data[i].y; s_last_accel_z = data[i].z;
          s_accel_primed = true;
          continue;
      }

      // 1. Get RAW Physical Movement
      int32_t raw_dx = data[i].x - s_last_accel_x;
      int32_t raw_dy = data[i].y - s_last_accel_y;
      int32_t raw_dz = data[i].z - s_last_accel_z;

      // 2. Apply LIVE TUNING MULTIPLIERS (Before squaring to prevent Integer Overflow!)
      int32_t dx = (raw_dx * s_worker_settings.xy_multiplier) / 100;
      int32_t dy = (raw_dy * s_worker_settings.xy_multiplier) / 100;
      int32_t dz = (raw_dz * s_worker_settings.z_multiplier) / 100;

      s_last_accel_x = data[i].x;
      s_last_accel_y = data[i].y;
      s_last_accel_z = data[i].z;

      int32_t z_shock_sq = (dz * dz);
      int32_t xy_shock_sq = (dx * dx) + (dy * dy);

      // TAP DETECTION LOGIC
      if (s_worker_settings.trigger_mode == 1) {
          if (z_shock_sq > 400000) { 
              if (z_shock_sq > (xy_shock_sq / 2) || z_shock_sq > 2000000) {
                  software_tap_handler(z_shock_sq, xy_shock_sq);
              } else {
                  APP_LOG(APP_LOG_LEVEL_DEBUG, ">>> TAP REJECTED (Excessive Wobble) | Z: %ld | XY: %ld", z_shock_sq, xy_shock_sq);
              }
          }
          continue; // Skip all gesture math entirely!
      }

      // GESTURE DETECTION LOGIC
      if (is_custom_quiet_time()) continue; 
      
      // Default flick check
      if (!s_has_trained_gesture) {
          int32_t wrist_snap_sq = (dx * dx) + (dy * dy);
          if (wrist_snap_sq > 4000000) {
              s_cooldown = 25; 
              uint8_t reason = 2; 
              persist_write_data(WAKE_REASON_PERSIST_KEY, &reason, sizeof(reason));
              worker_launch_app();
              return; 
          }
          continue;
      }

      // DTW Buffer Math
      s_buffer.data[s_buffer.index] = data[i];
      s_buffer.index++;
      
      if(s_buffer.index >= s_current_buffer_size) {
        s_buffer.index = 0;
        s_buffer.is_full = true;
      }

      if (s_buffer.is_full) {
        int16_t min_x = 4000, max_x = -4000;
        int16_t min_y = 4000, max_y = -4000;
        int16_t min_z = 4000, max_z = -4000;
        
        for (int j = 0; j < s_current_buffer_size; j++) {
          if (s_buffer.data[j].x < min_x) min_x = s_buffer.data[j].x;
          if (s_buffer.data[j].x > max_x) max_x = s_buffer.data[j].x;
          if (s_buffer.data[j].y < min_y) min_y = s_buffer.data[j].y;
          if (s_buffer.data[j].y > max_y) max_y = s_buffer.data[j].y;
          if (s_buffer.data[j].z < min_z) min_z = s_buffer.data[j].z;
          if (s_buffer.data[j].z > max_z) max_z = s_buffer.data[j].z;
        }

        // Use the XY Multiplier to loosen the motion gate!
        int16_t gate = (1200 * 100) / s_worker_settings.xy_multiplier;
        if ((max_x - min_x) < gate && (max_y - min_y) < gate && (max_z - min_z) < gate) continue; 

        uint32_t flat_index = 0;
        for (uint32_t j = s_buffer.index; j < (uint32_t)s_current_buffer_size; j++) {
          s_flat_live_data[flat_index++] = s_buffer.data[j];
        }
        for (uint32_t j = 0; j < s_buffer.index; j++) {
          s_flat_live_data[flat_index++] = s_buffer.data[j];
        }

        int16_t offset_live_x = s_flat_live_data[s_current_buffer_size - 1].x;
        int16_t offset_live_y = s_flat_live_data[s_current_buffer_size - 1].y;
        int16_t offset_live_z = s_flat_live_data[s_current_buffer_size - 1].z;

        int16_t offset_saved_x = s_gesture_template[s_current_buffer_size - 1].x;
        int16_t offset_saved_y = s_gesture_template[s_current_buffer_size - 1].y;
        int16_t offset_saved_z = s_gesture_template[s_current_buffer_size - 1].z;

        for (int j = 0; j < s_current_buffer_size; j++) {
          int32_t live_x = s_flat_live_data[j].x - offset_live_x;
          int32_t live_y = s_flat_live_data[j].y - offset_live_y;
          int32_t live_z = s_flat_live_data[j].z - offset_live_z;

          int32_t saved_x = s_gesture_template[j].x - offset_saved_x;
          int32_t saved_y = s_gesture_template[j].y - offset_saved_y;
          int32_t saved_z = s_gesture_template[j].z - offset_saved_z;

          int32_t weight = 50 + ((j * 50) / (s_current_buffer_size - 1));

          s_centered_live[j].x = (live_x * weight) / 100;
          s_centered_live[j].y = (live_y * weight) / 100;
          s_centered_live[j].z = (live_z * weight) / 100;

          s_centered_saved[j].x = (saved_x * weight) / 100;
          s_centered_saved[j].y = (saved_y * weight) / 100;
          s_centered_saved[j].z = (saved_z * weight) / 100;
        }

        int32_t dtw_cost = calculate_dtw_cost(s_current_buffer_size);
        int32_t dynamic_threshold = s_current_buffer_size * 600; 

        if (dtw_cost < dynamic_threshold) {
          s_cooldown = 25; 
          s_buffer.is_full = false; 
          s_buffer.index = 0;
          
          uint8_t reason = 3; 
          persist_write_data(WAKE_REASON_PERSIST_KEY, &reason, sizeof(reason));
          worker_launch_app();
          return; 
        } 
      }
  }
}

static void worker_init() {
  s_cooldown = 0;
  s_buffer.index = 0;
  s_buffer.is_full = false;
  s_absolute_tick = 0;
  s_last_tap_tick = 0;
  s_accel_primed = false;
  
  // Safe Defaults
  s_current_buffer_size = 25; 
  s_worker_settings.trigger_mode = 0;
  s_worker_settings.tap_count = 3;
  s_worker_settings.quiet_start_hour = 22;
  s_worker_settings.quiet_end_hour = 7;
  s_worker_settings.respect_quiet_time = true;
  s_worker_settings.enable_beta_features = false;
  s_worker_settings.z_multiplier = 100;
  s_worker_settings.xy_multiplier = 100;

  if (persist_exists(SETTINGS_PERSIST_KEY)) {
    int bytes = persist_read_data(SETTINGS_PERSIST_KEY, &s_worker_settings, sizeof(WhisperSettings));
    if (bytes < (int)sizeof(WhisperSettings)) {
        // Fallback for older save data
        s_worker_settings.enable_beta_features = false;
        s_worker_settings.z_multiplier = 100;
        s_worker_settings.xy_multiplier = 100;
    }
    s_current_buffer_size = s_worker_settings.gesture_buffer_size;
  }
  
  if (!s_worker_settings.enable_beta_features) {
      APP_LOG(APP_LOG_LEVEL_INFO, "Production build: Beta physics disabled. Shutting down.");
      return;
  }
  
  if (persist_exists(GESTURE_PERSIST_KEY)) {
    persist_read_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));
    s_has_trained_gesture = true;
  } else {
    s_has_trained_gesture = false;
  }

  APP_LOG(APP_LOG_LEVEL_INFO, "=== BACKGROUND WORKER STARTED: READY FOR TAPS/GESTURES ===");
  
  accel_data_service_subscribe(5, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_25HZ);
}

static void worker_deinit() {
  accel_data_service_unsubscribe();
}

int main(void) {
  worker_init();
  worker_event_loop();
  worker_deinit();
  return 0; 
}
