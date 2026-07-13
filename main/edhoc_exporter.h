#pragma once

#include <stdint.h>

#include "esp_now.h"

#define ESPNOW_LMK_LEN ESP_NOW_KEY_LEN

/* Log only a non-secret SHA-256 prefix for comparing the two derived LMKs. */
void edhoc_exporter_log_lmk_summary(const uint8_t lmk[ESPNOW_LMK_LEN]);
