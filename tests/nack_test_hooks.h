/*
 * The internals the tests reach into, declared for C.
 *
 * The library's own internal headers are C++ now - their structs hold
 * std::vector and std::string. There is no public C API left to prove
 * usable from C, but image_test.c and the Win32 ABI check are still C on
 * purpose (the ABI check specifically has to be, since it is comparing
 * struct layouts a C++ name-mangled declaration could not stand in for), so
 * this still exists for the two of them. They only ever call these
 * functions; none of them touches a struct field.
 */
#ifndef NACK_TEST_HOOKS_H_INCLUDED
#define NACK_TEST_HOOKS_H_INCLUDED

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* src/console/nack_image.h */
uint8_t *nack__image_decode(const void *data, size_t size, int *width,
                            int *height, const char **error);
void nack__image_free(uint8_t *pixels);

#ifdef __cplusplus
}   /* extern "C" */
#endif

#endif /* NACK_TEST_HOOKS_H_INCLUDED */
