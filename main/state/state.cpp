#include "state.h"
#include <freertos/FreeRTOS.h>
#include <freertos/semphr.h>
#include <esp_log.h>
#include "renderer/renderer.h"
#include "storage/storage.h"

static const char *TAG = "state";

static LedState current_state = {
    .on = false,
    .brightness = led_state_limits::kMaxBrightness,
    .hue = 0,
    .saturation = 0,
    .x = 0,
    .y = 0,
    .mireds = 250,
    .color_mode = 0
};

static SemaphoreHandle_t state_mutex = nullptr;

static bool ensure_mutex() {
    if (!state_mutex) {
        state_mutex = xSemaphoreCreateMutex();
    }
    if (!state_mutex) {
        ESP_LOGE(TAG, "Creating state mutex failed");
        return false;
    }
    return true;
}

esp_err_t state_init() {
    return ensure_mutex() ? ESP_OK : ESP_ERR_NO_MEM;
}

LedState state_get() {
    if (!ensure_mutex()) {
        return current_state;
    }
    LedState s;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return current_state;
    }
    s = current_state;
    xSemaphoreGive(state_mutex);
    return s;
}

void state_set(const LedState& new_state) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    current_state = new_state;
    LedState snapshot = current_state;
    xSemaphoreGive(state_mutex);
    
    // Store persistently config and enqueue rendering
    storage_save_state(snapshot);
    renderer_enqueue_update(snapshot);
}

void state_update_onoff(bool on) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    bool changed = current_state.on != on;
    current_state.on = on;
    LedState s = current_state;
    xSemaphoreGive(state_mutex);
    if (changed) {
        storage_save_state(s);
        renderer_enqueue_update(s);
    }
}

void state_update_level(uint8_t level) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    bool changed = current_state.brightness != level;
    current_state.brightness = level;
    LedState s = current_state;
    xSemaphoreGive(state_mutex);
    if (changed) {
        storage_save_state(s);
        renderer_enqueue_update(s);
    }
}

void state_update_hue(uint8_t hue) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    bool changed = (current_state.hue != hue) || (current_state.color_mode != 0);
    current_state.hue = hue;
    current_state.color_mode = 0;
    LedState s = current_state;
    xSemaphoreGive(state_mutex);
    if (changed) {
        storage_save_state(s);
        renderer_enqueue_update(s);
    }
}

void state_update_enhanced_hue(uint16_t hue) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    uint8_t mapped_hue = (hue * led_state_limits::kMaxHue) / 65535;
    bool changed = (current_state.hue != mapped_hue) || (current_state.color_mode != 0);
    current_state.hue = mapped_hue;
    current_state.color_mode = 0;
    LedState s = current_state;
    xSemaphoreGive(state_mutex);
    if (changed) {
        storage_save_state(s);
        renderer_enqueue_update(s);
    }
}

void state_update_saturation(uint8_t sat) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    bool changed = (current_state.saturation != sat) || (current_state.color_mode != 0);
    current_state.saturation = sat;
    current_state.color_mode = 0;
    LedState s = current_state;
    xSemaphoreGive(state_mutex);
    if (changed) {
        storage_save_state(s);
        renderer_enqueue_update(s);
    }
}

void state_update_xy(uint16_t x, uint16_t y) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    bool changed = (current_state.x != x || current_state.y != y) || (current_state.color_mode != 1);
    current_state.x = x;
    current_state.y = y;
    current_state.color_mode = 1;
    LedState s = current_state;
    xSemaphoreGive(state_mutex);
    if (changed) {
        storage_save_state(s);
        renderer_enqueue_update(s);
    }
}

void state_update_ct(uint16_t mireds) {
    if (!ensure_mutex()) return;
    if (xSemaphoreTake(state_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking state mutex failed");
        return;
    }
    bool changed = (current_state.mireds != mireds) || (current_state.color_mode != 2);
    current_state.mireds = mireds;
    current_state.color_mode = 2;
    LedState s = current_state;
    xSemaphoreGive(state_mutex);
    if (changed) {
        storage_save_state(s);
        renderer_enqueue_update(s);
    }
}
