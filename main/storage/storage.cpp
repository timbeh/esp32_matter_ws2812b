#include "storage.h"

#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/semphr.h>

static const char *TAG = "storage";
static const char *NVS_NAMESPACE = "led_cfg";
static constexpr uint32_t STATE_SAVE_BATCH_MS = 500;
static constexpr uint32_t STATE_SAVE_TASK_STACK_SIZE = 4096;

static QueueHandle_t state_save_queue = nullptr;
static SemaphoreHandle_t storage_mutex = nullptr;
static bool state_saves_paused = false;

static bool read_u8(nvs_handle_t handle, const char *key, uint8_t *value, bool *needs_commit)
{
    esp_err_t err = nvs_get_u8(handle, key, value);
    if (err == ESP_OK) {
        return true;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }

    if (err == ESP_ERR_NVS_TYPE_MISMATCH) {
        ESP_LOGW(TAG, "Saved key '%s' has the wrong type; using its default", key);
        esp_err_t erase_err = nvs_erase_key(handle, key);
        if (erase_err == ESP_OK) {
            *needs_commit = true;
        } else {
            ESP_LOGE(TAG, "Could not remove invalid saved key '%s': %s", key, esp_err_to_name(erase_err));
        }
        return false;
    }

    ESP_LOGW(TAG, "Could not read saved key '%s': %s; using its default", key, esp_err_to_name(err));
    return false;
}

static bool read_u16(nvs_handle_t handle, const char *key, uint16_t *value, bool *needs_commit)
{
    esp_err_t err = nvs_get_u16(handle, key, value);
    if (err == ESP_OK) {
        return true;
    }
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        return false;
    }

    if (err == ESP_ERR_NVS_TYPE_MISMATCH) {
        ESP_LOGW(TAG, "Saved key '%s' has the wrong type; using its default", key);
        esp_err_t erase_err = nvs_erase_key(handle, key);
        if (erase_err == ESP_OK) {
            *needs_commit = true;
        } else {
            ESP_LOGE(TAG, "Could not remove invalid saved key '%s': %s", key, esp_err_to_name(erase_err));
        }
        return false;
    }

    ESP_LOGW(TAG, "Could not read saved key '%s': %s; using its default", key, esp_err_to_name(err));
    return false;
}

template <typename T>
static bool value_in_range(const char *key, T value, T minimum, T maximum)
{
    if (value >= minimum && value <= maximum) {
        return true;
    }

    ESP_LOGW(TAG, "Saved key '%s' value %lu is outside the supported range %lu..%lu; using its default",
             key, (unsigned long)value, (unsigned long)minimum, (unsigned long)maximum);
    return false;
}

static void write_state(const LedState& state)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Opening light settings for writing failed: %s", esp_err_to_name(err));
        return;
    }

    err = nvs_set_u8(handle, "on", state.on ? 1 : 0);
    if (err == ESP_OK) err = nvs_set_u8(handle, "brightness", state.brightness);
    if (err == ESP_OK) err = nvs_set_u16(handle, "hue", state.hue);
    if (err == ESP_OK) err = nvs_set_u8(handle, "saturation", state.saturation);
    if (err == ESP_OK) err = nvs_set_u16(handle, "x", state.x);
    if (err == ESP_OK) err = nvs_set_u16(handle, "y", state.y);
    if (err == ESP_OK) err = nvs_set_u16(handle, "mireds", state.mireds);
    if (err == ESP_OK) err = nvs_set_u8(handle, "mode", state.color_mode);
    if (err == ESP_OK) err = nvs_commit(handle);

    nvs_close(handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Saving light settings failed: %s", esp_err_to_name(err));
    }
}

static void state_persistence_task(void *arg)
{
    (void)arg;
    LedState pending_state;

    while (true) {
        if (xQueueReceive(state_save_queue, &pending_state, portMAX_DELAY) != pdTRUE) {
            continue;
        }

        // Collect slider and color updates that arrive in the same short window.
        vTaskDelay(pdMS_TO_TICKS(STATE_SAVE_BATCH_MS));

        if (xSemaphoreTake(storage_mutex, portMAX_DELAY) != pdTRUE) {
            ESP_LOGE(TAG, "Taking storage mutex failed");
            continue;
        }

        LedState latest_state;
        while (xQueueReceive(state_save_queue, &latest_state, 0) == pdTRUE) {
            pending_state = latest_state;
        }

        if (!state_saves_paused) {
            write_state(pending_state);
        }
        xSemaphoreGive(storage_mutex);
    }
}

