use std::env;
use std::fmt::Write as _;

use lakers::{
    credential_check_or_fetch, Credential, CredentialTransfer, EadItems, EDHOCMethod,
    EDHOCSuite, EdhocInitiator, EdhocResponder,
};
use lakers_crypto::default_crypto;

const EXPORTER_LABEL_ESPNOW_LMK: u8 = 0xF0;
const EXPORTER_PURPOSE: &[u8] = b"ESP-NOW-LMK-v1";

/* Credentials and static authentication keys used by the upstream Lakers example. */
const CRED_I_HEX: &str = concat!(
    "A2027734322D35302D33312D46462D45462D33372D33322D3339",
    "08A101A5010202412B2001215820AC75E9ECE3E50BFC8ED6039988952240",
    "5C47BF16DF96660A41298CB4307F7EB62258206E5DE611388A4B8A8211334",
    "AC7D37ECB52A387D257E6DB3C2A93DF21FF3AFFC8"
);
const I_HEX: &str = "fb13adeb6518cee5f88417660841142e830a81fe334380a953406a1305e8706b";
const CRED_R_HEX: &str = concat!(
    "A2026008A101A5010202410A2001215820BBC34960526EA4D32E940CAD2A234148",
    "DDC21791A12AFBCBAC93622046DD44F02258204519E257236B2A0CE2023F0931",
    "F1F386CA7AFDA64FCDE0108C224C51EABF6072"
);
const R_HEX: &str = "72cc4761dbd4c78f758931aa589d348d1ef874a7e303ede2f140dcf3e6aa4aac";

const DEFAULT_INITIATOR_MAC: [u8; 6] = [0x02, 0x00, 0x00, 0x00, 0x00, 0x01];
const DEFAULT_RESPONDER_MAC: [u8; 6] = [0x02, 0x00, 0x00, 0x00, 0x00, 0x02];
const DEFAULT_CHANNEL: u8 = 1;

#[derive(Debug)]
struct HandshakeOutput {
    message_1: Vec<u8>,
    message_2: Vec<u8>,
    message_3: Vec<u8>,
    lmk: [u8; 16],
    context: Vec<u8>,
}

fn decode_hex(input: &str) -> Result<Vec<u8>, String> {
    if input.len() % 2 != 0 {
        return Err("hex string has an odd number of characters".to_owned());
    }

    input
        .as_bytes()
        .chunks_exact(2)
        .map(|pair| {
            let text = std::str::from_utf8(pair).map_err(|error| error.to_string())?;
            u8::from_str_radix(text, 16).map_err(|error| error.to_string())
        })
        .collect()
}

fn encode_hex(bytes: &[u8]) -> String {
    let mut output = String::with_capacity(bytes.len() * 2);
    for byte in bytes {
        write!(&mut output, "{byte:02x}").expect("writing to String cannot fail");
    }
    output
}

fn parse_mac(input: &str) -> Result<[u8; 6], String> {
    let normalized: String = input.chars().filter(|character| *character != ':' && *character != '-').collect();
    let bytes = decode_hex(&normalized)?;
    bytes
        .try_into()
        .map_err(|_| format!("MAC address must contain exactly 6 bytes: {input}"))
}

fn format_mac(mac: &[u8; 6]) -> String {
    format!(
        "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]
    )
}

fn build_exporter_context(
    initiator_mac: [u8; 6],
    responder_mac: [u8; 6],
    channel: u8,
) -> Vec<u8> {
    let mut context = Vec::with_capacity(EXPORTER_PURPOSE.len() + 13);
    context.extend_from_slice(EXPORTER_PURPOSE);
    context.extend_from_slice(&initiator_mac);
    context.extend_from_slice(&responder_mac);
    context.push(channel);
    context
}

