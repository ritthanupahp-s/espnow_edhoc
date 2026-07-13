#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_now.h"
#include "freertos/FreeRTOS.h"

#define ESPNOW_TRANSPORT_RX_QUEUE_LEN 10
#define ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN 250

typedef struct {
    uint8_t src_mac[ESP_NOW_ETH_ALEN];
    uint8_t data[ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN];
    int data_len;
} espnow_rx_packet_t;

esp_err_t espnow_transport_init(uint8_t channel);
esp_err_t espnow_transport_set_pmk(const uint8_t pmk[ESP_NOW_KEY_LEN]);
esp_err_t espnow_transport_add_peer(const uint8_t peer_mac[ESP_NOW_ETH_ALEN], bool encrypted, const uint8_t *lmk);
esp_err_t espnow_transport_send(const uint8_t peer_mac[ESP_NOW_ETH_ALEN], const void *data, size_t len);
bool espnow_transport_recv(espnow_rx_packet_t *packet, TickType_t timeout_ticks);
void espnow_transport_print_mac(const char *label, const uint8_t mac[ESP_NOW_ETH_ALEN]);
