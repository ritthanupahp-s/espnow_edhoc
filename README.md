# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally for a thesis proof of concept.

Current implementation: **Milestone 2 — static encrypted ESP-NOW unicast ping/pong using a manually configured LMK**.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       CURRENT
Milestone 3: fake EDHOC transport over ESP-NOW                    NEXT
Milestone 4: real EDHOC messages over ESP-NOW                     LATER
Milestone 5: derive 16-byte LMK using EDHOC exporter              LATER
Milestone 6: install EDHOC-derived LMK into ESP-NOW peer table    LATER
```

## Milestone 2 goal

Prove that encrypted ESP-NOW unicast works before adding EDHOC:

```text
ESP32 A -- encrypted ESP-NOW unicast PING --> ESP32 B
ESP32 A <-- encrypted ESP-NOW unicast PONG -- ESP32 B
```

No EDHOC yet. The LMK is still manually configured in firmware. This is the control case that later EDHOC-derived LMKs will be compared against.

## Files

```text
main/main.c                 Application logic: initiator sends PING, responder replies PONG
main/device_config.h        Role, Wi-Fi channel, peer MAC, static PMK, static LMK
main/espnow_transport.c     Wi-Fi + ESP-NOW setup, callbacks, PMK setup, peer add, send, receive queue
main/espnow_transport.h     ESP-NOW transport interface
main/key_manager.c          Milestone 2 static PMK/LMK setup helper
main/key_manager.h          Key manager interface
```

## How Milestone 2 works

1. Both boards initialize Wi-Fi STA mode on the same channel.
2. Both boards initialize ESP-NOW.
3. Both boards install the same static 16-byte PMK.
4. Each board adds the other board as an ESP-NOW peer with:

```text
encrypt = true
lmk = ESPNOW_STATIC_LMK
```

5. The initiator sends an encrypted PING.
6. The responder receives it and sends an encrypted PONG.
7. The initiator prints `Milestone 2 PASS`.

## How to run Milestone 2

### 1. Configure Board A

In `main/device_config.h`:

```c
#define DEVICE_IS_INITIATOR 1
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 1

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
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 1

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
EDHOC ESP-NOW thesis demo - Milestone 2
Role: INITIATOR
Security mode: static-lmk-encrypted
Installing ESP-NOW PMK len=16
Static LMK installed for peer; ESP-NOW unicast encryption is enabled
APP TX: mode=static-lmk-encrypted type=PING seq=1 payload="static-lmk encrypted hello from initiator"
TX callback: dest=24:6F:28:AA:BB:CC status=SUCCESS
APP RX: mode=static-lmk-encrypted from=24:6F:28:AA:BB:CC type=PONG seq=1 payload="static-lmk encrypted ack from responder"
Milestone 2 PASS: received PONG using static encrypted ESP-NOW for seq=1
```

Responder:

```text
EDHOC ESP-NOW thesis demo - Milestone 2
Role: RESPONDER
Security mode: static-lmk-encrypted
Installing ESP-NOW PMK len=16
Static LMK installed for peer; ESP-NOW unicast encryption is enabled
APP RX: mode=static-lmk-encrypted from=24:6F:28:11:22:33 type=PING seq=1 payload="static-lmk encrypted hello from initiator"
APP TX: mode=static-lmk-encrypted type=PONG seq=1 payload="static-lmk encrypted ack from responder"
TX callback: dest=24:6F:28:11:22:33 status=SUCCESS
```

## Important test

After Milestone 2 works, intentionally change one byte of `ESPNOW_STATIC_LMK` on only one board and flash it again.

Expected result:

```text
TX callback may show FAIL, or the peer will not receive valid packets.
Milestone 2 PASS should no longer appear.
```

Then restore the same LMK on both boards.

This proves the encrypted peer link depends on the shared LMK and gives you a baseline for the later EDHOC-derived LMK.

## Notes

- Both boards must use the same `ESPNOW_CHANNEL`.
- Both boards must use the same `ESPNOW_STATIC_PMK`.
- Both boards must use the same `ESPNOW_STATIC_LMK`.
- ESP-NOW encryption is unicast peer encryption. Do not use broadcast for this milestone.
- The receive callback copies packets into a FreeRTOS queue and the application task processes them later. This keeps heavy work out of the Wi-Fi callback.
- Milestone 3 will reuse this structure and add an EDHOC transport message format over unencrypted ESP-NOW first.
