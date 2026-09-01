/*
 * Compares libnack's hand-rolled Win32 declarations against the real SDK.
 *
 * Every structure size, member offset and constant the backend depends on is
 * evaluated on both sides from the same expression list, so a disagreement
 * about any of them fails here rather than corrupting memory at run time.
 */
#include <stdio.h>
#include <stdint.h>

extern const uint64_t nack_abi_table_sdk[];
extern const uint64_t nack_abi_table_nack[];
extern const unsigned nack_abi_count_sdk;
extern const unsigned nack_abi_count_nack;

/* Names for the mismatching entry, so a failure says which fact is wrong. */
#define NACK_ABI_VALUE(expr) #expr,
static const char *const nack_abi_names[] = {
#include "win32_abi_values.h"
};

int main(void)
{
    unsigned i, failures = 0;

    if (nack_abi_count_sdk != nack_abi_count_nack) {
        fprintf(stderr, "ABI table length differs: sdk %u, nack %u\n",
                nack_abi_count_sdk, nack_abi_count_nack);
        return 1;
    }

    for (i = 0; i < nack_abi_count_sdk; ++i) {
        if (nack_abi_table_sdk[i] == nack_abi_table_nack[i])
            continue;
        fprintf(stderr,
                "mismatch: %s\n    sdk  = %llu (0x%llX)\n    nack = %llu (0x%llX)\n",
                nack_abi_names[i],
                (unsigned long long)nack_abi_table_sdk[i],
                (unsigned long long)nack_abi_table_sdk[i],
                (unsigned long long)nack_abi_table_nack[i],
                (unsigned long long)nack_abi_table_nack[i]);
        failures++;
    }

    if (failures) {
        fprintf(stderr, "\n%u of %u Win32 ABI facts disagree with the SDK\n",
                failures, nack_abi_count_sdk);
        return 1;
    }

    printf("all %u Win32 ABI facts match the SDK\n", nack_abi_count_sdk);
    return 0;
}
