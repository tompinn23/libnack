/*
 * Wayland clipboard and primary selection.
 *
 * Wayland moves selection data over pipes: an offer names its MIME types, the
 * receiver hands the source a write end and reads until EOF, and a source we
 * own must service send requests as they arrive.
 */
#include "nack_wayland.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <unistd.h>

#define NACK_WL_MIME_UTF8  "text/plain;charset=utf-8"
#define NACK_WL_MIME_PLAIN "text/plain"
#define NACK_WL_READ_TIMEOUT_MS 2000

/* ------------------------------------------------------------------ */
/* Reading an offer                                                   */
/* ------------------------------------------------------------------ */

/*
 * Drains the read end of a selection pipe. The compositor may hand us the
 * data in arbitrarily many chunks, and a source that never writes must not
 * hang the caller, so the read is non-blocking with a deadline.
 */
static char *nack__wl_read_pipe(int fd)
{
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    size_t capacity = 4096, length = 0;
    char *buffer = (char *)malloc(capacity);
    if (!buffer) {
        close(fd);
        return NULL;
    }

    double deadline = nack__win_time_seconds() + NACK_WL_READ_TIMEOUT_MS / 1000.0;
    for (;;) {
        if (length + 1 >= capacity) {
            size_t grown_size = capacity * 2;
            char *grown = (char *)realloc(buffer, grown_size);
            if (!grown) {
                free(buffer);
                close(fd);
                return NULL;
            }
            buffer = grown;
            capacity = grown_size;
        }

        ssize_t n = read(fd, buffer + length, capacity - length - 1);
        if (n > 0) {
            length += (size_t)n;
            continue;
        }
        if (n == 0)
            break;                      /* writer closed: transfer complete */
        if (errno == EINTR)
            continue;
        if (errno != EAGAIN && errno != EWOULDBLOCK)
            break;

        double remaining = deadline - nack__win_time_seconds();
        if (remaining <= 0.0)
            break;
        struct pollfd pfd = { fd, POLLIN, 0 };
        poll(&pfd, 1, (int)(remaining * 1000.0));
    }

    close(fd);
    buffer[length] = '\0';
    return buffer;
}

/* ------------------------------------------------------------------ */
/* wl_data_offer                                                      */
/* ------------------------------------------------------------------ */

static void data_offer_offer(void *data, struct wl_data_offer *offer,
                             const char *mime_type)
{
    (void)data; (void)offer;
    if (strcmp(mime_type, NACK_WL_MIME_UTF8) == 0 ||
        strcmp(mime_type, NACK_WL_MIME_PLAIN) == 0 ||
        strcmp(mime_type, "UTF8_STRING") == 0 ||
        strcmp(mime_type, "STRING") == 0 ||
        strcmp(mime_type, "TEXT") == 0)
        nack__wl.offer_has_text = true;
}

static void data_offer_source_actions(void *data, struct wl_data_offer *offer,
                                      uint32_t source_actions)
{
    (void)data; (void)offer; (void)source_actions;
}

static void data_offer_action(void *data, struct wl_data_offer *offer, uint32_t action)
{
    (void)data; (void)offer; (void)action;
}

static const struct wl_data_offer_listener nack__wl_data_offer_listener = {
    .offer = data_offer_offer,
    .source_actions = data_offer_source_actions,
    .action = data_offer_action,
};

static void data_device_data_offer(void *data, struct wl_data_device *device,
                                   struct wl_data_offer *offer)
{
    (void)data; (void)device;
    nack__wl.offer_has_text = false;
    wl_data_offer_add_listener(offer, &nack__wl_data_offer_listener, NULL);
}

static void data_device_selection(void *data, struct wl_data_device *device,
                                  struct wl_data_offer *offer)
{
    (void)data; (void)device;
    if (nack__wl.pending_offer)
        wl_data_offer_destroy(nack__wl.pending_offer);
    nack__wl.pending_offer = offer;
    if (!offer)
        nack__wl.offer_has_text = false;
}

static void data_device_enter(void *data, struct wl_data_device *device, uint32_t serial,
                              struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y,
                              struct wl_data_offer *offer)
{
    (void)data; (void)device; (void)serial; (void)surface; (void)x; (void)y;
    /* Drag and drop is not implemented; decline the offer politely. */
    if (offer)
        wl_data_offer_accept(offer, serial, NULL);
}

