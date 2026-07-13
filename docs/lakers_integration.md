# Lakers integration notes

Reference implementation: <https://github.com/lake-rs/lakers>

## Why Lakers is relevant

`lakers` is a Rust implementation of EDHOC / RFC 9528. Its README describes it as:

- `no_std`
- optimized for microcontrollers
- no heap allocations
- configurable crypto backends
- with C bindings available through `lakers-c`
- currently supporting EDHOC authentication mode STAT-STAT and Cipher Suite 2

Those properties make it a good candidate for this ESP-NOW thesis project, especially because the current project already uses P-256 style raw-public-key assumptions.

## Important integration reality

This ESP-IDF project is currently written in C. Lakers is written in Rust.

There are three possible integration paths:

```text
Path A: Use Lakers C bindings as a prebuilt static library
Path B: Add a Rust component to the ESP-IDF project and call it from C
Path C: Keep the ESP-NOW transport in C but build the EDHOC handshake in a separate Rust firmware app
```

For this thesis prototype, the most practical path is:

```text
Path A first: Lakers C bindings -> C wrapper -> existing ESP-NOW transport
```

## Lakers API shape

The Rust API follows EDHOC's message order:

```text
Initiator:
prepare_message_1()
parse_message_2()
verify_message_2()
prepare_message_3()
completed_without_message_4()
edhoc_exporter()

Responder:
process_message_1()
prepare_message_2()
parse_message_3()
verify_message_3()
completed_without_message_4()
edhoc_exporter()
```

The Lakers README shows application key derivation using:

```rust
let oscore_secret = initiator.edhoc_exporter(0u8, &[], 16);
```

In the current Lakers Rust source, the completed initiator and responder states expose:

```rust
pub fn edhoc_exporter(&mut self, label: u8, context: &[u8], result: &mut [u8])
```

For ESP-NOW LMK derivation, the target call is conceptually:

```text
lakers_completed_session.edhoc_exporter(
    exporter_label_for_espnow_lmk,
    espnow_context,
    16-byte output buffer
)
```

## Exporter label note

The original project roadmap suggested a private-use EDHOC exporter label such as `32768`.

Lakers currently exposes the label as `u8`, so the immediate integration needs a decision:

```text
Option 1: use a project-local 8-bit label such as 0xF0 for the prototype
Option 2: patch Lakers / Lakers C bindings to accept a wider integer label
Option 3: confirm with the Lakers maintainers why the public API restricts labels to u8
```

The current scaffold uses:

```text
0xF0
```

This must be revisited before final thesis claims.

## Current project state after Milestone 6

The project now proves the full ESP-NOW handover mechanics:

```text
unencrypted EDHOC-style transport
    -> same 16-byte LMK-shaped output on both boards
        -> esp_now_mod_peer() with encrypt=true
            -> encrypted KEY_TEST / KEY_TEST_ACK round trip
```

The remaining missing piece is the real Lakers runtime. The current code still uses:

```text
RFC 9529 stored trace messages
trace-only SHA-256 LMK scaffold
```

It is clearly marked as not being a real EDHOC exporter result.

## Code that should remain unchanged

Milestone 6 already provides reusable ESP-NOW integration code:

```text
edhoc_transport.c/.h
key_manager_enable_derived_espnow_encryption()
KEY_TEST / KEY_TEST_ACK state transition
peer encrypt=false -> encrypt=true handover
negative LMK mismatch test
```

Real Lakers integration should replace only the current EDHOC backend and LMK source, not the ESP-NOW handover logic.

## Functions to replace

Replace the RFC trace functions:

```c
edhoc_trace_get_message()
edhoc_trace_verify_message()
edhoc_exporter_trace_derive_espnow_lmk()
```

with a Lakers-backed wrapper:

```c
typedef enum {
    EDHOC_ROLE_INITIATOR,
    EDHOC_ROLE_RESPONDER,
} edhoc_role_t;

esp_err_t edhoc_lakers_session_init(edhoc_role_t role);

esp_err_t edhoc_lakers_make_message_1(
    uint8_t *out,
    size_t out_max,
    size_t *out_len
);

esp_err_t edhoc_lakers_process_message_1(
    const uint8_t *m1,
    size_t m1_len,
    uint8_t *m2,
    size_t m2_max,
    size_t *m2_len
);

esp_err_t edhoc_lakers_process_message_2(
    const uint8_t *m2,
    size_t m2_len,
    uint8_t *m3,
    size_t m3_max,
    size_t *m3_len
);

esp_err_t edhoc_lakers_process_message_3(
    const uint8_t *m3,
    size_t m3_len
);

esp_err_t edhoc_lakers_export_espnow_lmk(
    uint8_t out_lmk[16]
);
```

`main.c` should continue passing the generated message buffers through `edhoc_transport_send()`.

## Raw public-key credential model

For the thesis prototype:

```text
Board A firmware stores:
- Board A static private authentication key
- Board A raw public-key credential
- trusted Board B raw public-key credential

Board B firmware stores:
- Board B static private authentication key
- Board B raw public-key credential
- trusted Board A raw public-key credential
```

Use credential transfer by reference where possible to keep ESP-NOW messages small.

## ESP-NOW exporter context

The exporter context should bind the key to this application and pair:

```text
"ESP-NOW-LMK-v1"
initiator STA MAC
responder STA MAC
Wi-Fi channel
EDHOC connection identifiers or session identifier
```

The output length must be exactly:

```text
16 bytes
```

because ESP-NOW LMKs are 16 bytes.

## Lakers C-binding build questions to resolve

Before linking the static library into ESP-IDF, confirm:

```text
1. Lakers C release includes an Xtensa-compatible library, or can be cross-compiled for xtensa-esp32-none-elf.
2. The chosen crypto backend can use ESP-IDF / Mbed TLS or another ESP32-compatible P-256 backend.
3. The generated C header exposes both initiator and responder operations needed by this project.
4. A C-callable completed-session exporter function is available; add one to lakers-c if it is missing.
5. Rust panic and allocator settings are compatible with no_std ESP32 firmware.
```

The existing Lakers C wrapper exposes several initiator functions, but the integration should be checked carefully because the downloadable bindings and current source may not expose every high-level Rust API directly.

## Definition of the next successful phase

The Lakers integration is successful when both devices can demonstrate:

```text
1. message_1 is freshly composed by Lakers on the initiator
2. message_1 is parsed and authenticated by Lakers on the responder
3. message_2 is freshly composed by Lakers on the responder
4. message_2 is parsed and authenticated by Lakers on the initiator
5. message_3 is freshly composed by Lakers on the initiator
6. message_3 is parsed and authenticated by Lakers on the responder
7. both completed sessions export the same 16-byte LMK
8. the existing Milestone 6 peer-table update succeeds
9. encrypted KEY_TEST / KEY_TEST_ACK succeeds
10. changing one trusted raw public key causes the EDHOC exchange to fail
```
