#include <stdint.h>
#include <string.h>

#include "device_config.h"
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

static const char *TAG = "milestone3";

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
#if FAKE_EDHOC_TRANSPORT_ENABLED
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

static esp_err_t send_fake_edhoc_message(edhoc_transport_msg_type_t type, uint16_t seq, const char *payload)
{
    const size_t payload_len = strlen(payload);

    ESP_LOGI(TAG,
             "FAKE EDHOC APP TX: type=%s seq=%u payload=\"%s\"",
             edhoc_transport_type_str(type),
             (unsigned)seq,
             payload);

    return edhoc_transport_send(
        PEER_MAC,
        type,
        FAKE_EDHOC_SESSION_ID,
        seq,
        (const uint8_t *)payload,
        payload_len
    );
}

static void handle_fake_edhoc_message(const edhoc_transport_msg_t *msg, pairing_state_t *state)
{
    ESP_LOGI(TAG,
             "FAKE EDHOC APP RX: type=%s seq=%u state=%s payload=\"%.*s\"",
             edhoc_transport_type_str(msg->type),
             (unsigned)msg->seq,
             pairing_state_str(*state),
             (int)msg->payload_len,
             (const char *)msg->payload);

    if (msg->session_id != FAKE_EDHOC_SESSION_ID) {
        ESP_LOGW(TAG, "Ignoring message for unexpected session: got=0x%04X expected=0x%04X",
                 (unsigned)msg->session_id,
                 (unsigned)FAKE_EDHOC_SESSION_ID);
        return;
    }

#if DEVICE_IS_INITIATOR
    if (*state == PAIR_STATE_WAIT_M2 && msg->type == EDHOC_TRANSPORT_MSG_M2) {
        ESP_LOGI(TAG, "Fake EDHOC message_2 accepted; sending fake message_3");
        ESP_ERROR_CHECK(send_fake_edhoc_message(EDHOC_TRANSPORT_MSG_M3, msg->seq + 1, "FAKE_EDHOC_MESSAGE_3 from initiator"));
        *state = PAIR_STATE_WAIT_ACK;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_ACK && msg->type == EDHOC_TRANSPORT_MSG_ACK) {
        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        ESP_LOGI(TAG, "Milestone 3 PASS: fake EDHOC M1/M2/M3 transport exchange completed");
        return;
    }

    ESP_LOGW(TAG, "Unexpected fake EDHOC message for initiator: state=%s type=%s",
             pairing_state_str(*state),
             edhoc_transport_type_str(msg->type));
#else
    if (*state == PAIR_STATE_WAIT_M1 && msg->type == EDHOC_TRANSPORT_MSG_M1) {
        ESP_LOGI(TAG, "Fake EDHOC message_1 accepted; sending fake message_2");
        ESP_ERROR_CHECK(send_fake_edhoc_message(EDHOC_TRANSPORT_MSG_M2, msg->seq + 1, "FAKE_EDHOC_MESSAGE_2 from responder"));
        *state = PAIR_STATE_WAIT_M3;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        return;
    }

    if (*state == PAIR_STATE_WAIT_M3 && msg->type == EDHOC_TRANSPORT_MSG_M3) {
        ESP_LOGI(TAG, "Fake EDHOC message_3 accepted; fake handshake complete on responder");
        ESP_ERROR_CHECK(send_fake_edhoc_message(EDHOC_TRANSPORT_MSG_ACK, msg->seq + 1, "FAKE_EDHOC_DONE from responder"));
        *state = PAIR_STATE_COMPLETE;
        ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(*state));
        ESP_LOGI(TAG, "Milestone 3 PASS: responder processed fake EDHOC M1/M2/M3");
        return;
    }

    ESP_LOGW(TAG, "Unexpected fake EDHOC message for responder: state=%s type=%s",
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
        if (!initiator_started && (xTaskGetTickCount() - boot_tick) >= pdMS_TO_TICKS(FAKE_EDHOC_START_DELAY_MS)) {
            ESP_LOGI(TAG, "Starting fake EDHOC transport exchange");
            ESP_ERROR_CHECK(send_fake_edhoc_message(EDHOC_TRANSPORT_MSG_M1, 1, "FAKE_EDHOC_MESSAGE_1 from initiator"));
            state = PAIR_STATE_WAIT_M2;
            initiator_started = true;
            ESP_LOGI(TAG, "Pairing state -> %s", pairing_state_str(state));
        }
#endif

        espnow_rx_packet_t rx = {0};
        if (espnow_transport_recv(&rx, pdMS_TO_TICKS(100))) {
            edhoc_transport_msg_t msg = {0};
            if (edhoc_transport_parse_rx(&rx, &msg)) {
                handle_fake_edhoc_message(&msg, &state);
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
    ESP_LOGI(TAG, "EDHOC ESP-NOW thesis demo - Milestone 3");
    ESP_LOGI(TAG, "Goal: fake EDHOC M1/M2/M3 transport over ESP-NOW");
#if DEVICE_IS_INITIATOR
    ESP_LOGI(TAG, "Role: INITIATOR");
#else
    ESP_LOGI(TAG, "Role: RESPONDER");
#endif
    ESP_LOGI(TAG, "Channel: %d", ESPNOW_CHANNEL);
    ESP_LOGI(TAG, "Security mode: %s", security_mode_str());
    ESP_LOGI(TAG, "Fake EDHOC session ID: 0x%04X", FAKE_EDHOC_SESSION_ID);
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
    ESP_LOGW(TAG, "Static encryption is enabled; Milestone 3 normally expects unencrypted pre-key transport");
    ESP_ERROR_CHECK(key_manager_enable_static_espnow_encryption(PEER_MAC));
#else
    ESP_ERROR_CHECK(espnow_transport_add_peer(PEER_MAC, false, NULL));
#endif

    xTaskCreate(app_task, "milestone3_app", 4096, NULL, 4, NULL);
}
