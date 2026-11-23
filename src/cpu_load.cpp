#include "cpu_load.h"

#include <esp_freertos_hooks.h>
#include <esp_timer.h>

namespace {

constexpr int kCores = 2;
constexpr uint64_t kPeriodUs = 1000000;  // sampling window

// Written only by each core's own idle task; read by the timer task.
volatile uint32_t idleLoops[kCores] = {0, 0};

// Timer-task state.
uint32_t lastLoops[kCores] = {0, 0};
int64_t lastUs = 0;
float maxRate[kCores] = {0, 0};  // idle loops per µs when fully idle (best seen)

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
float load[kCores] = {-1, -1};  // guarded by lock

// false = call again immediately (don't sleep until the next interrupt), so the
// loop count is proportional to idle time.
bool idleHook0() { ++idleLoops[0]; return false; }
bool idleHook1() { ++idleLoops[1]; return false; }

void sample(void *) {
  const int64_t now = esp_timer_get_time();
  const float dtUs = static_cast<float>(now - lastUs);
  lastUs = now;
  float pct[kCores];
  for (int c = 0; c < kCores; ++c) {
    const uint32_t loops = idleLoops[c];
    const float rate = (loops - lastLoops[c]) / dtUs;  // unsigned diff handles wrap
    lastLoops[c] = loops;
    if (rate > maxRate[c]) maxRate[c] = rate;
    pct[c] = maxRate[c] > 0 ? 100.0f * (1.0f - rate / maxRate[c]) : 0;
  }
  portENTER_CRITICAL(&lock);
  for (int c = 0; c < kCores; ++c) load[c] = pct[c];
  portEXIT_CRITICAL(&lock);
}

}  // namespace

namespace CpuLoad {

void begin() {
  esp_register_freertos_idle_hook_for_cpu(idleHook0, 0);
  esp_register_freertos_idle_hook_for_cpu(idleHook1, 1);
  lastUs = esp_timer_get_time();
  lastLoops[0] = idleLoops[0];
  lastLoops[1] = idleLoops[1];

  const esp_timer_create_args_t args = {sample, nullptr, ESP_TIMER_TASK, "cpu_load", false};
  esp_timer_handle_t t;
  if (esp_timer_create(&args, &t) == ESP_OK && esp_timer_start_periodic(t, kPeriodUs) == ESP_OK)
    Serial.println("[CPU] Load estimate started");
  else
    Serial.println("[CPU] Load estimate timer failed");
}

float percent(int core) {
  if (core < 0 || core >= kCores) return -1;
  portENTER_CRITICAL(&lock);
  const float v = load[core];
  portEXIT_CRITICAL(&lock);
  return v;
}

}  // namespace CpuLoad
