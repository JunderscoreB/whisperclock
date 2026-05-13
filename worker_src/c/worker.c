#include <pebble_worker.h>

#define MAX_BUFFER_SIZE 40 
#define GESTURE_PERSIST_KEY 2 
#define SETTINGS_PERSIST_KEY 3 

#define MIN3(a, b, c) ((a) < (b) ? ((a) < (c) ? (a) : (c)) : ((b) < (c) ? (b) : (c)))

// ALIGNMENT FIX: This must precisely match settings_engine.h!
typedef struct {
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

// Tap State
static int s_current_taps = 0;
static uint32_t s_last_tap_time = 0;

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

// --- HARDWARE TAP DETECTOR ---
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  // If set to Gesture Only, completely ignore
  if (s_worker_settings.trigger_mode == 0) return;

  // Glass knocks register heavily on the Z axis
  if (axis == ACCEL_AXIS_Z) {
//  if(true) {	
    uint32_t now = time(NULL);

    // If it's been more than 2 seconds since the last knock, restart the count
    if (now - s_last_tap_time > 2) {
      s_current_taps = 1;
    } else {
      s_current_taps++;
    }

    s_last_tap_time = now;

    // Has the sequence been completed?
    if (s_current_taps >= s_worker_settings.tap_count) {
      APP_LOG(APP_LOG_LEVEL_INFO, "GLASS TAP SEQUENCE DETECTED! Waking App...");
      s_current_taps = 0; 
      s_cooldown = 80; 
      worker_launch_app();
    }
  }
}


static void accel_data_handler(AccelData *data, uint32_t num_samples) {
  // If the user set it to TAP ONLY, completely ignore the sweep data!
  if (s_worker_settings.trigger_mode == 1) return;

  // MOTION GATE
  bool is_moving = false;
  for (uint32_t i = 0; i < num_samples; i++) {
    int32_t mag_sq = (data[i].x * data[i].x) + (data[i].y * data[i].y) + (data[i].z * data[i].z);
    if (mag_sq < 640000 || mag_sq > 1440000) { 
      is_moving = true;
      break; 
    }
  }

  if (!is_moving) return;

  // --- Core Processing ---
  for(uint32_t i = 0; i < num_samples; i++) {
    
    if (s_cooldown > 0) {
      s_cooldown--;
      continue;
    }

    if (!s_has_trained_gesture) {
      int32_t mag_sq = (data[i].x * data[i].x) + (data[i].y * data[i].y) + (data[i].z * data[i].z);
      if (mag_sq > 6250000) { 
        APP_LOG(APP_LOG_LEVEL_INFO, "WRIST FLICK DETECTED! Waking App...");
        s_cooldown = 80; 
        worker_launch_app();
        return; 
      }
    } else {
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

        if ((max_x - min_x) < 1200 && (max_y - min_y) < 1200 && (max_z - min_z) < 1200) {
           continue; 
        }

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
        int32_t dynamic_threshold = s_current_buffer_size * 500; 

        if (dtw_cost < dynamic_threshold) {
          APP_LOG(APP_LOG_LEVEL_INFO, "GESTURE MATCHED! (Cost: %ld). Launching App...", dtw_cost);
          
          s_cooldown = 80; 
          s_buffer.is_full = false; 
          s_buffer.index = 0;
          worker_launch_app();
          return; 
        } 
      }
    }
  }
}

static void worker_init() {
  s_cooldown = 0;
  s_buffer.index = 0;
  s_buffer.is_full = false;
  
  // Set safety defaults
  s_current_buffer_size = 25; 
  s_worker_settings.trigger_mode = 0;
  s_worker_settings.tap_count = 3;

  if (persist_exists(SETTINGS_PERSIST_KEY)) {
    persist_read_data(SETTINGS_PERSIST_KEY, &s_worker_settings, sizeof(WhisperSettings));
    s_current_buffer_size = s_worker_settings.gesture_buffer_size;
  }
  
  if (persist_exists(GESTURE_PERSIST_KEY)) {
    persist_read_data(GESTURE_PERSIST_KEY, s_gesture_template, sizeof(s_gesture_template));
    s_has_trained_gesture = true;
  } else {
    s_has_trained_gesture = false;
  }

  // Subscribe to BOTH hardware services!
  accel_data_service_subscribe(1, accel_data_handler);
  accel_service_set_sampling_rate(ACCEL_SAMPLING_10HZ);
  
  accel_tap_service_subscribe(accel_tap_handler);
}

static void worker_deinit() {
  accel_data_service_unsubscribe();
  accel_tap_service_unsubscribe();
}

int main(void) {
  worker_init();
  worker_event_loop();
  worker_deinit();
  return 0; 
}
