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

static const char *TAG = "milestone6";

static const uint8_t KEY_TEST_PAYLOAD[] = "ENCRYPTED_KEY_TEST_FROM_INITIATOR";
static const uint8_t KEY_TEST_ACK_PAYLOAD[] = "ENCRYPTED_KEY_TEST_ACK_FROM_RESPONDER";

typedef enum {
    PAIR_STATE_IDLE = 0,
    PAIR_STATE_WAIT_M1,
    PAIR_STATE_WAIT_M2,
    PAIR_STATE_WAIT_M3,
    PAIR_STATE_WAIT_EDHOC_ACK,
    PAIR_STATE_WAIT_KEY_TEST,
    PAIR_STATE_WAIT_KEY_TEST_ACK,
    PAIR_STATE_COMPLETE,
    PAIR_STATE_FAILED,
} pairing_state_t;

static const char *security_mode_str(void)
{
#if DYNAMIC_LMK_SWITCH_ENABLED
    return "unencrypted-edhoc-then-derived-lmk-encrypted";
#elif EDHOC_TRACE_TRANSPORT_ENABLED
    return "unencrypted-rfc9529-edhoc-trace";
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
    case PAIR_STATE_WAIT_EDHOC_ACK:
        return "WAIT_EDHOC_ACK";
    case PAIR_STATE_WAIT_KEY_TEST:
        return "WAIT_KEY_TEST";
    case PAIR_STATE_WAIT_KEY_TEST_ACK:
        return "WAIT_KEY_TEST_ACK";
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

static esp_err_t derive_and_install_lmk(void)
{
    uint8_t lmk[ESPNOW_LMK_LEN] = {0};

    esp_err_t err = edhoc_exporter_trace_derive_espnow_lmk(lmk);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to derive LMK candidate: %s", esp_err_to_name(err));
        return err;
    }

#if MILESTONE6_CORRUPT_LMK_FOR_TEST
    ESP_LOGW(TAG, "Negative test enabled: corrupting LMK byte 0 before peer installation");
    lmk[0] ^= 0x01;
#endif

    edhoc_exporter_log_lmk_summary(lmk);

    err = key_manager_enable_derived_espnow_encryption(PEER_MAC, lmk);
    memset(lmk, 0, sizeof(lmk));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install derived LMK: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Security transition complete: peer is now encrypt=true");
    return ESP_OK;
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

static esp_err_t send_control_message(edhoc_transport_msg_type_t type, uint16_t seq)
{
    const uint8_t *payload = NULL;
    size_t payload_len = 0;

    switch (type) {
    case EDHOC_TRANSPORT_MSG_KEY_TEST:
        payload = KEY_TEST_PAYLOAD;
        payload_len = sizeof(KEY_TEST_PAYLOAD) - 1;
        break;
    case EDHOC_TRANSPORT_MSG_KEY_TEST_ACK:
        payload = KEY_TEST_ACK_PAYLOAD;
        payload_len = sizeof(KEY_TEST_ACK_PAYLOAD) - 1;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGI(TAG,
             "ENCRYPTED APP TX: type=%s seq=%u payload=\"%.*s\"",
             edhoc_transport_type_str(type),
             (unsigned)seq,
             (int)payload_len,
             (const char *)payload);

    return edhoc_transport_send(
        PEER_MAC,
        type,
        EDHOC_TRACE_SESSION_ID,
        seq,
        payload,
        payload_len
    );
}

static bool verify_control_payload(const edhoc_transport_msg_t *msg)
{
    const uint8_t *expected = NULL;
    size_t expected_len = 0;

    switch (msg->type) {
    case EDHOC_TRANSPORT_MSG_KEY_TEST:
        expected = KEY_TEST_PAYLOAD;
        expected_len = sizeof(KEY_TEST_PAYLOAD) - 1;
        break;
    case EDHOC_TRANSPORT_MSG_KEY_TEST_ACK:
        expected = KEY_TEST_ACK_PAYLOAD;
        expected_len = sizeof(KEY_TEST_ACK_PAYLOAD) - 1;
        break;
    default:
        return false;
    }

    if (msg->payload_len != expected_len || memcmp(msg->payload, expected, expected_len) != 0) {
        ESP_LOGW(TAG, "Invalid encrypted control payload for type=%s", edhoc_transport_type_str(msg->type));
        return false;
    }

    ESP_LOGI(TAG,
             "ENCRYPTED APP RX verified: type=%s seq=%u payload=\"%.*s\"",
             edhoc_transport_type_str(msg->type),
             (unsigned)msg->seq,
             (int)msg->payload_len,
             (const char *)msg->payload);

    return true;
}

static bool verify_trace_payload(const edhoc_transport_msg_t *msg)
{
    if (!edhoc_trace_verify_message((edhoc_transport_msg_type_t)msg->type, msg->payload, msg->payload_len)) {
        ESP_LOGW(TAG,
                 "Received %s but payload does not match the expected RFC 9529 trace bytes",
                 edhoc_transport_type_str(msg->type));
        return false;
    }

    edhoc_trace_log_message(
        "EDHOC TRACE APP RX verified",
        (edhoc_transport_msg_type_t)msg->type,
        msg->payload,
        msg->payload_len
    );
    return true;
}

static void fail_pairing(pairing_state_t *state, const char *reason)
{
    *state = PAIR_STATE_FAILED;
    ESP_LOGE(TAG, "%s", reason);
    ESP_LOGE(TAG, "Pairing state -> %s", pairing_state_str(*state));
}

static void handle_received_message(const edhoc_transport_msg_t *msg, pairing_state_t *state)
{
    ESP_LOGI(TAG,
             "STATE RX: type=%s seq=%u state=%s payload_len=%u",
             edhoc_transport_type_str(msg->type),
             (unsigned)msg->seq,
             pairing_state_str(*state),
             (unsigned)msg->payload_len);

    if (msg->session_id != EDHOC_TRACE_SESSION_ID) {
        fail_pairing(state, "Unexpected EDHOC transport session ID");
        return;
    }

#if DEVICE_IS_INITIATOR
    if (*state == PAIR_STATE_WAIT_M2 && msg->type == EDHOC_TRANSPORT_MSG_M2) {
        if (!verify_trace_payload(msg)) {
            fail_pairing(state, "EDHOC message_2 verification failed");
            return;
        }

        ESP_LOGI(TAG, "EDHOC message_2 accepted; sending message_3 while peer is still unencrypted");
        ESP_ERROR_CHECK(send_edhoc_trace_message(EDHOC_TRANSPORT_MSG_M3, msg->seq + 1));
        *state = PAIR_STATE_WAIT_EDHOC_ACK;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_EDHOC_ACK && msg->type == EDHOC_TRANSPORT_MSG_ACK) {
        if (!verify_trace_payload(msg)) {
            fail_pairing(state, "Final EDHOC ACK verification failed");
            return;
        }

        ESP_LOGI(TAG, "Final unencrypted EDHOC ACK received; installing LMK on initiator");
        if (derive_and_install_lmk() != ESP_OK) {
            fail_pairing(state, "Initiator LMK installation failed");
            return;
        }

        *state = PAIR_STATE_WAIT_KEY_TEST_ACK;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));

        vTaskDelay(pdMS_TO_TICKS(INITIATOR_KEY_TEST_DELAY_MS));
        ESP_LOGI(TAG, "Sending first post-handshake frame; ESP-NOW peer should encrypt this frame");
        ESP_ERROR_CHECK(send_control_message(EDHOC_TRANSPORT_MSG_KEY_TEST, msg->seq + 1));
        return;
    }

    if (*state == PAIR_STATE_WAIT_KEY_TEST_ACK && msg->type == EDHOC_TRANSPORT_MSG_KEY_TEST_ACK) {
        if (!verify_control_payload(msg)) {
            fail_pairing(state, "Encrypted KEY_TEST_ACK verification failed");
            return;
        }

        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        ESP_LOGI(TAG, "Milestone 6 PASS: derived LMK installed and encrypted ESP-NOW KEY_TEST round trip succeeded");
        return;
    }

    ESP_LOGW(TAG,
             "Unexpected message for initiator: state=%s type=%s",
             pairing_state_str(*state),
             edhoc_transport_type_str(msg->type));
#else
    if (*state == PAIR_STATE_WAIT_M1 && msg->type == EDHOC_TRANSPORT_MSG_M1) {
        if (!verify_trace_payload(msg)) {
            fail_pairing(state, "EDHOC message_1 verification failed");
            return;
        }

        ESP_LOGI(TAG, "EDHOC message_1 accepted; sending message_2 while peer is still unencrypted");
        ESP_ERROR_CHECK(send_edhoc_trace_message(EDHOC_TRANSPORT_MSG_M2, msg->seq + 1));
        *state = PAIR_STATE_WAIT_M3;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_M3 && msg->type == EDHOC_TRANSPORT_MSG_M3) {
        if (!verify_trace_payload(msg)) {
            fail_pairing(state, "EDHOC message_3 verification failed");
            return;
        }

        ESP_LOGI(TAG, "EDHOC message_3 accepted; sending final ACK before enabling peer encryption");
        ESP_ERROR_CHECK(send_edhoc_trace_message(EDHOC_TRANSPORT_MSG_ACK, msg->seq + 1));

        vTaskDelay(pdMS_TO_TICKS(RESPONDER_ENCRYPTION_SWITCH_DELAY_MS));

        ESP_LOGI(TAG, "Installing LMK on responder after final unencrypted ACK");
        if (derive_and_install_lmk() != ESP_OK) {
            fail_pairing(state, "Responder LMK installation failed");
            return;
        }

        *state = PAIR_STATE_WAIT_KEY_TEST;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_KEY_TEST && msg->type == EDHOC_TRANSPORT_MSG_KEY_TEST) {
        if (!verify_control_payload(msg)) {
            fail_pairing(state, "Encrypted KEY_TEST verification failed");
            return;
        }

        ESP_LOGI(TAG, "Encrypted KEY_TEST accepted; replying with encrypted KEY_TEST_ACK");
        ESP_ERROR_CHECK(send_control_message(EDHOC_TRANSPORT_MSG_KEY_TEST_ACK, msg->seq + 1));

        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        ESP_LOGI(TAG, "Milestone 6 PASS: responder received and acknowledged encrypted traffic using the derived LMK");
        return;
    }

    ESP_LOGW(TAG,
             "Unexpected message for responder: state=%s type=%s",
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
        if (!initiator_started &&
            (xTaskGetTickCount() - boot_tick) >= pdMS_TO_TICKS(EDHOC_TRACE_START_DELAY_MS)) {
            ESP_LOGI(TAG, "Starting unencrypted EDHOC trace transport exchange");
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
                handle_received_message(&msg, &state);
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
    ESP_LOGI(TAG, "EDHOC ESP-NOW thesis demo - Milestone 6");
    ESP_LOGI(TAG, "Goal: install derived LMK and switch to encrypted ESP-NOW unicast");
    ESP_LOGW(TAG, "LMK source is still the trace-only scaffold; Lakers exporter integration remains required");
#if DEVICE_IS_INITIATOR
    ESP_LOGI(TAG, "Role: INITIATOR");
#else
    ESP_LOGI(TAG, "Role: RESPONDER");
#endif
    ESP_LOGI(TAG, "Channel: %d", ESPNOW_CHANNEL);
    ESP_LOGI(TAG, "Security mode: %s", security_mode_str());
    ESP_LOGI(TAG, "EDHOC trace source: %s", EDHOC_TRACE_RFC9529_SECTION);
    ESP_LOGI(TAG, "EDHOC transport session ID: 0x%04X", EDHOC_TRACE_SESSION_ID);
    ESP_LOGI(TAG, "Responder switch delay: %d ms", RESPONDER_ENCRYPTION_SWITCH_DELAY_MS);
    ESP_LOGI(TAG, "Initiator KEY_TEST delay: %d ms", INITIATOR_KEY_TEST_DELAY_MS);
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

    /* EDHOC transport starts without an LMK. */
    ESP_ERROR_CHECK(espnow_transport_add_peer(PEER_MAC, false, NULL));
    ESP_LOGI(TAG, "Initial peer state: encrypt=false for EDHOC transport");

    xTaskCreate(app_task, "milestone6_app", 4096, NULL, 4, NULL);
}
