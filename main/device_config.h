#pragma once

#include <stdint.h>

/*
 * Milestone 5 configuration
 * -------------------------
 * Flash one board with DEVICE_IS_INITIATOR = 1.
 * Flash the other board with DEVICE_IS_INITIATOR = 0.
 *
 * PEER_MAC must be the other ESP32 board's printed STA MAC address.
 *
 * Milestone 5 still transports RFC 9529 EDHOC message_1/message_2/message_3
 * bytes over the ESP-NOW EDHOC transport frame. After the trace exchange
 * completes, both boards derive a deterministic 16-byte LMK candidate.
 *
 * Important: this is a trace-only LMK derivation scaffold, not a real EDHOC
 * exporter result yet. The next step is to replace the trace-only derivation
 * with Lakers' completed-session edhoc_exporter().
 */
#define DEVICE_IS_INITIATOR 1

/* Both ESP32 boards must use the same Wi-Fi channel. */
#define ESPNOW_CHANNEL 1

/* Initiator starts the EDHOC trace exchange once after this delay. */
#define EDHOC_TRACE_START_DELAY_MS 2000

/* Keep payloads small for the first prototype. ESP-NOW v1 safe limit is 250 bytes. */
#define APP_PAYLOAD_MAX_LEN 64

/* Active Milestone 5 mode: RFC 9529 EDHOC trace + trace-only LMK scaffold. */
#define EDHOC_TRACE_TRANSPORT_ENABLED 1

/* Milestone 3 fake string mode is disabled. */
#define FAKE_EDHOC_TRANSPORT_ENABLED 0

/* Keep Milestone 2 control keys in the repo, but disable static encryption for Milestone 5. */
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 0

/* One fixed EDHOC transport session ID for the two-board prototype. */
#define EDHOC_TRACE_SESSION_ID 0x1234

/*
 * ESP-NOW PMK and LMK are both 16 bytes.
 * These are lab/demo keys only. Do not use these values in a real deployment.
 *
 * PMK: Primary Master Key, used by ESP-NOW to protect LMKs internally.
 * LMK: Local Master Key, installed per peer for encrypted unicast frames.
 *
 * Milestone 2 uses these. Milestone 5 leaves them unused.
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
