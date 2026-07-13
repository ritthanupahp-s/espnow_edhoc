#include "key_manager.h"

#include "device_config.h"
#include "esp_check.h"
#include "esp_log.h"
#include "espnow_transport.h"

static const char *TAG = "key_manager";

esp_err_t key_manager_enable_static_espnow_encryption(
    const uint8_t peer_mac[ESP_NOW_ETH_ALEN])
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

esp_err_t key_manager_enable_derived_espnow_encryption(
    const uint8_t peer_mac[ESP_NOW_ETH_ALEN],
    const uint8_t lmk[ESP_NOW_KEY_LEN])
{
    if (peer_mac == NULL || lmk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Installing post-handshake LMK into ESP-NOW peer table");

    /*
     * The PMK protects LMKs inside ESP-NOW. The thesis variable under test is
     * the per-peer LMK; this prototype keeps one fixed lab PMK on both boards.
     */
    ESP_RETURN_ON_ERROR(
        espnow_transport_set_pmk(ESPNOW_STATIC_PMK),
        TAG,
        "failed to install ESP-NOW PMK"
    );

    /*
     * The peer already exists as encrypt=false during EDHOC transport.
     * espnow_transport_add_peer() uses esp_now_mod_peer() when it exists.
     */
    ESP_RETURN_ON_ERROR(
        espnow_transport_add_peer(peer_mac, true, lmk),
        TAG,
        "failed to modify peer with derived LMK"
    );

    esp_now_peer_info_t peer = {0};
    ESP_RETURN_ON_ERROR(
        esp_now_get_peer(peer_mac, &peer),
        TAG,
        "failed to read peer after LMK installation"
    );

    if (!peer.encrypt) {
        ESP_LOGE(TAG, "Peer update returned successfully but encrypt flag is still false");
        return ESP_FAIL;
    }

    ESP_LOGI(TAG, "Peer table updated successfully: encrypt=true LMK_len=%d", ESP_NOW_KEY_LEN);
    return ESP_OK;
}
