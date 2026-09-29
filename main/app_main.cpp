#include <esp_err.h>
#include <esp_log.h>
#include <esp_matter.h>

#if CONFIG_ENABLE_CHIP_SHELL
#include <esp_matter_console.h>
#endif

#include "state/state.h"
#include "storage/storage.h"
#include "renderer/renderer.h"
#include "led_driver/led_driver.h"
#include "matter/matter_driver.h"

static const char *TAG = "app_main";

#if CONFIG_ENABLE_CHIP_SHELL
static esp_err_t app_factory_reset_command(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    ESP_LOGW(TAG, "Factory reset requested: erasing saved light settings and Matter pairing data");
    esp_err_t err = storage_reset_state();
    if (err != ESP_OK) {
        return err;
    }

    err = esp_matter::factory_reset();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Matter factory reset failed: %s", esp_err_to_name(err));
    }
    return err;
}

static esp_err_t recovery_console_init()
{
    static const esp_matter::console::command_t commands[] = {
        {
            .name = "factoryreset",
            .description = "Erase light settings and Matter pairing data, then restart",
            .handler = app_factory_reset_command,
        },
    };

    esp_err_t err = esp_matter::console::add_commands(commands, sizeof(commands) / sizeof(commands[0]));
    if (err == ESP_OK) {
        err = esp_matter::console::init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Starting Matter recovery console failed: %s", esp_err_to_name(err));
    }
    return err;
}
#endif

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(state_init());

    const LedState restored_state = storage_load_state();
    state_set(restored_state);

    ESP_ERROR_CHECK(led_driver_init());
    ESP_ERROR_CHECK(renderer_init());
    ESP_ERROR_CHECK(matter_driver_init());

#if CONFIG_ENABLE_CHIP_SHELL
    recovery_console_init();
#else
    ESP_LOGW(TAG, "Serial factory reset command is disabled; use local full-flash erase for recovery");
#endif
}
