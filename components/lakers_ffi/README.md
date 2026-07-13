# Milestone 8: ESP32 Rust/C FFI bridge

This ESP-IDF component proves that the C firmware can build, link, and call a Rust static library compiled for the original Xtensa ESP32.

It intentionally does not contain Lakers session state yet. The goal is to isolate the cross-language build and ABI risk before moving the live EDHOC state machine onto the board.

## What the component does

During `idf.py build`, CMake runs:

```text
cargo +esp build --target xtensa-esp32-none-elf --release
```

The generated library is linked into the ESP-IDF application:

```text
build/lakers-rust-target/xtensa-esp32-none-elf/release/liblakers_esp32_ffi.a
```

At boot, the C component calls two Rust exports:

```c
uint32_t lakers_ffi_abi_version(void);
uint32_t lakers_ffi_transform(uint32_t input);
```

The firmware prints `Milestone 8 PASS` only if the ABI version and transformed value match the C-side expectations.

## Windows setup

Install the Espressif Rust toolchain from a normal PowerShell or Developer PowerShell:

```powershell
cargo install espup --locked
espup install --targets esp32
```

On Windows, current `espup` versions add the environment variables automatically. Close and reopen VS Code and PowerShell after installation.

Verify the toolchain:

```powershell
rustup toolchain list
rustc +esp --version
rustc +esp --print target-list | Select-String xtensa-esp32-none-elf
```

Then open an ESP-IDF terminal in the repository root:

```powershell
idf.py fullclean
idf.py build
idf.py -p COMx flash monitor
```

## Pass condition

The serial monitor must contain:

```text
Milestone 8 PASS: ESP-IDF C called Xtensa Rust staticlib abi=0x00080001 output=<value>
```

The existing Milestone 6 ESP-NOW handshake will then continue normally.

## Common errors

### `toolchain 'esp' is not installed`

Run:

```powershell
espup install --targets esp32
```

Then restart the terminal.

### `can't find crate for core`

Confirm the target is installed:

```powershell
rustc +esp --print target-list | Select-String xtensa-esp32-none-elf
```

Re-run `espup install --targets esp32` if it is missing.

### CMake keeps using an old failed Rust build

Run:

```powershell
idf.py fullclean
Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
idf.py build
```

## Next milestone

Replace the two smoke-test exports with a narrow stateful Lakers API for:

```text
session initialization
message_1 compose/process
message_2 compose/process/verify
message_3 compose/process/verify
completed_without_message_4
16-byte edhoc_exporter output
```