static void data_device_leave(void *data, struct wl_data_device *device)
{
    (void)data; (void)device;
}

static void data_device_motion(void *data, struct wl_data_device *device, uint32_t time,
                               wl_fixed_t x, wl_fixed_t y)
{
    (void)data; (void)device; (void)time; (void)x; (void)y;
}

static void data_device_drop(void *data, struct wl_data_device *device)
{
    (void)data; (void)device;
}

static const struct wl_data_device_listener nack__wl_data_device_listener = {
    .data_offer = data_device_data_offer,
    .enter = data_device_enter,
    .leave = data_device_leave,
    .motion = data_device_motion,
    .drop = data_device_drop,
    .selection = data_device_selection,
};

/* ------------------------------------------------------------------ */
/* wl_data_source (we own the selection)                              */
/* ------------------------------------------------------------------ */

static void data_source_target(void *data, struct wl_data_source *source,
                               const char *mime_type)
{
    (void)data; (void)source; (void)mime_type;
}

static void data_source_send(void *data, struct wl_data_source *source,
                             const char *mime_type, int32_t fd)
{
    (void)source; (void)mime_type;
    const char *text = (const char *)data;
    if (!text) {
        close(fd);
        return;
    }

    /* Write the whole payload; a large paste will not fit one pipe buffer,
     * and SIGPIPE must not reach the application if the reader gives up. */
    size_t remaining = strlen(text);
    const char *cursor = text;
    while (remaining > 0) {
        ssize_t n = write(fd, cursor, remaining);
        if (n > 0) {
            cursor += n;
            remaining -= (size_t)n;
            continue;
        }
        if (n < 0 && errno == EINTR)
            continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd pfd = { fd, POLLOUT, 0 };
            if (poll(&pfd, 1, NACK_WL_READ_TIMEOUT_MS) <= 0)
                break;
            continue;
        }
        break;   /* reader closed early */
    }
    close(fd);
}

static void data_source_cancelled(void *data, struct wl_data_source *source)
{
    (void)data;
    if (nack__wl.data_source == source) {
        wl_data_source_destroy(source);
        nack__wl.data_source = NULL;
        free(nack__wl.clipboard_offered);
        nack__wl.clipboard_offered = NULL;
    }
}

static void data_source_dnd_drop_performed(void *data, struct wl_data_source *source)
{
    (void)data; (void)source;
}

static void data_source_dnd_finished(void *data, struct wl_data_source *source)
{
    (void)data; (void)source;
}

static void data_source_action(void *data, struct wl_data_source *source,
                               uint32_t action)
{
    (void)data; (void)source; (void)action;
}

static const struct wl_data_source_listener nack__wl_data_source_listener = {
    .target = data_source_target,
    .send = data_source_send,
    .cancelled = data_source_cancelled,
    .dnd_drop_performed = data_source_dnd_drop_performed,
    .dnd_finished = data_source_dnd_finished,
    .action = data_source_action,
};

/* ------------------------------------------------------------------ */
/* Primary selection                                                  */
/* ------------------------------------------------------------------ */

static void primary_offer_offer(void *data,
                                struct zwp_primary_selection_offer_v1 *offer,
                                const char *mime_type)
{
    (void)data; (void)offer;
    if (strcmp(mime_type, NACK_WL_MIME_UTF8) == 0 ||
        strcmp(mime_type, NACK_WL_MIME_PLAIN) == 0 ||
        strcmp(mime_type, "UTF8_STRING") == 0 ||
        strcmp(mime_type, "STRING") == 0)
        nack__wl.primary_offer_has_text = true;
}

static const struct zwp_primary_selection_offer_v1_listener nack__wl_primary_offer_listener = {
    .offer = primary_offer_offer,
};

static void primary_device_data_offer(void *data,
                                      struct zwp_primary_selection_device_v1 *device,
                                      struct zwp_primary_selection_offer_v1 *offer)
{
    (void)data; (void)device;
    nack__wl.primary_offer_has_text = false;
    zwp_primary_selection_offer_v1_add_listener(offer,
                                                &nack__wl_primary_offer_listener, NULL);
}

static void primary_device_selection(void *data,
                                     struct zwp_primary_selection_device_v1 *device,
                                     struct zwp_primary_selection_offer_v1 *offer)
{
    (void)data; (void)device;
    if (nack__wl.pending_primary_offer)
        zwp_primary_selection_offer_v1_destroy(nack__wl.pending_primary_offer);
    nack__wl.pending_primary_offer = offer;
    if (!offer)
        nack__wl.primary_offer_has_text = false;
}

