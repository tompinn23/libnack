/*
 * Which windowing system is underneath.
 *
 * Its own header because it is the one part of the window layer that outlives
 * the C/C++ split: nack_window.h is C++ (nack_window holds a
 * std::string), but a C caller still has to be able to name a backend, and a
 * second copy of the enumeration would be a second thing to keep correct.
 */
#ifndef NACK_BACKEND_ID_H_INCLUDED
#define NACK_BACKEND_ID_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

enum nack_backend {
    NACK_BACKEND_NONE = 0,
    NACK_BACKEND_WIN32,
    NACK_BACKEND_COCOA,
    NACK_BACKEND_WAYLAND,
    NACK_BACKEND_X11
};

#ifdef __cplusplus
}
#endif

#endif /* NACK_BACKEND_ID_H_INCLUDED */
