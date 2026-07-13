#include <stdlib.h>
#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lakers_ffi.h"

#define FFI_TEST_INPUT UINT32_C(0x12345678)
#define FFI_TRANSFORM_MASK UINT32_C(0xED0C0009)
#define LAKERS_INIT_TASK_STACK_SIZE 32768U
#define LAKERS_INIT_TASK_PRIORITY 5U

static const char *TAG = "lakers_ffi";

typedef struct {
    uint8_t role;
    uint8_t own_mac[6];
    uint8_t peer_mac[6];
    uint8_t channel;
    TaskHandle_t caller;
    int32_t result;
} lakers_init_job_t;

/* Provided automatically by the GNU linker when --wrap is enabled. */
extern int32_t __real_lakers_edhoc_session_init(
    uint8_t role,
    const uint8_t own_mac[6],
    const uint8_t peer_mac[6],
    uint8_t channel
);

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

const char *lakers_edhoc_status_string(int32_t status)
{
    switch (status) {
    case LAKERS_EDHOC_STATUS_OK:
        return "OK";
    case LAKERS_EDHOC_STATUS_INVALID_ARGUMENT:
        return "INVALID_ARGUMENT";
    case LAKERS_EDHOC_STATUS_WRONG_STATE:
        return "WRONG_STATE";
    case LAKERS_EDHOC_STATUS_BUFFER_TOO_SMALL:
        return "BUFFER_TOO_SMALL";
    case LAKERS_EDHOC_STATUS_PROTOCOL_ERROR:
        return "PROTOCOL_ERROR";
    case LAKERS_EDHOC_STATUS_CREDENTIAL_ERROR:
        return "CREDENTIAL_ERROR";
    default:
        return "UNKNOWN";
    }
}

static void lakers_init_task(void *context)
{
    lakers_init_job_t *job = (lakers_init_job_t *)context;

    job->result = __real_lakers_edhoc_session_init(
        job->role,
        job->own_mac,
        job->peer_mac,
        job->channel
    );

    ESP_LOGI(TAG,
             "Lakers init worker completed: status=%ld stack_high_water_mark=%u",
             (long)job->result,
             (unsigned)uxTaskGetStackHighWaterMark(NULL));

    xTaskNotifyGive(job->caller);
    vTaskDelete(NULL);
}

/*
 * The linker redirects lakers_edhoc_session_init() here. The raw Rust session
 * construction performs P-256 operations and must not run on ESP-IDF's small
 * built-in main task stack.
 */
int32_t __wrap_lakers_edhoc_session_init(
    uint8_t role,
    const uint8_t own_mac[6],
    const uint8_t peer_mac[6],
    uint8_t channel
)
{
    if (own_mac == NULL || peer_mac == NULL) {
        return LAKERS_EDHOC_STATUS_INVALID_ARGUMENT;
    }

    lakers_init_job_t job = {
        .role = role,
        .channel = channel,
        .caller = xTaskGetCurrentTaskHandle(),
        .result = LAKERS_EDHOC_STATUS_PROTOCOL_ERROR,
    };
    memcpy(job.own_mac, own_mac, sizeof(job.own_mac));
    memcpy(job.peer_mac, peer_mac, sizeof(job.peer_mac));

    const BaseType_t created = xTaskCreate(
        lakers_init_task,
        "lakers_init",
        LAKERS_INIT_TASK_STACK_SIZE,
        &job,
        LAKERS_INIT_TASK_PRIORITY,
        NULL
    );
    if (created != pdPASS) {
        ESP_LOGE(TAG, "Could not allocate dedicated Lakers initialization task");
        return LAKERS_EDHOC_STATUS_PROTOCOL_ERROR;
    }

    (void)ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    return job.result;
}

static void __attribute__((constructor)) lakers_ffi_boot_probe(void)
{
    uint32_t rust_output = 0;
    const uint32_t abi_version = lakers_ffi_abi_version();

    if (!lakers_ffi_self_test(&rust_output)) {
        ESP_EARLY_LOGE(TAG,
                       "Milestone 9 FAIL: Rust/C FFI self-test failed abi=0x%08lX output=0x%08lX",
                       (unsigned long)abi_version,
                       (unsigned long)rust_output);
        abort();
    }

    ESP_EARLY_LOGI(TAG,
                   "Milestone 9 FFI ready: ESP-IDF C called Lakers Rust staticlib abi=0x%08lX output=0x%08lX",
                   (unsigned long)abi_version,
                   (unsigned long)rust_output);
}
