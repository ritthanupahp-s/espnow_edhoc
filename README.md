# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally for a thesis proof of concept.

Current implementation: **Milestone 5 — EDHOC trace transport plus 16-byte ESP-NOW LMK derivation scaffold**.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       DONE
Milestone 3: fake EDHOC transport over ESP-NOW                    DONE
Milestone 4: real EDHOC trace messages over ESP-NOW               DONE
Milestone 5: derive 16-byte LMK-shaped output after EDHOC flow    CURRENT
Milestone 6: install EDHOC-derived LMK into ESP-NOW peer table    NEXT
```

## Milestone 5 goal

Prepare the project for the EDHOC exporter step.

After the RFC 9529 EDHOC message exchange completes, both boards now derive the same 16-byte ESP-NOW LMK-shaped value and print a safe hash summary.

Important limitation:

```text
This milestone does not yet call the real Lakers edhoc_exporter().
The current derivation is trace-only and must not be treated as secure key material.
```

The purpose is to create and test the C boundary where the future Lakers exporter result will be handed to ESP-NOW.

## Lakers reference

The intended EDHOC implementation is:

```text
https://github.com/lake-rs/lakers
```

Lakers is a Rust EDHOC implementation. Its README says it is `no_std`, optimized for microcontrollers, avoids heap allocations, has configurable crypto backends, provides C bindings, and currently supports STAT-STAT with Cipher Suite 2.

See:

```text
docs/lakers_integration.md
```

for the integration plan.

## Current Milestone 5 flow

```text
ESP32 A -- unencrypted ESP-NOW: RFC9529 message_1 --> ESP32 B
ESP32 A <-- unencrypted ESP-NOW: RFC9529 message_2 -- ESP32 B
ESP32 A -- unencrypted ESP-NOW: RFC9529 message_3 --> ESP32 B
ESP32 A <-- unencrypted ESP-NOW: small demo ACK     -- ESP32 B

Both boards:
RFC9529 transcript -> trace-only LMK scaffold -> 16-byte LMK candidate
```

Both boards should print the same `LMK summary` prefix.

## Files

```text
main/main.c                    RFC 9529 trace state machine + LMK scaffold call
main/device_config.h           Role, Wi-Fi channel, EDHOC trace session ID, peer MAC
main/espnow_transport.c        Wi-Fi + ESP-NOW setup, callbacks, PMK setup, peer add, send, receive queue
main/espnow_transport.h        Raw ESP-NOW transport interface
main/edhoc_transport.c         EDHOC-style frame serialization/parsing over ESP-NOW
main/edhoc_transport.h         EDHOC transport interface and message types
main/edhoc_trace_vectors.c     RFC 9529 message_1/message_2/message_3 byte strings and verification
main/edhoc_trace_vectors.h     RFC 9529 trace vector interface
main/edhoc_exporter.c          Temporary trace-only LMK derivation scaffold
main/edhoc_exporter.h          EDHOC exporter / ESP-NOW LMK interface
main/key_manager.c             Milestone 2 static PMK/LMK helper, retained for later comparison
docs/lakers_integration.md     Lakers integration plan and API mapping
```

## Why this milestone exists

The final thesis goal requires this sequence:

```text
EDHOC complete
    -> EDHOC exporter
        -> 16-byte ESP-NOW LMK
            -> install LMK in ESP-NOW peer table
                -> encrypted post-handshake ESP-NOW unicast
```

Milestone 5 creates the boundary for:

```c
esp_err_t edhoc_exporter_trace_derive_espnow_lmk(uint8_t out_lmk[16]);
```

Later, this function should be replaced by a Lakers-backed function such as:

```c
esp_err_t edhoc_lakers_export_espnow_lmk(uint8_t out_lmk[16]);
```

## How to run Milestone 5

### 1. Configure Board A

In `main/device_config.h`:

```c
#define DEVICE_IS_INITIATOR 1
#define EDHOC_TRACE_TRANSPORT_ENABLED 1
#define FAKE_EDHOC_TRANSPORT_ENABLED 0
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 0

