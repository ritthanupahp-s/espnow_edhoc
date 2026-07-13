#![no_std]

use core::panic::PanicInfo;

const ABI_VERSION: u32 = 0x0008_0001;
const TRANSFORM_MASK: u32 = 0xED0C_0008;

#[no_mangle]
pub extern "C" fn lakers_ffi_abi_version() -> u32 {
    ABI_VERSION
}

#[no_mangle]
pub extern "C" fn lakers_ffi_transform(input: u32) -> u32 {
    input.rotate_left(7) ^ TRANSFORM_MASK
}

#[panic_handler]
fn panic(_info: &PanicInfo<'_>) -> ! {
    loop {
        core::hint::spin_loop();
    }
}
