#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "edhoc_transport.h"

/*
 * Milestone 4 uses real EDHOC bytes from RFC 9529 Section 3:
 * "Authentication with Static DH, CCS Identified by 'kid'".
 *
 * This is still not full live cryptographic processing. It is a transport proof
 * that actual EDHOC message_1/message_2/message_3 byte strings can be carried
 * over the ESP-NOW EDHOC transport from Milestone 3.
 */

#define EDHOC_TRACE_RFC9529_SECTION "RFC9529 Section 3 Static DH CCS/kid"

bool edhoc_trace_get_message(
    edhoc_transport_msg_type_t type,
    const uint8_t **data,
    size_t *len
);

bool edhoc_trace_verify_message(
    edhoc_transport_msg_type_t type,
    const uint8_t *data,
    size_t len
);

void edhoc_trace_log_message(
    const char *prefix,
    edhoc_transport_msg_type_t type,
    const uint8_t *data,
    size_t len
);
