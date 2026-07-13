# Lakers integration notes

Reference implementation: <https://github.com/lake-rs/lakers>

## Current state

Milestone 7 now includes a live Lakers host reference at:

```text
tools/lakers_reference
```

The reference performs a complete STAT-STAT / Cipher Suite 2 handshake, verifies matching `PRK_out`, and confirms that both completed peers export the same 16-byte ESP-NOW LMK.

This separates two questions that were previously mixed together:

```text
1. Does Lakers produce valid M1/M2/M3 and matching exporter output?   YES, host reference
2. Can the same Lakers runtime be linked into the ESP32 C firmware?  NEXT milestone
```

## Why Lakers is relevant

Lakers is a Rust implementation of EDHOC / RFC 9528. Its upstream project describes it as:

- `no_std`
- microcontroller-oriented
- no heap allocations in the core protocol
- configurable crypto backends
- providing C bindings through `lakers-c`
- supporting STAT-STAT and Cipher Suite 2

Those properties fit this thesis design, which uses constrained ESP32 nodes and pre-provisioned raw-public-key credentials.

## Live host reference API flow

The current host reference runs:

```text
Initiator:
EdhocInitiator::new
prepare_message_1
parse_message_2
credential_check_or_fetch
set_identity
verify_message_2
prepare_message_3
completed_without_message_4
edhoc_exporter

Responder:
EdhocResponder::new
process_message_1
prepare_message_2
parse_message_3
credential_check_or_fetch
verify_message_3
completed_without_message_4
edhoc_exporter
```

The program checks:

```text
initiator PRK_out == responder PRK_out
initiator exporter output == responder exporter output
exporter output length == 16 bytes
```

## Exporter definition used by this project

Current project-local settings:

```text
Exporter label: 0xF0
Exporter output length: 16 bytes
```

Exporter context:

```text
"ESP-NOW-LMK-v1"
|| initiator STA MAC
|| responder STA MAC
|| Wi-Fi channel
```

This context binds the LMK to:

```text
application purpose
ordered peer identities
radio channel
```

The ordering must be identical on both peers. The initiator MAC always comes first.

## Exporter label issue

The original roadmap considered a wider private-use exporter label such as `32768`.

The current Lakers public API accepts a `u8` label:

```rust
edhoc_exporter(label: u8, context: &[u8], result: &mut [u8])
```

The prototype therefore uses:

```text
0xF0
```

Before final standards-compliance claims, either:

```text
1. justify 0xF0 as a project-local label,
2. confirm the intended usage with Lakers maintainers, or
3. extend the Lakers API if a wider label is required.
```

## What remains unchanged from Milestone 6

The following C firmware logic should remain reusable:

```text
espnow_transport.c/.h
edhoc_transport.c/.h
key_manager_enable_derived_espnow_encryption()
peer encrypt=false -> encrypt=true transition
KEY_TEST / KEY_TEST_ACK
LMK mismatch negative test
```

Only the temporary EDHOC backend should be replaced:

```c
edhoc_trace_get_message()
edhoc_trace_verify_message()
edhoc_exporter_trace_derive_espnow_lmk()
```

## ESP32 integration reality

The application firmware is C and built by ESP-IDF. Lakers is Rust.

The upstream `lakers-c/build.sh` currently chooses:

```text
thumbv7em-none-eabihf for embedded Cortex-M4 backends
host target for rustcrypto
```

It does not currently build an Xtensa ESP32 static library directly.

Milestone 8 therefore needs an explicit ESP32 bridge rather than simply copying the existing Cortex-M archive into the project.

## Preferred Milestone 8 architecture

```text
ESP-IDF C application
        |
        | extern "C" calls
        v
small Rust staticlib / ESP-IDF Rust component
        |
        v
Lakers live session state
        |
        v
ESP32-compatible crypto backend
```

The wrapper should own the typestated Lakers objects internally. C should only see opaque session state and byte buffers.

## Target C-facing API

```c
typedef enum {
    EDHOC_LAKERS_ROLE_INITIATOR = 0,
    EDHOC_LAKERS_ROLE_RESPONDER = 1,
} edhoc_lakers_role_t;

int edhoc_lakers_session_init(
    edhoc_lakers_role_t role
);

int edhoc_lakers_make_message_1(
    uint8_t *out,
    size_t out_max,
    size_t *out_len
);

int edhoc_lakers_process_message_1(
    const uint8_t *message_1,
    size_t message_1_len,
    uint8_t *message_2,
    size_t message_2_max,
    size_t *message_2_len
);

int edhoc_lakers_process_message_2(
    const uint8_t *message_2,
    size_t message_2_len,
    uint8_t *message_3,
    size_t message_3_max,
    size_t *message_3_len
);

int edhoc_lakers_process_message_3(
    const uint8_t *message_3,
    size_t message_3_len
);

int edhoc_lakers_export_espnow_lmk(
    const uint8_t initiator_mac[6],
    const uint8_t responder_mac[6],
    uint8_t wifi_channel,
    uint8_t out_lmk[16]
);
```

## Credential model

For the first ESP32 live integration:

```text
Board A stores:
- Board A static private authentication key
- Board A CCS / raw-public-key credential
- trusted Board B credential

Board B stores:
- Board B static private authentication key
- Board B CCS / raw-public-key credential
- trusted Board A credential
```

Use the public upstream test credentials first. Replace them with project-specific generated credentials only after the transport and build bridge work.

## Crypto backend questions for Milestone 8

The bridge must resolve:

```text
1. Which Rust ESP32 target is used by the installed ESP-IDF version?
2. Can Lakers rustcrypto use an ESP32-compatible entropy source?
3. Is an Mbed TLS / ESP-IDF crypto adapter preferable for P-256, SHA-256, HKDF, and AES-CCM?
4. Does the target support every dependency required by lakers-crypto-rustcrypto?
5. How are Rust panic handlers and allocation configured?
6. How is the resulting static library linked by idf_component_register()?
```

## Milestone 8 acceptance conditions

Both physical ESP32 devices must demonstrate:

```text
1. Initiator composes fresh Lakers message_1 on-device.
2. Responder processes message_1 and composes fresh message_2 on-device.
3. Initiator authenticates message_2 and composes message_3 on-device.
4. Responder authenticates message_3 on-device.
5. Both completed sessions export the same 16-byte LMK.
6. Existing peer-table handover installs that LMK.
7. Encrypted KEY_TEST / KEY_TEST_ACK succeeds.
8. Replacing one trusted peer credential causes EDHOC authentication to fail.
9. The stored RFC trace and SHA-256 scaffold are no longer used in the active path.
```

## Useful commands for Milestone 7

```bash
cd tools/lakers_reference
cargo test
cargo run --release
cargo run --release -- <initiator-mac> <responder-mac> <channel>
```
