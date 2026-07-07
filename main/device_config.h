#pragma once

#include <stdint.h>

/*
 * Milestone 1 configuration
 * -------------------------
 * Flash one board with DEVICE_IS_INITIATOR = 1.
 * Flash the other board with DEVICE_IS_INITIATOR = 0.
 *
 * First flash each board once with the default PEER_MAC below.
 * Open the serial monitor and copy each board's printed STA MAC address.
 * Then paste Board B's MAC into Board A's PEER_MAC, and Board A's MAC into Board B's PEER_MAC.
 */
#define DEVICE_IS_INITIATOR 1

/* Both ESP32 boards must use the same Wi-Fi channel. */
#define ESPNOW_CHANNEL 1

/* Initiator sends one ping every 2 seconds. */
#define PING_INTERVAL_MS 2000

/* Keep payloads small for the first prototype. ESP-NOW v1 safe limit is 250 bytes. */
#define APP_PAYLOAD_MAX_LEN 64

/* Replace this with the other ESP32 board's printed STA MAC address. */
static const uint8_t PEER_MAC[6] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
