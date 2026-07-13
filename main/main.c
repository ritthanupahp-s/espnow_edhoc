#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "device_config.h"
#include "edhoc_exporter.h"
#include "edhoc_transport.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_now.h"
#include "esp_system.h"
#include "espnow_transport.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "key_manager.h"
#include "lakers_ffi.h"
#include "nvs_flash.h"

static const char *TAG = "milestone9";

static const uint8_t EDHOC_HANDSHAKE_ACK_PAYLOAD[] = "LAKERS_EDHOC_COMPLETE";
static const uint8_t KEY_TEST_PAYLOAD[] = "ENCRYPTED_KEY_TEST_FROM_INITIATOR";
static const uint8_t KEY_TEST_ACK_PAYLOAD[] = "ENCRYPTED_KEY_TEST_ACK_FROM_RESPONDER";

_Static_assert(
    EDHOC_TRANSPORT_MAX_PAYLOAD_LEN >= LAKERS_EDHOC_MAX_MESSAGE_LEN,
    "EDHOC transport payload must hold the largest Lakers message buffer"
);

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

    return (mac[0] & 0x01) == 0;
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

static void log_edhoc_message(
    const char *direction,
    edhoc_transport_msg_type_t type,
    const uint8_t *payload,
    size_t payload_len
)
{
    const size_t preview_len = payload_len < 32 ? payload_len : 32;

    ESP_LOGI(TAG,
             "%s: type=%s payload_len=%u preview_len=%u",
             direction,
             edhoc_transport_type_str(type),
             (unsigned)payload_len,
             (unsigned)preview_len);

    if (preview_len > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, payload, preview_len, ESP_LOG_INFO);
    }
}

static esp_err_t send_edhoc_payload(
    edhoc_transport_msg_type_t type,
    uint16_t seq,
    const uint8_t *payload,
    size_t payload_len
)
{
    log_edhoc_message("LIVE LAKERS TX", type, payload, payload_len);

    return edhoc_transport_send(
        PEER_MAC,
        type,
        EDHOC_SESSION_ID,
        seq,
        payload,
        payload_len
    );
}

static bool verify_handshake_ack(const edhoc_transport_msg_t *msg)
{
    const size_t expected_len = sizeof(EDHOC_HANDSHAKE_ACK_PAYLOAD) - 1;

    if (msg->payload_len != expected_len ||
        memcmp(msg->payload, EDHOC_HANDSHAKE_ACK_PAYLOAD, expected_len) != 0) {
        ESP_LOGW(TAG, "Invalid transport-level EDHOC completion ACK");
        return false;
    }

    ESP_LOGI(TAG, "Transport-level EDHOC completion ACK verified");
    return true;
}

static esp_err_t send_handshake_ack(uint16_t seq)
{
    return send_edhoc_payload(
        EDHOC_TRANSPORT_MSG_ACK,
        seq,
        EDHOC_HANDSHAKE_ACK_PAYLOAD,
        sizeof(EDHOC_HANDSHAKE_ACK_PAYLOAD) - 1
    );
}

