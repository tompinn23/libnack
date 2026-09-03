/*
 * Small RAII helpers for the backends.
 *
 * The platform APIs libnack sits on are all C: xcb, xkbcommon, EGL, Win32.
 * Their allocations come back as raw pointers to be released with a matching
 * call, and a function that acquires three of them and can fail in five
 * places is where leaks and double frees come from - not because the cleanup
 * is hard to write, but because it has to be written correctly on every exit.
 * This type writes it once.
 *
 * C++ only, and internal. The public headers stay as they are: <nack/nack.h>
 * is C, and <nack/nack.hpp> is a wrapper a user includes, not this.
 */
#ifndef NACK_SCOPED_H_INCLUDED
#define NACK_SCOPED_H_INCLUDED

#include <cstdlib>
#include <memory>

namespace nack {

/*
 * A pointer released with free(): malloc'd buffers, and every xcb reply and
 * event, which are documented to be freed that way.
 */
struct c_free {
    void operator()(void *pointer) const noexcept { std::free(pointer); }
};

template <class T>
using c_ptr = std::unique_ptr<T, c_free>;

}   /* namespace nack */

#endif /* NACK_SCOPED_H_INCLUDED */
