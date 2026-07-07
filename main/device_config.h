#pragma once

#include <stdint.h>

/*
 * Milestone 2 configuration
 * -------------------------
 * Flash one board with DEVICE_IS_INITIATOR = 1.
 * Flash the other board with DEVICE_IS_INITIATOR = 0.
 *
 * PEER_MAC must be the other ESP32 board's printed STA MAC address.
 *
 * This milestone enables ESP-NOW unicast encryption using a manually configured
 * static PMK + static LMK. Later, Milestone 5/6 will replace STATIC_LMK with an
 * EDHOC exporter-derived LMK.
 */
#define DEVICE_IS_INITIATOR 1

/* Both ESP32 boards must use the same Wi-Fi channel. */
#define ESPNOW_CHANNEL 1

/* Initiator sends one ping every 2 seconds. */
#define PING_INTERVAL_MS 2000

/* Keep payloads small for the first prototype. ESP-NOW v1 safe limit is 250 bytes. */
#define APP_PAYLOAD_MAX_LEN 64

/* Set to 1 for Milestone 2. Set to 0 only if you want to re-run Milestone 1. */
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 1

/*
 * ESP-NOW PMK and LMK are both 16 bytes.
 * These are lab/demo keys only. Do not use these values in a real deployment.
 *
 * PMK: Primary Master Key, used by ESP-NOW to protect LMKs internally.
 * LMK: Local Master Key, installed per peer for encrypted unicast frames.
 */
static const uint8_t ESPNOW_STATIC_PMK[16] = {
    0x10, 0x11, 0x12, 0x13,
    0x14, 0x15, 0x16, 0x17,
    0x18, 0x19, 0x1A, 0x1B,
    0x1C, 0x1D, 0x1E, 0x1F
};

static const uint8_t ESPNOW_STATIC_LMK[16] = {
    0x20, 0x21, 0x22, 0x23,
    0x24, 0x25, 0x26, 0x27,
    0x28, 0x29, 0x2A, 0x2B,
    0x2C, 0x2D, 0x2E, 0x2F
};

/* Replace this with the other ESP32 board's printed STA MAC address. */
static const uint8_t PEER_MAC[6] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00
};
