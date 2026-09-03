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

#if !defined(_WIN32)
#  include <unistd.h>
#endif

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

#if !defined(_WIN32)

/*
 * A descriptor closed on the way out. Wayland hands selection data over a
 * pipe whose read end is ours to close however the read turns out.
 */
class unique_fd {
public:
    explicit unique_fd(int fd = -1) noexcept : descriptor(fd) {}
    ~unique_fd() { if (descriptor >= 0) ::close(descriptor); }

    unique_fd(unique_fd &&other) noexcept : descriptor(other.descriptor)
    {
        other.descriptor = -1;
    }

    unique_fd(const unique_fd &) = delete;
    unique_fd &operator=(const unique_fd &) = delete;
    unique_fd &operator=(unique_fd &&) = delete;

    int get() const noexcept { return descriptor; }
    explicit operator bool() const noexcept { return descriptor >= 0; }

private:
    int descriptor;
};

#endif /* !_WIN32 */

}   /* namespace nack */

#endif /* NACK_SCOPED_H_INCLUDED */
