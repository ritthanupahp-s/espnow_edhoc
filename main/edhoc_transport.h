#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_now.h"
#include "espnow_transport.h"

#define EDHOC_TRANSPORT_MAGIC 0xE7
#define EDHOC_TRANSPORT_VERSION 1
#define EDHOC_TRANSPORT_MAX_PAYLOAD_LEN 160

typedef enum {
    EDHOC_TRANSPORT_MSG_M1           = 0x10,
    EDHOC_TRANSPORT_MSG_M2           = 0x11,
    EDHOC_TRANSPORT_MSG_M3           = 0x12,
    EDHOC_TRANSPORT_MSG_ACK          = 0x13,
    EDHOC_TRANSPORT_MSG_KEY_TEST     = 0x20,
    EDHOC_TRANSPORT_MSG_KEY_TEST_ACK = 0x21,
} edhoc_transport_msg_type_t;

typedef struct {
    uint8_t src_mac[ESP_NOW_ETH_ALEN];
    uint8_t type;
    uint8_t flags;
    uint16_t session_id;
    uint16_t seq;
    uint8_t frag_idx;
    uint8_t frag_count;
    uint16_t payload_len;
    uint8_t payload[EDHOC_TRANSPORT_MAX_PAYLOAD_LEN];
} edhoc_transport_msg_t;

esp_err_t edhoc_transport_send(
    const uint8_t peer_mac[ESP_NOW_ETH_ALEN],
    edhoc_transport_msg_type_t type,
    uint16_t session_id,
    uint16_t seq,
    const uint8_t *payload,
    size_t payload_len
);

bool edhoc_transport_parse_rx(const espnow_rx_packet_t *rx, edhoc_transport_msg_t *out_msg);
const char *edhoc_transport_type_str(uint8_t type);
