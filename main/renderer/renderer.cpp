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
static constexpr uint32_t RENDER_FRAME_INTERVAL_MS = 20;
static constexpr float RENDER_SMOOTHING_TIME_CONSTANT_MS = 120.0f;
static constexpr float RENDER_SETTLED_THRESHOLD = 0.5f;

struct RgbFrame {
    float red;
    float green;
    float blue;
};

static QueueHandle_t renderer_queue;
static TaskHandle_t renderer_task_handle;

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

static RgbFrame state_to_rgb(const LedState& target_state)
{
    if (!target_state.on || target_state.brightness == 0) {
        return {0.0f, 0.0f, 0.0f};
    }

    uint8_t r = 0, g = 0, b = 0;
    if (target_state.color_mode == 0) {
        hsv_to_rgb(target_state.hue, target_state.saturation, &r, &g, &b);
    } else if (target_state.color_mode == 1) {
        xy_to_rgb(target_state.x, target_state.y, &r, &g, &b);
    } else {
        const uint16_t mireds = target_state.mireds == 0 ? 250 : target_state.mireds;
        ct_to_rgb(mireds, &r, &g, &b);
    }

    r = apply_gamma(r);
    g = apply_gamma(g);
    b = apply_gamma(b);

    float brightness = (float)target_state.brightness / led_state_limits::kMaxBrightness;
    brightness *= (float)CONFIG_LED_STRIP_GLOBAL_BRIGHTNESS_CAP / 255.0f;

    // Preserve the established channel balance while smoothing the final LED output.
    float r_f = r * brightness;
    float g_f = g * 0.75f * brightness;
    float b_f = b * 0.50f * brightness;

    // Keep the current estimate and limiter applied to every transition endpoint.
    const float quiescent_ma = (float)CONFIG_LED_STRIP_MAX_LEDS;
    const float r_draw_ma = (r_f / 255.0f) * 20.0f * CONFIG_LED_STRIP_MAX_LEDS;
    const float g_draw_ma = (g_f / 255.0f) * 20.0f * CONFIG_LED_STRIP_MAX_LEDS;
    const float b_draw_ma = (b_f / 255.0f) * 20.0f * CONFIG_LED_STRIP_MAX_LEDS;
    const float current_estimate_ma = quiescent_ma + r_draw_ma + g_draw_ma + b_draw_ma;

    if (current_estimate_ma > CONFIG_LED_STRIP_MAX_CURRENT_MA) {
        float available_for_leds = (float)CONFIG_LED_STRIP_MAX_CURRENT_MA - quiescent_ma;
        if (available_for_leds < 0.0f) available_for_leds = 0.0f;

        const float requested_for_leds = r_draw_ma + g_draw_ma + b_draw_ma;
        float scaling_factor = 0.0f;
        if (quiescent_ma < CONFIG_LED_STRIP_MAX_CURRENT_MA && requested_for_leds > 0.0f) {
            scaling_factor = fminf(1.0f, available_for_leds / requested_for_leds);
        } else if (quiescent_ma >= CONFIG_LED_STRIP_MAX_CURRENT_MA) {
            ESP_LOGE(TAG, "Configured current budget is at or below the estimated LED idle current");
        }

        r_f *= scaling_factor;
        g_f *= scaling_factor;
        b_f *= scaling_factor;
        ESP_LOGD(TAG, "Dynamic limit: scale %.2f under %dmA (estimated %.0fmA)", scaling_factor,
                 CONFIG_LED_STRIP_MAX_CURRENT_MA, current_estimate_ma);
    }

    return {
        fminf(255.0f, fmaxf(0.0f, r_f)),
        fminf(255.0f, fmaxf(0.0f, g_f)),
        fminf(255.0f, fmaxf(0.0f, b_f)),
    };
}

static bool frame_is_settled(const RgbFrame& current, const RgbFrame& target)
{
    return fabsf(target.red - current.red) <= RENDER_SETTLED_THRESHOLD &&
           fabsf(target.green - current.green) <= RENDER_SETTLED_THRESHOLD &&
           fabsf(target.blue - current.blue) <= RENDER_SETTLED_THRESHOLD;
}

static bool receive_latest_state(LedState *state, TickType_t timeout)
{
    if (xQueueReceive(renderer_queue, state, timeout) != pdTRUE) {
        return false;
    }

    LedState latest_state;
    while (xQueueReceive(renderer_queue, &latest_state, 0) == pdTRUE) {
        *state = latest_state;
    }
    return true;
}

