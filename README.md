# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally for a thesis proof of concept.

Current implementation: **Milestone 1 — minimal unencrypted ESP-NOW unicast ping/pong**.

## Milestone 1 goal

Start from the ESP-IDF ESP-NOW example and reduce it to a clean 2-board baseline:

```text
ESP32 A -- unencrypted ESP-NOW unicast PING --> ESP32 B
ESP32 A <-- unencrypted ESP-NOW unicast PONG -- ESP32 B
```

No EDHOC yet. No LMK yet. No encryption yet.

## Files

```text
main/main.c                 Application logic: initiator sends PING, responder replies PONG
main/device_config.h        Role, Wi-Fi channel, and peer MAC address
main/espnow_transport.c     Wi-Fi + ESP-NOW setup, callbacks, peer add, send, receive queue
main/espnow_transport.h     Transport interface
```

## How to run Milestone 1

### 1. Build once and flash both boards to read their MAC addresses

Leave `PEER_MAC` as all zeros for the first flash.

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

Each board will print something like:

```text
Own STA MAC: 24:6F:28:11:22:33
Configured peer MAC: 00:00:00:00:00:00
PEER_MAC is not configured yet.
```

### 2. Configure Board A

In `main/device_config.h`:

```c
#define DEVICE_IS_INITIATOR 1

static const uint8_t PEER_MAC[6] = {
    /* Board B STA MAC */
    0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC
};
```

Build and flash Board A.

### 3. Configure Board B

In `main/device_config.h`:

```c
#define DEVICE_IS_INITIATOR 0

static const uint8_t PEER_MAC[6] = {
    /* Board A STA MAC */
    0x24, 0x6F, 0x28, 0x11, 0x22, 0x33
};
```

Build and flash Board B.

### 4. Expected logs

Initiator:

```text
Role: INITIATOR
APP TX: type=PING seq=1 payload="hello from initiator"
TX callback: dest=24:6F:28:AA:BB:CC status=SUCCESS
APP RX: from=24:6F:28:AA:BB:CC type=PONG seq=1 payload="hello-ack from responder"
Milestone 1 PASS: received PONG for seq=1
```

Responder:

```text
Role: RESPONDER
APP RX: from=24:6F:28:11:22:33 type=PING seq=1 payload="hello from initiator"
APP TX: type=PONG seq=1 payload="hello-ack from responder"
TX callback: dest=24:6F:28:11:22:33 status=SUCCESS
```

## Notes

- Both boards must use the same `ESPNOW_CHANNEL`.
- This milestone uses unencrypted unicast only.
- The receive callback copies packets into a FreeRTOS queue and the application task processes them later. This keeps heavy work out of the Wi-Fi callback.
- Milestone 2 will reuse this structure and add static LMK encrypted ESP-NOW.