fn run_live_lakers_handshake(context: Vec<u8>) -> Result<HandshakeOutput, String> {
    let cred_i_bytes = decode_hex(CRED_I_HEX)?;
    let cred_r_bytes = decode_hex(CRED_R_HEX)?;
    let initiator_private: [u8; 32] = decode_hex(I_HEX)?
        .try_into()
        .map_err(|_| "initiator private key is not 32 bytes".to_owned())?;
    let responder_private: [u8; 32] = decode_hex(R_HEX)?
        .try_into()
        .map_err(|_| "responder private key is not 32 bytes".to_owned())?;

    let cred_i = Credential::parse_ccs(&cred_i_bytes)
        .map_err(|error| format!("failed to parse initiator credential: {error:?}"))?;
    let cred_r = Credential::parse_ccs(&cred_r_bytes)
        .map_err(|error| format!("failed to parse responder credential: {error:?}"))?;

    let initiator = EdhocInitiator::new(
        default_crypto(),
        EDHOCMethod::StatStat,
        EDHOCSuite::CipherSuite2,
    );
    let responder = EdhocResponder::new(default_crypto(), responder_private, cred_r.clone());

    let (initiator, message_1) = initiator
        .prepare_message_1(None, &EadItems::new())
        .map_err(|error| format!("prepare_message_1 failed: {error:?}"))?;

    let (responder, _c_i, _ead_1) = responder
        .process_message_1(&message_1)
        .map_err(|error| format!("process_message_1 failed: {error:?}"))?;
    let (responder, message_2) = responder
        .prepare_message_2(CredentialTransfer::ByReference, None, &EadItems::new())
        .map_err(|error| format!("prepare_message_2 failed: {error:?}"))?;

    let (mut initiator, _c_r, id_cred_r, _ead_2) = initiator
        .parse_message_2(&message_2)
        .map_err(|error| format!("parse_message_2 failed: {error:?}"))?;
    let valid_cred_r = credential_check_or_fetch(Some(cred_r), id_cred_r)
        .map_err(|error| format!("responder credential check failed: {error:?}"))?;
    initiator
        .set_identity(initiator_private, cred_i.clone())
        .map_err(|error| format!("set_identity failed: {error:?}"))?;
    let initiator = initiator
        .verify_message_2(valid_cred_r)
        .map_err(|error| format!("verify_message_2 failed: {error:?}"))?;

    let (initiator, message_3, initiator_prk_out) = initiator
        .prepare_message_3(CredentialTransfer::ByReference, &EadItems::new())
        .map_err(|error| format!("prepare_message_3 failed: {error:?}"))?;

    let (responder, id_cred_i, _ead_3) = responder
        .parse_message_3(&message_3)
        .map_err(|error| format!("parse_message_3 failed: {error:?}"))?;
    let valid_cred_i = credential_check_or_fetch(Some(cred_i), id_cred_i)
        .map_err(|error| format!("initiator credential check failed: {error:?}"))?;
    let (responder, responder_prk_out) = responder
        .verify_message_3(valid_cred_i)
        .map_err(|error| format!("verify_message_3 failed: {error:?}"))?;

    if initiator_prk_out != responder_prk_out {
        return Err("Lakers produced different PRK_out values for the two peers".to_owned());
    }

    let mut initiator = initiator
        .completed_without_message_4()
        .map_err(|error| format!("initiator completion failed: {error:?}"))?;
    let mut responder = responder
        .completed_without_message_4()
        .map_err(|error| format!("responder completion failed: {error:?}"))?;

    let mut initiator_lmk = [0u8; 16];
    initiator.edhoc_exporter(EXPORTER_LABEL_ESPNOW_LMK, &context, &mut initiator_lmk);

    let mut responder_lmk = [0u8; 16];
    responder.edhoc_exporter(EXPORTER_LABEL_ESPNOW_LMK, &context, &mut responder_lmk);

    if initiator_lmk != responder_lmk {
        return Err("Lakers exporter produced different ESP-NOW LMKs".to_owned());
    }

    Ok(HandshakeOutput {
        message_1: message_1.as_slice().to_vec(),
        message_2: message_2.as_slice().to_vec(),
        message_3: message_3.as_slice().to_vec(),
        lmk: initiator_lmk,
        context,
    })
}

