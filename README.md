# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally for a thesis proof of concept.

Current implementation: **Milestone 4 — RFC 9529 EDHOC message bytes over ESP-NOW**.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       DONE
Milestone 3: fake EDHOC transport over ESP-NOW                    DONE
Milestone 4: real EDHOC trace messages over ESP-NOW               CURRENT
Milestone 5: derive 16-byte LMK using EDHOC exporter              NEXT
Milestone 6: install EDHOC-derived LMK into ESP-NOW peer table    LATER
```

## Milestone 4 goal

Prove that the ESP-NOW EDHOC transport can carry **actual EDHOC byte strings**, not just fake text payloads.

This milestone uses the official EDHOC trace from **RFC 9529 Section 3: Static DH, CCS identified by `kid`**. The trace provides compact EDHOC messages with these sizes:

```text
message_1 = 39 bytes
message_2 = 45 bytes
message_3 = 19 bytes
```

The ESP-NOW exchange is:

```text
ESP32 A -- unencrypted ESP-NOW: RFC9529 message_1 --> ESP32 B
ESP32 A <-- unencrypted ESP-NOW: RFC9529 message_2 -- ESP32 B
ESP32 A -- unencrypted ESP-NOW: RFC9529 message_3 --> ESP32 B
ESP32 A <-- unencrypted ESP-NOW: small demo ACK     -- ESP32 B
```

Important: this milestone transports and verifies real EDHOC trace bytes, but it does **not** yet run live EDHOC cryptographic compose/process functions. The next integration step is to replace the RFC trace vector module with a real EDHOC library such as `libedhoc`.

## Files

```text
main/main.c                    RFC 9529 EDHOC trace initiator/responder state machine
main/device_config.h           Role, Wi-Fi channel, EDHOC trace session ID, peer MAC
main/espnow_transport.c        Wi-Fi + ESP-NOW setup, callbacks, PMK setup, peer add, send, receive queue
main/espnow_transport.h        Raw ESP-NOW transport interface
main/edhoc_transport.c         EDHOC-style frame serialization/parsing over ESP-NOW
main/edhoc_transport.h         EDHOC transport interface and message types
main/edhoc_trace_vectors.c     RFC 9529 message_1/message_2/message_3 byte strings and verification
main/edhoc_trace_vectors.h     RFC 9529 trace vector interface
main/key_manager.c             Milestone 2 static PMK/LMK helper, retained for later comparison
main/key_manager.h             Key manager interface
```

## EDHOC-style transport frame

Milestone 4 still uses the transport frame created in Milestone 3:

```text
magic | version | type | flags | session_id | seq | frag_idx | frag_count | payload_len | payload
```

Current message types:

```text
0x10 = EDHOC_M1
0x11 = EDHOC_M2
0x12 = EDHOC_M3
0x13 = EDHOC_ACK
```

Fragment fields are already present, but Milestone 4 only sends one fragment:

```text
frag_idx = 0
frag_count = 1
```

## How Milestone 4 works

1. Both boards initialize Wi-Fi STA mode on the same channel.
2. Both boards initialize ESP-NOW.
3. Each board adds the other board as an **unencrypted** unicast peer.
4. Initiator waits `EDHOC_TRACE_START_DELAY_MS`.
5. Initiator sends RFC 9529 EDHOC `message_1` bytes.
6. Responder verifies `message_1` byte-for-byte against the stored RFC 9529 vector.
7. Responder sends RFC 9529 EDHOC `message_2` bytes.
8. Initiator verifies `message_2` byte-for-byte.
9. Initiator sends RFC 9529 EDHOC `message_3` bytes.
10. Responder verifies `message_3` byte-for-byte.
11. Responder sends a small demo ACK.
12. Initiator verifies the ACK and prints `Milestone 4 PASS`.

## How to run Milestone 4

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
EDHOC ESP-NOW thesis demo - Milestone 4
Role: INITIATOR
Security mode: unencrypted-rfc9529-edhoc-trace
EDHOC trace source: RFC9529 Section 3 Static DH CCS/kid
EDHOC transport session ID: 0x1234
Adding peer 24:6F:28:AA:BB:CC encrypted=0
Starting RFC 9529 EDHOC trace transport exchange
EDHOC TRACE APP TX: type=EDHOC_M1 len=39 source=RFC9529 Section 3 Static DH CCS/kid
EDHOC RX: from=24:6F:28:AA:BB:CC type=EDHOC_M2 session=0x1234 seq=2 frag=0/1 payload_len=45
EDHOC TRACE APP RX verified: type=EDHOC_M2 len=45 source=RFC9529 Section 3 Static DH CCS/kid
RFC 9529 EDHOC message_2 accepted; sending message_3 trace bytes
EDHOC TRACE APP TX: type=EDHOC_M3 len=19 source=RFC9529 Section 3 Static DH CCS/kid
EDHOC RX: from=24:6F:28:AA:BB:CC type=EDHOC_ACK session=0x1234 seq=4 frag=0/1 payload_len=16
Milestone 4 PASS: RFC 9529 EDHOC message_1/message_2/message_3 bytes transported over ESP-NOW
```

Responder:

```text
EDHOC ESP-NOW thesis demo - Milestone 4
Role: RESPONDER
Security mode: unencrypted-rfc9529-edhoc-trace
EDHOC trace source: RFC9529 Section 3 Static DH CCS/kid
EDHOC transport session ID: 0x1234
Adding peer 24:6F:28:11:22:33 encrypted=0
EDHOC RX: from=24:6F:28:11:22:33 type=EDHOC_M1 session=0x1234 seq=1 frag=0/1 payload_len=39
EDHOC TRACE APP RX verified: type=EDHOC_M1 len=39 source=RFC9529 Section 3 Static DH CCS/kid
RFC 9529 EDHOC message_1 accepted; sending message_2 trace bytes
EDHOC TRACE APP TX: type=EDHOC_M2 len=45 source=RFC9529 Section 3 Static DH CCS/kid
EDHOC RX: from=24:6F:28:11:22:33 type=EDHOC_M3 session=0x1234 seq=3 frag=0/1 payload_len=19
EDHOC TRACE APP RX verified: type=EDHOC_M3 len=19 source=RFC9529 Section 3 Static DH CCS/kid
Milestone 4 PASS: responder processed RFC 9529 EDHOC message_1/message_2/message_3 bytes
```

## Important tests

### Test 1: normal flow

Both boards should print `Milestone 4 PASS`.

### Test 2: wrong session ID

Change `EDHOC_TRACE_SESSION_ID` on only one board.

Expected result:

```text
Ignoring message for unexpected session
Milestone 4 PASS should not appear
```

Then restore the same session ID on both boards.

### Test 3: corrupted EDHOC byte

Change one byte in one RFC 9529 message array in `main/edhoc_trace_vectors.c` on only one board.

Expected result:

```text
bytes do not match RFC 9529 trace
Pairing state -> FAILED
Milestone 4 PASS should not appear
```

Then restore the original byte.

## Notes

- Both boards must use the same `ESPNOW_CHANNEL`.
- Both boards must use each other's correct STA MAC in `PEER_MAC`.
- Milestone 4 intentionally uses unencrypted ESP-NOW because EDHOC is the pairing protocol that will later create the encrypted LMK.
- Milestone 2 static PMK/LMK code remains in the repo as a control path for future comparison.
- The RFC 9529 trace proves binary EDHOC messages fit inside the current ESP-NOW frame design.
- The next step is live `libedhoc` integration: call `edhoc_message_1_compose`, process `message_2`, compose `message_3`, then use the EDHOC exporter for a 16-byte ESP-NOW LMK.
