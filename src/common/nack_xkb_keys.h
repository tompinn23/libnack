#ifndef NACK_XKB_KEYS_H_INCLUDED
#define NACK_XKB_KEYS_H_INCLUDED

#include "../nack_internal.h"

#include <xkbcommon/xkbcommon.h>

/* Fills out[keycode] for every keycode the keymap defines, preferring the
 * layout-independent XKB key name and falling back to the keysym. */
void nack__xkb_build_keycodes(struct xkb_keymap *keymap, enum nack_key out[256]);

/* Best-effort physical key for a keysym. */
enum nack_key nack__key_from_keysym(uint32_t sym);

#endif /* NACK_XKB_KEYS_H_INCLUDED */