fn parse_arguments() -> Result<([u8; 6], [u8; 6], u8), String> {
    let arguments: Vec<String> = env::args().skip(1).collect();

    if arguments.is_empty() {
        return Ok((DEFAULT_INITIATOR_MAC, DEFAULT_RESPONDER_MAC, DEFAULT_CHANNEL));
    }

    if arguments.len() != 3 {
        return Err(
            "usage: cargo run --release -- <initiator-mac> <responder-mac> <channel>".to_owned(),
        );
    }

    let initiator_mac = parse_mac(&arguments[0])?;
    let responder_mac = parse_mac(&arguments[1])?;
    let channel: u8 = arguments[2]
        .parse()
        .map_err(|_| format!("invalid Wi-Fi channel: {}", arguments[2]))?;

    if channel == 0 || channel > 14 {
        return Err(format!("Wi-Fi channel must be between 1 and 14: {channel}"));
    }

    Ok((initiator_mac, responder_mac, channel))
}

fn run() -> Result<(), String> {
    let (initiator_mac, responder_mac, channel) = parse_arguments()?;
    let context = build_exporter_context(initiator_mac, responder_mac, channel);
    let output = run_live_lakers_handshake(context)?;

    println!("LAKERS_LIVE_HANDSHAKE=PASS");
    println!("authentication_method=STAT-STAT");
    println!("cipher_suite=2");
    println!("exporter_label=0x{EXPORTER_LABEL_ESPNOW_LMK:02X}");
    println!("initiator_mac={}", format_mac(&initiator_mac));
    println!("responder_mac={}", format_mac(&responder_mac));
    println!("wifi_channel={channel}");
    println!("exporter_context_len={}", output.context.len());
    println!("exporter_context_hex={}", encode_hex(&output.context));
    println!("message_1_len={}", output.message_1.len());
    println!("message_1_hex={}", encode_hex(&output.message_1));
    println!("message_2_len={}", output.message_2.len());
    println!("message_2_hex={}", encode_hex(&output.message_2));
    println!("message_3_len={}", output.message_3.len());
    println!("message_3_hex={}", encode_hex(&output.message_3));
    println!("espnow_lmk_len={}", output.lmk.len());
    println!("espnow_lmk_hex={}", encode_hex(&output.lmk));
    println!("initiator_responder_lmk_match=true");

    Ok(())
}

fn main() {
    if let Err(error) = run() {
        eprintln!("LAKERS_LIVE_HANDSHAKE=FAIL");
        eprintln!("error={error}");
        std::process::exit(1);
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn live_lakers_handshake_exports_matching_16_byte_lmk() {
        let context = build_exporter_context(
            DEFAULT_INITIATOR_MAC,
            DEFAULT_RESPONDER_MAC,
            DEFAULT_CHANNEL,
        );
        let output = run_live_lakers_handshake(context).expect("live Lakers handshake failed");

        assert!(!output.message_1.is_empty());
        assert!(!output.message_2.is_empty());
        assert!(!output.message_3.is_empty());
        assert_eq!(output.lmk.len(), 16);
    }

    #[test]
    fn exporter_context_changes_when_peer_identity_changes() {
        let context_a = build_exporter_context(
            DEFAULT_INITIATOR_MAC,
            DEFAULT_RESPONDER_MAC,
            DEFAULT_CHANNEL,
        );
        let context_b = build_exporter_context(
            DEFAULT_INITIATOR_MAC,
            [0x02, 0x00, 0x00, 0x00, 0x00, 0x03],
            DEFAULT_CHANNEL,
        );

        assert_ne!(context_a, context_b);
    }
}
