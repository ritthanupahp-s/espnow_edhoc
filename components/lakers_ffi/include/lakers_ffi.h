#ifndef LAKERS_FFI_H
#define LAKERS_FFI_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAKERS_FFI_ABI_VERSION_EXPECTED UINT32_C(0x00090001)
#define LAKERS_EDHOC_MAX_MESSAGE_LEN 192U
#define LAKERS_EDHOC_LMK_LEN 16U

#define LAKERS_EDHOC_STATUS_OK 0
#define LAKERS_EDHOC_STATUS_INVALID_ARGUMENT (-1)
#define LAKERS_EDHOC_STATUS_WRONG_STATE (-2)
#define LAKERS_EDHOC_STATUS_BUFFER_TOO_SMALL (-3)
#define LAKERS_EDHOC_STATUS_PROTOCOL_ERROR (-4)
#define LAKERS_EDHOC_STATUS_CREDENTIAL_ERROR (-5)

typedef enum {
    LAKERS_EDHOC_ROLE_INITIATOR = 1,
    LAKERS_EDHOC_ROLE_RESPONDER = 2,
} lakers_edhoc_role_t;

/* Smoke-test exports retained from Milestone 8. */
uint32_t lakers_ffi_abi_version(void);
uint32_t lakers_ffi_transform(uint32_t input);
bool lakers_ffi_self_test(uint32_t *rust_output);

/* Live Lakers state machine implemented by the Rust static library. */
int32_t lakers_edhoc_session_init(
    uint8_t role,
    const uint8_t own_mac[6],
    const uint8_t peer_mac[6],
    uint8_t channel
);

int32_t lakers_edhoc_make_message_1(
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length
);

int32_t lakers_edhoc_process_message_1(
    const uint8_t *message_1,
    size_t message_1_length,
    uint8_t *message_2,
    size_t message_2_capacity,
    size_t *message_2_length
);

int32_t lakers_edhoc_process_message_2(
    const uint8_t *message_2,
    size_t message_2_length,
    uint8_t *message_3,
    size_t message_3_capacity,
    size_t *message_3_length
);

int32_t lakers_edhoc_process_message_3(
    const uint8_t *message_3,
    size_t message_3_length
);

int32_t lakers_edhoc_export_espnow_lmk(
    uint8_t output_lmk[LAKERS_EDHOC_LMK_LEN]
);

const char *lakers_edhoc_status_string(int32_t status);

#ifdef __cplusplus
}
#endif

#endif
