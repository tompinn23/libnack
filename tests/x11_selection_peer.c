/*
 * A second X client, so the selection tests have someone to talk to.
 *
 * X11's INCR protocol only ever runs between two processes: a client reading
 * a selection it owns itself is served from its own copy and never reaches
 * the wire. Testing either direction therefore needs a peer, and a peer whose
 * behaviour is known - xclip will not do, because whether it uses INCR
 * depends on the server's maximum request length, and on a modern server with
 * BIG-REQUESTS a 300K selection goes across in a single property. This peer
 * always uses INCR, in small chunks, so the chunking loop runs many times.
 *
 *   x11_selection_peer own    take CLIPBOARD, print "ready", serve it once
 *   x11_selection_peer read   fetch CLIPBOARD, print "len=<n> sum=<hex>"
 *
 * Both modes exit non-zero if the exchange does not complete, and give up
 * after a fixed deadline rather than hanging a test run.
 */
#include <xcb/xcb.h>

#include <poll.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "x11_selection_payload.h"

/* Small on purpose: 300K of payload is then some seventy round trips. */
#define NACK_PEER_CHUNK 4096
#define NACK_PEER_DEADLINE 15.0

struct peer {
    xcb_connection_t *connection;
    xcb_window_t window;
    xcb_atom_t clipboard;
    xcb_atom_t utf8_string;
    xcb_atom_t targets;
    xcb_atom_t incr;
    xcb_atom_t property;
};

static double peer_now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}

static xcb_atom_t peer_atom(xcb_connection_t *connection, const char *name)
{
    xcb_intern_atom_cookie_t cookie =
        xcb_intern_atom(connection, 0, (uint16_t)strlen(name), name);
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(connection, cookie, NULL);
    xcb_atom_t atom = reply ? reply->atom : XCB_ATOM_NONE;

    free(reply);
    return atom;
}

/* Waits for one event, or returns NULL once the deadline has passed. */
static xcb_generic_event_t *peer_wait(struct peer *peer, double deadline)
{
    for (;;) {
        xcb_generic_event_t *event = xcb_poll_for_event(peer->connection);
        double remaining;
        struct pollfd pfd;

        if (event)
            return event;
        if (xcb_connection_has_error(peer->connection))
            return NULL;

        remaining = (deadline - peer_now()) * 1000.0;
        if (remaining <= 0.0)
            return NULL;
        if (remaining > 100.0)
            remaining = 100.0;

        xcb_flush(peer->connection);
        pfd.fd = xcb_get_file_descriptor(peer->connection);
        pfd.events = POLLIN;
        pfd.revents = 0;
        poll(&pfd, 1, (int)remaining);
    }
}

static int peer_open(struct peer *peer, uint32_t event_mask)
{
    const xcb_setup_t *setup;
    xcb_screen_t *screen;

    peer->connection = xcb_connect(NULL, NULL);
    if (!peer->connection || xcb_connection_has_error(peer->connection)) {
        fprintf(stderr, "peer: cannot open a display\n");
        return 0;
    }

    setup = xcb_get_setup(peer->connection);
    screen = xcb_setup_roots_iterator(setup).data;
    peer->window = xcb_generate_id(peer->connection);
    xcb_create_window(peer->connection, XCB_COPY_FROM_PARENT, peer->window,
                      screen->root, 0, 0, 1, 1, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_EVENT_MASK, &event_mask);

    peer->clipboard = peer_atom(peer->connection, "CLIPBOARD");
    peer->utf8_string = peer_atom(peer->connection, "UTF8_STRING");
    peer->targets = peer_atom(peer->connection, "TARGETS");
    peer->incr = peer_atom(peer->connection, "INCR");
    peer->property = peer_atom(peer->connection, "NACK_PEER_SELECTION");
    xcb_flush(peer->connection);
    return 1;
}

static void peer_notify(struct peer *peer,
                        const xcb_selection_request_event_t *request,
                        xcb_atom_t property)
{
    xcb_selection_notify_event_t reply;

    memset(&reply, 0, sizeof reply);
    reply.response_type = XCB_SELECTION_NOTIFY;
    reply.time = request->time;
    reply.requestor = request->requestor;
    reply.selection = request->selection;
    reply.target = request->target;
    reply.property = property;
    xcb_send_event(peer->connection, 0, request->requestor,
                   XCB_EVENT_MASK_NO_EVENT, (const char *)&reply);
    xcb_flush(peer->connection);
}

/*
 * Owns CLIPBOARD and serves it, always incrementally: the property is first
 * set to the total size with type INCR, and each chunk goes out in answer to
 * the requestor deleting the property, ending with a zero-length one.
 */
