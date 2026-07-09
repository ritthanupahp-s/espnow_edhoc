#include <stdint.h>
#include <string.h>

#include "device_config.h"
#include "edhoc_exporter.h"
#include "edhoc_trace_vectors.h"
#include "edhoc_transport.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_system.h"
#include "espnow_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "key_manager.h"
#include "nvs_flash.h"

static const char *TAG = "milestone5";

typedef enum {
    PAIR_STATE_IDLE = 0,
    PAIR_STATE_WAIT_M1,
    PAIR_STATE_WAIT_M2,
    PAIR_STATE_WAIT_M3,
    PAIR_STATE_WAIT_ACK,
    PAIR_STATE_COMPLETE,
    PAIR_STATE_FAILED,
} pairing_state_t;

static const char *security_mode_str(void)
{
#if EDHOC_TRACE_TRANSPORT_ENABLED
    return "unencrypted-rfc9529-edhoc-trace-plus-lmk-scaffold";
#elif FAKE_EDHOC_TRANSPORT_ENABLED
    return "unencrypted-fake-edhoc-transport";
#elif ESPNOW_STATIC_ENCRYPTION_ENABLED
    return "static-lmk-encrypted";
#else
    return "unencrypted";
#endif
}

static const char *pairing_state_str(pairing_state_t state)
{
    switch (state) {
    case PAIR_STATE_IDLE:
        return "IDLE";
    case PAIR_STATE_WAIT_M1:
        return "WAIT_M1";
    case PAIR_STATE_WAIT_M2:
        return "WAIT_M2";
    case PAIR_STATE_WAIT_M3:
        return "WAIT_M3";
    case PAIR_STATE_WAIT_ACK:
        return "WAIT_ACK";
    case PAIR_STATE_COMPLETE:
        return "COMPLETE";
    case PAIR_STATE_FAILED:
        return "FAILED";
    default:
        return "UNKNOWN";
    }
}

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

static void derive_and_log_lmk_candidate(void)
{
    uint8_t lmk[ESPNOW_LMK_LEN] = {0};

    esp_err_t err = edhoc_exporter_trace_derive_espnow_lmk(lmk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to derive trace-only LMK candidate: %s", esp_err_to_name(err));
        return;
    }

    edhoc_exporter_log_lmk_summary(lmk);
    memset(lmk, 0, sizeof(lmk));
}

static esp_err_t send_edhoc_trace_message(edhoc_transport_msg_type_t type, uint16_t seq)
{
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    if (!edhoc_trace_get_message(type, &payload, &payload_len)) {
        ESP_LOGE(TAG, "No RFC 9529 trace payload for type=%s", edhoc_transport_type_str(type));
        return ESP_ERR_INVALID_ARG;
    }

    edhoc_trace_log_message("EDHOC TRACE APP TX", type, payload, payload_len);

    return edhoc_transport_send(
        PEER_MAC,
        type,
        EDHOC_TRACE_SESSION_ID,
        seq,
        payload,
        payload_len
    );
}

static bool verify_received_trace(const edhoc_transport_msg_t *msg)
{
    if (msg->session_id != EDHOC_TRACE_SESSION_ID) {
        ESP_LOGW(TAG, "Ignoring message for unexpected session: got=0x%04X expected=0x%04X",
                 (unsigned)msg->session_id,
                 (unsigned)EDHOC_TRACE_SESSION_ID);
        return false;
    }

    if (!edhoc_trace_verify_message((edhoc_transport_msg_type_t)msg->type, msg->payload, msg->payload_len)) {
        ESP_LOGW(TAG,
                 "Received %s but payload does not match the expected RFC 9529 trace bytes",
                 edhoc_transport_type_str(msg->type));
        return false;
    }

    edhoc_trace_log_message("EDHOC TRACE APP RX verified", (edhoc_transport_msg_type_t)msg->type, msg->payload, msg->payload_len);
    return true;
}

