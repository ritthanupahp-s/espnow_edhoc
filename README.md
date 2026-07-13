# EDHOC-Based Dynamic Pairing for ESP-NOW on ESP32

This repository is being built incrementally as a thesis proof of concept.

Current implementation: **Milestone 9 — live Lakers EDHOC and real exporter-derived ESP-NOW LMK on two ESP32 boards**.

## Roadmap position

```text
Milestone 1: minimal unencrypted ESP-NOW unicast ping/pong        DONE
Milestone 2: static encrypted ESP-NOW with manual PMK + LMK       DONE
Milestone 3: fake EDHOC transport over ESP-NOW                    DONE
Milestone 4: RFC 9529 EDHOC trace messages over ESP-NOW           DONE
Milestone 5: derive 16-byte LMK-shaped output after EDHOC flow    DONE
Milestone 6: install derived LMK and test encrypted traffic       DONE
Milestone 7: live Lakers handshake and exporter on host           DONE
Milestone 8: ESP32 Rust/C FFI and Xtensa staticlib build          DONE
Milestone 9: live Lakers session and exporter on both ESP32s      CURRENT
Milestone 10: retransmission, timeouts, duplicate handling        NEXT
```

## Milestone 9 flow

```text
ESP-NOW peer encrypt=false
    -> initiator Lakers prepare_message_1()
    -> responder Lakers process_message_1() + prepare_message_2()
    -> initiator Lakers parse/verify message_2 + prepare_message_3()
    -> responder Lakers parse/verify message_3
    -> both complete without message_4
    -> both call edhoc_exporter(label=0xF0, context, output_len=16)
    -> both install the exported value as the ESP-NOW LMK
    -> peer encrypt=true
    -> encrypted KEY_TEST / KEY_TEST_ACK
```

The Rust static library retains Lakers' typed session state. The C firmware sees only a narrow ABI for session initialization, M1/M2/M3 operations, and 16-byte LMK export.

## Cryptographic configuration

```text
EDHOC authentication method: STAT-STAT
EDHOC cipher suite: 2
Credential transfer: by reference
Exporter label: 0xF0
Exporter output length: 16 bytes
```

Exporter context:

```text
"ESP-NOW-LMK-v1"
|| initiator STA MAC
|| responder STA MAC
|| Wi-Fi channel
```

The initiator/responder ordering is fixed so both boards construct identical context bytes.

The embedded RustCrypto backend obtains randomness through ESP-IDF's `esp_fill_random()` function.

## Test credentials

Milestone 9 still uses the public Lakers example credentials and private keys. They are suitable only for development and interoperability testing. Replace them with provisioned device credentials before any deployment.

## Configure the boards

Edit `main/device_config.h` separately for each board.

Board A:

```c
#define DEVICE_IS_INITIATOR 1
static const uint8_t PEER_MAC[6] = { /* Board B STA MAC */ };
```

Board B:

```c
#define DEVICE_IS_INITIATOR 0
static const uint8_t PEER_MAC[6] = { /* Board A STA MAC */ };
```

Both boards must use the same:

```c
#define ESPNOW_CHANNEL 1
#define EDHOC_SESSION_ID 0x1234
#define MILESTONE9_CORRUPT_LMK_FOR_TEST 0
```

## Build requirements

Install the Xtensa Rust toolchain once:

```powershell
cargo install espup --locked
espup install --targets esp32
```

Verify:

```powershell
rustup toolchain list
rustc +esp --version
rustc +esp --print target-list | Select-String xtensa-esp32-none-elf
```

## Build and flash

The Rust library is built automatically by `idf.py build`.

For each board, after setting its role and peer MAC:

```powershell
idf.py fullclean
idf.py build
idf.py -p COMx flash monitor
```

The first build downloads and compiles the pinned Lakers and RustCrypto dependencies.

## Expected logs

Both boards should first print:

```text
Milestone 9 FFI ready: ESP-IDF C called Lakers Rust staticlib abi=0x00090001
Live Lakers session initialized using ESP32 hardware RNG
```

The initiator should finish with:

```text
Milestone 9 PASS: live Lakers EDHOC, exporter LMK, and encrypted ESP-NOW round trip succeeded
```

The responder should finish with:

```text
Milestone 9 PASS: responder authenticated live Lakers EDHOC and accepted encrypted ESP-NOW traffic
```

Both boards also print a non-secret LMK SHA-256 prefix. The prefixes must match.

## Negative tests

### Exported LMK mismatch

On exactly one board:

```c
#define MILESTONE9_CORRUPT_LMK_FOR_TEST 1
```

The encrypted `KEY_TEST` round trip must fail and the initiator must not print `Milestone 9 PASS`.

### Credential authentication failure

Change one byte of one test credential or static private key inside:

```text
components/lakers_ffi/rust/src/lib.rs
```

Then rebuild that board. Lakers should return `PROTOCOL_ERROR` while processing authenticated message 2 or message 3.

Restore the original test value after the negative test.

## Milestone 9 acceptance checklist

```text
[ ] both firmware builds compile the live Lakers Rust dependencies
[ ] both boards print FFI ABI 0x00090001
[ ] live message_1, message_2, and message_3 are generated on-device
[ ] responder authenticates message_3
[ ] both LMK SHA-256 prefixes match
[ ] both peers switch to encrypt=true
[ ] encrypted KEY_TEST / KEY_TEST_ACK succeeds
[ ] both boards print Milestone 9 PASS
[ ] corrupting one LMK prevents the encrypted round trip
```

Detailed Rust/C bridge information is in `components/lakers_ffi/README.md`.
