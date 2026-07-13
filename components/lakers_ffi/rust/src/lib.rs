#![no_std]

use core::cell::UnsafeCell;
use core::ffi::c_void;
use core::mem;
use core::panic::PanicInfo;
use core::ptr;
use core::slice;

use lakers::{
    credential_check_or_fetch, BufferMessage1, BufferMessage2, BufferMessage3, Credential,
    CredentialTransfer, EadItems, EDHOCMethod, EDHOCSuite, EdhocInitiator,
    EdhocInitiatorWaitM2, EdhocResponder, EdhocResponderDone, EdhocResponderWaitM3,
};
use lakers_crypto_rustcrypto::Crypto;
use rand_core::{CryptoRng, Error as RngError, RngCore};

const ABI_VERSION: u32 = 0x0009_0001;
const TRANSFORM_MASK: u32 = 0xED0C_0009;

const STATUS_OK: i32 = 0;
const STATUS_INVALID_ARGUMENT: i32 = -1;
const STATUS_WRONG_STATE: i32 = -2;
const STATUS_BUFFER_TOO_SMALL: i32 = -3;
const STATUS_PROTOCOL_ERROR: i32 = -4;
const STATUS_CREDENTIAL_ERROR: i32 = -5;

const ROLE_INITIATOR: u8 = 1;
const ROLE_RESPONDER: u8 = 2;
const MAX_EDHOC_MESSAGE_LEN: usize = 192;
const ESPNOW_LMK_LEN: usize = 16;
const EXPORTER_LABEL_ESPNOW_LMK: u8 = 0xF0;
const EXPORTER_PURPOSE: &[u8; 14] = b"ESP-NOW-LMK-v1";
const EXPORTER_CONTEXT_LEN: usize = 27;

/* Public Lakers test credentials. Replace before any deployment. */
const CRED_I: [u8; 107] = [
    0xA2, 0x02, 0x77, 0x34, 0x32, 0x2D, 0x35, 0x30, 0x2D, 0x33, 0x31, 0x2D,
    0x46, 0x46, 0x2D, 0x45, 0x46, 0x2D, 0x33, 0x37, 0x2D, 0x33, 0x32, 0x2D,
    0x33, 0x39, 0x08, 0xA1, 0x01, 0xA5, 0x01, 0x02, 0x02, 0x41, 0x2B, 0x20,
    0x01, 0x21, 0x58, 0x20, 0xAC, 0x75, 0xE9, 0xEC, 0xE3, 0xE5, 0x0B, 0xFC,
    0x8E, 0xD6, 0x03, 0x99, 0x88, 0x95, 0x22, 0x40, 0x5C, 0x47, 0xBF, 0x16,
    0xDF, 0x96, 0x66, 0x0A, 0x41, 0x29, 0x8C, 0xB4, 0x30, 0x7F, 0x7E, 0xB6,
    0x22, 0x58, 0x20, 0x6E, 0x5D, 0xE6, 0x11, 0x38, 0x8A, 0x4B, 0x8A, 0x82,
    0x11, 0x33, 0x4A, 0xC7, 0xD3, 0x7E, 0xCB, 0x52, 0xA3, 0x87, 0xD2, 0x57,
    0xE6, 0xDB, 0x3C, 0x2A, 0x93, 0xDF, 0x21, 0xFF, 0x3A, 0xFF, 0xC8,
];

const INITIATOR_PRIVATE: [u8; 32] = [
    0xFB, 0x13, 0xAD, 0xEB, 0x65, 0x18, 0xCE, 0xE5, 0xF8, 0x84, 0x17, 0x66,
    0x08, 0x41, 0x14, 0x2E, 0x83, 0x0A, 0x81, 0xFE, 0x33, 0x43, 0x80, 0xA9,
    0x53, 0x40, 0x6A, 0x13, 0x05, 0xE8, 0x70, 0x6B,
];

