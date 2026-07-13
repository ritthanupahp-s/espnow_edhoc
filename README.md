# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally for a thesis proof of concept.

Current implementation: **Milestone 7 — live Lakers EDHOC reference handshake and exporter on the host**.

The ESP32 firmware remains at the completed Milestone 6 handover flow while the project establishes a verified live-EDHOC source of truth before adding the Rust/C ESP32 bridge.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       DONE
Milestone 3: fake EDHOC transport over ESP-NOW                    DONE
Milestone 4: RFC 9529 EDHOC trace messages over ESP-NOW           DONE
Milestone 5: derive 16-byte LMK-shaped output after EDHOC flow    DONE
Milestone 6: install derived LMK and test encrypted traffic       DONE
Milestone 7: live Lakers handshake and exporter on host           CURRENT
Milestone 8: build Lakers for ESP32 and connect it through FFI    NEXT
```

## Milestone 7 goal

Run genuine EDHOC cryptographic processing with the selected reference implementation:

```text
https://github.com/lake-rs/lakers
```

The host reference performs:

```text
Initiator prepare_message_1
Responder process_message_1
Responder prepare_message_2
Initiator parse_message_2
Initiator verify_message_2
Initiator prepare_message_3
Responder parse_message_3
Responder verify_message_3
Both peers complete without message_4
Both peers call edhoc_exporter
```

It proves:

```text
initiator PRK_out == responder PRK_out
initiator 16-byte LMK == responder 16-byte LMK
```

Unlike Milestones 4–6, this path does not use stored RFC trace messages or the temporary SHA-256 transcript scaffold.

## New host reference tool

```text
tools/lakers_reference/Cargo.toml
tools/lakers_reference/src/main.rs
tools/lakers_reference/README.md
```

A GitHub Actions workflow also builds and runs the reference:

```text
.github/workflows/lakers-reference.yml
```

## Run the live Lakers handshake

Install stable Rust, then run from the repository root:

```bash
cd tools/lakers_reference
cargo run --release
```

The default run uses placeholder ESP-NOW identities:

```text
Initiator MAC: 02:00:00:00:00:01
Responder MAC: 02:00:00:00:00:02
Wi-Fi channel: 1
```

Run with the actual two ESP32 STA MAC addresses and channel:

```bash
cargo run --release -- <initiator-mac> <responder-mac> <channel>
```

Example:

```bash
cargo run --release -- 24:6F:28:11:22:33 24:6F:28:AA:BB:CC 1
```

The MAC arguments must be ordered by EDHOC role:

```text
first argument  = initiator STA MAC
second argument = responder STA MAC
```

## Exporter configuration

Milestone 7 uses:

```text
Exporter label: 0xF0
Output length: 16 bytes
```

The exporter context is:

```text
"ESP-NOW-LMK-v1"
|| initiator STA MAC
|| responder STA MAC
|| Wi-Fi channel
```

The same byte ordering must be used later on both ESP32 devices.

## Expected output

The actual EDHOC messages and LMK change between runs because Lakers generates fresh ephemeral keys.

```text
LAKERS_LIVE_HANDSHAKE=PASS
authentication_method=STAT-STAT
cipher_suite=2
exporter_label=0xF0
initiator_mac=24:6F:28:11:22:33
responder_mac=24:6F:28:AA:BB:CC
wifi_channel=1
exporter_context_len=28
exporter_context_hex=<context bytes>
message_1_len=<generated length>
message_1_hex=<live EDHOC message_1>
message_2_len=<generated length>
message_2_hex=<live EDHOC message_2>
message_3_len=<generated length>
message_3_hex=<live EDHOC message_3>
espnow_lmk_len=16
espnow_lmk_hex=<live Lakers exporter result>
initiator_responder_lmk_match=true
```

Run the automated tests with:

```bash
cargo test
```

## Credentials used by the reference

The host program uses the public test credentials and static authentication keys from the upstream Lakers embedded example.

They are appropriate for interoperability and development testing only:

```text
Authentication method: STAT-STAT
Cipher Suite: 2
Credential transfer: by reference
```

Do not reuse the included private keys in a deployed system.

## Relationship to the ESP32 firmware

The ESP32 C firmware still demonstrates the complete transport and key handover mechanics:

```text
peer encrypt=false
    -> EDHOC-shaped M1/M2/M3 transport
        -> shared 16-byte value
            -> esp_now_mod_peer(encrypt=true, LMK)
                -> encrypted KEY_TEST / KEY_TEST_ACK
```

Milestone 7 now supplies the verified live Lakers logic that must replace these temporary firmware functions:

```c
edhoc_trace_get_message()
edhoc_trace_verify_message()
edhoc_exporter_trace_derive_espnow_lmk()
```

The existing Milestone 6 ESP-NOW code should remain reusable:

```text
edhoc_transport_send()
espnow_transport_receive queue
key_manager_enable_derived_espnow_encryption()
KEY_TEST / KEY_TEST_ACK
peer encrypt=false -> encrypt=true handover
```

## Why Lakers is not linked into ESP-IDF yet

The project firmware is currently C, while Lakers is Rust.

The upstream `lakers-c/build.sh` currently builds its static C library for Cortex-M4 targets or the host. It does not directly produce an Xtensa ESP32 library.

Therefore, the next milestone must create and validate one of these bridges:

```text
A. Cross-compile a Lakers staticlib for the ESP32 Rust target and call it from C
B. Add a Rust ESP-IDF component that exposes a small extern "C" API
C. Extend lakers-c with an ESP32-compatible target and crypto backend
```

The preferred direction is a small Rust static library with a narrow C API, preserving the existing ESP-IDF transport.

## Milestone 8 target API

The ESP32 C application should eventually call an interface similar to:

```c
esp_err_t edhoc_lakers_session_init(edhoc_role_t role);

esp_err_t edhoc_lakers_make_message_1(
    uint8_t *out,
    size_t out_max,
    size_t *out_len
);

esp_err_t edhoc_lakers_process_message_1(
    const uint8_t *message_1,
    size_t message_1_len,
    uint8_t *message_2,
    size_t message_2_max,
    size_t *message_2_len
);

esp_err_t edhoc_lakers_process_message_2(
    const uint8_t *message_2,
    size_t message_2_len,
    uint8_t *message_3,
    size_t message_3_max,
    size_t *message_3_len
);

esp_err_t edhoc_lakers_process_message_3(
    const uint8_t *message_3,
    size_t message_3_len
);

esp_err_t edhoc_lakers_export_espnow_lmk(
    uint8_t out_lmk[16]
);
```

## Milestone 7 acceptance checklist

```text
[ ] cargo run --release prints LAKERS_LIVE_HANDSHAKE=PASS
[ ] generated message_1 is non-empty
[ ] generated message_2 is non-empty
[ ] generated message_3 is non-empty
[ ] exported LMK length is 16 bytes
[ ] initiator_responder_lmk_match=true
[ ] cargo test passes
[ ] run once with the real board MAC addresses and Wi-Fi channel
```

After this passes, Milestone 8 is the ESP32 Rust/C FFI and cross-compilation step.