static int peer_own(void)
{
    struct peer peer;
    char *payload;
    size_t total = NACK_PEER_PAYLOAD_BYTES, sent = 0;
    bool sending = false, finished = false;
    xcb_window_t requestor = XCB_WINDOW_NONE;
    xcb_atom_t property = XCB_ATOM_NONE;
    double deadline;

    if (!peer_open(&peer, XCB_EVENT_MASK_PROPERTY_CHANGE))
        return 1;

    payload = (char *)malloc(total);
    if (!payload)
        return 1;
    nack__peer_payload(payload, total);
    total = strlen(payload);

    xcb_set_selection_owner(peer.connection, peer.window, peer.clipboard,
                            XCB_CURRENT_TIME);
    xcb_flush(peer.connection);
    {
        xcb_get_selection_owner_cookie_t cookie =
            xcb_get_selection_owner(peer.connection, peer.clipboard);
        xcb_get_selection_owner_reply_t *reply =
            xcb_get_selection_owner_reply(peer.connection, cookie, NULL);
        int owned = reply && reply->owner == peer.window;
        free(reply);
        if (!owned) {
            fprintf(stderr, "peer: could not take CLIPBOARD\n");
            free(payload);
            return 1;
        }
    }

    /* The test waits for this line rather than sleeping. */
    printf("ready\n");
    fflush(stdout);

    deadline = peer_now() + NACK_PEER_DEADLINE;
    while (!finished) {
        xcb_generic_event_t *event = peer_wait(&peer, deadline);
        uint8_t type;

        if (!event)
            break;
        type = event->response_type & 0x7F;

        if (type == XCB_SELECTION_REQUEST) {
            xcb_selection_request_event_t *request =
                (xcb_selection_request_event_t *)event;
            xcb_atom_t target_property = request->property != XCB_ATOM_NONE
                                             ? request->property
                                             : request->target;

            if (request->target == peer.targets) {
                xcb_atom_t offered[2] = { peer.targets, peer.utf8_string };
                xcb_change_property(peer.connection, XCB_PROP_MODE_REPLACE,
                                    request->requestor, target_property,
                                    XCB_ATOM_ATOM, 32, 2, offered);
                peer_notify(&peer, request, target_property);
            } else if (request->target == peer.utf8_string ||
                       request->target == XCB_ATOM_STRING) {
                uint32_t announced = (uint32_t)total;
                const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;

                /* Watch the requestor so its deletes are visible here. */
                xcb_change_window_attributes(peer.connection,
                                             request->requestor,
                                             XCB_CW_EVENT_MASK, &mask);
                xcb_change_property(peer.connection, XCB_PROP_MODE_REPLACE,
                                    request->requestor, target_property,
                                    peer.incr, 32, 1, &announced);
                requestor = request->requestor;
                property = target_property;
                sending = true;
                sent = 0;
                peer_notify(&peer, request, target_property);
            } else {
                peer_notify(&peer, request, XCB_ATOM_NONE);
            }
        } else if (type == XCB_PROPERTY_NOTIFY && sending) {
            xcb_property_notify_event_t *notify =
                (xcb_property_notify_event_t *)event;

            if (notify->window == requestor && notify->atom == property &&
                notify->state == XCB_PROPERTY_DELETE) {
                size_t chunk = total - sent;

                if (chunk > NACK_PEER_CHUNK)
                    chunk = NACK_PEER_CHUNK;
                xcb_change_property(peer.connection, XCB_PROP_MODE_APPEND,
                                    requestor, property, peer.utf8_string, 8,
                                    (uint32_t)chunk, payload + sent);
                xcb_flush(peer.connection);
                sent += chunk;
                if (chunk == 0) {   /* the zero-length chunk ends it */
                    sending = false;
                    finished = true;
                }
                deadline = peer_now() + NACK_PEER_DEADLINE;
            }
        } else if (type == XCB_SELECTION_CLEAR) {
            fprintf(stderr, "peer: lost CLIPBOARD before serving it\n");
            free(event);
            free(payload);
            return 1;
        }

        free(event);
    }

    xcb_flush(peer.connection);
    free(payload);
    if (!finished) {
        fprintf(stderr, "peer: timed out with %zu of %zu bytes sent\n", sent,
                total);
        return 1;
    }
    xcb_disconnect(peer.connection);
    return 0;
}

/* Appends a property's contents to a growing buffer. */
static int peer_append(char **buffer, size_t *length, size_t *capacity,
                       const void *data, size_t bytes)
{
    if (*length + bytes + 1 > *capacity) {
        size_t wanted = *capacity ? *capacity : 8192;
        char *grown;

        while (*length + bytes + 1 > wanted)
            wanted *= 2;
        grown = (char *)realloc(*buffer, wanted);
        if (!grown)
            return 0;
        *buffer = grown;
        *capacity = wanted;
    }
    memcpy(*buffer + *length, data, bytes);
    *length += bytes;
    return 1;
}

