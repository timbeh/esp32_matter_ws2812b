#include "renderer.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/queue.h>
#include <esp_log.h>
#include <math.h>
#include <string.h>
#include "led_driver/led_driver.h"
#include "sdkconfig.h"

static const char *TAG = "renderer";

static QueueHandle_t renderer_queue;
static LedState last_rendered_state;

static void hsv_to_rgb(uint8_t h, uint8_t s, uint8_t *r, uint8_t *g, uint8_t *b) {
    float h_f = (float)h * 360.0f / 254.0f;
    float s_f = (float)s / 254.0f;
    float c = s_f; 
    float x = c * (1.0f - fabsf(fmodf(h_f / 60.0f, 2.0f) - 1.0f));
    float m = 1.0f - c;

    float r_f, g_f, b_f;
    if (h_f < 60) { r_f = c; g_f = x; b_f = 0; }
    else if (h_f < 120) { r_f = x; g_f = c; b_f = 0; }
    else if (h_f < 180) { r_f = 0; g_f = c; b_f = x; }
    else if (h_f < 240) { r_f = 0; g_f = x; b_f = c; }
    else if (h_f < 300) { r_f = x; g_f = 0; b_f = c; }
    else { r_f = c; g_f = 0; b_f = x; }

    *r = (uint8_t)((r_f + m) * 255.0f);
    *g = (uint8_t)((g_f + m) * 255.0f);
    *b = (uint8_t)((b_f + m) * 255.0f);
}

static void xy_to_rgb(uint16_t x, uint16_t y, uint8_t *r, uint8_t *g, uint8_t *b) {
    const float chroma_x = (float)x / 65535.0f;
    const float chroma_y = (float)y / 65535.0f;
    if (chroma_y <= 0.0f || chroma_x + chroma_y > 1.0f) {
        *r = 0;
        *g = 0;
        *b = 0;
        return;
    }

    // Convert CIE xy chromaticity to XYZ at a fixed Y, then to linear sRGB.
    const float X = chroma_x / chroma_y;
    const float Y = 1.0f;
    const float Z = (1.0f - chroma_x - chroma_y) / chroma_y;
    const float linear_r = X * 3.2406f - Y * 1.5372f - Z * 0.4986f;
    const float linear_g = -X * 0.9689f + Y * 1.8758f + Z * 0.0415f;
    const float linear_b = X * 0.0557f - Y * 0.2040f + Z * 1.0570f;

    // The renderer applies its 2.2 gamma correction after each color mode.
    // Encode here so that the later correction yields these linear intensities.
    const float encoded_r = powf(fmaxf(0.0f, fminf(1.0f, linear_r)), 1.0f / 2.2f);
    const float encoded_g = powf(fmaxf(0.0f, fminf(1.0f, linear_g)), 1.0f / 2.2f);
    const float encoded_b = powf(fmaxf(0.0f, fminf(1.0f, linear_b)), 1.0f / 2.2f);
    *r = (uint8_t)(encoded_r * 255.0f);
    *g = (uint8_t)(encoded_g * 255.0f);
    *b = (uint8_t)(encoded_b * 255.0f);
}

static void ct_to_rgb(uint16_t mireds, uint8_t *r, uint8_t *g, uint8_t *b) {
    uint32_t kelvin = 1000000 / mireds;
    float temp = kelvin / 100.0f;
    float red, green, blue;

    if (temp <= 66) {
        red = 255;
        green = 99.4708025861f * logf(temp) - 161.1195681661f;
        if (temp <= 19) {
            blue = 0;
        } else {
            blue = 138.5177312231f * logf(temp - 10) - 305.0447927307f;
        }
    } else {
        red = 329.698727446f * powf(temp - 60, -0.1332047592f);
        green = 288.1221695283f * powf(temp - 60, -0.0755148492f);
        blue = 255;
    }

    *r = (uint8_t)fmaxf(0, fminf(255, red));
    *g = (uint8_t)fmaxf(0, fminf(255, green));
    *b = (uint8_t)fmaxf(0, fminf(255, blue));
}

static uint8_t apply_gamma(uint8_t channel) {
    float f = (float)channel / 255.0f;
    return (uint8_t)(powf(f, 2.2f) * 255.0f);
}

static bool state_equals(const LedState& a, const LedState& b) {
    return a.on == b.on && a.brightness == b.brightness && a.hue == b.hue && 
           a.saturation == b.saturation && a.x == b.x && a.y == b.y && 
           a.mireds == b.mireds && a.color_mode == b.color_mode;
}

