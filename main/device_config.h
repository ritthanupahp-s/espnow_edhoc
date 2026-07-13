#pragma once

#include <stdint.h>

/*
 * Milestone 9 configuration
 * -------------------------
 * Flash one board with DEVICE_IS_INITIATOR = 1.
 * Flash the other board with DEVICE_IS_INITIATOR = 0.
 *
 * PEER_MAC must be the other ESP32 board's STA MAC address.
 * Both boards must use the same ESPNOW_CHANNEL.
 *
 * Milestone 9 runs live Lakers EDHOC STAT-STAT / Cipher Suite 2 on each ESP32,
 * transports generated message_1/message_2/message_3 over unencrypted ESP-NOW,
 * exports a fresh 16-byte LMK on both devices, switches the peer to
 * encrypt=true, and verifies the key using KEY_TEST / KEY_TEST_ACK.
 */
#define DEVICE_IS_INITIATOR 1

/* Both ESP32 boards must use the same Wi-Fi channel. */
#define ESPNOW_CHANNEL 1

/* Initiator starts one live EDHOC exchange after this delay. */
#define EDHOC_START_DELAY_MS 2000

/* Allow the responder's final unencrypted ACK to leave before peer modification. */
#define RESPONDER_ENCRYPTION_SWITCH_DELAY_MS 250

/* Give the responder time to install its LMK before the encrypted KEY_TEST. */
#define INITIATOR_KEY_TEST_DELAY_MS 750

/* Lakers uses substantial stack during P-256 and transcript processing. */
#define EDHOC_APP_TASK_STACK_SIZE 24576

/* Set to 1 on exactly one board to prove mismatched exporter keys break encryption. */
#define MILESTONE9_CORRUPT_LMK_FOR_TEST 0

/* One fixed transport session ID for the two-board prototype. */
#define EDHOC_SESSION_ID 0x1234

/*
 * ESP-NOW PMK and LMK are both 16 bytes.
 * These are lab/demo values only.
 *
 * The fixed PMK is retained to protect the dynamically installed per-peer LMK.
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
