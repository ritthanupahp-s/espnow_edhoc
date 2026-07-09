#include "edhoc_exporter.h"

#include <string.h>

#include "device_config.h"
#include "edhoc_trace_vectors.h"
#include "edhoc_transport.h"
#include "esp_log.h"
#include "mbedtls/md.h"

static const char *TAG = "edhoc_exporter";

#define TRACE_ONLY_EXPORTER_LABEL 0xF0

static esp_err_t sha256_transcript_digest(uint8_t digest[32])
{
    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL) {
        return ESP_FAIL;
    }

    mbedtls_md_context_t ctx;
    mbedtls_md_init(&ctx);

    int ret = mbedtls_md_setup(&ctx, md_info, 0);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }

    ret = mbedtls_md_starts(&ctx);
    if (ret != 0) {
        mbedtls_md_free(&ctx);
        return ESP_FAIL;
    }

    const uint8_t purpose[] = "ESP-NOW-LMK-v1";
    const uint8_t backend[] = "trace-only-rfc9529";
    const uint8_t label = TRACE_ONLY_EXPORTER_LABEL;
    const uint8_t session_id_bytes[2] = {
        (uint8_t)((EDHOC_TRACE_SESSION_ID >> 8) & 0xFF),
        (uint8_t)(EDHOC_TRACE_SESSION_ID & 0xFF),
    };

    ret |= mbedtls_md_update(&ctx, purpose, sizeof(purpose) - 1);
    ret |= mbedtls_md_update(&ctx, backend, sizeof(backend) - 1);
    ret |= mbedtls_md_update(&ctx, &label, sizeof(label));
    ret |= mbedtls_md_update(&ctx, session_id_bytes, sizeof(session_id_bytes));

    const edhoc_transport_msg_type_t types[] = {
        EDHOC_TRANSPORT_MSG_M1,
        EDHOC_TRANSPORT_MSG_M2,
        EDHOC_TRANSPORT_MSG_M3,
    };

    for (size_t i = 0; i < sizeof(types) / sizeof(types[0]); ++i) {
        const uint8_t *msg = NULL;
        size_t msg_len = 0;
        if (!edhoc_trace_get_message(types[i], &msg, &msg_len)) {
            mbedtls_md_free(&ctx);
            return ESP_FAIL;
        }

        const uint8_t type_byte = (uint8_t)types[i];
        const uint8_t len_bytes[2] = {
            (uint8_t)((msg_len >> 8) & 0xFF),
            (uint8_t)(msg_len & 0xFF),
        };

        ret |= mbedtls_md_update(&ctx, &type_byte, sizeof(type_byte));
        ret |= mbedtls_md_update(&ctx, len_bytes, sizeof(len_bytes));
        ret |= mbedtls_md_update(&ctx, msg, msg_len);
    }

    ret |= mbedtls_md_finish(&ctx, digest);
    mbedtls_md_free(&ctx);

    return ret == 0 ? ESP_OK : ESP_FAIL;
}

esp_err_t edhoc_exporter_trace_derive_espnow_lmk(uint8_t out_lmk[ESPNOW_LMK_LEN])
{
    if (out_lmk == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    ESP_LOGW(TAG, "TRACE-ONLY LMK derivation active");
    ESP_LOGW(TAG, "This is NOT a real EDHOC exporter result; replace with Lakers edhoc_exporter() after live EDHOC integration");

    uint8_t digest[32] = {0};
    esp_err_t err = sha256_transcript_digest(digest);
    if (err != ESP_OK) {
        memset(digest, 0, sizeof(digest));
        return err;
    }

    memcpy(out_lmk, digest, ESPNOW_LMK_LEN);
    memset(digest, 0, sizeof(digest));

    ESP_LOGI(TAG,
             "Derived trace-only ESP-NOW LMK candidate len=%u exporter_label=0x%02X context=ESP-NOW-LMK-v1 session=0x%04X",
             (unsigned)ESPNOW_LMK_LEN,
             (unsigned)TRACE_ONLY_EXPORTER_LABEL,
             (unsigned)EDHOC_TRACE_SESSION_ID);

    return ESP_OK;
}

void edhoc_exporter_log_lmk_summary(const uint8_t lmk[ESPNOW_LMK_LEN])
{
    if (lmk == NULL) {
        ESP_LOGW(TAG, "Cannot summarize NULL LMK");
        return;
    }

    uint8_t digest[32] = {0};

    const mbedtls_md_info_t *md_info = mbedtls_md_info_from_type(MBEDTLS_MD_SHA256);
    if (md_info == NULL || mbedtls_md(md_info, lmk, ESPNOW_LMK_LEN, digest) != 0) {
        ESP_LOGW(TAG, "Could not hash LMK summary");
        return;
    }

    ESP_LOGI(TAG,
             "LMK summary: len=%u sha256_prefix=%02X:%02X:%02X:%02X",
             (unsigned)ESPNOW_LMK_LEN,
             digest[0], digest[1], digest[2], digest[3]);

    memset(digest, 0, sizeof(digest));
}