const CRED_R: [u8; 84] = [
    0xA2, 0x02, 0x60, 0x08, 0xA1, 0x01, 0xA5, 0x01, 0x02, 0x02, 0x41, 0x0A,
    0x20, 0x01, 0x21, 0x58, 0x20, 0xBB, 0xC3, 0x49, 0x60, 0x52, 0x6E, 0xA4,
    0xD3, 0x2E, 0x94, 0x0C, 0xAD, 0x2A, 0x23, 0x41, 0x48, 0xDD, 0xC2, 0x17,
    0x91, 0xA1, 0x2A, 0xFB, 0xCB, 0xAC, 0x93, 0x62, 0x20, 0x46, 0xDD, 0x44,
    0xF0, 0x22, 0x58, 0x20, 0x45, 0x19, 0xE2, 0x57, 0x23, 0x6B, 0x2A, 0x0C,
    0xE2, 0x02, 0x3F, 0x09, 0x31, 0xF1, 0xF3, 0x86, 0xCA, 0x7A, 0xFD, 0xA6,
    0x4F, 0xCD, 0xE0, 0x10, 0x8C, 0x22, 0x4C, 0x51, 0xEA, 0xBF, 0x60, 0x72,
];

const RESPONDER_PRIVATE: [u8; 32] = [
    0x72, 0xCC, 0x47, 0x61, 0xDB, 0xD4, 0xC7, 0x8F, 0x75, 0x89, 0x31, 0xAA,
    0x58, 0x9D, 0x34, 0x8D, 0x1E, 0xF8, 0x74, 0xA7, 0xE3, 0x03, 0xED, 0xE2,
    0xF1, 0x40, 0xDC, 0xF3, 0xE6, 0xAA, 0x4A, 0xAC,
];

extern "C" {
    fn esp_fill_random(buffer: *mut c_void, length: usize);
}

#[derive(Clone, Copy)]
struct EspRng;

impl RngCore for EspRng {
    fn next_u32(&mut self) -> u32 {
        let mut bytes = [0u8; 4];
        self.fill_bytes(&mut bytes);
        u32::from_ne_bytes(bytes)
    }

    fn next_u64(&mut self) -> u64 {
        let mut bytes = [0u8; 8];
        self.fill_bytes(&mut bytes);
        u64::from_ne_bytes(bytes)
    }

    fn fill_bytes(&mut self, destination: &mut [u8]) {
        if destination.is_empty() {
            return;
        }

        unsafe {
            esp_fill_random(
                destination.as_mut_ptr().cast::<c_void>(),
                destination.len(),
            );
        }
    }

    fn try_fill_bytes(&mut self, destination: &mut [u8]) -> Result<(), RngError> {
        self.fill_bytes(destination);
        Ok(())
    }
}

impl CryptoRng for EspRng {}

type LakersCrypto = Crypto<EspRng>;
type LakersDone = EdhocResponderDone<LakersCrypto>;

enum InitiatorPhase {
    Start(EdhocInitiator<LakersCrypto>),
    WaitM2(EdhocInitiatorWaitM2<LakersCrypto>),
    Done(LakersDone),
    Failed,
}

struct InitiatorSession {
    phase: InitiatorPhase,
    exporter_context: [u8; EXPORTER_CONTEXT_LEN],
}

enum ResponderPhase {
    Start(EdhocResponder<LakersCrypto>),
    WaitM3(EdhocResponderWaitM3<LakersCrypto>),
    Done(LakersDone),
    Failed,
}

struct ResponderSession {
    phase: ResponderPhase,
    exporter_context: [u8; EXPORTER_CONTEXT_LEN],
}

enum Session {
    Empty,
    Initiator(InitiatorSession),
    Responder(ResponderSession),
}

struct SessionCell(UnsafeCell<Session>);

/* All calls are serialized by one ESP-IDF application task. */
unsafe impl Sync for SessionCell {}

static SESSION: SessionCell = SessionCell(UnsafeCell::new(Session::Empty));

fn parse_initiator_credential() -> Result<Credential, i32> {
    Credential::parse_ccs(&CRED_I).map_err(|_| STATUS_CREDENTIAL_ERROR)
}

fn parse_responder_credential() -> Result<Credential, i32> {
    Credential::parse_ccs(&CRED_R).map_err(|_| STATUS_CREDENTIAL_ERROR)
}

