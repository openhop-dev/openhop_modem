#pragma once
#include <cstdint>
struct esp_task_wdt_config_t {uint32_t timeout_ms; int idle_core_mask; bool trigger_panic;};
inline void esp_task_wdt_reset(){} inline void esp_task_wdt_reconfigure(const esp_task_wdt_config_t*){} inline void esp_task_wdt_add(void*){}
