#ifndef LAKERS_FFI_H
#define LAKERS_FFI_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAKERS_FFI_ABI_VERSION_EXPECTED UINT32_C(0x00080001)

/* Implemented by the Rust static library. */
uint32_t lakers_ffi_abi_version(void);
uint32_t lakers_ffi_transform(uint32_t input);

/* Implemented by the ESP-IDF C component; calls both Rust exports. */
bool lakers_ffi_self_test(uint32_t *rust_output);

#ifdef __cplusplus
}
#endif

#endif
