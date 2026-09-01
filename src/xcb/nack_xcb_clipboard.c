/*
 * X11 selection handling over XCB.
 *
 * Covers both directions of ICCCM section 2: serving CLIPBOARD/PRIMARY to
 * other clients (TARGETS, MULTIPLE, UTF8_STRING, STRING) and fetching them,
 * including INCR transfers, which a terminal hits as soon as someone pastes
 * more than a screenful of text.
 */
#include "nack_xcb.h"

#include <poll.h>
#include <stdio.h>

#define NACK_SELECTION_TIMEOUT 2.0
/* Chunk size for outgoing INCR transfers, kept well under the server's
 * maximum request length. */
#define NACK_INCR_CHUNK 65536

static char **nack__xcb_owned_slot(xcb_atom_t selection)
{
    if (selection == nack__xcb.atom.CLIPBOARD)
        return &nack__xcb.clipboard_owned;
    if (selection == XCB_ATOM_PRIMARY)
        return &nack__xcb.primary_owned;
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Serving a selection                                                */
/* ------------------------------------------------------------------ */

static xcb_atom_t nack__xcb_write_target(xcb_selection_request_event_t *request,
                                         xcb_atom_t target, xcb_atom_t property)
{
    char **owned = nack__xcb_owned_slot(request->selection);
    if (!owned || !*owned || property == XCB_ATOM_NONE)
        return XCB_ATOM_NONE;

    if (target == nack__xcb.atom.TARGETS) {
        xcb_atom_t targets[4] = {
            nack__xcb.atom.TARGETS,
            nack__xcb.atom.UTF8_STRING,
            nack__xcb.atom.TEXT_PLAIN_UTF8,
            XCB_ATOM_STRING
        };
        xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE,
                            request->requestor, property, XCB_ATOM_ATOM, 32,
                            4, targets);
        return property;
    }

    if (target == nack__xcb.atom.UTF8_STRING ||
        target == nack__xcb.atom.TEXT_PLAIN_UTF8 ||
        target == XCB_ATOM_STRING) {
        size_t len = strlen(*owned);

        /* Anything larger than the server can carry in one request has to go
         * through INCR; refuse instead of silently truncating. */
        uint32_t max_bytes = xcb_get_maximum_request_length(nack__xcb.connection) * 4;
        if (max_bytes > 8192)
            max_bytes -= 8192;   /* leave room for the request header */
        if (len > max_bytes) {
            uint32_t total = (uint32_t)len;
            xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE,
                                request->requestor, property, nack__xcb.atom.INCR,
                                32, 1, &total);
            /* The requestor now drives the transfer by deleting the property;
             * we serve the chunks from the PropertyNotify handler below. */
            const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
            xcb_change_window_attributes(nack__xcb.connection, request->requestor,
                                         XCB_CW_EVENT_MASK, &mask);
            return property;
        }

        xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE,
                            request->requestor, property, target, 8,
                            (uint32_t)len, *owned);
        return property;
    }

    return XCB_ATOM_NONE;
}

bool nack__xcb_handle_selection_request(xcb_selection_request_event_t *request)
{
    xcb_selection_notify_event_t reply;
    memset(&reply, 0, sizeof reply);
    reply.response_type = XCB_SELECTION_NOTIFY;
    reply.time = request->time;
    reply.requestor = request->requestor;
    reply.selection = request->selection;
    reply.target = request->target;
    reply.property = XCB_ATOM_NONE;

    /* ICCCM: obsolete clients send property == None meaning "use target". */
    xcb_atom_t property = request->property != XCB_ATOM_NONE ? request->property
                                                            : request->target;

    if (request->target == nack__xcb.atom.MULTIPLE) {
        xcb_get_property_cookie_t cookie =
            xcb_get_property(nack__xcb.connection, 0, request->requestor, property,
                             nack__xcb.atom.ATOM_PAIR, 0, 1024);
        xcb_get_property_reply_t *pairs_reply =
            xcb_get_property_reply(nack__xcb.connection, cookie, NULL);
        if (pairs_reply) {
            xcb_atom_t *pairs = (xcb_atom_t *)xcb_get_property_value(pairs_reply);
            int count = xcb_get_property_value_length(pairs_reply) /
                        (int)sizeof(xcb_atom_t);
            for (int i = 0; i + 1 < count; i += 2)
                pairs[i + 1] = nack__xcb_write_target(request, pairs[i], pairs[i + 1]);
            xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE,
                                request->requestor, property,
                                nack__xcb.atom.ATOM_PAIR, 32, (uint32_t)count, pairs);
            free(pairs_reply);
            reply.property = property;
        }
    } else {
        reply.property = nack__xcb_write_target(request, request->target, property);
    }

    xcb_send_event(nack__xcb.connection, 0, request->requestor,
                   XCB_EVENT_MASK_NO_EVENT, (const char *)&reply);
    xcb_flush(nack__xcb.connection);
    return reply.property != XCB_ATOM_NONE;
}