static const struct zwp_primary_selection_device_v1_listener nack__wl_primary_device_listener = {
    .data_offer = primary_device_data_offer,
    .selection = primary_device_selection,
};

static void primary_source_send(void *data,
                                struct zwp_primary_selection_source_v1 *source,
                                const char *mime_type, int32_t fd)
{
    (void)source;
    data_source_send(data, NULL, mime_type, fd);
}

static void primary_source_cancelled(void *data,
                                     struct zwp_primary_selection_source_v1 *source)
{
    (void)data;
    if (nack__wl.primary_source == source) {
        zwp_primary_selection_source_v1_destroy(source);
        nack__wl.primary_source = NULL;
        free(nack__wl.primary_offered);
        nack__wl.primary_offered = NULL;
    }
}

static const struct zwp_primary_selection_source_v1_listener nack__wl_primary_source_listener = {
    .send = primary_source_send,
    .cancelled = primary_source_cancelled,
};

/* ------------------------------------------------------------------ */
/* Public entry points                                                */
/* ------------------------------------------------------------------ */

void nack__wl_data_device_bind(void)
{
    if (nack__wl.data_device_manager && nack__wl.seat && !nack__wl.data_device) {
        nack__wl.data_device = wl_data_device_manager_get_data_device(
            nack__wl.data_device_manager, nack__wl.seat);
        wl_data_device_add_listener(nack__wl.data_device,
                                    &nack__wl_data_device_listener, NULL);
    }
    if (nack__wl.primary_manager && nack__wl.seat && !nack__wl.primary_device) {
        nack__wl.primary_device =
            zwp_primary_selection_device_manager_v1_get_device(
                nack__wl.primary_manager, nack__wl.seat);
        zwp_primary_selection_device_v1_add_listener(
            nack__wl.primary_device, &nack__wl_primary_device_listener, NULL);
    }
}

bool nack__wl_clipboard_set(const char *utf8)
{
    nack__wl_data_device_bind();   /* the seat may have appeared since init */

    if (!nack__wl.data_device_manager)
        return nack__fail(NACK_ERROR_UNSUPPORTED,
                          "compositor does not offer wl_data_device_manager");
    if (!nack__wl.seat)
        return nack__fail(NACK_ERROR_UNSUPPORTED,
                          "compositor has no wl_seat; selections need one");
    if (!nack__wl.data_device)
        return nack__fail(NACK_ERROR_PLATFORM, "no wl_data_device for the seat");

    char *copy = nack__strdup(utf8);
    if (!copy)
        return false;

    if (nack__wl.data_source)
        wl_data_source_destroy(nack__wl.data_source);
    free(nack__wl.clipboard_offered);
    nack__wl.clipboard_offered = copy;

    nack__wl.data_source = wl_data_device_manager_create_data_source(
        nack__wl.data_device_manager);
    if (!nack__wl.data_source)
        return nack__fail(NACK_ERROR_PLATFORM, "create_data_source failed");

    wl_data_source_add_listener(nack__wl.data_source, &nack__wl_data_source_listener,
                                nack__wl.clipboard_offered);
    wl_data_source_offer(nack__wl.data_source, NACK_WL_MIME_UTF8);
    wl_data_source_offer(nack__wl.data_source, NACK_WL_MIME_PLAIN);
    wl_data_source_offer(nack__wl.data_source, "UTF8_STRING");
    wl_data_source_offer(nack__wl.data_source, "STRING");
    wl_data_source_offer(nack__wl.data_source, "TEXT");

    wl_data_device_set_selection(nack__wl.data_device, nack__wl.data_source,
                                 nack__wl.last_serial);
    wl_display_flush(nack__wl.display);
    return true;
}

const char *nack__wl_clipboard_get(void)
{
    nack__wl_data_device_bind();

    /* We own the selection: no round trip needed. */
    if (nack__wl.data_source && nack__wl.clipboard_offered) {
        free(nack__wl.clipboard_text);
        nack__wl.clipboard_text = nack__strdup(nack__wl.clipboard_offered);
        return nack__wl.clipboard_text;
    }

    if (!nack__wl.pending_offer || !nack__wl.offer_has_text)
        return NULL;

    int fds[2];
    if (pipe(fds) != 0)
        return NULL;

    wl_data_offer_receive(nack__wl.pending_offer, NACK_WL_MIME_UTF8, fds[1]);
    close(fds[1]);
    /* The compositor only acts on the receive once it is flushed. */
    wl_display_roundtrip(nack__wl.display);

    free(nack__wl.clipboard_text);
    nack__wl.clipboard_text = nack__wl_read_pipe(fds[0]);
    return nack__wl.clipboard_text;
}