fn build_exporter_context(
    initiator_mac: &[u8],
    responder_mac: &[u8],
    channel: u8,
) -> [u8; EXPORTER_CONTEXT_LEN] {
    let mut context = [0u8; EXPORTER_CONTEXT_LEN];
    context[..EXPORTER_PURPOSE.len()].copy_from_slice(EXPORTER_PURPOSE);
    context[14..20].copy_from_slice(initiator_mac);
    context[20..26].copy_from_slice(responder_mac);
    context[26] = channel;
    context
}

fn create_session(role: u8, own_mac: &[u8], peer_mac: &[u8], channel: u8) -> Result<Session, i32> {
    if own_mac.len() != 6 || peer_mac.len() != 6 || channel == 0 || channel > 14 {
        return Err(STATUS_INVALID_ARGUMENT);
    }

    match role {
        ROLE_INITIATOR => {
            let context = build_exporter_context(own_mac, peer_mac, channel);
            let credential = parse_initiator_credential()?;
            let mut initiator = EdhocInitiator::new(
                LakersCrypto::new(EspRng),
                EDHOCMethod::StatStat,
                EDHOCSuite::CipherSuite2,
            );
            initiator.set_identity(INITIATOR_PRIVATE, credential);

            Ok(Session::Initiator(InitiatorSession {
                phase: InitiatorPhase::Start(initiator),
                exporter_context: context,
            }))
        }
        ROLE_RESPONDER => {
            let context = build_exporter_context(peer_mac, own_mac, channel);
            let credential = parse_responder_credential()?;
            let responder =
                EdhocResponder::new(LakersCrypto::new(EspRng), RESPONDER_PRIVATE, credential);

            Ok(Session::Responder(ResponderSession {
                phase: ResponderPhase::Start(responder),
                exporter_context: context,
            }))
        }
        _ => Err(STATUS_INVALID_ARGUMENT),
    }
}

unsafe fn prepare_output(
    output: *mut u8,
    output_capacity: usize,
    output_length: *mut usize,
) -> Result<(), i32> {
    if output.is_null() || output_length.is_null() {
        return Err(STATUS_INVALID_ARGUMENT);
    }

    *output_length = 0;

    if output_capacity < MAX_EDHOC_MESSAGE_LEN {
        *output_length = MAX_EDHOC_MESSAGE_LEN;
        return Err(STATUS_BUFFER_TOO_SMALL);
    }

    Ok(())
}

unsafe fn copy_output(data: &[u8], output: *mut u8, output_length: *mut usize) {
    ptr::copy_nonoverlapping(data.as_ptr(), output, data.len());
    *output_length = data.len();
}

unsafe fn input_slice<'a>(input: *const u8, input_length: usize) -> Result<&'a [u8], i32> {
    if input.is_null() || input_length == 0 || input_length > MAX_EDHOC_MESSAGE_LEN {
        return Err(STATUS_INVALID_ARGUMENT);
    }

    Ok(slice::from_raw_parts(input, input_length))
}

fn protocol_failed(session: &mut Session, role: u8) -> i32 {
    *session = match role {
        ROLE_INITIATOR => Session::Initiator(InitiatorSession {
            phase: InitiatorPhase::Failed,
            exporter_context: [0u8; EXPORTER_CONTEXT_LEN],
        }),
        ROLE_RESPONDER => Session::Responder(ResponderSession {
            phase: ResponderPhase::Failed,
            exporter_context: [0u8; EXPORTER_CONTEXT_LEN],
        }),
        _ => Session::Empty,
    };
    STATUS_PROTOCOL_ERROR
}

#[no_mangle]
pub extern "C" fn lakers_ffi_abi_version() -> u32 {
    ABI_VERSION
}

#[no_mangle]
pub extern "C" fn lakers_ffi_transform(input: u32) -> u32 {
    input.rotate_left(7) ^ TRANSFORM_MASK
}

#[no_mangle]
pub unsafe extern "C" fn lakers_edhoc_session_init(
    role: u8,
    own_mac: *const u8,
    peer_mac: *const u8,
    channel: u8,
) -> i32 {
    if own_mac.is_null() || peer_mac.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }

    let own = slice::from_raw_parts(own_mac, 6);
    let peer = slice::from_raw_parts(peer_mac, 6);

    match create_session(role, own, peer, channel) {
        Ok(new_session) => {
            *SESSION.0.get() = new_session;
            STATUS_OK
        }
        Err(status) => status,
    }
}

