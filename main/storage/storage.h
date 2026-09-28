#pragma once

#include <esp_err.h>
#include "state/state.h"

esp_err_t storage_init();
void storage_save_state(const LedState& state);
LedState storage_load_state();
