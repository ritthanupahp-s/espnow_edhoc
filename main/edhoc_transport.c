#include "edhoc_transport.h"

#include <string.h>

#include "esp_log.h"
#include "esp_mac.h"

static const char *TAG = "edhoc_transport";

/*
 * Milestone 3 wire format:
 *
 * magic | version | type | flags | session_id | seq | frag_idx | frag_count | payload_len | payload
 *
 * Fragment fields are included now so the format is ready for larger real EDHOC
 * messages later. Milestone 3 only sends one fragment: frag_idx=0, frag_count=1.
 */
typedef struct __attribute__((packed)) {
    uint8_t magic;
    uint8_t version;
    uint8_t type;
    uint8_t flags;
    uint16_t session_id;
    uint16_t seq;
    uint8_t frag_idx;
    uint8_t frag_count;
    uint16_t payload_len;
} edhoc_transport_hdr_t;

const char *edhoc_transport_type_str(uint8_t type)
{
    switch (type) {
    case EDHOC_TRANSPORT_MSG_M1:
        return "EDHOC_M1";
    case EDHOC_TRANSPORT_MSG_M2:
        return "EDHOC_M2";
    case EDHOC_TRANSPORT_MSG_M3:
        return "EDHOC_M3";
    case EDHOC_TRANSPORT_MSG_ACK:
        return "EDHOC_ACK";
    default:
        return "UNKNOWN";
    }
}

esp_err_t edhoc_transport_send(
    const uint8_t peer_mac[ESP_NOW_ETH_ALEN],
    edhoc_transport_msg_type_t type,
    uint16_t session_id,
    uint16_t seq,
    const uint8_t *payload,
    size_t payload_len)
{
    if (peer_mac == NULL || (payload_len > 0 && payload == NULL)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (payload_len > EDHOC_TRANSPORT_MAX_PAYLOAD_LEN) {
        ESP_LOGE(TAG, "Payload too large for Milestone 3 transport: len=%u max=%u",
                 (unsigned)payload_len,
                 (unsigned)EDHOC_TRANSPORT_MAX_PAYLOAD_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    const size_t header_len = sizeof(edhoc_transport_hdr_t);
    const size_t wire_len = header_len + payload_len;

    if (wire_len > ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN) {
        ESP_LOGE(TAG, "Frame too large for ESP-NOW transport: wire_len=%u max=%u",
                 (unsigned)wire_len,
                 (unsigned)ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN);
        return ESP_ERR_INVALID_SIZE;
    }

    uint8_t frame[ESPNOW_TRANSPORT_MAX_PAYLOAD_LEN] = {0};
    edhoc_transport_hdr_t hdr = {
        .magic = EDHOC_TRANSPORT_MAGIC,
        .version = EDHOC_TRANSPORT_VERSION,
        .type = (uint8_t)type,
        .flags = 0,
        .session_id = session_id,
        .seq = seq,
        .frag_idx = 0,
        .frag_count = 1,
        .payload_len = (uint16_t)payload_len,
    };

    memcpy(frame, &hdr, header_len);
    if (payload_len > 0) {
        memcpy(frame + header_len, payload, payload_len);
    }

    ESP_LOGI(TAG,
             "EDHOC TX: dest=" MACSTR " type=%s session=0x%04X seq=%u frag=%u/%u payload_len=%u wire_len=%u",
             MAC2STR(peer_mac),
             edhoc_transport_type_str(type),
             (unsigned)session_id,
             (unsigned)seq,
             (unsigned)hdr.frag_idx,
             (unsigned)hdr.frag_count,
             (unsigned)payload_len,
             (unsigned)wire_len);

    return espnow_transport_send(peer_mac, frame, wire_len);
}

bool edhoc_transport_parse_rx(const espnow_rx_packet_t *rx, edhoc_transport_msg_t *out_msg)
{
    if (rx == NULL || out_msg == NULL) {
        return false;
    }

    const size_t header_len = sizeof(edhoc_transport_hdr_t);
    if (rx->data_len < (int)header_len) {
        ESP_LOGW(TAG, "RX frame too short for EDHOC transport: len=%d", rx->data_len);
        return false;
    }

    const edhoc_transport_hdr_t *hdr = (const edhoc_transport_hdr_t *)rx->data;
    if (hdr->magic != EDHOC_TRANSPORT_MAGIC || hdr->version != EDHOC_TRANSPORT_VERSION) {
        ESP_LOGW(TAG, "RX frame is not EDHOC transport: magic=0x%02X version=%u",
                 (unsigned)hdr->magic,
                 (unsigned)hdr->version);
        return false;
    }

    if (hdr->frag_count == 0 || hdr->frag_idx >= hdr->frag_count) {
        ESP_LOGW(TAG, "Invalid fragment metadata: frag=%u/%u",
                 (unsigned)hdr->frag_idx,
                 (unsigned)hdr->frag_count);
        return false;
    }

    if (hdr->payload_len > EDHOC_TRANSPORT_MAX_PAYLOAD_LEN) {
        ESP_LOGW(TAG, "EDHOC payload too large: len=%u max=%u",
                 (unsigned)hdr->payload_len,
                 (unsigned)EDHOC_TRANSPORT_MAX_PAYLOAD_LEN);
        return false;
    }

    if (header_len + hdr->payload_len > (size_t)rx->data_len) {
        ESP_LOGW(TAG, "Invalid EDHOC payload length: payload_len=%u wire_len=%d",
                 (unsigned)hdr->payload_len,
                 rx->data_len);
        return false;
    }

    memset(out_msg, 0, sizeof(*out_msg));
    memcpy(out_msg->src_mac, rx->src_mac, ESP_NOW_ETH_ALEN);
    out_msg->type = hdr->type;
    out_msg->flags = hdr->flags;
    out_msg->session_id = hdr->session_id;
    out_msg->seq = hdr->seq;
    out_msg->frag_idx = hdr->frag_idx;
    out_msg->frag_count = hdr->frag_count;
    out_msg->payload_len = hdr->payload_len;

    if (hdr->payload_len > 0) {
        memcpy(out_msg->payload, rx->data + header_len, hdr->payload_len);
    }

    ESP_LOGI(TAG,
             "EDHOC RX: from=" MACSTR " type=%s session=0x%04X seq=%u frag=%u/%u payload_len=%u",
             MAC2STR(out_msg->src_mac),
             edhoc_transport_type_str(out_msg->type),
             (unsigned)out_msg->session_id,
             (unsigned)out_msg->seq,
             (unsigned)out_msg->frag_idx,
             (unsigned)out_msg->frag_count,
             (unsigned)out_msg->payload_len);

    return true;
}