#[no_mangle]
pub unsafe extern "C" fn lakers_edhoc_make_message_1(
    output: *mut u8,
    output_capacity: usize,
    output_length: *mut usize,
) -> i32 {
    if let Err(status) = prepare_output(output, output_capacity, output_length) {
        return status;
    }

    let session = &mut *SESSION.0.get();
    let current = mem::replace(session, Session::Empty);

    match current {
        Session::Initiator(mut initiator_session) => {
            let phase = mem::replace(&mut initiator_session.phase, InitiatorPhase::Failed);
            match phase {
                InitiatorPhase::Start(initiator) => {
                    match initiator.prepare_message_1(None, &EadItems::new()) {
                        Ok((wait_m2, message_1)) => {
                            initiator_session.phase = InitiatorPhase::WaitM2(wait_m2);
                            copy_output(message_1.as_slice(), output, output_length);
                            *session = Session::Initiator(initiator_session);
                            STATUS_OK
                        }
                        Err(_) => protocol_failed(session, ROLE_INITIATOR),
                    }
                }
                other => {
                    initiator_session.phase = other;
                    *session = Session::Initiator(initiator_session);
                    STATUS_WRONG_STATE
                }
            }
        }
        other => {
            *session = other;
            STATUS_WRONG_STATE
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn lakers_edhoc_process_message_1(
    input: *const u8,
    input_length: usize,
    output: *mut u8,
    output_capacity: usize,
    output_length: *mut usize,
) -> i32 {
    if let Err(status) = prepare_output(output, output_capacity, output_length) {
        return status;
    }

    let input = match input_slice(input, input_length) {
        Ok(value) => value,
        Err(status) => return status,
    };
    let message_1 = match BufferMessage1::new_from_slice(input) {
        Ok(value) => value,
        Err(_) => return STATUS_INVALID_ARGUMENT,
    };

    let session = &mut *SESSION.0.get();
    let current = mem::replace(session, Session::Empty);

    match current {
        Session::Responder(mut responder_session) => {
            let phase = mem::replace(&mut responder_session.phase, ResponderPhase::Failed);
            match phase {
                ResponderPhase::Start(responder) => {
                    let result = responder
                        .process_message_1(&message_1)
                        .and_then(|(processed_m1, _c_i, _ead_1)| {
                            processed_m1.prepare_message_2(
                                CredentialTransfer::ByReference,
                                None,
                                &EadItems::new(),
                            )
                        });

                    match result {
                        Ok((wait_m3, message_2)) => {
                            responder_session.phase = ResponderPhase::WaitM3(wait_m3);
                            copy_output(message_2.as_slice(), output, output_length);
                            *session = Session::Responder(responder_session);
                            STATUS_OK
                        }
                        Err(_) => protocol_failed(session, ROLE_RESPONDER),
                    }
                }
                other => {
                    responder_session.phase = other;
                    *session = Session::Responder(responder_session);
                    STATUS_WRONG_STATE
                }
            }
        }
        other => {
            *session = other;
            STATUS_WRONG_STATE
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn lakers_edhoc_process_message_2(
    input: *const u8,
    input_length: usize,
    output: *mut u8,
    output_capacity: usize,
    output_length: *mut usize,
) -> i32 {
    if let Err(status) = prepare_output(output, output_capacity, output_length) {
        return status;
    }

    let input = match input_slice(input, input_length) {
        Ok(value) => value,
        Err(status) => return status,
    };
    let message_2 = match BufferMessage2::new_from_slice(input) {
        Ok(value) => value,
        Err(_) => return STATUS_INVALID_ARGUMENT,
    };

    let session = &mut *SESSION.0.get();
    let current = mem::replace(session, Session::Empty);

    match current {
        Session::Initiator(mut initiator_session) => {
            let phase = mem::replace(&mut initiator_session.phase, InitiatorPhase::Failed);
            match phase {
                InitiatorPhase::WaitM2(wait_m2) => {
                    let result = (|| {
                        let (processing_m2, _c_r, id_cred_r, _ead_2) =
                            wait_m2.parse_message_2(&message_2)?;
                        let expected_r = parse_responder_credential()
                            .map_err(|_| lakers::EDHOCError::UnexpectedCredential)?;
                        let valid_r = credential_check_or_fetch(Some(expected_r), id_cred_r)?;
                        let processed_m2 = processing_m2.verify_message_2(valid_r)?;
                        let (wait_m4, message_3, _prk_out) = processed_m2.prepare_message_3(
                            CredentialTransfer::ByReference,
                            &EadItems::new(),
                        )?;
                        let done = wait_m4.completed_without_message_4()?;
                        Ok::<_, lakers::EDHOCError>((done, message_3))
                    })();

                    match result {
                        Ok((done, message_3)) => {
                            initiator_session.phase = InitiatorPhase::Done(done);
                            copy_output(message_3.as_slice(), output, output_length);
                            *session = Session::Initiator(initiator_session);
                            STATUS_OK
                        }
                        Err(_) => protocol_failed(session, ROLE_INITIATOR),
                    }
                }
                other => {
                    initiator_session.phase = other;
                    *session = Session::Initiator(initiator_session);
                    STATUS_WRONG_STATE
                }
            }
        }
        other => {
            *session = other;
            STATUS_WRONG_STATE
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn lakers_edhoc_process_message_3(
    input: *const u8,
    input_length: usize,
) -> i32 {
    let input = match input_slice(input, input_length) {
        Ok(value) => value,
        Err(status) => return status,
    };
    let message_3 = match BufferMessage3::new_from_slice(input) {
        Ok(value) => value,
        Err(_) => return STATUS_INVALID_ARGUMENT,
    };

    let session = &mut *SESSION.0.get();
    let current = mem::replace(session, Session::Empty);

    match current {
        Session::Responder(mut responder_session) => {
            let phase = mem::replace(&mut responder_session.phase, ResponderPhase::Failed);
            match phase {
                ResponderPhase::WaitM3(wait_m3) => {
                    let result = (|| {
                        let (processing_m3, id_cred_i, _ead_3) =
                            wait_m3.parse_message_3(&message_3)?;
                        let expected_i = parse_initiator_credential()
                            .map_err(|_| lakers::EDHOCError::UnexpectedCredential)?;
                        let valid_i = credential_check_or_fetch(Some(expected_i), id_cred_i)?;
                        let (processed_m3, _prk_out) = processing_m3.verify_message_3(valid_i)?;
                        processed_m3.completed_without_message_4()
                    })();

                    match result {
                        Ok(done) => {
                            responder_session.phase = ResponderPhase::Done(done);
                            *session = Session::Responder(responder_session);
                            STATUS_OK
                        }
                        Err(_) => protocol_failed(session, ROLE_RESPONDER),
                    }
                }
                other => {
                    responder_session.phase = other;
                    *session = Session::Responder(responder_session);
                    STATUS_WRONG_STATE
                }
            }
        }
        other => {
            *session = other;
            STATUS_WRONG_STATE
        }
    }
}

#[no_mangle]
pub unsafe extern "C" fn lakers_edhoc_export_espnow_lmk(output_lmk: *mut u8) -> i32 {
    if output_lmk.is_null() {
        return STATUS_INVALID_ARGUMENT;
    }

    let output = slice::from_raw_parts_mut(output_lmk, ESPNOW_LMK_LEN);
    let session = &mut *SESSION.0.get();

    match session {
        Session::Initiator(initiator_session) => match &mut initiator_session.phase {
            InitiatorPhase::Done(done) => {
                done.edhoc_exporter(
                    EXPORTER_LABEL_ESPNOW_LMK,
                    &initiator_session.exporter_context,
                    output,
                );
                STATUS_OK
            }
            _ => STATUS_WRONG_STATE,
        },
        Session::Responder(responder_session) => match &mut responder_session.phase {
            ResponderPhase::Done(done) => {
                done.edhoc_exporter(
                    EXPORTER_LABEL_ESPNOW_LMK,
                    &responder_session.exporter_context,
                    output,
                );
                STATUS_OK
            }
            _ => STATUS_WRONG_STATE,
        },
        Session::Empty => STATUS_WRONG_STATE,
    }
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