static const uint8_t PEER_MAC[6] = {
    /* Board B STA MAC */
    0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC
};
```

Build and flash Board A.

### 2. Configure Board B

In `main/device_config.h`:

```c
#define DEVICE_IS_INITIATOR 0
#define EDHOC_TRACE_TRANSPORT_ENABLED 1
#define FAKE_EDHOC_TRANSPORT_ENABLED 0
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 0

static const uint8_t PEER_MAC[6] = {
    /* Board A STA MAC */
    0x24, 0x6F, 0x28, 0x11, 0x22, 0x33
};
```

Build and flash Board B.

### 3. Build and flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

## Expected logs

Initiator:

```text
EDHOC ESP-NOW thesis demo - Milestone 5
Role: INITIATOR
Security mode: unencrypted-rfc9529-edhoc-trace-plus-lmk-scaffold
Starting RFC 9529 EDHOC trace transport exchange
EDHOC TRACE APP TX: type=EDHOC_M1 len=39 source=RFC9529 Section 3 Static DH CCS/kid
EDHOC TRACE APP RX verified: type=EDHOC_M2 len=45 source=RFC9529 Section 3 Static DH CCS/kid
RFC 9529 EDHOC message_2 accepted; sending message_3 trace bytes
EDHOC TRACE APP TX: type=EDHOC_M3 len=19 source=RFC9529 Section 3 Static DH CCS/kid
TRACE-ONLY LMK derivation active
Derived trace-only ESP-NOW LMK candidate len=16 exporter_label=0xF0 context=ESP-NOW-LMK-v1 session=0x1234
LMK summary: len=16 sha256_prefix=AA:BB:CC:DD
Milestone 5 PASS: 16-byte ESP-NOW LMK candidate derived after EDHOC trace transport
```

Responder:

```text
EDHOC ESP-NOW thesis demo - Milestone 5
Role: RESPONDER
Security mode: unencrypted-rfc9529-edhoc-trace-plus-lmk-scaffold
EDHOC TRACE APP RX verified: type=EDHOC_M1 len=39 source=RFC9529 Section 3 Static DH CCS/kid
RFC 9529 EDHOC message_1 accepted; sending message_2 trace bytes
EDHOC TRACE APP TX: type=EDHOC_M2 len=45 source=RFC9529 Section 3 Static DH CCS/kid
EDHOC TRACE APP RX verified: type=EDHOC_M3 len=19 source=RFC9529 Section 3 Static DH CCS/kid
TRACE-ONLY LMK derivation active
Derived trace-only ESP-NOW LMK candidate len=16 exporter_label=0xF0 context=ESP-NOW-LMK-v1 session=0x1234
LMK summary: len=16 sha256_prefix=AA:BB:CC:DD
Milestone 5 PASS: responder derived matching 16-byte ESP-NOW LMK candidate
```

The exact `sha256_prefix` value is not important, but it should match on both boards.

## Important tests

### Test 1: normal flow

Both boards should print `Milestone 5 PASS` and the same LMK summary prefix.

### Test 2: wrong session ID

Change `EDHOC_TRACE_SESSION_ID` on only one board.

Expected result:

```text
Ignoring message for unexpected session
Milestone 5 PASS should not appear
```

### Test 3: corrupted EDHOC byte

Change one byte in one RFC 9529 message array in `main/edhoc_trace_vectors.c` on only one board.

Expected result:

```text
bytes do not match RFC 9529 trace
Pairing state -> FAILED
Milestone 5 PASS should not appear
```

## Next milestone

Milestone 6 should install the 16-byte value into the ESP-NOW peer table and switch to encrypted post-handshake ESP-NOW traffic.

However, before final thesis claims, replace the trace-only derivation with the real Lakers exporter:

```text
Lakers completed EDHOC session
    -> edhoc_exporter(label, context, 16-byte output)
    -> ESP-NOW LMK
```