static uint8_t output_channel(float value)
{
    return (uint8_t)lroundf(fminf(255.0f, fmaxf(0.0f, value)));
}

static void render_frame(const RgbFrame& frame, uint8_t last_output[3], bool *has_rendered)
{
    const uint8_t output[3] = {
        output_channel(frame.red),
        output_channel(frame.green),
        output_channel(frame.blue),
    };
    if (*has_rendered && output[0] == last_output[0] && output[1] == last_output[1] &&
        output[2] == last_output[2]) {
        return;
    }

    esp_err_t err = ESP_OK;
    for (uint32_t i = 0; i < CONFIG_LED_STRIP_MAX_LEDS; ++i) {
        err = led_driver_set_pixel(i, output[0], output[1], output[2]);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Setting LED %lu failed: %s", (unsigned long)i, esp_err_to_name(err));
            return;
        }
    }

    err = led_driver_refresh();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "LED refresh failed: %s", esp_err_to_name(err));
        return;
    }

    memcpy(last_output, output, sizeof(output));
    *has_rendered = true;
}

static void renderer_task(void *arg)
{
    (void)arg;

    LedState target_state = state_get();
    RgbFrame target_frame = state_to_rgb(target_state);
    RgbFrame current_frame = {0.0f, 0.0f, 0.0f};
    bool transitioning = !frame_is_settled(current_frame, target_frame);
    uint8_t last_output[3] = {0, 0, 0};
    bool has_rendered = false;

    TickType_t frame_ticks = pdMS_TO_TICKS(RENDER_FRAME_INTERVAL_MS);
    if (frame_ticks == 0) {
        frame_ticks = 1;
    }
    TickType_t last_wake = xTaskGetTickCount();
    TickType_t last_render_tick = last_wake;

    while (true) {
        if (!transitioning) {
            LedState latest_state;
            if (!receive_latest_state(&latest_state, portMAX_DELAY)) {
                continue;
            }
            if (!state_equals(latest_state, target_state)) {
                target_state = latest_state;
                target_frame = state_to_rgb(target_state);
            }
            if (frame_is_settled(current_frame, target_frame)) {
                current_frame = target_frame;
                continue;
            }

            transitioning = true;
            last_wake = xTaskGetTickCount();
            last_render_tick = last_wake;
        }

        // Pace output frames while keeping only the newest requested Matter state.
        vTaskDelayUntil(&last_wake, frame_ticks);

        LedState latest_state;
        if (receive_latest_state(&latest_state, 0) && !state_equals(latest_state, target_state)) {
            target_state = latest_state;
            target_frame = state_to_rgb(target_state);
        }

        const TickType_t now = xTaskGetTickCount();
        const TickType_t elapsed_ticks = now - last_render_tick;
        last_render_tick = now;
        const float elapsed_ms = (float)elapsed_ticks * 1000.0f / configTICK_RATE_HZ;
        const float blend = 1.0f - expf(-elapsed_ms / RENDER_SMOOTHING_TIME_CONSTANT_MS);

        current_frame.red += (target_frame.red - current_frame.red) * blend;
        current_frame.green += (target_frame.green - current_frame.green) * blend;
        current_frame.blue += (target_frame.blue - current_frame.blue) * blend;

        if (frame_is_settled(current_frame, target_frame)) {
            current_frame = target_frame;
            transitioning = false;
        }
        render_frame(current_frame, last_output, &has_rendered);
    }
}

esp_err_t renderer_init() {
    if (renderer_queue || renderer_task_handle) {
        return (renderer_queue && renderer_task_handle) ? ESP_OK : ESP_ERR_INVALID_STATE;
    }

    renderer_queue = xQueueCreate(1, sizeof(LedState));
    if (!renderer_queue) {
        ESP_LOGE(TAG, "Creating renderer queue failed");
        return ESP_ERR_NO_MEM;
    }

    if (xTaskCreate(renderer_task, "renderer_task", 4096, NULL, 5, &renderer_task_handle) != pdPASS) {
        ESP_LOGE(TAG, "Creating renderer task failed");
        vQueueDelete(renderer_queue);
        renderer_queue = nullptr;
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void renderer_enqueue_update(const LedState& new_state) {
    if (renderer_queue) {
        // Keep the latest target; intermediate slider values need not be rendered.
        if (xQueueOverwrite(renderer_queue, &new_state) != pdPASS) {
            ESP_LOGE(TAG, "Queueing renderer state failed");
        }
    }
}
