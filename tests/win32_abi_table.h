/*
 * Emits the ABI fact table for one side of the comparison. Included by
 * win32_abi_sdk.c (compiled against <windows.h>) and win32_abi_nack.c
 * (compiled against libnack's hand-rolled declarations).
 */
#include <stddef.h>
#include <stdint.h>

#define NACK_ABI_VALUE(expr) (uint64_t)(expr),

const uint64_t NACK_ABI_TABLE[] = {
#include "win32_abi_values.h"
};

const unsigned NACK_ABI_COUNT =
    (unsigned)(sizeof NACK_ABI_TABLE / sizeof NACK_ABI_TABLE[0]);
