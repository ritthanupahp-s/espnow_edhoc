#pragma once

#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_now.h"

#define ESPNOW_LMK_LEN ESP_NOW_KEY_LEN

/*
 * Milestone 5 temporary exporter boundary.
 *
 * This function intentionally derives an LMK-shaped 16-byte value from the
 * RFC 9529 trace transcript. It is deterministic and useful for testing the
 * ESP-NOW key handoff path, but it is NOT a real EDHOC exporter result.
 *
 * After Lakers is linked, this module should be changed so this public function
 * calls Lakers' completed-session edhoc_exporter() instead.
 */
esp_err_t edhoc_exporter_trace_derive_espnow_lmk(uint8_t out_lmk[ESPNOW_LMK_LEN]);

void edhoc_exporter_log_lmk_summary(const uint8_t lmk[ESPNOW_LMK_LEN]);
