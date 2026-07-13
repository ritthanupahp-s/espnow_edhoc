#include "edhoc_exporter.h"

#include <string.h>

#include "esp_log.h"
#include "mbedtls/md.h"

static const char *TAG = "edhoc_exporter";

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
