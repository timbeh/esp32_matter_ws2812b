#pragma once

#include <stdint.h>
#include <stdbool.h>
#include <esp_err.h>

struct LedState {
    bool on;
    uint8_t brightness;
    uint16_t hue;
    uint8_t saturation;
    uint16_t x;
    uint16_t y;
    uint16_t mireds;
    uint8_t color_mode; // 0: HSV, 1: XY, 2: CT
};

esp_err_t state_init();
LedState state_get();
void state_set(const LedState& new_state);
void state_update_onoff(bool on);
void state_update_level(uint8_t level);
void state_update_hue(uint8_t hue);
void state_update_enhanced_hue(uint16_t hue);
void state_update_saturation(uint8_t sat);
void state_update_xy(uint16_t x, uint16_t y);
void state_update_ct(uint16_t mireds);
