#include "edhoc_trace_vectors.h"

#include <string.h>

#include "esp_log.h"

static const char *TAG = "edhoc_trace";

/*
 * RFC 9529 Section 3 trace, Static DH with CCS identified by kid.
 * The RFC describes this as a low-overhead trace with message sizes:
 * message_1 = 39 bytes, message_2 = 45 bytes, message_3 = 19 bytes.
 */
static const uint8_t RFC9529_S3_MESSAGE_1[] = {
    0x03, 0x82, 0x06, 0x02, 0x58, 0x20, 0x8a, 0xf6,
    0xf4, 0x30, 0xeb, 0xe1, 0x8d, 0x34, 0x18, 0x40,
    0x17, 0xa9, 0xa1, 0x1b, 0xf5, 0x11, 0xc8, 0xdf,
    0xf8, 0xf8, 0x34, 0x73, 0x0b, 0x96, 0xc1, 0xb7,
    0xc8, 0xdb, 0xca, 0x2f, 0xc3, 0xb6, 0x37
};

static const uint8_t RFC9529_S3_MESSAGE_2[] = {
    0x58, 0x2b, 0x41, 0x97, 0x01, 0xd7, 0xf0, 0x0a,
    0x26, 0xc2, 0xdc, 0x58, 0x7a, 0x36, 0xdd, 0x75,
    0x25, 0x49, 0xf3, 0x37, 0x63, 0xc8, 0x93, 0x42,
    0x2c, 0x8e, 0xa0, 0xf9, 0x55, 0xa1, 0x3a, 0x4f,
    0xf5, 0xd5, 0x98, 0x62, 0xa1, 0xee, 0xf9, 0xe0,
    0xe7, 0xe1, 0x88, 0x6f, 0xcd
};

static const uint8_t RFC9529_S3_MESSAGE_3[] = {
    0x52, 0xe5, 0x62, 0x09, 0x7b, 0xc4, 0x17, 0xdd,
    0x59, 0x19, 0x48, 0x5a, 0xc7, 0x89, 0x1f, 0xfd,
    0x90, 0xa9, 0xfc
};

static const uint8_t MILESTONE4_DONE_ACK[] = {
    0x4f, 0x4b, 0x5f, 0x52, 0x46, 0x43, 0x39, 0x35,
    0x32, 0x39, 0x5f, 0x54, 0x52, 0x41, 0x43, 0x45
};

bool edhoc_trace_get_message(
    edhoc_transport_msg_type_t type,
    const uint8_t **data,
    size_t *len)
{
    if (data == NULL || len == NULL) {
        return false;
    }

    switch (type) {
    case EDHOC_TRANSPORT_MSG_M1:
        *data = RFC9529_S3_MESSAGE_1;
        *len = sizeof(RFC9529_S3_MESSAGE_1);
        return true;
    case EDHOC_TRANSPORT_MSG_M2:
        *data = RFC9529_S3_MESSAGE_2;
        *len = sizeof(RFC9529_S3_MESSAGE_2);
        return true;
    case EDHOC_TRANSPORT_MSG_M3:
        *data = RFC9529_S3_MESSAGE_3;
        *len = sizeof(RFC9529_S3_MESSAGE_3);
        return true;
    case EDHOC_TRANSPORT_MSG_ACK:
        *data = MILESTONE4_DONE_ACK;
        *len = sizeof(MILESTONE4_DONE_ACK);
        return true;
    default:
        *data = NULL;
        *len = 0;
        return false;
    }
}

bool edhoc_trace_verify_message(
    edhoc_transport_msg_type_t type,
    const uint8_t *data,
    size_t len)
{
    const uint8_t *expected = NULL;
    size_t expected_len = 0;

    if (data == NULL || !edhoc_trace_get_message(type, &expected, &expected_len)) {
        return false;
    }

    if (len != expected_len) {
        ESP_LOGW(TAG,
                 "%s length mismatch: got=%u expected=%u",
                 edhoc_transport_type_str(type),
                 (unsigned)len,
                 (unsigned)expected_len);
        return false;
    }

    if (memcmp(data, expected, expected_len) != 0) {
        ESP_LOGW(TAG, "%s bytes do not match RFC 9529 trace", edhoc_transport_type_str(type));
        return false;
    }

    return true;
}

void edhoc_trace_log_message(
    const char *prefix,
    edhoc_transport_msg_type_t type,
    const uint8_t *data,
    size_t len)
{
    if (prefix == NULL) {
        prefix = "EDHOC trace";
    }

    ESP_LOGI(TAG,
             "%s: type=%s len=%u source=%s",
             prefix,
             edhoc_transport_type_str(type),
             (unsigned)len,
             EDHOC_TRACE_RFC9529_SECTION);

    if (data != NULL && len > 0) {
        ESP_LOG_BUFFER_HEX_LEVEL(TAG, data, len, ESP_LOG_INFO);
    }
}
