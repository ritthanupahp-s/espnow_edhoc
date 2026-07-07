#pragma once

#include <stdint.h>

#include "esp_err.h"
#include "esp_now.h"

esp_err_t key_manager_enable_static_espnow_encryption(const uint8_t peer_mac[ESP_NOW_ETH_ALEN]);