static void renderer_task(void *arg) {
    LedState target_state;
    target_state = state_get();
    last_rendered_state = target_state;
    last_rendered_state.on = !target_state.on; // Force first render!

    while (1) {
        if (xQueueReceive(renderer_queue, &target_state, portMAX_DELAY) == pdTRUE) {
            
            vTaskDelay(pdMS_TO_TICKS(50));
            LedState latest_state;
            while(xQueueReceive(renderer_queue, &latest_state, 0) == pdTRUE) {
                target_state = latest_state;
            }

            if (state_equals(target_state, last_rendered_state)) {
                continue;
            }
            last_rendered_state = target_state;

            if (!target_state.on || target_state.brightness == 0) {
                ESP_LOGI(TAG, "Rendering OUT: STRIP CLEAR");
                led_driver_clear();
                continue;
            }

            uint8_t r = 0, g = 0, b = 0;
            if (target_state.color_mode == 0) {
                hsv_to_rgb(target_state.hue, target_state.saturation, &r, &g, &b);
            } else if (target_state.color_mode == 1) {
                xy_to_rgb(target_state.x, target_state.y, &r, &g, &b);
            } else {
                if (target_state.mireds == 0) target_state.mireds = 250;
                ct_to_rgb(target_state.mireds, &r, &g, &b);
            }

            r = apply_gamma(r);
            g = apply_gamma(g);
            b = apply_gamma(b);
            
            float brightness_f = ((float)target_state.brightness / 254.0f);
            float max_cap_f = (float)CONFIG_LED_STRIP_GLOBAL_BRIGHTNESS_CAP / 255.0f;
            brightness_f *= max_cap_f;
            
            float wb_r = 1.0f;
            float wb_g = 0.75f;
            float wb_b = 0.50f;

            float r_f = r * wb_r * brightness_f;
            float g_f = g * wb_g * brightness_f;
            float b_f = b * wb_b * brightness_f;

            // Dynamic Power Model (WS2812B specific):
            // 1. Quiescent Current: Each WS2812B IC draws ~1mA just to power the internal logic.
            // 2. Channel Current: Each color channel (R,G,B) draws ~20mA at full (255) intensity.
            float quiescent_ma = (float)CONFIG_LED_STRIP_MAX_LEDS * 1.0f;
            float r_draw_ma = (r_f / 255.0f) * 20.0f * CONFIG_LED_STRIP_MAX_LEDS;
            float g_draw_ma = (g_f / 255.0f) * 20.0f * CONFIG_LED_STRIP_MAX_LEDS;
            float b_draw_ma = (b_f / 255.0f) * 20.0f * CONFIG_LED_STRIP_MAX_LEDS;

            float current_estimate_ma = quiescent_ma + r_draw_ma + g_draw_ma + b_draw_ma;
            
            if (current_estimate_ma > CONFIG_LED_STRIP_MAX_CURRENT_MA) {
                // If we are over the limit, calculate how much headroom we have left 
                // after the quiescent draw is subtracted.
                float available_for_leds = (float)CONFIG_LED_STRIP_MAX_CURRENT_MA - quiescent_ma;
                if (available_for_leds < 0) available_for_leds = 0;

                float requested_for_leds = r_draw_ma + g_draw_ma + b_draw_ma;
                float scaling_factor = 0.0f;
                if (quiescent_ma < CONFIG_LED_STRIP_MAX_CURRENT_MA && requested_for_leds > 0.0f) {
                    scaling_factor = fminf(1.0f, available_for_leds / requested_for_leds);
                } else if (quiescent_ma >= CONFIG_LED_STRIP_MAX_CURRENT_MA) {
                    ESP_LOGE(TAG, "Configured current budget is at or below the estimated LED idle current");
                }

                r_f *= scaling_factor;
                g_f *= scaling_factor;
                b_f *= scaling_factor;
                ESP_LOGI(TAG, "Dynamic Limit: Scaling by %.2f to stay under %dmA (Estimate was %.0fmA)", 
                         scaling_factor, CONFIG_LED_STRIP_MAX_CURRENT_MA, current_estimate_ma);
            }

            uint8_t fn_r = (uint8_t)fmaxf(0, fminf(255, r_f));
            uint8_t fn_g = (uint8_t)fmaxf(0, fminf(255, g_f));
            uint8_t fn_b = (uint8_t)fmaxf(0, fminf(255, b_f));

            ESP_LOGI(TAG, "Rendering OUT: mode=%d fn_r=%d fn_g=%d fn_b=%d", target_state.color_mode, fn_r, fn_g, fn_b);

            uint32_t max_leds = CONFIG_LED_STRIP_MAX_LEDS;
            for (uint32_t i = 0; i < max_leds; i++) {
                led_driver_set_pixel(i, fn_r, fn_g, fn_b);
            }
            
            led_driver_refresh();
        }
    }
}

void renderer_init() {
    renderer_queue = xQueueCreate(10, sizeof(LedState));
    xTaskCreate(renderer_task, "renderer_task", 4096, NULL, 5, NULL);
}

void renderer_enqueue_update(const LedState& new_state) {
    if (renderer_queue) {
        // Non-blocking enqueue from callbacks
        xQueueSendToBack(renderer_queue, &new_state, 0);
    }
}
