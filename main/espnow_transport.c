#include "espnow_transport.h"

#include <string.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/queue.h"

static const char *TAG = "espnow_transport";

static QueueHandle_t s_rx_queue;
static uint8_t s_channel;

static void wifi_init(uint8_t channel)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_ERROR_CHECK(esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE));

    ESP_LOGI(TAG, "Wi-Fi STA started on channel %u", (unsigned)channel);
}

static void espnow_send_cb(const esp_now_send_info_t *tx_info, esp_now_send_status_t status)
{
    if (tx_info == NULL) {
        ESP_LOGW(TAG, "Send callback with NULL tx_info");
        return;
    }

    ESP_LOGI(TAG,
             "TX callback: dest=" MACSTR " status=%s",
             MAC2STR(tx_info->des_addr),
             status == ESP_NOW_SEND_SUCCESS ? "SUCCESS" : "FAIL");
}

static void espnow_recv_cb(const esp_now_recv_info_t *recv_info, const uint8_t *data, int data_len)
{
    if (recv_info == NULL || recv_info->src_addr == NULL || data == NULL || data_len <= 0) {
        ESP_LOGW(TAG, "Receive callback with invalid args");
        return;
    }

    if (data_len > ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN) {
        ESP_LOGW(TAG, "Dropping oversized packet: len=%d max=%d", data_len, ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN);
        return;
    }

    espnow_rx_packet_t packet = {0};
    memcpy(packet.src_mac, recv_info->src_addr, ESP_NOW_ETH_ALEN);
    memcpy(packet.data, data, data_len);
    packet.data_len = data_len;

    if (xQueueSend(s_rx_queue, &packet, 0) != pdTRUE) {
        ESP_LOGW(TAG, "RX queue full; dropping packet from " MACSTR, MAC2STR(recv_info->src_addr));
    }
}

esp_err_t espnow_transport_init(uint8_t channel)
{
    s_channel = channel;

    wifi_init(channel);

    s_rx_queue = xQueueCreate(ESPNOW_TRANSPORT_RX_QUEUE_LEN, sizeof(espnow_rx_packet_t));
    if (s_rx_queue == NULL) {
        return ESP_ERR_NO_MEM;
    }

    ESP_RETURN_ON_ERROR(esp_now_init(), TAG, "esp_now_init failed");
    ESP_RETURN_ON_ERROR(esp_now_register_send_cb(espnow_send_cb), TAG, "send callback registration failed");
    ESP_RETURN_ON_ERROR(esp_now_register_recv_cb(espnow_recv_cb), TAG, "recv callback registration failed");

    uint32_t version = 0;
    if (esp_now_get_version(&version) == ESP_OK) {
        ESP_LOGI(TAG, "ESP-NOW initialized, version=%lu", (unsigned long)version);
    } else {
        ESP_LOGI(TAG, "ESP-NOW initialized");
    }

    return ESP_OK;
}

esp_err_t espnow_transport_set_pmk(const uint8_t pmk[ESP_NOW_KEY_LEN])
{
    if (pmk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "Installing ESP-NOW PMK len=%d", ESP_NOW_KEY_LEN);
    return esp_now_set_pmk(pmk);
}

esp_err_t espnow_transport_add_peer(const uint8_t peer_mac[ESP_NOW_ETH_ALEN], bool encrypted, const uint8_t *lmk)
{
    esp_now_peer_info_t peer = {0};
    memcpy(peer.peer_addr, peer_mac, ESP_NOW_ETH_ALEN);
    peer.channel = s_channel;
    peer.ifidx = WIFI_IF_STA;
    peer.encrypt = encrypted;

    if (encrypted) {
        if (lmk == NULL) {
            ESP_LOGE(TAG, "encrypted peer requested but LMK is NULL");
            return ESP_ERR_INVALID_ARG;
        }
        memcpy(peer.lmk, lmk, ESP_NOW_KEY_LEN);
    }

    if (esp_now_is_peer_exist(peer_mac)) {
        ESP_LOGI(TAG, "Modifying existing peer " MACSTR " encrypted=%d", MAC2STR(peer_mac), encrypted);
        return esp_now_mod_peer(&peer);
    }

    ESP_LOGI(TAG, "Adding peer " MACSTR " encrypted=%d", MAC2STR(peer_mac), encrypted);
    return esp_now_add_peer(&peer);
}

esp_err_t espnow_transport_send(const uint8_t peer_mac[ESP_NOW_ETH_ALEN], const void *data, size_t len)
{
    if (peer_mac == NULL || data == NULL || len == 0 || len > ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG, "TX request: dest=" MACSTR " len=%u", MAC2STR(peer_mac), (unsigned)len);
    return esp_now_send(peer_mac, data, len);
}

bool espnow_transport_recv(espnow_rx_packet_t *packet, TickType_t timeout_ticks)
{
    if (packet == NULL || s_rx_queue == NULL) {
        return false;
    }

    return xQueueReceive(s_rx_queue, packet, timeout_ticks) == pdTRUE;
}

void espnow_transport_print_mac(const char *label, const uint8_t mac[ESP_NOW_ETH_ALEN])
{
    ESP_LOGI(TAG, "%s: " MACSTR, label, MAC2STR(mac));
}
