#pragma once

#include <esp_err.h>
#include "state/state.h"

esp_err_t renderer_init();
void renderer_enqueue_update(const LedState& new_state);
