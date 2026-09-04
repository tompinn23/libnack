/*
 * Which windowing system is underneath.
 *
 * Its own header on the theory that it might be shared more widely than the
 * rest of the window layer; nothing still-C actually reaches in here, so
 * unlike nack_image.h this carries no extern "C".
 */
#ifndef NACK_BACKEND_ID_H_INCLUDED
#define NACK_BACKEND_ID_H_INCLUDED

enum nack_backend {
    NACK_BACKEND_NONE = 0,
    NACK_BACKEND_WIN32,
    NACK_BACKEND_COCOA,
    NACK_BACKEND_WAYLAND,
    NACK_BACKEND_X11
};

#endif /* NACK_BACKEND_ID_H_INCLUDED */