static esp_err_t derive_and_install_lmk(void)
{
    uint8_t lmk[ESPNOW_LMK_LEN] = {0};

    const int32_t status = lakers_edhoc_export_espnow_lmk(lmk);
    if (status != LAKERS_EDHOC_STATUS_OK) {
        ESP_LOGE(TAG,
                 "Lakers exporter failed: status=%ld (%s)",
                 (long)status,
                 lakers_edhoc_status_string(status));
        return ESP_FAIL;
    }

#if MILESTONE9_CORRUPT_LMK_FOR_TEST
    ESP_LOGW(TAG, "Negative test enabled: corrupting Lakers LMK byte 0");
    lmk[0] ^= 0x01;
#endif

    edhoc_exporter_log_lmk_summary(lmk);

    esp_err_t err = key_manager_enable_derived_espnow_encryption(PEER_MAC, lmk);
    memset(lmk, 0, sizeof(lmk));

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to install Lakers-derived LMK: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGI(TAG, "Security transition complete: live Lakers LMK installed, peer encrypt=true");
    return ESP_OK;
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
        EDHOC_SESSION_ID,
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

    if (msg->payload_len != expected_len ||
        memcmp(msg->payload, expected, expected_len) != 0) {
        ESP_LOGW(TAG, "Invalid encrypted control payload for type=%s",
                 edhoc_transport_type_str(msg->type));
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

static void fail_pairing(pairing_state_t *state, const char *reason)
{
    *state = PAIR_STATE_FAILED;
    ESP_LOGE(TAG, "%s", reason);
    ESP_LOGE(TAG, "Pairing state -> %s", pairing_state_str(*state));
}

static bool lakers_status_ok(int32_t status, const char *operation)
{
    if (status == LAKERS_EDHOC_STATUS_OK) {
        return true;
    }

    ESP_LOGE(TAG,
             "%s failed: status=%ld (%s)",
             operation,
             (long)status,
             lakers_edhoc_status_string(status));
    return false;
}

static void handle_received_message(const edhoc_transport_msg_t *msg, pairing_state_t *state)
{
    ESP_LOGI(TAG,
             "STATE RX: type=%s seq=%u state=%s payload_len=%u",
             edhoc_transport_type_str(msg->type),
             (unsigned)msg->seq,
             pairing_state_str(*state),
             (unsigned)msg->payload_len);

    if (msg->session_id != EDHOC_SESSION_ID) {
        fail_pairing(state, "Unexpected EDHOC transport session ID");
        return;
    }

#if DEVICE_IS_INITIATOR
    if (*state == PAIR_STATE_WAIT_M2 && msg->type == EDHOC_TRANSPORT_MSG_M2) {
        uint8_t message_3[LAKERS_EDHOC_MAX_MESSAGE_LEN] = {0};
        size_t message_3_len = 0;

        log_edhoc_message("LIVE LAKERS RX", EDHOC_TRANSPORT_MSG_M2, msg->payload, msg->payload_len);

        const int32_t status = lakers_edhoc_process_message_2(
            msg->payload,
            msg->payload_len,
            message_3,
            sizeof(message_3),
            &message_3_len
        );
        if (!lakers_status_ok(status, "Lakers process message_2 / create message_3")) {
            fail_pairing(state, "Authenticated EDHOC message_2 processing failed");
            return;
        }

        ESP_LOGI(TAG, "Authenticated EDHOC message_2 accepted; sending live message_3");
        ESP_ERROR_CHECK(send_edhoc_payload(
            EDHOC_TRANSPORT_MSG_M3,
            msg->seq + 1,
            message_3,
            message_3_len
        ));

        memset(message_3, 0, sizeof(message_3));
        *state = PAIR_STATE_WAIT_EDHOC_ACK;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_EDHOC_ACK && msg->type == EDHOC_TRANSPORT_MSG_ACK) {
        if (!verify_handshake_ack(msg)) {
            fail_pairing(state, "Final EDHOC completion ACK verification failed");
            return;
        }

        ESP_LOGI(TAG, "Responder confirmed message_3; exporting live Lakers LMK");
        if (derive_and_install_lmk() != ESP_OK) {
            fail_pairing(state, "Initiator live Lakers LMK installation failed");
            return;
        }

        *state = PAIR_STATE_WAIT_KEY_TEST_ACK;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));

        vTaskDelay(pdMS_TO_TICKS(INITIATOR_KEY_TEST_DELAY_MS));
        ESP_LOGI(TAG, "Sending first encrypted frame using live Lakers exporter output");
        ESP_ERROR_CHECK(send_control_message(EDHOC_TRANSPORT_MSG_KEY_TEST, msg->seq + 1));
        return;
    }

    if (*state == PAIR_STATE_WAIT_KEY_TEST_ACK &&
        msg->type == EDHOC_TRANSPORT_MSG_KEY_TEST_ACK) {
        if (!verify_control_payload(msg)) {
            fail_pairing(state, "Encrypted KEY_TEST_ACK verification failed");
            return;
        }

        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        ESP_LOGI(TAG,
                 "Milestone 9 PASS: live Lakers EDHOC, exporter LMK, and encrypted ESP-NOW round trip succeeded");
        return;
    }

    ESP_LOGW(TAG,
             "Unexpected message for initiator: state=%s type=%s",
             pairing_state_str(*state),
             edhoc_transport_type_str(msg->type));
#else
    if (*state == PAIR_STATE_WAIT_M1 && msg->type == EDHOC_TRANSPORT_MSG_M1) {
        uint8_t message_2[LAKERS_EDHOC_MAX_MESSAGE_LEN] = {0};
        size_t message_2_len = 0;

        log_edhoc_message("LIVE LAKERS RX", EDHOC_TRANSPORT_MSG_M1, msg->payload, msg->payload_len);

        const int32_t status = lakers_edhoc_process_message_1(
            msg->payload,
            msg->payload_len,
            message_2,
            sizeof(message_2),
            &message_2_len
        );
        if (!lakers_status_ok(status, "Lakers process message_1 / create message_2")) {
            fail_pairing(state, "Live EDHOC message_1 processing failed");
            return;
        }

        ESP_LOGI(TAG, "Live EDHOC message_1 accepted; sending generated message_2");
        ESP_ERROR_CHECK(send_edhoc_payload(
            EDHOC_TRANSPORT_MSG_M2,
            msg->seq + 1,
            message_2,
            message_2_len
        ));

        memset(message_2, 0, sizeof(message_2));
        *state = PAIR_STATE_WAIT_M3;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_M3 && msg->type == EDHOC_TRANSPORT_MSG_M3) {
        log_edhoc_message("LIVE LAKERS RX", EDHOC_TRANSPORT_MSG_M3, msg->payload, msg->payload_len);

        const int32_t status =
            lakers_edhoc_process_message_3(msg->payload, msg->payload_len);
        if (!lakers_status_ok(status, "Lakers authenticate message_3")) {
            fail_pairing(state, "Authenticated EDHOC message_3 processing failed");
            return;
        }

        ESP_LOGI(TAG, "Authenticated EDHOC message_3 accepted; sending completion ACK");
        ESP_ERROR_CHECK(send_handshake_ack(msg->seq + 1));

        vTaskDelay(pdMS_TO_TICKS(RESPONDER_ENCRYPTION_SWITCH_DELAY_MS));

        ESP_LOGI(TAG, "Exporting and installing live Lakers LMK on responder");
        if (derive_and_install_lmk() != ESP_OK) {
            fail_pairing(state, "Responder live Lakers LMK installation failed");
            return;
        }

        *state = PAIR_STATE_WAIT_KEY_TEST;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_KEY_TEST &&
        msg->type == EDHOC_TRANSPORT_MSG_KEY_TEST) {
        if (!verify_control_payload(msg)) {
            fail_pairing(state, "Encrypted KEY_TEST verification failed");
            return;
        }

        ESP_LOGI(TAG, "Encrypted KEY_TEST accepted; replying with encrypted KEY_TEST_ACK");
        ESP_ERROR_CHECK(send_control_message(
            EDHOC_TRANSPORT_MSG_KEY_TEST_ACK,
            msg->seq + 1
        ));

        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        ESP_LOGI(TAG,
                 "Milestone 9 PASS: responder authenticated live Lakers EDHOC and accepted encrypted ESP-NOW traffic");
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
    (void)arg;

    pairing_state_t state;
    bool initiator_started = false;
    const TickType_t boot_tick = xTaskGetTickCount();

#if DEVICE_IS_INITIATOR
    state = PAIR_STATE_IDLE;
#else
    state = PAIR_STATE_WAIT_M1;
#endif

    ESP_LOGI(TAG, "App task started, state=%s", pairing_state_str(state));

    while (true) {
#if DEVICE_IS_INITIATOR
        if (!initiator_started &&
            (xTaskGetTickCount() - boot_tick) >= pdMS_TO_TICKS(EDHOC_START_DELAY_MS)) {
            uint8_t message_1[LAKERS_EDHOC_MAX_MESSAGE_LEN] = {0};
            size_t message_1_len = 0;

            const int32_t status = lakers_edhoc_make_message_1(
                message_1,
                sizeof(message_1),
                &message_1_len
            );
            if (!lakers_status_ok(status, "Lakers create message_1")) {
                fail_pairing(&state, "Unable to start live Lakers EDHOC handshake");
                initiator_started = true;
                continue;
            }

            ESP_LOGI(TAG, "Starting live Lakers EDHOC over unencrypted ESP-NOW");
            ESP_ERROR_CHECK(send_edhoc_payload(
                EDHOC_TRANSPORT_MSG_M1,
                1,
                message_1,
                message_1_len
            ));
            memset(message_1, 0, sizeof(message_1));

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
    ESP_LOGI(TAG, "EDHOC ESP-NOW thesis demo - Milestone 9");
    ESP_LOGI(TAG, "Goal: live Lakers M1/M2/M3 and real EDHOC exporter LMK on ESP32");
#if DEVICE_IS_INITIATOR
    ESP_LOGI(TAG, "Role: INITIATOR");
#else
    ESP_LOGI(TAG, "Role: RESPONDER");
#endif
    ESP_LOGI(TAG, "Authentication: STAT-STAT, Cipher Suite 2");
    ESP_LOGI(TAG, "Channel: %d", ESPNOW_CHANNEL);
    ESP_LOGI(TAG, "EDHOC transport session ID: 0x%04X", EDHOC_SESSION_ID);
    ESP_LOGI(TAG, "Lakers FFI ABI: 0x%08lX", (unsigned long)lakers_ffi_abi_version());
    espnow_transport_print_mac("Own STA MAC", own_mac);
    espnow_transport_print_mac("Configured peer MAC", PEER_MAC);
    ESP_LOGW(TAG, "Using public Lakers test credentials; not suitable for deployment");
    ESP_LOGI(TAG, "========================================");

    ESP_ERROR_CHECK(espnow_transport_init(ESPNOW_CHANNEL));

    if (!peer_mac_is_configured(PEER_MAC)) {
        ESP_LOGE(TAG, "PEER_MAC is not configured yet.");
        ESP_LOGE(TAG,
                 "Copy the other board's Own STA MAC into main/device_config.h, then rebuild and flash.");
        while (true) {
            vTaskDelay(pdMS_TO_TICKS(5000));
        }
    }

    ESP_ERROR_CHECK(espnow_transport_add_peer(PEER_MAC, false, NULL));
    ESP_LOGI(TAG, "Initial peer state: encrypt=false for live EDHOC transport");

#if DEVICE_IS_INITIATOR
    const uint8_t role = LAKERS_EDHOC_ROLE_INITIATOR;
#else
    const uint8_t role = LAKERS_EDHOC_ROLE_RESPONDER;
#endif

    const int32_t init_status =
        lakers_edhoc_session_init(role, own_mac, PEER_MAC, ESPNOW_CHANNEL);
    if (!lakers_status_ok(init_status, "Lakers session initialization")) {
        ESP_LOGE(TAG, "Milestone 9 cannot continue");
        abort();
    }

    ESP_LOGI(TAG, "Live Lakers session initialized using ESP32 hardware RNG");

    BaseType_t task_created = xTaskCreate(
        app_task,
        "milestone9_app",
        EDHOC_APP_TASK_STACK_SIZE,
        NULL,
        4,
        NULL
    );
    ESP_ERROR_CHECK(task_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
}
