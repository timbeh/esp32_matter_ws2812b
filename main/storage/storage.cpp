#include "storage.h"
#include <nvs_flash.h>
#include <nvs.h>
#include <esp_log.h>

static const char* TAG = "storage";
static const char* NVS_NAMESPACE = "led_cfg";

esp_err_t storage_init() {
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        err = nvs_flash_erase();
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "NVS erase failed: %s", esp_err_to_name(err));
            return err;
        }
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS initialization failed: %s", esp_err_to_name(err));
    }
    return err;
}

void storage_save_state(const LedState& state) {
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) return;

    nvs_set_u8(handle, "on", state.on ? 1 : 0);
    nvs_set_u8(handle, "brightness", state.brightness);
    nvs_set_u16(handle, "hue", state.hue);
    nvs_set_u8(handle, "saturation", state.saturation);
    nvs_set_u16(handle, "x", state.x);
    nvs_set_u16(handle, "y", state.y);
    nvs_set_u16(handle, "mireds", state.mireds);
    nvs_set_u8(handle, "mode", state.color_mode);
    
    nvs_commit(handle);
    nvs_close(handle);
}

LedState storage_load_state() {
    LedState s = {
        .on = false,
        .brightness = 254,
        .hue = 0,
        .saturation = 0,
        .x = 0,
        .y = 0,
        .mireds = 250,
        .color_mode = 0
    };

    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return s; // default values
    }

    uint8_t u8_val;
    uint16_t u16_val;

    if (nvs_get_u8(handle, "on", &u8_val) == ESP_OK) s.on = (u8_val == 1);
    if (nvs_get_u8(handle, "brightness", &u8_val) == ESP_OK) s.brightness = u8_val;
    if (nvs_get_u16(handle, "hue", &u16_val) == ESP_OK) s.hue = u16_val;
    if (nvs_get_u8(handle, "saturation", &u8_val) == ESP_OK) s.saturation = u8_val;
    if (nvs_get_u16(handle, "x", &u16_val) == ESP_OK) s.x = u16_val;
    if (nvs_get_u16(handle, "y", &u16_val) == ESP_OK) s.y = u16_val;
    if (nvs_get_u16(handle, "mireds", &u16_val) == ESP_OK) s.mireds = u16_val;
    if (nvs_get_u8(handle, "mode", &u8_val) == ESP_OK) s.color_mode = u8_val;

    nvs_close(handle);
    return s;
}
