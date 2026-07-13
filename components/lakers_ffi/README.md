# Milestone 9: live Lakers EDHOC through the ESP32 Rust/C bridge

This ESP-IDF component now builds and links a real Lakers-based Rust static library for the original Xtensa ESP32.

## Build path

During `idf.py build`, CMake runs:

```text
cargo +esp build --target xtensa-esp32-none-elf --release
```

The generated archive is:

```text
build/lakers-rust-target/xtensa-esp32-none-elf/release/liblakers_esp32_ffi.a
```

The Rust crate is `no_std`, uses the Lakers RustCrypto backend, and obtains random bytes from the ESP-IDF symbol:

```c
void esp_fill_random(void *buffer, size_t length);
```

## C ABI

The firmware calls:

```c
int32_t lakers_edhoc_session_init(...);
int32_t lakers_edhoc_make_message_1(...);
int32_t lakers_edhoc_process_message_1(...);
int32_t lakers_edhoc_process_message_2(...);
int32_t lakers_edhoc_process_message_3(...);
int32_t lakers_edhoc_export_espnow_lmk(...);
```

The Rust side owns all typed Lakers states. Rust layouts and pointers are never exposed to the C application.

## Session behavior

Initiator state:

```text
Start -> WaitM2 -> Done
```

Responder state:

```text
Start -> WaitM3 -> Done
```

Exporter calls are accepted only in `Done` state.

## Error codes

```text
 0  OK
-1  INVALID_ARGUMENT
-2  WRONG_STATE
-3  BUFFER_TOO_SMALL
-4  PROTOCOL_ERROR
-5  CREDENTIAL_ERROR
```

The C helper `lakers_edhoc_status_string()` converts these values into log-friendly names.

## Windows setup

Install the Espressif Rust toolchain:

```powershell
cargo install espup --locked
espup install --targets esp32
```

Restart VS Code and verify:

```powershell
rustup toolchain list
rustc +esp --version
rustc +esp --print target-list | Select-String xtensa-esp32-none-elf
```

## Build

Open an ESP-IDF terminal in the repository root:

```powershell
idf.py fullclean
idf.py build
```

The first live-Lakers build must download the pinned Git dependencies and RustCrypto crates.

Successful build output should include:

```text
Building Milestone 9 live Lakers Rust static library for xtensa-esp32-none-elf
```

## Runtime pass condition

The early FFI probe should print:

```text
Milestone 9 FFI ready: ESP-IDF C called Lakers Rust staticlib abi=0x00090001
```

This proves the ABI and static library are linked. The full Milestone 9 pass condition is printed later by `main/main.c` only after:

```text
live M1/M2/M3 processing
real Lakers exporter output
ESP-NOW peer encrypt=true
successful encrypted KEY_TEST / KEY_TEST_ACK
```

## Important constraints

- Only the original Xtensa `esp32` target is configured.
- Calls must be serialized through one task; the Rust session cell is intentionally not a multi-threaded API.
- The current task stack is 24 KiB because Lakers and P-256 processing use substantially more stack than the earlier trace milestones.
- The included identities are public Lakers test keys and must not be deployed.
- Maximum EDHOC message capacity is 192 bytes; the ESP-NOW wire buffer is 250 bytes including transport overhead.

## Common errors

### `toolchain 'esp' is not installed`

```powershell
espup install --targets esp32
```

Restart the terminal afterward.

### `can't find crate for core`

Confirm:

```powershell
rustc +esp --print target-list | Select-String xtensa-esp32-none-elf
```

The crate-level `.cargo/config.toml` enables:

```toml
[unstable]
build-std = ["core"]
```

### Git dependency download failure

Confirm Git is available:

```powershell
git --version
```

Then retry:

```powershell
idf.py fullclean
idf.py build
```

### Task creation fails with `ESP_ERR_NO_MEM`

Reduce other memory usage or temporarily lower `EDHOC_APP_TASK_STACK_SIZE` in `main/device_config.h`. Do not lower it aggressively until measured stack high-water results are available.

### `PROTOCOL_ERROR`

This means Lakers rejected a message or credential during live EDHOC processing. Check that both boards use the matching unmodified test credential set and that packets are not truncated.
