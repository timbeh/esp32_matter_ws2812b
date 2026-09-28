#pragma once

#include <stdint.h>
#include <esp_err.h>

esp_err_t led_driver_init();
esp_err_t led_driver_set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b);
esp_err_t led_driver_clear();
esp_err_t led_driver_refresh();
