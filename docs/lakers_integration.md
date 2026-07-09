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

The Lakers README shows the initiator deriving application key material with:

```rust
let oscore_secret = initiator.edhoc_exporter(0u8, &[], 16);
```

In the current Lakers Rust source, the completed initiator and responder states expose:

```rust
pub fn edhoc_exporter(&mut self, label: u8, context: &[u8], result: &mut [u8])
```

So for ESP-NOW LMK derivation, the target call is conceptually:

```text
lakers_completed_session.edhoc_exporter(
    exporter_label_for_espnow_lmk,
    espnow_context,
    16-byte output buffer
)
```

## Exporter label note

The original project roadmap suggested a private-use EDHOC exporter label like `32768`.

However, Lakers currently exposes the label as `u8` in the Rust API, so the immediate integration needs a small decision:

```text
Option 1: use a project-local 8-bit label such as 0xF0 for the prototype
Option 2: patch Lakers / Lakers C bindings to accept a wider integer label
Option 3: check whether the Lakers maintainers intentionally restrict labels to u8
```

For the current scaffold, the code uses a temporary local label value:

```text
0xF0
```

This must be revisited before final thesis claims.

## Current Milestone 5 state

Milestone 5 does **not** yet link Lakers.

It adds the LMK/exporter boundary and derives a deterministic 16-byte LMK-shaped value from the RFC 9529 trace transcript. This lets both ESP32 boards prove that:

```text
same EDHOC transcript -> same 16-byte LMK candidate
```

The current derivation is clearly marked in the code as:

```text
TRACE-ONLY LMK derivation
NOT a real EDHOC exporter result
```

## Next code step

Replace this function:

```c
edhoc_exporter_trace_derive_espnow_lmk()
```

with a real Lakers-backed function such as:

```c
edhoc_lakers_export_espnow_lmk()
```

The real function should only run after the Lakers session reaches the completed state.

## Target C wrapper design

Create a wrapper that hides Lakers details from the ESP-NOW transport:

```c
typedef enum {
    EDHOC_ROLE_INITIATOR,
    EDHOC_ROLE_RESPONDER,
} edhoc_role_t;

esp_err_t edhoc_lakers_session_init(edhoc_role_t role);
esp_err_t edhoc_lakers_make_message_1(uint8_t *out, size_t out_max, size_t *out_len);
esp_err_t edhoc_lakers_process_message_1(const uint8_t *m1, size_t m1_len,
                                          uint8_t *m2, size_t m2_max, size_t *m2_len);
esp_err_t edhoc_lakers_process_message_2(const uint8_t *m2, size_t m2_len,
                                          uint8_t *m3, size_t m3_max, size_t *m3_len);
esp_err_t edhoc_lakers_process_message_3(const uint8_t *m3, size_t m3_len);
esp_err_t edhoc_lakers_export_espnow_lmk(uint8_t out_lmk[16]);
```

Then `main.c` should not care whether the backend is:

```text
RFC trace vectors
Lakers C static library
native Rust component
another EDHOC implementation
```

## ESP-NOW context for LMK export

The exporter context should bind the derived key to this application:

```text
"ESP-NOW-LMK-v1"
initiator STA MAC
responder STA MAC
Wi-Fi channel
selected EDHOC session / connection ID if available
```

The output length must be:

```text
16 bytes
```

because ESP-NOW LMKs are 16 bytes.

## After real Lakers export works

The next milestone is:

```text
1. call Lakers edhoc_exporter(..., 16)
2. install the exported bytes as ESP-NOW LMK
3. update peer encrypt=true
4. send encrypted KEY_TEST
5. compare against static LMK baseline
```