void nack__xcb_handle_selection_clear(xcb_selection_clear_event_t *event)
{
    char **owned = nack__xcb_owned_slot(event->selection);
    if (owned) {
        free(*owned);
        *owned = NULL;
    }
}

static bool nack__xcb_own_selection(xcb_atom_t selection, const char *utf8)
{
    char **slot = nack__xcb_owned_slot(selection);
    if (!slot)
        return false;

    char *copy = nack__strdup(utf8);
    if (!copy)
        return false;
    free(*slot);
    *slot = copy;

    xcb_set_selection_owner(nack__xcb.connection, nack__xcb.helper, selection,
                            XCB_CURRENT_TIME);
    xcb_flush(nack__xcb.connection);

    xcb_get_selection_owner_cookie_t cookie =
        xcb_get_selection_owner(nack__xcb.connection, selection);
    xcb_get_selection_owner_reply_t *reply =
        xcb_get_selection_owner_reply(nack__xcb.connection, cookie, NULL);
    bool ok = reply && reply->owner == nack__xcb.helper;
    free(reply);

    if (!ok)
        return nack__fail(NACK_ERROR_PLATFORM, "failed to take selection ownership");
    return true;
}

/* ------------------------------------------------------------------ */
/* Fetching a selection                                               */
/* ------------------------------------------------------------------ */

/*
 * Waits for one event matching `wanted`, dispatching everything else normally
 * so the application does not miss input while a paste is in flight.
 */
static xcb_generic_event_t *nack__xcb_wait_for(uint8_t wanted, double deadline)
{
    for (;;) {
        xcb_generic_event_t *event;
        while ((event = xcb_poll_for_event(nack__xcb.connection)) != NULL) {
            if (event->response_type == 0) {   /* error reply */
                free(event);
                continue;
            }
            if ((event->response_type & 0x7F) == wanted)
                return event;
            nack__xcb_dispatch(event);
            free(event);
        }

        if (nack_time_seconds() >= deadline)
            return NULL;

        xcb_flush(nack__xcb.connection);
        struct pollfd pfd = { xcb_get_file_descriptor(nack__xcb.connection), POLLIN, 0 };
        double remaining = (deadline - nack_time_seconds()) * 1000.0;
        if (remaining < 0.0)
            remaining = 0.0;
        if (remaining > 50.0)
            remaining = 50.0;   /* re-check the deadline regularly */
        poll(&pfd, 1, (int)remaining);
    }
}

static char *nack__xcb_read_incr(void)
{
    size_t capacity = 8192, length = 0;
    char *buffer = (char *)malloc(capacity);
    if (!buffer)
        return NULL;

    /* Deleting the property signals the owner to post the next chunk. */
    xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                        nack__xcb.atom.NACK_SELECTION);
    xcb_flush(nack__xcb.connection);

    for (;;) {
        double deadline = nack_time_seconds() + NACK_SELECTION_TIMEOUT;
        xcb_generic_event_t *event =
            nack__xcb_wait_for(XCB_PROPERTY_NOTIFY, deadline);
        if (!event) {
            free(buffer);
            return NULL;
        }

        xcb_property_notify_event_t *notify = (xcb_property_notify_event_t *)event;
        bool ours = notify->window == nack__xcb.helper &&
                    notify->atom == nack__xcb.atom.NACK_SELECTION &&
                    notify->state == XCB_PROPERTY_NEW_VALUE;
        free(event);
        if (!ours)
            continue;

        xcb_get_property_cookie_t cookie =
            xcb_get_property(nack__xcb.connection, 0, nack__xcb.helper,
                             nack__xcb.atom.NACK_SELECTION, XCB_GET_PROPERTY_TYPE_ANY,
                             0, UINT32_MAX / 4);
        xcb_get_property_reply_t *reply =
            xcb_get_property_reply(nack__xcb.connection, cookie, NULL);
        if (!reply) {
            free(buffer);
            return NULL;
        }

        int chunk_len = xcb_get_property_value_length(reply);
        if (chunk_len == 0) {          /* zero-length chunk ends the transfer */
            free(reply);
            xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                                nack__xcb.atom.NACK_SELECTION);
            xcb_flush(nack__xcb.connection);
            break;
        }

        if (length + (size_t)chunk_len + 1 > capacity) {
            while (length + (size_t)chunk_len + 1 > capacity)
                capacity *= 2;
            char *grown = (char *)realloc(buffer, capacity);
            if (!grown) {
                free(buffer);
                free(reply);
                return NULL;
            }
            buffer = grown;
        }
        memcpy(buffer + length, xcb_get_property_value(reply), (size_t)chunk_len);
        length += (size_t)chunk_len;
        free(reply);

        xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                            nack__xcb.atom.NACK_SELECTION);
        xcb_flush(nack__xcb.connection);
    }

    buffer[length] = '\0';
    return buffer;
}

