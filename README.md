# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally for a thesis proof of concept.

Current implementation: **Milestone 3 — fake EDHOC message transport over ESP-NOW**.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       DONE
Milestone 3: fake EDHOC transport over ESP-NOW                    CURRENT
Milestone 4: real EDHOC messages over ESP-NOW                     NEXT
Milestone 5: derive 16-byte LMK using EDHOC exporter              LATER
Milestone 6: install EDHOC-derived LMK into ESP-NOW peer table    LATER
```

## Milestone 3 goal

Prove that an EDHOC-style transport frame can carry handshake messages over ESP-NOW before adding a real EDHOC library.

This milestone sends fake EDHOC payloads:

```text
ESP32 A -- unencrypted ESP-NOW: FAKE_EDHOC_MESSAGE_1 --> ESP32 B
ESP32 A <-- unencrypted ESP-NOW: FAKE_EDHOC_MESSAGE_2 -- ESP32 B
ESP32 A -- unencrypted ESP-NOW: FAKE_EDHOC_MESSAGE_3 --> ESP32 B
ESP32 A <-- unencrypted ESP-NOW: FAKE_EDHOC_DONE      -- ESP32 B
```

No real EDHOC crypto yet. No dynamic LMK yet. The goal is only to prove framing, message type handling, sequencing, and the basic initiator/responder state machine.

Milestone 3 intentionally uses **unencrypted ESP-NOW** because real EDHOC will run before a dynamic LMK exists.

## Files

```text
main/main.c                 Fake EDHOC initiator/responder state machine
main/device_config.h        Role, Wi-Fi channel, fake session ID, peer MAC, old static LMK config
main/espnow_transport.c     Wi-Fi + ESP-NOW setup, callbacks, PMK setup, peer add, send, receive queue
main/espnow_transport.h     Raw ESP-NOW transport interface
main/edhoc_transport.c      EDHOC-style frame serialization/parsing over ESP-NOW
main/edhoc_transport.h      Fake EDHOC transport interface and message types
main/key_manager.c          Milestone 2 static PMK/LMK helper, retained for later comparison
main/key_manager.h          Key manager interface
```

## EDHOC-style transport frame

Milestone 3 wraps fake EDHOC bytes inside this frame:

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

Fragment fields are already present, but Milestone 3 only sends one fragment:

```text
frag_idx = 0
frag_count = 1
```

This prepares the project for real EDHOC messages later, where message size may require fragmentation.

## How Milestone 3 works

1. Both boards initialize Wi-Fi STA mode on the same channel.
2. Both boards initialize ESP-NOW.
3. Each board adds the other board as an **unencrypted** unicast peer.
4. Initiator waits `FAKE_EDHOC_START_DELAY_MS`.
5. Initiator sends fake `EDHOC_M1`.
6. Responder accepts M1 and sends fake `EDHOC_M2`.
7. Initiator accepts M2 and sends fake `EDHOC_M3`.
8. Responder accepts M3 and sends fake `EDHOC_ACK`.
9. Both boards print `Milestone 3 PASS`.

## How to run Milestone 3

### 1. Configure Board A

In `main/device_config.h`:

```c
#define DEVICE_IS_INITIATOR 1
#define FAKE_EDHOC_TRANSPORT_ENABLED 1
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
#define FAKE_EDHOC_TRANSPORT_ENABLED 1
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
EDHOC ESP-NOW thesis demo - Milestone 3
Role: INITIATOR
Security mode: unencrypted-fake-edhoc-transport
Fake EDHOC session ID: 0x1234
Adding peer 24:6F:28:AA:BB:CC encrypted=0
Starting fake EDHOC transport exchange
FAKE EDHOC APP TX: type=EDHOC_M1 seq=1 payload="FAKE_EDHOC_MESSAGE_1 from initiator"
EDHOC TX: dest=24:6F:28:AA:BB:CC type=EDHOC_M1 session=0x1234 seq=1 frag=0/1
EDHOC RX: from=24:6F:28:AA:BB:CC type=EDHOC_M2 session=0x1234 seq=2 frag=0/1
Fake EDHOC message_2 accepted; sending fake message_3
FAKE EDHOC APP TX: type=EDHOC_M3 seq=3 payload="FAKE_EDHOC_MESSAGE_3 from initiator"
EDHOC RX: from=24:6F:28:AA:BB:CC type=EDHOC_ACK session=0x1234 seq=4 frag=0/1
Milestone 3 PASS: fake EDHOC M1/M2/M3 transport exchange completed
```

Responder:

```text
EDHOC ESP-NOW thesis demo - Milestone 3
Role: RESPONDER
Security mode: unencrypted-fake-edhoc-transport
Fake EDHOC session ID: 0x1234
Adding peer 24:6F:28:11:22:33 encrypted=0
EDHOC RX: from=24:6F:28:11:22:33 type=EDHOC_M1 session=0x1234 seq=1 frag=0/1
Fake EDHOC message_1 accepted; sending fake message_2
FAKE EDHOC APP TX: type=EDHOC_M2 seq=2 payload="FAKE_EDHOC_MESSAGE_2 from responder"
EDHOC RX: from=24:6F:28:11:22:33 type=EDHOC_M3 session=0x1234 seq=3 frag=0/1
Fake EDHOC message_3 accepted; fake handshake complete on responder
FAKE EDHOC APP TX: type=EDHOC_ACK seq=4 payload="FAKE_EDHOC_DONE from responder"
Milestone 3 PASS: responder processed fake EDHOC M1/M2/M3
```

## Important tests

### Test 1: normal flow

Both boards should print `Milestone 3 PASS`.

### Test 2: wrong session ID

Change `FAKE_EDHOC_SESSION_ID` on only one board.

Expected result:

```text
Ignoring message for unexpected session
Milestone 3 PASS should not appear
```

Then restore the same session ID on both boards.

### Test 3: wrong role

Flash both boards as initiator or both as responder.

Expected result:

```text
No complete M1/M2/M3 exchange
Milestone 3 PASS should not appear
```

## Notes

- Both boards must use the same `ESPNOW_CHANNEL`.
- Both boards must use each other's correct STA MAC in `PEER_MAC`.
- Milestone 3 intentionally uses unencrypted ESP-NOW because EDHOC is the pairing protocol that will later create the encrypted LMK.
- Milestone 2 static PMK/LMK code remains in the repo as a control path for future comparison.
- Milestone 4 will replace fake EDHOC payload strings with real EDHOC message_1, message_2, and message_3 bytes from an EDHOC library.
