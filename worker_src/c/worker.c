/*
 * WhisperClock - Background Physics Worker
 * Copyright (c) 2026 J_B
 *
 * Released under the MIT License.
 */

#include <pebble_worker.h>

#define MAX_BUFFER_SIZE 50
#define GESTURE_PERSIST_KEY 2
#define SETTINGS_PERSIST_KEY 13
#define WAKE_REASON_PERSIST_KEY 4
#define MIN(a, b) ((a) < (b) ? (a) : (b))
#define MIN3(a, b, c) MIN(MIN(a, b), c)
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MAX3(a, b, c) MAX(MAX(a, b), c)

typedef struct __attribute__((__packed__)) {
  uint8_t prefix_mode;
  bool say_ampm;
  bool is_us_dialect;
  int16_t playback_speed;
  uint8_t clock_mode;
  uint8_t volume;
  int16_t clip_trim;

  bool enable_beta_features;

  // Background Worker & Scheduling
  bool respect_quiet_time;
  uint8_t quiet_start_hour;
  uint8_t quiet_end_hour;
  uint8_t night_volume;
  bool night_worker_sleep;

  // Gesture Configuration
  uint8_t gesture_mode; // 0 = Default Flick, 1 = Tap Glass, 2 = Custom Axes
  uint8_t default_flick_sensitivity; // Range: 55 to 70
  uint8_t tap_sensitivity;           // Range: 0 to 30 (Mapped to 40-70% physics)

  // Physics & Gestures (Fully Isolated Axes)
  int16_t x_multiplier;
  int16_t y_multiplier;
  int16_t z_multiplier;
  int16_t gesture_buffer_size;

  // FUZZY TUNER
  int16_t prefix_gap;
  int16_t prefix_trim;
  int16_t fuzzy_mod_gap;
  int16_t fuzzy_conv_gap;
  int16_t fuzzy_past_gap;
  int16_t fuzzy_to_gap;
  int16_t fuzzy_tight_gap;
  int16_t fuzzy_ampm_gap;
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

static uint32_t s_absolute_tick = 0;

static int16_t s_last_accel_x = 0;
static int16_t s_last_accel_y = 0;
static int16_t s_last_accel_z = 0;
static bool s_accel_primed = false;

// Default Flick Bi-Directional State Tracking
static uint8_t s_flick_state = 0;
static int8_t s_flick_initial_sign = 0;
static uint8_t s_flick_timeout = 0;

static int32_t get_distance(CustomAccelData live_point, CustomAccelData template_point) {
  return abs(live_point.x - template_point.x) + abs(live_point.y - template_point.y) + abs(live_point.z - template_point.z);
}

static int32_t calculate_dtw_cost(uint32_t length) {
  s_prev_row[0] = get_distance(s_centered_live[0], s_centered_saved[0]);
  for (uint32_t j = 1; j < length; j++) s_prev_row[j] = s_prev_row[j-1] + get_distance(s_centered_live[0], s_centered_saved[j]);

  for (uint32_t i = 1; i < length; i++) {
    s_curr_row[0] = s_prev_row[0] + get_distance(s_centered_live[i], s_centered_saved[0]);
    for (uint32_t j = 1; j < length; j++) {
      int32_t cost = get_distance(s_centered_live[i], s_centered_saved[j]);
      int32_t cheapest_path = MIN3(s_prev_row[j], s_curr_row[j-1], s_prev_row[j-1]);
      s_curr_row[j] = cost + cheapest_path;
    }
    for (uint32_t k = 0; k < length; k++) s_prev_row[k] = s_curr_row[k];
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

static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  // Respect the complex Scheduled Night Mode sleep logic
  bool in_qt = is_custom_quiet_time();
  bool is_muted = in_qt ? (s_worker_settings.night_volume == 0) : (s_worker_settings.volume == 0);
  bool worker_sleep = in_qt ? s_worker_settings.night_worker_sleep : false;

  if (is_muted || worker_sleep) return; // Saves significant battery by exiting immediately!

  for (uint32_t i = 0; i < num_samples; i++) {
    s_absolute_tick++;

    if (s_cooldown > 0) {
      s_cooldown--;
      continue;
    }

    if (!s_accel_primed) {
      s_last_accel_x = data[i].x; s_last_accel_y = data[i].y; s_last_accel_z = data[i].z;
      s_accel_primed = true;
      continue;
    }

    int32_t raw_dx = data[i].x - s_last_accel_x;
    int32_t raw_dy = data[i].y - s_last_accel_y;
    int32_t raw_dz = data[i].z - s_last_accel_z;

    s_last_accel_x = data[i].x;
    s_last_accel_y = data[i].y;
    s_last_accel_z = data[i].z;

    // --- BRANCH A: DEFAULT WRIST-FLICK MODE ---
    if (s_worker_settings.gesture_mode == 0) {

      int32_t dy = (raw_dy * (int32_t)(s_worker_settings.default_flick_sensitivity * 10)) / 1000L;
      int32_t y_sq = dy * dy;

      if (s_flick_state == 0) {
        if (y_sq > 1500000) {
          s_flick_state = 1;
          s_flick_initial_sign = (dy > 0) ? 1 : -1;
          s_flick_timeout = 6;
        }
      }
      else if (s_flick_state == 1) {
        s_flick_timeout--;
        if (s_flick_timeout == 0) {
          s_flick_state = 0;
        } else {
          if (y_sq > 1500000) {
            int8_t current_sign = (dy > 0) ? 1 : -1;
            if (current_sign != s_flick_initial_sign) {

              s_flick_state = 0;
              s_cooldown = 50;
              uint8_t reason = 2;
              persist_write_data(WAKE_REASON_PERSIST_KEY, &reason, sizeof(reason));
              worker_launch_app();
              return;
            }
          }
        }
      }
      continue;
    }

    // --- BRANCH C: TAP GLASS MODE ---
    else if (s_worker_settings.gesture_mode == 1) {
      // 0-30 slider mapped directly to 70-40% multiplier
      int32_t multiplier = 70 - s_worker_settings.tap_sensitivity;

      int32_t dx = (raw_dx * multiplier) / 100L;
      int32_t dy = (raw_dy * multiplier) / 100L;
      int32_t dz = (raw_dz * multiplier) / 100L;

      int32_t x_sq = dx * dx;
      int32_t y_sq = dy * dy;
      int32_t z_sq = dz * dz;

      // The Strict Geometry Wobble Filter
      if (z_sq > 1500000) {
        if (z_sq > (x_sq + y_sq) * 2) {
          APP_LOG(APP_LOG_LEVEL_INFO, ">>> WORKER TAP TRIGGERED! Z: %ld, XY Wobble: %ld", z_sq, (x_sq + y_sq));
          s_cooldown = 50;
          uint8_t reason = 2;
          persist_write_data(WAKE_REASON_PERSIST_KEY, &reason, sizeof(reason));
          worker_launch_app();
          return;
        }
      }
      continue;
    }


    // --- BRANCH B: CUSTOM AXES MODE ---
    if (s_worker_settings.x_multiplier == 0 && s_worker_settings.y_multiplier == 0 && s_worker_settings.z_multiplier == 0) continue;

    int32_t dx = (s_worker_settings.x_multiplier == 0) ? 0 : (raw_dx * s_worker_settings.x_multiplier) / 1000L;
    int32_t dy = (s_worker_settings.y_multiplier == 0) ? 0 : (raw_dy * s_worker_settings.y_multiplier) / 1000L;
    int32_t dz = (s_worker_settings.z_multiplier == 0) ? 0 : (raw_dz * s_worker_settings.z_multiplier) / 1000L;

    if (s_absolute_tick % 25 == 0) {
      APP_LOG(APP_LOG_LEVEL_DEBUG, "WORKER | X: %ld | Y: %ld | Z: %ld", dx, dy, dz);
    }

    if (!s_has_trained_gesture) {
      int32_t jerk_sq = (dx * dx) + (dy * dy) + (dz * dz);

      if (jerk_sq > 1500000) {
        APP_LOG(APP_LOG_LEVEL_INFO, ">>> WORKER CUSTOM FLICK TRIGGERED! Jerk_Sq: %ld", jerk_sq);
        APP_LOG(APP_LOG_LEVEL_INFO, ">>> WORKER AXIS SNAPSHOT | X: %ld, Y: %ld, Z: %ld", dx, dy, dz);

        s_cooldown = 50;
        uint8_t reason = 2;
        persist_write_data(WAKE_REASON_PERSIST_KEY, &reason, sizeof(reason));
        worker_launch_app();
        return;
      }
      continue;
    }

    s_buffer.data[s_buffer.index] = data[i];
    s_buffer.index++;

    if (s_buffer.index >= s_current_buffer_size) {
      s_buffer.index = 0;
      s_buffer.is_full = true;
    }

    if (s_buffer.is_full && (s_absolute_tick % 5 == 0)) {
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

      int16_t range_x = max_x - min_x;
      int16_t range_y = max_y - min_y;
      int16_t range_z = max_z - min_z;

      bool skip_dtw = true;
      if (s_worker_settings.x_multiplier > 0) {
        if (range_x >= (800L * 1000L) / s_worker_settings.x_multiplier) skip_dtw = false;
      }
      if (s_worker_settings.y_multiplier > 0) {
        if (range_y >= (800L * 1000L) / s_worker_settings.y_multiplier) skip_dtw = false;
      }
      if (s_worker_settings.z_multiplier > 0) {
        if (range_z >= (800L * 1000L) / s_worker_settings.z_multiplier) skip_dtw = false;
      }
      if (skip_dtw) continue;

      uint32_t flat_index = 0;
      for (uint32_t j = s_buffer.index; j < (uint32_t)s_current_buffer_size; j++) s_flat_live_data[flat_index++] = s_buffer.data[j];
      for (uint32_t j = 0; j < s_buffer.index; j++) s_flat_live_data[flat_index++] = s_buffer.data[j];

      int16_t offset_live_x = s_flat_live_data[s_current_buffer_size - 1].x;
      int16_t offset_live_y = s_flat_live_data[s_current_buffer_size - 1].y;
      int16_t offset_live_z = s_flat_live_data[s_current_buffer_size - 1].z;

      int16_t offset_saved_x = s_gesture_template[s_current_buffer_size - 1].x;
      int16_t offset_saved_y = s_gesture_template[s_current_buffer_size - 1].y;
      int16_t offset_saved_z = s_gesture_template[s_current_buffer_size - 1].z;

      for (int j = 0; j < s_current_buffer_size; j++) {
        int32_t weight = 50 + ((j * 50) / (s_current_buffer_size - 1));

        int32_t live_x = (s_worker_settings.x_multiplier == 0) ? 0 : s_flat_live_data[j].x - offset_live_x;
        int32_t live_y = (s_worker_settings.y_multiplier == 0) ? 0 : s_flat_live_data[j].y - offset_live_y;
        int32_t live_z = (s_worker_settings.z_multiplier == 0) ? 0 : s_flat_live_data[j].z - offset_live_z;

        int32_t saved_x = (s_worker_settings.x_multiplier == 0) ? 0 : s_gesture_template[j].x - offset_saved_x;
        int32_t saved_y = (s_worker_settings.y_multiplier == 0) ? 0 : s_gesture_template[j].y - offset_saved_y;
        int32_t saved_z = (s_worker_settings.z_multiplier == 0) ? 0 : s_gesture_template[j].z - offset_saved_z;

        s_centered_live[j].x = (live_x * weight) / 100;
        s_centered_live[j].y = (live_y * weight) / 100;
        s_centered_live[j].z = (live_z * weight) / 100;

        s_centered_saved[j].x = (saved_x * weight) / 100;
        s_centered_saved[j].y = (saved_y * weight) / 100;
        s_centered_saved[j].z = (saved_z * weight) / 100;
      }

      int32_t dtw_cost = calculate_dtw_cost(s_current_buffer_size);

      int32_t max_mult = MAX3(s_worker_settings.x_multiplier, s_worker_settings.y_multiplier, s_worker_settings.z_multiplier);
      int32_t base_threshold = s_current_buffer_size * 3000;
      int32_t dynamic_threshold = (base_threshold * max_mult) / 1000;

      if (dtw_cost < dynamic_threshold) {
        APP_LOG(APP_LOG_LEVEL_INFO, ">>> WORKER DTW GESTURE TRIGGERED! Cost: %ld | Threshold: %ld", dtw_cost, dynamic_threshold);

        s_cooldown = 50;
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
  s_accel_primed = false;

  s_flick_state = 0;
  s_flick_initial_sign = 0;
  s_flick_timeout = 0;

  s_current_buffer_size = 25;

  s_worker_settings.respect_quiet_time = true;
  s_worker_settings.quiet_start_hour = 22;
  s_worker_settings.quiet_end_hour = 7;
  s_worker_settings.night_volume = 10;
  s_worker_settings.night_worker_sleep = true;

  s_worker_settings.enable_beta_features = false;

  s_worker_settings.gesture_mode = 0;
  s_worker_settings.default_flick_sensitivity = 62;
  s_worker_settings.tap_sensitivity = 15;
  s_worker_settings.x_multiplier = 1000;
  s_worker_settings.y_multiplier = 1000;
  s_worker_settings.z_multiplier = 1000;
  s_worker_settings.is_us_dialect = false;

  if (persist_exists(SETTINGS_PERSIST_KEY)) {
    WhisperSettings temp_settings;
    int bytes = persist_read_data(SETTINGS_PERSIST_KEY, &temp_settings, sizeof(WhisperSettings));
    if (bytes == sizeof(WhisperSettings)) {
      s_worker_settings = temp_settings;
    }
  }

  s_current_buffer_size = s_worker_settings.gesture_buffer_size;

  if (!s_worker_settings.enable_beta_features) {
    return;
  }

  if (persist_exists(GESTURE_PERSIST_KEY)) {
    persist_read_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));
    s_has_trained_gesture = true;
  } else {
    s_has_trained_gesture = false;
  }

  accel_data_service_subscribe(5, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_50HZ);
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