bool nack__wl_primary_set(const char *utf8)
{
    nack__wl_data_device_bind();
    if (!nack__wl.primary_manager || !nack__wl.primary_device)
        return false;

    char *copy = nack__strdup(utf8);
    if (!copy)
        return false;

    if (nack__wl.primary_source)
        zwp_primary_selection_source_v1_destroy(nack__wl.primary_source);
    free(nack__wl.primary_offered);
    nack__wl.primary_offered = copy;

    nack__wl.primary_source =
        zwp_primary_selection_device_manager_v1_create_source(nack__wl.primary_manager);
    if (!nack__wl.primary_source)
        return false;

    zwp_primary_selection_source_v1_add_listener(
        nack__wl.primary_source, &nack__wl_primary_source_listener,
        nack__wl.primary_offered);
    zwp_primary_selection_source_v1_offer(nack__wl.primary_source, NACK_WL_MIME_UTF8);
    zwp_primary_selection_source_v1_offer(nack__wl.primary_source, NACK_WL_MIME_PLAIN);
    zwp_primary_selection_source_v1_offer(nack__wl.primary_source, "UTF8_STRING");
    zwp_primary_selection_source_v1_offer(nack__wl.primary_source, "STRING");

    zwp_primary_selection_device_v1_set_selection(nack__wl.primary_device,
                                                  nack__wl.primary_source,
                                                  nack__wl.last_serial);
    wl_display_flush(nack__wl.display);
    return true;
}

const char *nack__wl_primary_get(void)
{
    if (nack__wl.primary_source && nack__wl.primary_offered) {
        free(nack__wl.primary_text);
        nack__wl.primary_text = nack__strdup(nack__wl.primary_offered);
        return nack__wl.primary_text;
    }

    if (!nack__wl.pending_primary_offer || !nack__wl.primary_offer_has_text)
        return NULL;

    int fds[2];
    if (pipe(fds) != 0)
        return NULL;

    zwp_primary_selection_offer_v1_receive(nack__wl.pending_primary_offer,
                                           NACK_WL_MIME_UTF8, fds[1]);
    close(fds[1]);
    wl_display_roundtrip(nack__wl.display);

    free(nack__wl.primary_text);
    nack__wl.primary_text = nack__wl_read_pipe(fds[0]);
    return nack__wl.primary_text;
}

void nack__wl_clipboard_shutdown(void)
{
    if (nack__wl.data_source)    wl_data_source_destroy(nack__wl.data_source);
    if (nack__wl.primary_source)
        zwp_primary_selection_source_v1_destroy(nack__wl.primary_source);
    if (nack__wl.pending_offer)  wl_data_offer_destroy(nack__wl.pending_offer);
    if (nack__wl.pending_primary_offer)
        zwp_primary_selection_offer_v1_destroy(nack__wl.pending_primary_offer);
    if (nack__wl.data_device)    wl_data_device_destroy(nack__wl.data_device);
    if (nack__wl.primary_device)
        zwp_primary_selection_device_v1_destroy(nack__wl.primary_device);
    if (nack__wl.data_device_manager)
        wl_data_device_manager_destroy(nack__wl.data_device_manager);
    if (nack__wl.primary_manager)
        zwp_primary_selection_device_manager_v1_destroy(nack__wl.primary_manager);

    free(nack__wl.clipboard_text);
    free(nack__wl.primary_text);
    free(nack__wl.clipboard_offered);
    free(nack__wl.primary_offered);

    nack__wl.data_source = NULL;
    nack__wl.primary_source = NULL;
    nack__wl.pending_offer = NULL;
    nack__wl.pending_primary_offer = NULL;
    nack__wl.data_device = NULL;
    nack__wl.primary_device = NULL;
    nack__wl.data_device_manager = NULL;
    nack__wl.primary_manager = NULL;
    nack__wl.clipboard_text = NULL;
    nack__wl.primary_text = NULL;
    nack__wl.clipboard_offered = NULL;
    nack__wl.primary_offered = NULL;
}
