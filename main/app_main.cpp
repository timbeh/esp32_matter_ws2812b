#include <esp_err.h>

#include "state/state.h"
#include "storage/storage.h"
#include "renderer/renderer.h"
#include "led_driver/led_driver.h"
#include "matter/matter_driver.h"

extern "C" void app_main(void)
{
    ESP_ERROR_CHECK(storage_init());
    ESP_ERROR_CHECK(state_init());

    const LedState restored_state = storage_load_state();
    state_set(restored_state);

    ESP_ERROR_CHECK(led_driver_init());
    ESP_ERROR_CHECK(renderer_init());
    ESP_ERROR_CHECK(matter_driver_init());
}
