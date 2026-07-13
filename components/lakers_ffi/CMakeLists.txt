idf_component_register(
    SRCS "lakers_ffi_component.c"
    INCLUDE_DIRS "include"
)

if(NOT IDF_TARGET STREQUAL "esp32")
    message(FATAL_ERROR
        "Milestone 8 currently targets the original Xtensa ESP32 only; IDF_TARGET=${IDF_TARGET}"
    )
endif()

find_program(CARGO_EXECUTABLE cargo REQUIRED)

set(LAKERS_RUST_CRATE_DIR "${CMAKE_CURRENT_LIST_DIR}/rust")
set(LAKERS_RUST_TARGET "xtensa-esp32-none-elf")
set(LAKERS_RUST_TARGET_DIR "${CMAKE_BINARY_DIR}/lakers-rust-target")
set(LAKERS_RUST_LIBRARY
    "${LAKERS_RUST_TARGET_DIR}/${LAKERS_RUST_TARGET}/release/liblakers_esp32_ffi.a"
)

file(GLOB_RECURSE LAKERS_RUST_SOURCES CONFIGURE_DEPENDS
    "${LAKERS_RUST_CRATE_DIR}/src/*.rs"
)

add_custom_command(
    OUTPUT "${LAKERS_RUST_LIBRARY}"
    COMMAND "${CMAKE_COMMAND}" -E env
            "CARGO_TARGET_DIR=${LAKERS_RUST_TARGET_DIR}"
            "${CARGO_EXECUTABLE}" +esp build
            --manifest-path "${LAKERS_RUST_CRATE_DIR}/Cargo.toml"
            --target "${LAKERS_RUST_TARGET}"
            --release
    DEPENDS
        "${LAKERS_RUST_CRATE_DIR}/Cargo.toml"
        ${LAKERS_RUST_SOURCES}
    WORKING_DIRECTORY "${LAKERS_RUST_CRATE_DIR}"
    COMMENT "Building Milestone 8 Rust static library for ${LAKERS_RUST_TARGET}"
    VERBATIM
)

add_custom_target(lakers_ffi_rust_build DEPENDS "${LAKERS_RUST_LIBRARY}")
add_dependencies(${COMPONENT_LIB} lakers_ffi_rust_build)
target_link_libraries(${COMPONENT_LIB} INTERFACE "${LAKERS_RUST_LIBRARY}")
