# Live Lakers EDHOC reference

This host-side Rust program runs a real EDHOC handshake using
[`lake-rs/lakers`](https://github.com/lake-rs/lakers).

It is Milestone 7 of the ESP-NOW EDHOC project.

## What it proves

The program runs the complete Lakers STAT-STAT / Cipher Suite 2 flow:

```text
Initiator prepare_message_1
Responder process_message_1
Responder prepare_message_2
Initiator parse_message_2
Initiator verify_message_2
Initiator prepare_message_3
Responder parse_message_3
Responder verify_message_3
Both peers complete without message_4
Both peers call edhoc_exporter
```

It then verifies:

```text
initiator PRK_out == responder PRK_out
initiator 16-byte LMK == responder 16-byte LMK
```

This is genuine Lakers cryptographic processing. It does not use the RFC trace arrays or the temporary SHA-256 transcript scaffold used by the ESP32 firmware in Milestones 4–6.

## Requirements

Install the stable Rust toolchain:

```bash
rustup toolchain install stable
rustup default stable
```

### Windows MSVC linker prerequisite

The default Windows Rust target is normally `x86_64-pc-windows-msvc`. It requires the Microsoft C/C++ linker, libraries, and Windows SDK.

Install Visual Studio 2022 Build Tools from an elevated PowerShell terminal:

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools --source winget --force --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

Alternatively, open Visual Studio Installer and select the **Desktop development with C++** workload. At minimum, install:

```text
MSVC v143 C++ x64/x86 build tools
Windows 10 or Windows 11 SDK
```

After installation, close and reopen PowerShell or VS Code before running Cargo again.

Verify the toolchain:

```powershell
rustc -vV
where.exe link
```

`rustc -vV` should report:

```text
host: x86_64-pc-windows-msvc
```

If `where.exe link` still finds nothing in an ordinary PowerShell window, open **Developer PowerShell for VS 2022**, return to this directory, and run Cargo there.

## Dependency source

The project pins both `lakers` and `lakers-crypto` to the same Lakers Git revision.
The `lakers-crypto` dispatch crate is part of the Lakers workspace but is not
published as a standalone crates.io package, so declaring
`lakers-crypto = "0.8.0"` will fail.

After updating an older local copy, run from the repository root:

```bash
git pull
cd tools/lakers_reference
cargo clean
cargo run --release
```

If an old `Cargo.lock` was generated from the incorrect dependency declaration,
remove it once before running Cargo again:

```powershell
Remove-Item Cargo.lock -ErrorAction SilentlyContinue
cargo run --release
```

## Run with default test identities

From the repository root:

```bash
cd tools/lakers_reference
cargo run --release
```

The default exporter context uses these placeholder values:

```text
Initiator MAC: 02:00:00:00:00:01
Responder MAC: 02:00:00:00:00:02
Wi-Fi channel: 1
```

## Run with the two real ESP32 identities

Arguments must be ordered by EDHOC role, not by which board was flashed first:

```bash
cargo run --release -- <initiator-mac> <responder-mac> <channel>
```

Example:

```bash
cargo run --release -- 24:6F:28:11:22:33 24:6F:28:AA:BB:CC 1
```

The exporter context is constructed as:

```text
"ESP-NOW-LMK-v1"
|| initiator STA MAC
|| responder STA MAC
|| Wi-Fi channel
```

This ordering must be identical on both ESP32 boards when the live Lakers exporter is moved into the firmware.

## Expected output

The actual messages and LMK change between runs because EDHOC creates fresh ephemeral keys.

```text
LAKERS_LIVE_HANDSHAKE=PASS
authentication_method=STAT-STAT
cipher_suite=2
exporter_label=0xF0
initiator_mac=24:6F:28:11:22:33
responder_mac=24:6F:28:AA:BB:CC
wifi_channel=1
message_1_len=<generated length>
message_1_hex=<generated EDHOC message_1>
message_2_len=<generated length>
message_2_hex=<generated EDHOC message_2>
message_3_len=<generated length>
message_3_hex=<generated EDHOC message_3>
espnow_lmk_len=16
espnow_lmk_hex=<16-byte Lakers exporter result>
initiator_responder_lmk_match=true
```

## Tests

```bash
cargo test
```

The tests run a live handshake and confirm that the exported LMK has the required ESP-NOW length.

## Security notes

- The credentials and static private keys are public test values copied from the upstream Lakers example. Do not use them in a deployed system.
- The program prints the derived LMK because it is a development reference tool. Do not log production key material.
- Exporter label `0xF0` is currently project-local. Confirm the final label strategy before making standards-compliance claims.

## Next integration step

Milestone 8 should build a Lakers Rust static library for the ESP32 target and expose a small C FFI wrapper to the existing ESP-IDF application.

The existing C transport should then replace:

```c
edhoc_trace_get_message(...)
edhoc_exporter_trace_derive_espnow_lmk(...)
```

with live calls equivalent to:

```text
prepare/process/verify M1, M2, M3
completed_without_message_4
edhoc_exporter(0xF0, context, 16-byte output)
```
