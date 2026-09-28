#pragma once

#include <esp_err.h>
#include <esp_matter.h>
#include "state/state.h"

esp_err_t matter_driver_init();
esp_err_t matter_driver_attribute_update(void *driver_handle, uint16_t endpoint_id, uint32_t cluster_id, uint32_t attribute_id, esp_matter_attr_val_t *val);
esp_err_t matter_driver_sync_attributes(uint16_t endpoint_id, const LedState& current_state);
