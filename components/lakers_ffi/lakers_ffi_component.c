#include <stdlib.h>

#include "esp_log.h"
#include "lakers_ffi.h"

#define FFI_TEST_INPUT UINT32_C(0x12345678)
#define FFI_TRANSFORM_MASK UINT32_C(0xED0C0008)

static const char *TAG = "lakers_ffi";

static uint32_t rotate_left_32(uint32_t value, unsigned shift)
{
    return (value << shift) | (value >> (32U - shift));
}

bool lakers_ffi_self_test(uint32_t *rust_output)
{
    const uint32_t output = lakers_ffi_transform(FFI_TEST_INPUT);
    const uint32_t expected = rotate_left_32(FFI_TEST_INPUT, 7U) ^ FFI_TRANSFORM_MASK;

    if (rust_output != NULL) {
        *rust_output = output;
    }

    return lakers_ffi_abi_version() == LAKERS_FFI_ABI_VERSION_EXPECTED && output == expected;
}

static void __attribute__((constructor)) lakers_ffi_boot_probe(void)
{
    uint32_t rust_output = 0;
    const uint32_t abi_version = lakers_ffi_abi_version();

    if (!lakers_ffi_self_test(&rust_output)) {
        ESP_EARLY_LOGE(TAG,
                       "Milestone 8 FAIL: Rust/C FFI self-test failed abi=0x%08lX output=0x%08lX",
                       (unsigned long)abi_version,
                       (unsigned long)rust_output);
        abort();
    }

    ESP_EARLY_LOGI(TAG,
                   "Milestone 8 PASS: ESP-IDF C called Xtensa Rust staticlib abi=0x%08lX output=0x%08lX",
                   (unsigned long)abi_version,
                   (unsigned long)rust_output);
}
