# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally for a thesis proof of concept.

Current implementation: **Milestone 6 — install the post-handshake LMK and switch to encrypted ESP-NOW unicast**.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       DONE
Milestone 3: fake EDHOC transport over ESP-NOW                    DONE
Milestone 4: real EDHOC trace messages over ESP-NOW               DONE
Milestone 5: derive 16-byte LMK-shaped output after EDHOC flow    DONE
Milestone 6: install derived LMK and test encrypted traffic       CURRENT
Next: replace trace scaffold with live Lakers EDHOC/exporter       NEXT
```

## Milestone 6 goal

Prove the complete ESP-NOW security handover:

```text
1. Start with peer encrypt=false
2. Transport EDHOC message_1/message_2/message_3 over ESP-NOW
3. Derive the same 16-byte LMK candidate on both boards
4. Modify the existing peer with encrypt=true and the derived LMK
5. Send encrypted KEY_TEST
6. Reply with encrypted KEY_TEST_ACK
```

A successful KEY_TEST round trip proves that both ESP32 devices installed matching LMKs and that ESP-NOW accepted the transition from unencrypted to encrypted unicast.

## Important limitation

The current LMK source is still the Milestone 5 **trace-only scaffold**:

```text
RFC 9529 trace transcript -> SHA-256 scaffold -> first 16 bytes -> ESP-NOW LMK
```

It is not yet the result of a live Lakers EDHOC session and must not be treated as production security.

The next development phase must replace:

```c
edhoc_exporter_trace_derive_espnow_lmk()
```

with a Lakers-backed completed-session exporter call.

## Security transition

The handover is deliberately ordered to avoid changing the peer too early:

```text
Initiator                         Responder
---------                         ---------
M1          -- unencrypted -->
            <-- unencrypted --   M2
M3          -- unencrypted -->
            <-- unencrypted --   final EDHOC ACK
install LMK                       wait 250 ms
peer encrypt=true                 install LMK
wait 750 ms                       peer encrypt=true
KEY_TEST    -- encrypted ----->
            <---- encrypted --   KEY_TEST_ACK
```

The responder delay allows its final unencrypted ACK to leave before it modifies the peer. The longer initiator delay gives the responder time to install its LMK before encrypted traffic starts.

If the transition is unreliable on a particular ESP32 or radio environment, increase:

```c
RESPONDER_ENCRYPTION_SWITCH_DELAY_MS
INITIATOR_KEY_TEST_DELAY_MS
```

## New transport message types

```text
0x20 = KEY_TEST
0x21 = KEY_TEST_ACK
```

These use the same transport frame as EDHOC:

```text
magic | version | type | flags | session_id | seq | frag_idx | frag_count | payload_len | payload
```

Before the peer update, ESP-NOW carries the frame unencrypted. After `encrypt=true`, ESP-NOW encrypts the same frame format using the installed LMK.

## Files

```text
main/main.c                    Milestone 6 handover state machine
main/device_config.h           Role, peer MAC, transition delays, negative-test flag
main/espnow_transport.c        Raw ESP-NOW peer/send/receive functions
main/edhoc_transport.c         EDHOC and KEY_TEST frame serialization/parsing
main/edhoc_transport.h         Message types including KEY_TEST and KEY_TEST_ACK
main/edhoc_trace_vectors.c     RFC 9529 message vectors
main/edhoc_exporter.c          Temporary trace-only 16-byte LMK derivation
main/key_manager.c             Static LMK baseline and derived LMK peer installation
main/key_manager.h             Key-manager interfaces
docs/lakers_integration.md     Planned Lakers integration boundary
```

## How peer installation works

After deriving the 16-byte value, Milestone 6 calls:

```c
key_manager_enable_derived_espnow_encryption(PEER_MAC, lmk);
```

The function:

```text
1. installs the lab PMK
2. finds the existing peer
3. changes peer.encrypt from false to true
4. copies the derived 16-byte LMK into the peer entry
5. calls esp_now_mod_peer()
6. reads the peer back and verifies encrypt=true
```

The LMK buffer used by the application is cleared after installation.

## Configure Board A

In `main/device_config.h`:

```c
#define DEVICE_IS_INITIATOR 1
#define EDHOC_TRACE_TRANSPORT_ENABLED 1
#define DYNAMIC_LMK_SWITCH_ENABLED 1
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 0
#define MILESTONE6_CORRUPT_LMK_FOR_TEST 0

