#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "device_config.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_system.h"
#include "espnow_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "milestone1";

#define APP_MAGIC 0xA7
#define APP_VERSION 1

typedef enum {
    APP_MSG_PING = 1,
    APP_MSG_PONG = 2,
} app_msg_type_t;

typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t version;
    uint8_t type;
    uint16_t seq;
    uint16_t payload_len;
    uint8_t payload[APP_PAYLOAD_MAX_LEN];
} app_packet_t;

static bool mac_is_all_value(const uint8_t mac[ESP_NOW_ETH_ALEN], uint8_t value)
{
    for (int i = 0; i < ESP_NOW_ETH_ALEN; ++i) {
        if (mac[i] != value) {
            return false;
        }
    }
    return true;
}

static bool peer_mac_is_configured(const uint8_t mac[ESP_NOW_ETH_ALEN])
{
    if (mac_is_all_value(mac, 0x00) || mac_is_all_value(mac, 0xFF)) {
        return false;
    }

    /* Reject multicast/broadcast group addresses for this unicast milestone. */
    if ((mac[0] & 0x01) != 0) {
        return false;
    }

    return true;
}

static esp_err_t nvs_init(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    return ret;
}

static esp_err_t send_app_message(app_msg_type_t type, uint16_t seq, const char *text)
{
    app_packet_t packet = {0};
    packet.magic = APP_MAGIC;
    packet.version = APP_VERSION;
    packet.type = (uint8_t)type;
    packet.seq = seq;

    size_t text_len = strnlen(text, APP_PAYLOAD_MAX_LEN);
    packet.payload_len = (uint16_t)text_len;
    memcpy(packet.payload, text, text_len);

    size_t wire_len = offsetof(app_packet_t, payload) + text_len;

    ESP_LOGI(TAG,
             "APP TX: type=%s seq=%u payload=\"%.*s\" wire_len=%u",
             type == APP_MSG_PING ? "PING" : "PONG",
             (unsigned)seq,
             (int)text_len,
             text,
             (unsigned)wire_len);

    return espnow_transport_send(PEER_MAC, &packet, wire_len);
}

static void handle_rx_packet(const espnow_rx_packet_t *rx)
{
    if (rx->data_len < (int)offsetof(app_packet_t, payload)) {
        ESP_LOGW(TAG, "APP RX: packet too short len=%d", rx->data_len);
        return;
    }

    const app_packet_t *packet = (const app_packet_t *)rx->data;

    if (packet->magic != APP_MAGIC || packet->version != APP_VERSION) {
        ESP_LOGW(TAG, "APP RX: invalid magic/version from " MACSTR, MAC2STR(rx->src_mac));
        return;
    }

    size_t header_len = offsetof(app_packet_t, payload);
    if (packet->payload_len > APP_PAYLOAD_MAX_LEN || header_len + packet->payload_len > (size_t)rx->data_len) {
        ESP_LOGW(TAG, "APP RX: invalid payload length=%u wire_len=%d", (unsigned)packet->payload_len, rx->data_len);
        return;
    }

    const char *type_str = packet->type == APP_MSG_PING ? "PING" :
                           packet->type == APP_MSG_PONG ? "PONG" : "UNKNOWN";

    ESP_LOGI(TAG,
             "APP RX: from=" MACSTR " type=%s seq=%u payload=\"%.*s\" wire_len=%d",
             MAC2STR(rx->src_mac),
             type_str,
             (unsigned)packet->seq,
             (int)packet->payload_len,
             (const char *)packet->payload,
             rx->data_len);

#if DEVICE_IS_INITIATOR
    if (packet->type == APP_MSG_PONG) {
        ESP_LOGI(TAG, "Milestone 1 PASS: received PONG for seq=%u", (unsigned)packet->seq);
    }
#else
    if (packet->type == APP_MSG_PING) {
        ESP_ERROR_CHECK(send_app_message(APP_MSG_PONG, packet->seq, "hello-ack from responder"));
    }
#endif
}

static void app_task(void *arg)
{
    uint16_t seq = 1;
    TickType_t last_ping_tick = xTaskGetTickCount();

    ESP_LOGI(TAG, "App task started");

    while (true) {
#if DEVICE_IS_INITIATOR
        TickType_t now = xTaskGetTickCount();
        if ((now - last_ping_tick) >= pdMS_TO_TICKS(PING_INTERVAL_MS)) {
            esp_err_t err = send_app_message(APP_MSG_PING, seq++, "hello from initiator");
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Failed to send PING: %s", esp_err_to_name(err));
            }
            last_ping_tick = now;
        }
#endif

        espnow_rx_packet_t rx = {0};
        if (espnow_transport_recv(&rx, pdMS_TO_TICKS(100))) {
            handle_rx_packet(&rx);
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());

    uint8_t own_mac[ESP_NOW_ETH_ALEN] = {0};
    ESP_ERROR_CHECK(esp_read_mac(own_mac, ESP_MAC_WIFI_STA));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "EDHOC ESP-NOW thesis demo - Milestone 1");
#if DEVICE_IS_INITIATOR
    ESP_LOGI(TAG, "Role: INITIATOR");
#else
    ESP_LOGI(TAG, "Role: RESPONDER");
#endif
    ESP_LOGI(TAG, "Channel: %d", ESPNOW_CHANNEL);
    espnow_transport_print_mac("Own STA MAC", own_mac);
    espnow_transport_print_mac("Configured peer MAC", PEER_MAC);
    ESP_LOGI(TAG, "========================================");

    ESP_ERROR_CHECK(espnow_transport_init(ESPNOW_CHANNEL));

    if (!peer_mac_is_configured(PEER_MAC)) {
        ESP_LOGE(TAG, "PEER_MAC is not configured yet.");
        ESP_LOGE(TAG, "Copy the other board's printed Own STA MAC into main/device_config.h, then rebuild and flash.");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    ESP_ERROR_CHECK(espnow_transport_add_peer(PEER_MAC, false, NULL));

    xTaskCreate(app_task, "milestone1_app", 4096, NULL, 4, NULL);
}
