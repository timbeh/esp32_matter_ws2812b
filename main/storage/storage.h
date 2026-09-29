#pragma once

#include <esp_err.h>
#include "state/state.h"

esp_err_t storage_init();
// Queues the latest state for a coalesced NVS write.
void storage_save_state(const LedState& state);
LedState storage_load_state();
// Clears the light-state namespace and pauses queued writes until reboot.
esp_err_t storage_reset_state();
