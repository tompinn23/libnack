/*
 * The boundary between the C API and the C++ inside it.
 *
 * The internals use std::vector and std::string, which report a failed
 * allocation by throwing. The public API is extern "C" and reports it by
 * returning false or NULL, and an exception unwinding into a C caller ends
 * the process instead. Every entry point that can allocate goes through here,
 * so a failure that used to be a clean `false` still is one.
 *
 * This is not a general catch-all: it is the translation layer for allocation
 * failure, and it deliberately catches std::exception rather than only
 * std::bad_alloc, because a container refuses an impossible size with
 * std::length_error rather than by trying to allocate it.
 */
#ifndef NACK_GUARD_H_INCLUDED
#define NACK_GUARD_H_INCLUDED

#include "nack_console_internal.h"

#include <exception>
#include <utility>

namespace nack {

/*
 * Runs body(), and on failure records the reason and answers with `on_error`
 * - the same value the hand-written out-of-memory paths used to return.
 */
template <class Body, class Result>
Result guarded(const char *what, Body &&body, Result on_error)
{
    try {
        return std::forward<Body>(body)();
    } catch (const std::exception &failure) {
        nack__error("%s: %s", what, failure.what());
        return on_error;
    }
}

/* The same, for an entry point that returns nothing. */
template <class Body>
void guarded_void(const char *what, Body &&body)
{
    try {
        std::forward<Body>(body)();
    } catch (const std::exception &failure) {
        nack__error("%s: %s", what, failure.what());
    }
}

}   /* namespace nack */

#endif /* NACK_GUARD_H_INCLUDED */