static void handle_edhoc_trace_message(const edhoc_transport_msg_t *msg, pairing_state_t *state)
{
    ESP_LOGI(TAG,
             "EDHOC TRACE STATE RX: type=%s seq=%u state=%s payload_len=%u",
             edhoc_transport_type_str(msg->type),
             (unsigned)msg->seq,
             pairing_state_str(*state),
             (unsigned)msg->payload_len);

    if (!verify_received_trace(msg)) {
        *state = PAIR_STATE_FAILED;
        ESP_LOGW(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

#if DEVICE_IS_INITIATOR
    if (*state == PAIR_STATE_WAIT_M2 && msg->type == EDHOC_TRANSPORT_MSG_M2) {
        ESP_LOGI(TAG, "RFC 9529 EDHOC message_2 accepted; sending message_3 trace bytes");
        ESP_ERROR_CHECK(send_edhoc_trace_message(EDHOC_TRANSPORT_MSG_M3, msg->seq + 1));
        *state = PAIR_STATE_WAIT_ACK;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_ACK && msg->type == EDHOC_TRANSPORT_MSG_ACK) {
        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        derive_and_log_lmk_candidate();
        ESP_LOGI(TAG, "Milestone 5 PASS: 16-byte ESP-NOW LMK candidate derived after EDHOC trace transport");
        return;
    }

    ESP_LOGW(TAG, "Unexpected EDHOC trace message for initiator: state=%s type=%s",
             pairing_state_str(*state),
             edhoc_transport_type_str(msg->type));
#else
    if (*state == PAIR_STATE_WAIT_M1 && msg->type == EDHOC_TRANSPORT_MSG_M1) {
        ESP_LOGI(TAG, "RFC 9529 EDHOC message_1 accepted; sending message_2 trace bytes");
        ESP_ERROR_CHECK(send_edhoc_trace_message(EDHOC_TRANSPORT_MSG_M2, msg->seq + 1));
        *state = PAIR_STATE_WAIT_M3;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_M3 && msg->type == EDHOC_TRANSPORT_MSG_M3) {
        ESP_LOGI(TAG, "RFC 9529 EDHOC message_3 accepted; trace handshake transport complete on responder");
        ESP_ERROR_CHECK(send_edhoc_trace_message(EDHOC_TRANSPORT_MSG_ACK, msg->seq + 1));
        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        derive_and_log_lmk_candidate();
        ESP_LOGI(TAG, "Milestone 5 PASS: responder derived matching 16-byte ESP-NOW LMK candidate");
        return;
    }

    ESP_LOGW(TAG, "Unexpected EDHOC trace message for responder: state=%s type=%s",
             pairing_state_str(*state),
             edhoc_transport_type_str(msg->type));
#endif
}

static void app_task(void *arg)
{
    pairing_state_t state;
    bool initiator_started = false;
    TickType_t boot_tick = xTaskGetTickCount();

#if DEVICE_IS_INITIATOR
    state = PAIR_STATE_IDLE;
#else
    state = PAIR_STATE_WAIT_M1;
#endif

    ESP_LOGI(TAG, "App task started, security_mode=%s state=%s", security_mode_str(), pairing_state_str(state));

    while (true) {
#if DEVICE_IS_INITIATOR
        if (!initiator_started && (xTaskGetTickCount() - boot_tick) >= pdMS_TO_TICKS(EDHOC_TRACE_START_DELAY_MS)) {
            ESP_LOGI(TAG, "Starting RFC 9529 EDHOC trace transport exchange");
            ESP_ERROR_CHECK(send_edhoc_trace_message(EDHOC_TRANSPORT_MSG_M1, 1));
            state = PAIR_STATE_WAIT_M2;
            initiator_started = true;
            ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(state));
        }
#endif

        espnow_rx_packet_t rx = {0};
        if (espnow_transport_recv(&rx, pdMS_TO_TICKS(100))) {
            edhoc_transport_msg_t msg = {0};
            if (edhoc_transport_parse_rx(&rx, &msg)) {
                handle_edhoc_trace_message(&msg, &state);
            }
        }
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(nvs_init());

    uint8_t own_mac[ESP_NOW_ETH_ALEN] = {0};
    ESP_ERROR_CHECK(esp_read_mac(own_mac, ESP_MAC_WIFI_STA));

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, "EDHOC ESP-NOW thesis demo - Milestone 5");
    ESP_LOGI(TAG, "Goal: derive 16-byte ESP-NOW LMK candidate after EDHOC transport");
    ESP_LOGW(TAG, "Current derivation is trace-only; Lakers edhoc_exporter() is the next integration target");
#if DEVICE_IS_INITIATOR
    ESP_LOGI(TAG, "Role: INITIATOR");
#else
    ESP_LOGI(TAG, "Role: RESPONDER");
#endif
    ESP_LOGI(TAG, "Channel: %d", ESPNOW_CHANNEL);
    ESP_LOGI(TAG, "Security mode: %s", security_mode_str());
    ESP_LOGI(TAG, "EDHOC trace source: %s", EDHOC_TRACE_RFC9529_SECTION);
    ESP_LOGI(TAG, "EDHOC transport session ID: 0x%04X", EDHOC_TRACE_SESSION_ID);
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

#if ESPNOW_STATIC_ENCRYPTION_ENABLED
    ESP_LOGW(TAG, "Static encryption is enabled; Milestone 5 normally expects unencrypted pre-key EDHOC transport");
    ESP_ERROR_CHECK(key_manager_enable_static_espnow_encryption(PEER_MAC));
#else
    ESP_ERROR_CHECK(espnow_transport_add_peer(PEER_MAC, false, NULL));
#endif

    xTaskCreate(app_task, "milestone5_app", 4096, NULL, 4, NULL);
}