static const uint8_t PEER_MAC[6] = {
    /* Board B STA MAC */
    0x24, 0x6F, 0x28, 0xAA, 0xBB, 0xCC
};
```

## Configure Board B

```c
#define DEVICE_IS_INITIATOR 0
#define EDHOC_TRACE_TRANSPORT_ENABLED 1
#define DYNAMIC_LMK_SWITCH_ENABLED 1
#define ESPNOW_STATIC_ENCRYPTION_ENABLED 0
#define MILESTONE6_CORRUPT_LMK_FOR_TEST 0

static const uint8_t PEER_MAC[6] = {
    /* Board A STA MAC */
    0x24, 0x6F, 0x28, 0x11, 0x22, 0x33
};
```

## Build and flash

```bash
idf.py set-target esp32
idf.py build
idf.py -p COMx flash monitor
```

## Expected initiator logs

```text
EDHOC ESP-NOW thesis demo - Milestone 6
Role: INITIATOR
Security mode: unencrypted-edhoc-then-derived-lmk-encrypted
Initial peer state: encrypt=false for EDHOC transport
Starting unencrypted EDHOC trace transport exchange
EDHOC message_2 accepted; sending message_3 while peer is still unencrypted
Final unencrypted EDHOC ACK received; installing LMK on initiator
TRACE-ONLY LMK derivation active
LMK summary: len=16 sha256_prefix=AA:BB:CC:DD
Installing post-handshake LMK into ESP-NOW peer table
Modifying existing peer ... encrypted=1
Peer table updated successfully: encrypt=true LMK_len=16
Security transition complete: peer is now encrypt=true
Sending first post-handshake frame; ESP-NOW peer should encrypt this frame
ENCRYPTED APP TX: type=KEY_TEST
ENCRYPTED APP RX verified: type=KEY_TEST_ACK
Milestone 6 PASS: derived LMK installed and encrypted ESP-NOW KEY_TEST round trip succeeded
```

## Expected responder logs

```text
EDHOC ESP-NOW thesis demo - Milestone 6
Role: RESPONDER
Initial peer state: encrypt=false for EDHOC transport
EDHOC message_1 accepted; sending message_2 while peer is still unencrypted
EDHOC message_3 accepted; sending final ACK before enabling peer encryption
Installing LMK on responder after final unencrypted ACK
TRACE-ONLY LMK derivation active
LMK summary: len=16 sha256_prefix=AA:BB:CC:DD
Peer table updated successfully: encrypt=true LMK_len=16
Pairing state -> WAIT_KEY_TEST
ENCRYPTED APP RX verified: type=KEY_TEST
Encrypted KEY_TEST accepted; replying with encrypted KEY_TEST_ACK
Milestone 6 PASS: responder received and acknowledged encrypted traffic using the derived LMK
```

Both boards should print the same LMK hash prefix.

## Required negative test

To prove the encrypted exchange depends on matching LMKs:

1. Keep this value on Board A:

```c
#define MILESTONE6_CORRUPT_LMK_FOR_TEST 0
```

2. Set this value on Board B only:

```c
#define MILESTONE6_CORRUPT_LMK_FOR_TEST 1
```

3. Rebuild and flash Board B.

Expected result:

```text
The two LMK summary prefixes differ.
KEY_TEST is not successfully received or acknowledged.
Milestone 6 PASS does not appear on the initiator.
The ESP-NOW send callback may report FAIL.
```

Restore the value to `0` after the test.

## What Milestone 6 demonstrates

Milestone 6 demonstrates the engineering path required by the thesis:

```text
pre-key unencrypted ESP-NOW transport
    -> EDHOC-style exchange
        -> shared 16-byte output
            -> ESP-NOW peer-table update
                -> encrypted post-handshake communication
```

It does not yet demonstrate real EDHOC authentication, forward secrecy, or a standards-compliant EDHOC exporter because the Lakers runtime has not yet been linked.

## Next phase

Replace the RFC trace and SHA-256 scaffold with a live Lakers session:

```text
Lakers initiator.prepare_message_1()
Lakers responder.process_message_1()
Lakers responder.prepare_message_2()
Lakers initiator.parse_message_2() + verify_message_2()
Lakers initiator.prepare_message_3()
Lakers responder.parse_message_3() + verify_message_3()
Both completed sessions -> edhoc_exporter(..., 16-byte output)
The existing Milestone 6 peer installation and KEY_TEST code stays unchanged.
```