esp_err_t storage_init()
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG,
                 "NVS needs recovery (%s); erasing the default NVS partition. Saved light settings and Matter pairing "
                 "data may be erased, so recommissioning may be required.",
                 esp_err_to_name(err));
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
        return err;
    }

    state_save_queue = xQueueCreate(1, sizeof(LedState));
    if (!state_save_queue) {
        ESP_LOGE(TAG, "Creating state persistence queue failed");
        return ESP_ERR_NO_MEM;
    }

    storage_mutex = xSemaphoreCreateMutex();
    if (!storage_mutex) {
        ESP_LOGE(TAG, "Creating storage mutex failed");
        vQueueDelete(state_save_queue);
        state_save_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(state_persistence_task, "state_store", STATE_SAVE_TASK_STACK_SIZE, nullptr,
                    tskIDLE_PRIORITY + 1, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "Creating state persistence task failed");
        vSemaphoreDelete(storage_mutex);
        vQueueDelete(state_save_queue);
        storage_mutex = nullptr;
        state_save_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

void storage_save_state(const LedState& state)
{
    if (!state_save_queue || !storage_mutex) {
        ESP_LOGE(TAG, "State persistence is not initialized");
        return;
    }

    BaseType_t lock_taken = xSemaphoreTake(storage_mutex, 0);
    if (lock_taken == pdTRUE && state_saves_paused) {
        xSemaphoreGive(storage_mutex);
        return;
    }

    if (xQueueOverwrite(state_save_queue, &state) != pdPASS) {
        ESP_LOGE(TAG, "Queueing light state for persistence failed");
    }
    if (lock_taken == pdTRUE) {
        xSemaphoreGive(storage_mutex);
    }
}

LedState storage_load_state()
{
    LedState state = {
        .on = false,
        .brightness = led_state_limits::kMaxBrightness,
        .hue = 0,
        .saturation = 0,
        .x = 0,
        .y = 0,
        .mireds = 250,
        .color_mode = 0
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved light settings; using defaults");
        return state;
    }
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "Opening saved light settings failed: %s; using defaults", esp_err_to_name(err));
        return state;
    }

    bool needs_commit = false;
    uint8_t value8;
    uint16_t value16;

    if (read_u8(handle, "on", &value8, &needs_commit) && value_in_range("on", value8, uint8_t{0}, uint8_t{1})) {
        state.on = value8 == 1;
    }
    if (read_u8(handle, "brightness", &value8, &needs_commit) &&
        value_in_range("brightness", value8, led_state_limits::kMinBrightness, led_state_limits::kMaxBrightness)) {
        state.brightness = value8;
    }
    if (read_u16(handle, "hue", &value16, &needs_commit) &&
        value_in_range("hue", value16, uint16_t{0}, uint16_t{led_state_limits::kMaxHue})) {
        state.hue = value16;
    }
    if (read_u8(handle, "saturation", &value8, &needs_commit) &&
        value_in_range("saturation", value8, uint8_t{0}, led_state_limits::kMaxSaturation)) {
        state.saturation = value8;
    }
    if (read_u16(handle, "x", &value16, &needs_commit) &&
        value_in_range("x", value16, uint16_t{0}, led_state_limits::kMaxXyCoordinate)) {
        state.x = value16;
    }
    if (read_u16(handle, "y", &value16, &needs_commit) &&
        value_in_range("y", value16, uint16_t{0}, led_state_limits::kMaxXyCoordinate)) {
        state.y = value16;
    }
    if (read_u16(handle, "mireds", &value16, &needs_commit) &&
        value_in_range("mireds", value16, led_state_limits::kMinColorTemperatureMireds,
                       led_state_limits::kMaxColorTemperatureMireds)) {
        state.mireds = value16;
    }
    if (read_u8(handle, "mode", &value8, &needs_commit) &&
        value_in_range("mode", value8, uint8_t{0}, led_state_limits::kMaxColorMode)) {
        state.color_mode = value8;
    }

    if (needs_commit) {
        err = nvs_commit(handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Removing incorrectly typed settings failed: %s", esp_err_to_name(err));
        }
    }
    nvs_close(handle);
    return state;
}

esp_err_t storage_reset_state()
{
    if (!state_save_queue || !storage_mutex) {
        ESP_LOGE(TAG, "Cannot reset light settings before storage is initialized");
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(storage_mutex, portMAX_DELAY) != pdTRUE) {
        ESP_LOGE(TAG, "Taking storage mutex for factory reset failed");
        return ESP_FAIL;
    }

    state_saves_paused = true;
    xQueueReset(state_save_queue);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Opening light settings for factory reset failed: %s", esp_err_to_name(err));
        state_saves_paused = false;
        xSemaphoreGive(storage_mutex);
        storage_save_state(state_get());
        return err;
    }

    err = nvs_erase_all(handle);
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        state_saves_paused = false;
    }
    xSemaphoreGive(storage_mutex);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Erasing saved light settings failed: %s", esp_err_to_name(err));
        storage_save_state(state_get());
    }
    return err;
}
