#include "key_manager.h"

#include "device_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "espnow_transport.h"

static const char *TAG = "key_manager";

esp_err_t key_manager_enable_static_espnow_encryption(const uint8_t peer_mac[ESP_NOW_ETH_ALEN])
{
    ESP_LOGI(TAG, "Milestone 2 static security mode enabled");
    ESP_LOGI(TAG, "Using manually configured PMK + LMK; EDHOC is not used yet");

    ESP_RETURN_ON_ERROR(
        espnow_transport_set_pmk(ESPNOW_STATIC_PMK),
        TAG,
        "failed to install ESP-NOW PMK"
    );

    ESP_RETURN_ON_ERROR(
        espnow_transport_add_peer(peer_mac, true, ESPNOW_STATIC_LMK),
        TAG,
        "failed to add encrypted ESP-NOW peer"
    );

    ESP_LOGI(TAG, "Static LMK installed for peer; ESP-NOW unicast encryption is enabled");
    return ESP_OK;
}