/*
 * Reads CLIPBOARD from whoever owns it, following an INCR transfer if that is
 * what is offered, and reports the size and checksum of what arrived.
 */
static int peer_read(void)
{
    struct peer peer;
    char *buffer = NULL;
    size_t length = 0, capacity = 0;
    double deadline;
    int incremental = 0, done = 0;

    if (!peer_open(&peer, XCB_EVENT_MASK_PROPERTY_CHANGE))
        return 1;

    xcb_delete_property(peer.connection, peer.window, peer.property);
    xcb_convert_selection(peer.connection, peer.window, peer.clipboard,
                          peer.utf8_string, peer.property, XCB_CURRENT_TIME);
    xcb_flush(peer.connection);

    deadline = peer_now() + NACK_PEER_DEADLINE;
    while (!done) {
        xcb_generic_event_t *event = peer_wait(&peer, deadline);
        uint8_t type;

        if (!event)
            break;
        type = event->response_type & 0x7F;

        if (type == XCB_SELECTION_NOTIFY) {
            xcb_selection_notify_event_t *notify =
                (xcb_selection_notify_event_t *)event;
            xcb_get_property_cookie_t cookie;
            xcb_get_property_reply_t *reply;

            if (notify->property == XCB_ATOM_NONE) {
                fprintf(stderr, "peer: selection refused\n");
                free(event);
                free(buffer);
                return 1;
            }

            cookie = xcb_get_property(peer.connection, 0, peer.window,
                                      peer.property, XCB_GET_PROPERTY_TYPE_ANY,
                                      0, UINT32_MAX / 4);
            reply = xcb_get_property_reply(peer.connection, cookie, NULL);
            if (!reply) {
                free(event);
                free(buffer);
                return 1;
            }

            if (reply->type == peer.incr) {
                /* Deleting the property asks the owner for the first chunk. */
                incremental = 1;
                xcb_delete_property(peer.connection, peer.window,
                                    peer.property);
                xcb_flush(peer.connection);
            } else {
                int bytes = xcb_get_property_value_length(reply);
                if (bytes > 0 &&
                    !peer_append(&buffer, &length, &capacity,
                                 xcb_get_property_value(reply),
                                 (size_t)bytes)) {
                    free(reply);
                    free(event);
                    free(buffer);
                    return 1;
                }
                done = 1;
            }
            free(reply);
            deadline = peer_now() + NACK_PEER_DEADLINE;
        } else if (type == XCB_PROPERTY_NOTIFY && incremental) {
            xcb_property_notify_event_t *notify =
                (xcb_property_notify_event_t *)event;
            xcb_get_property_cookie_t cookie;
            xcb_get_property_reply_t *reply;
            int bytes;

            if (notify->window != peer.window ||
                notify->atom != peer.property ||
                notify->state != XCB_PROPERTY_NEW_VALUE) {
                free(event);
                continue;
            }

            cookie = xcb_get_property(peer.connection, 0, peer.window,
                                      peer.property, XCB_GET_PROPERTY_TYPE_ANY,
                                      0, UINT32_MAX / 4);
            reply = xcb_get_property_reply(peer.connection, cookie, NULL);
            if (!reply) {
                free(event);
                free(buffer);
                return 1;
            }

            bytes = xcb_get_property_value_length(reply);
            if (bytes == 0) {
                done = 1;
            } else if (!peer_append(&buffer, &length, &capacity,
                                    xcb_get_property_value(reply),
                                    (size_t)bytes)) {
                free(reply);
                free(event);
                free(buffer);
                return 1;
            }
            free(reply);
            xcb_delete_property(peer.connection, peer.window, peer.property);
            xcb_flush(peer.connection);
            deadline = peer_now() + NACK_PEER_DEADLINE;
        }

        free(event);
    }

    if (!done) {
        fprintf(stderr, "peer: timed out after %zu bytes\n", length);
        free(buffer);
        return 1;
    }

    printf("len=%zu sum=%08x incr=%d\n", length,
           nack__peer_checksum(buffer ? buffer : "", length), incremental);
    fflush(stdout);
    free(buffer);
    xcb_disconnect(peer.connection);
    return 0;
}

int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "own") == 0)
        return peer_own();
    if (argc == 2 && strcmp(argv[1], "read") == 0)
        return peer_read();
    fprintf(stderr, "usage: %s own|read\n", argv[0]);
    return 2;
}
