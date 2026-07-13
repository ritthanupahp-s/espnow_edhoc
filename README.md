# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally as a thesis proof of concept.

Current implementation: **Milestone 8 — ESP32 Rust/C FFI and Xtensa cross-compilation smoke test**.

The existing ESP-NOW firmware still runs the completed Milestone 6 transport and encrypted handover flow. Milestone 8 adds a Rust static library to the same ESP-IDF application and proves that C code on the ESP32 can call Rust exports successfully.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       DONE
Milestone 3: fake EDHOC transport over ESP-NOW                    DONE
Milestone 4: RFC 9529 EDHOC trace messages over ESP-NOW           DONE
Milestone 5: derive 16-byte LMK-shaped output after EDHOC flow    DONE
Milestone 6: install derived LMK and test encrypted traffic       DONE
Milestone 7: live Lakers handshake and exporter on host           DONE
Milestone 8: ESP32 Rust/C FFI and Xtensa staticlib build          CURRENT
Milestone 9: move live Lakers session state into ESP32 firmware   NEXT
```

## Milestone 8 goal

Prove this complete build and runtime path before adding Lakers state:

```text
Rust no_std crate
    -> cargo +esp
        -> xtensa-esp32-none-elf static library
            -> ESP-IDF CMake component
                -> final ESP32 firmware
                    -> C calls Rust function on device
```

The Milestone 8 Rust crate deliberately contains only two small exported functions. This isolates toolchain, linker, static-library, symbol, and C ABI problems from the much more complex EDHOC state machine.

## New component

```text
components/lakers_ffi/
├── CMakeLists.txt
├── README.md
├── include/
│   └── lakers_ffi.h
├── lakers_ffi_component.c
└── rust/
    ├── Cargo.toml
    └── src/
        └── lib.rs
```

During `idf.py build`, the component runs:

```text
cargo +esp build --target xtensa-esp32-none-elf --release
```

The generated archive is linked into the ESP-IDF application:

```text
build/lakers-rust-target/xtensa-esp32-none-elf/release/liblakers_esp32_ffi.a
```

## Install the ESP32 Rust toolchain

The normal stable Rust toolchain used by the Milestone 7 host reference is not enough for the original Xtensa ESP32.

From PowerShell:

```powershell
cargo install espup --locked
espup install --targets esp32
```

On Windows, current `espup` versions inject the environment variables automatically. Close and reopen VS Code and PowerShell after installation.

Verify the installation:

```powershell
rustup toolchain list
rustc +esp --version
rustc +esp --print target-list | Select-String xtensa-esp32-none-elf
```

You should see the `esp` toolchain and the `xtensa-esp32-none-elf` target.

## Build and flash

Open an ESP-IDF terminal at the repository root:

```powershell
idf.py fullclean
idf.py build
idf.py -p COMx flash monitor
```

The Rust archive is built automatically as part of `idf.py build`; do not run a separate Cargo command inside the component.

## Milestone 8 pass condition

During early boot, the C component calls the Rust ABI and transform functions. The monitor must print:

```text
Milestone 8 PASS: ESP-IDF C called Xtensa Rust staticlib abi=0x00080001 output=<value>
```

If the ABI value or calculation differs, the component prints `Milestone 8 FAIL` and aborts rather than continuing with an unverified bridge.

After the FFI pass line, the existing Milestone 6 application continues with:

```text
unencrypted EDHOC-shaped exchange
    -> shared 16-byte value
        -> peer encrypt=true
            -> encrypted KEY_TEST / KEY_TEST_ACK
```

## Milestone 7 host reference

The genuine Lakers handshake and exporter reference remains under:

```text
tools/lakers_reference/
```

Run it with:

```powershell
cd tools/lakers_reference
cargo run --release -- <initiator-mac> <responder-mac> <channel>
```

Expected final output:

```text
LAKERS_LIVE_HANDSHAKE=PASS
espnow_lmk_len=16
initiator_responder_lmk_match=true
```

## Why Lakers is not inside the bridge yet

The upstream Lakers C wrapper currently assumes its own target and crypto-backend build paths. Milestone 8 first proves the project-specific ESP32 static-library pipeline using a dependency-free `no_std` crate.

Milestone 9 will place Lakers behind this already-tested C ABI and replace the temporary firmware functions:

```c
edhoc_trace_get_message();
edhoc_trace_verify_message();
edhoc_exporter_trace_derive_espnow_lmk();
```

with live operations equivalent to:

```text
prepare/process/verify message_1
prepare/process/verify message_2
prepare/process/verify message_3
completed_without_message_4
edhoc_exporter(label=0xF0, context, output_len=16)
```

## Milestone 8 acceptance checklist

```text
[ ] cargo +esp is available
[ ] xtensa-esp32-none-elf appears in the ESP toolchain target list
[ ] idf.py fullclean succeeds
[ ] idf.py build creates liblakers_esp32_ffi.a
[ ] firmware links without undefined Rust symbols
[ ] serial monitor prints Milestone 8 PASS
[ ] the previous encrypted ESP-NOW KEY_TEST flow still passes
```

Detailed setup and troubleshooting are in `components/lakers_ffi/README.md`.