static char *nack__xcb_read_selection(xcb_atom_t selection)
{
    /* Serving ourselves avoids a pointless round trip through the server. */
    xcb_get_selection_owner_cookie_t owner_cookie =
        xcb_get_selection_owner(nack__xcb.connection, selection);
    xcb_get_selection_owner_reply_t *owner_reply =
        xcb_get_selection_owner_reply(nack__xcb.connection, owner_cookie, NULL);
    bool self_owned = owner_reply && owner_reply->owner == nack__xcb.helper;
    bool no_owner = owner_reply && owner_reply->owner == XCB_WINDOW_NONE;
    free(owner_reply);

    if (self_owned) {
        char **owned = nack__xcb_owned_slot(selection);
        return (owned && *owned) ? nack__strdup(*owned) : NULL;
    }
    if (no_owner)
        return NULL;

    xcb_atom_t targets[2] = { nack__xcb.atom.UTF8_STRING, XCB_ATOM_STRING };
    for (int t = 0; t < 2; ++t) {
        xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                            nack__xcb.atom.NACK_SELECTION);
        xcb_convert_selection(nack__xcb.connection, nack__xcb.helper, selection,
                              targets[t], nack__xcb.atom.NACK_SELECTION,
                              XCB_CURRENT_TIME);
        xcb_flush(nack__xcb.connection);

        double deadline = nack_time_seconds() + NACK_SELECTION_TIMEOUT;
        xcb_generic_event_t *event = nack__xcb_wait_for(XCB_SELECTION_NOTIFY, deadline);
        if (!event)
            continue;

        xcb_selection_notify_event_t *notify = (xcb_selection_notify_event_t *)event;
        bool refused = notify->property == XCB_ATOM_NONE;
        free(event);
        if (refused)
            continue;

        xcb_get_property_cookie_t cookie =
            xcb_get_property(nack__xcb.connection, 0, nack__xcb.helper,
                             nack__xcb.atom.NACK_SELECTION, XCB_GET_PROPERTY_TYPE_ANY,
                             0, UINT32_MAX / 4);
        xcb_get_property_reply_t *reply =
            xcb_get_property_reply(nack__xcb.connection, cookie, NULL);
        if (!reply)
            continue;

        if (reply->type == nack__xcb.atom.INCR) {
            free(reply);
            char *result = nack__xcb_read_incr();
            if (result)
                return result;
            continue;
        }

        int len = xcb_get_property_value_length(reply);
        char *result = NULL;
        if (len > 0) {
            result = (char *)malloc((size_t)len + 1);
            if (result) {
                memcpy(result, xcb_get_property_value(reply), (size_t)len);
                result[len] = '\0';
            }
        }
        free(reply);
        xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                            nack__xcb.atom.NACK_SELECTION);
        if (result)
            return result;
    }
    return NULL;
}

/* ------------------------------------------------------------------ */
/* Public entry points                                                */
/* ------------------------------------------------------------------ */

bool nack__xcb_clipboard_set(const char *utf8)
{
    return nack__xcb_own_selection(nack__xcb.atom.CLIPBOARD, utf8);
}

const char *nack__xcb_clipboard_get(void)
{
    free(nack__xcb.clipboard_received);
    nack__xcb.clipboard_received = nack__xcb_read_selection(nack__xcb.atom.CLIPBOARD);
    return nack__xcb.clipboard_received;
}

bool nack__xcb_primary_set(const char *utf8)
{
    return nack__xcb_own_selection(XCB_ATOM_PRIMARY, utf8);
}

const char *nack__xcb_primary_get(void)
{
    free(nack__xcb.primary_received);
    nack__xcb.primary_received = nack__xcb_read_selection(XCB_ATOM_PRIMARY);
    return nack__xcb.primary_received;
}

void nack__xcb_clipboard_shutdown(void)
{
    free(nack__xcb.clipboard_owned);
    free(nack__xcb.primary_owned);
    free(nack__xcb.clipboard_received);
    free(nack__xcb.primary_received);
    nack__xcb.clipboard_owned = NULL;
    nack__xcb.primary_owned = NULL;
    nack__xcb.clipboard_received = NULL;
    nack__xcb.primary_received = NULL;
}
