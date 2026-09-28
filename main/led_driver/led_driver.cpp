#include "led_driver.h"
#include <led_strip.h>
#include <driver/rmt.h>
#include <esp_log.h>
#include "sdkconfig.h"

static const char *TAG = "led_driver";
static led_strip_t *led_strip = nullptr;

esp_err_t led_driver_init() {
    if (led_strip) {
        return ESP_OK;
    }

    uint32_t max_leds = CONFIG_LED_STRIP_MAX_LEDS;
    int gpio_pin = CONFIG_LED_STRIP_GPIO_PIN;

    // VERY IMPORTANT: Reset the GPIO pin before overriding with RMT, to clear UART or boot artifacts 
    esp_err_t err = gpio_reset_pin((gpio_num_t)gpio_pin);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "gpio_reset_pin failed: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Initializing WS2812B on GPIO %d with %lu LEDs", gpio_pin, (unsigned long)max_leds);

    rmt_config_t rmt_cfg = RMT_DEFAULT_CONFIG_TX((gpio_num_t)gpio_pin, RMT_CHANNEL_0);
    rmt_cfg.clk_div = 2; // RMT divider for proper 80MHz clock mapping to ws2812 timing
    err = rmt_config(&rmt_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_config failed: %s", esp_err_to_name(err));
        return err;
    }

    err = rmt_driver_install(rmt_cfg.channel, 0, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "rmt_driver_install failed: %s", esp_err_to_name(err));
        return err;
    }

    led_strip_config_t strip_config = LED_STRIP_DEFAULT_CONFIG(max_leds, (led_strip_dev_t)rmt_cfg.channel);
    led_strip = led_strip_new_rmt_ws2812(&strip_config);
    
    if (!led_strip) {
        ESP_LOGE(TAG, "Creating the WS2812B strip driver failed");
        esp_err_t cleanup_err = rmt_driver_uninstall(rmt_cfg.channel);
        if (cleanup_err != ESP_OK) {
            ESP_LOGW(TAG, "RMT cleanup after strip creation failure failed: %s", esp_err_to_name(cleanup_err));
        }
        return ESP_ERR_NO_MEM;
    }

    return led_driver_clear(); // ensure off during boot
}

esp_err_t led_driver_set_pixel(uint32_t index, uint8_t r, uint8_t g, uint8_t b) {
    if (!led_strip) {
        return ESP_ERR_INVALID_STATE;
    }
    if (index >= CONFIG_LED_STRIP_MAX_LEDS) {
        return ESP_ERR_INVALID_ARG;
    }
    return led_strip->set_pixel(led_strip, index, r, g, b);
}

esp_err_t led_driver_clear() {
    if (!led_strip) {
        return ESP_ERR_INVALID_STATE;
    }
    return led_strip->clear(led_strip, 50);
}

esp_err_t led_driver_refresh() {
    if (!led_strip) return ESP_ERR_INVALID_STATE;
    return led_strip->refresh(led_strip, 50);
}
