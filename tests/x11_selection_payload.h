/*
 * The bytes the selection tests move between two processes, and a checksum
 * over them.
 *
 * Both sides generate the payload rather than passing it around, so a
 * transfer is compared against something neither process received from the
 * other. The pattern is deliberately unlike anything else the smoke test puts
 * on the clipboard: if a read is quietly served from libnack's own copy
 * instead of going over the wire, the contents will not match and the test
 * will say so.
 */
#ifndef NACK_X11_SELECTION_PAYLOAD_H_INCLUDED
#define NACK_X11_SELECTION_PAYLOAD_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

/* Comfortably more than one chunk of any transfer either side makes. */
#define NACK_PEER_PAYLOAD_BYTES (300 * 1024)

/* Fills `out` with `bytes` characters and a terminating NUL. */
static void peer_payload(char *out, size_t bytes)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
    size_t i;

    for (i = 0; i + 1 < bytes; ++i)
        out[i] = alphabet[(i * 7 + 3) % (sizeof alphabet - 1)];
    if (bytes)
        out[bytes - 1] = '\0';
}

/* FNV-1a, so a mismatch can be reported as one number rather than a diff. */
static uint32_t peer_checksum(const char *data, size_t length)
{
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < length; ++i) {
        hash ^= (uint8_t)data[i];
        hash *= 16777619u;
    }
    return hash;
}

#endif /* NACK_X11_SELECTION_PAYLOAD_H_INCLUDED */
