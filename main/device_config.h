#pragma once

#include <stdint.h>

/*
 * Milestone 6 configuration
 * -------------------------
 * Flash one board with DEVICE_IS_INITIATOR = 1.
 * Flash the other board with DEVICE_IS_INITIATOR = 0.
 *
 * PEER_MAC must be the other ESP32 board's printed STA MAC address.
 *
 * Milestone 6 transports RFC 9529 EDHOC trace messages while the ESP-NOW peer
 * is unencrypted, derives the same 16-byte LMK candidate on both boards,
 * updates the existing peer to encrypt=true, and verifies the switch using an
 * encrypted KEY_TEST / KEY_TEST_ACK exchange.
 *
 * Important: the current LMK source remains the trace-only scaffold from
 * Milestone 5. Replace it with Lakers' completed-session edhoc_exporter()
 * before treating this as real EDHOC-derived security.
 */
#define DEVICE_IS_INITIATOR 1

/* Both ESP32 boards must use the same Wi-Fi channel. */
#define ESPNOW_CHANNEL 1

/* Initiator starts the EDHOC trace exchange once after this delay. */
#define EDHOC_TRACE_START_DELAY_MS 2000

/* Allow the responder's final unencrypted ACK to leave before peer modification. */
#define RESPONDER_ENCRYPTION_SWITCH_DELAY_MS 250

/* Give the responder time to install its LMK before the encrypted KEY_TEST. */
#define INITIATOR_KEY_TEST_DELAY_MS 750

/* Keep payloads small for the prototype. ESP-NOW v1 safe limit is 250 bytes. */
#define APP_PAYLOAD_MAX_LEN 64

/* Active Milestone 6 mode. */
#define EDHOC_TRACE_TRANSPORT_ENABLED 1
#define DYNAMIC_LMK_SWITCH_ENABLED 1

/* Previous modes remain disabled. */
#define FAKE_EDHOC_TRANSPORT_ENABLED 0
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 0

/* Set to 1 on exactly one board to prove mismatched LMKs break encryption. */
#define MILESTONE6_CORRUPT_LMK_FOR_TEST 0

/* One fixed EDHOC transport session ID for the two-board prototype. */
#define EDHOC_TRACE_SESSION_ID 0x1234

/*
 * ESP-NOW PMK and LMK are both 16 bytes.
 * These are lab/demo keys only. Do not use these values in a real deployment.
 *
 * The static PMK is retained to protect the dynamically installed per-peer LMK.
 * The static LMK remains only as the Milestone 2 comparison baseline.
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
