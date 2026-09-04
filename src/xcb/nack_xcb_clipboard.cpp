/*
 * X11 selection handling over XCB.
 *
 * Covers both directions of ICCCM section 2: serving CLIPBOARD/PRIMARY to
 * other clients (TARGETS, MULTIPLE, UTF8_STRING, STRING) and fetching them.
 *
 * Both directions include INCR, the incremental transfer a selection too
 * large for one property goes through - which is anything a console-sized
 * copy or paste is likely to be. Serving it is not a single reply: the
 * property is set to the total size, and each chunk is then sent in answer to
 * the requestor deleting it, so a transfer is a conversation that outlives
 * the request that began it.
 */
#include "nack_xcb.h"

#include "../nack_scoped.h"

#include <optional>
#include <poll.h>
#include <stdio.h>
#include <string>

#define NACK_SELECTION_TIMEOUT 2.0
/* Chunk size for outgoing INCR transfers, kept well under the server's
 * maximum request length. */
#define NACK_INCR_CHUNK 65536
/*
 * Selections larger than this are served incrementally.
 *
 * The obvious threshold is the server's maximum request length, since that is
 * the limit INCR exists to get around - but with BIG-REQUESTS that is around
 * 16MB, so in practice nothing would ever cross it and the whole incremental
 * path would be dead code that nobody notices is broken. Chunking anything
 * over 64K instead means an ordinary large paste exercises it, and saves the
 * server a 16MB request nobody asked for.
 */
#define NACK_INCR_THRESHOLD NACK_INCR_CHUNK
/*
 * How long a half-finished outgoing transfer is kept. The requestor drives it
 * by deleting the property; one that has not asked for the next chunk in this
 * long has died or lost interest, and its slot is worth more than its data.
 */
#define NACK_INCR_SEND_TIMEOUT 10.0

static std::optional<std::string> *nack__xcb_owned_slot(xcb_atom_t selection)
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

/*
 * Outgoing incremental transfers.
 *
 * ICCCM: the property is set to the total size with type INCR, and from there
 * the requestor drives - every time it deletes the property we append the next
 * chunk, and a zero-length one ends the transfer. So a transfer outlives the
 * SelectionRequest that began it, and has to be carried until the requestor
 * has taken the lot. The data is copied rather than referenced, because the
 * program is free to put something else on the clipboard, or to lose the
 * selection entirely, while one of these is still in flight.
 *
 * A fixed few, because a well-behaved requestor asks for one target at a time
 * and there is no bound worth having on a client that does not: a request
 * arriving with all of them busy is refused, which ICCCM allows, rather than
 * being allowed to grow this list without limit.
 */
#define NACK_INCR_SENDS 4

struct nack_incr_send {
    xcb_window_t requestor;
    xcb_atom_t property;
    xcb_atom_t type;
    std::string data;
    size_t sent;
    double deadline;
    bool active;
};

static struct nack_incr_send nack__incr_sends[NACK_INCR_SENDS];

static void nack__xcb_incr_end(struct nack_incr_send *send)
{
    if (!send->active)
        return;

    /*
     * Stop watching a window we only ever cared about for this transfer. The
     * mask is per-client, so this clears ours and leaves everyone else's
     * alone; if the requestor has already gone the request fails with a
     * BadWindow that the dispatcher drops, which is the outcome either way.
     */
    const uint32_t none = XCB_EVENT_MASK_NO_EVENT;
    xcb_change_window_attributes(nack__xcb.connection, send->requestor,
                                 XCB_CW_EVENT_MASK, &none);
    send->data.clear();
    send->active = false;
}

/* A free slot, first retiring any transfer whose requestor has stopped asking. */
static struct nack_incr_send *nack__xcb_incr_slot(void)
{
    double now = nack__win_time_seconds();
    struct nack_incr_send *available = NULL;

    for (int i = 0; i < NACK_INCR_SENDS; ++i) {
        struct nack_incr_send *send = &nack__incr_sends[i];
        if (send->active && now > send->deadline)
            nack__xcb_incr_end(send);
        if (!send->active && !available)
            available = send;
    }
    return available;
}

/*
 * Answers one property delete with the next chunk. Returns false if the event
 * belongs to something else, so the caller can go on handling it.
 */
bool nack__xcb_handle_property_notify(xcb_property_notify_event_t *event)
{
    if (event->state != XCB_PROPERTY_DELETE)
        return false;

    for (int i = 0; i < NACK_INCR_SENDS; ++i) {
        struct nack_incr_send *send = &nack__incr_sends[i];
        size_t chunk;

        if (!send->active || send->requestor != event->window ||
            send->property != event->atom)
            continue;

        chunk = send->data.size() - send->sent;
        if (chunk > NACK_INCR_CHUNK)
            chunk = NACK_INCR_CHUNK;

        xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_APPEND,
                            send->requestor, send->property, send->type, 8,
                            (uint32_t)chunk, send->data.data() + send->sent);
        xcb_flush(nack__xcb.connection);
        send->sent += chunk;
        send->deadline = nack__win_time_seconds() + NACK_INCR_SEND_TIMEOUT;

        /* The zero-length chunk is the end of the transfer, not a mistake. */
        if (chunk == 0)
            nack__xcb_incr_end(send);
        return true;
    }
    return false;
}


static xcb_atom_t nack__xcb_write_target(xcb_selection_request_event_t *request,
                                         xcb_atom_t target, xcb_atom_t property)
{
    std::optional<std::string> *owned = nack__xcb_owned_slot(request->selection);
    if (!owned || !owned->has_value() || property == XCB_ATOM_NONE)
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
        size_t len = owned->value().size();

        if (len > NACK_INCR_THRESHOLD) {
            struct nack_incr_send *send = nack__xcb_incr_slot();
            uint32_t total = (uint32_t)len;

            if (!send)
                return XCB_ATOM_NONE;   /* too many at once; ICCCM says refuse */

            /* Selecting on the requestor is what makes its deletes visible. */
            const uint32_t mask = XCB_EVENT_MASK_PROPERTY_CHANGE;
            xcb_change_window_attributes(nack__xcb.connection, request->requestor,
                                         XCB_CW_EVENT_MASK, &mask);
            xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE,
                                request->requestor, property, nack__xcb.atom.INCR,
                                32, 1, &total);

            send->requestor = request->requestor;
            send->property = property;
            send->type = target;
            send->data = owned->value();
            send->sent = 0;
            send->deadline = nack__win_time_seconds() + NACK_INCR_SEND_TIMEOUT;
            send->active = true;
            return property;
        }

        xcb_change_property(nack__xcb.connection, XCB_PROP_MODE_REPLACE,
                            request->requestor, property, target, 8,
                            (uint32_t)len, owned->value().data());
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
    std::optional<std::string> *owned = nack__xcb_owned_slot(event->selection);
    if (owned)
        owned->reset();
}

static bool nack__xcb_own_selection(xcb_atom_t selection, const char *utf8)
{
    std::optional<std::string> *slot = nack__xcb_owned_slot(selection);
    if (!slot)
        return false;

    *slot = utf8;

    xcb_set_selection_owner(nack__xcb.connection, nack__xcb.helper, selection,
                            XCB_CURRENT_TIME);
    xcb_flush(nack__xcb.connection);

    xcb_get_selection_owner_cookie_t cookie =
        xcb_get_selection_owner(nack__xcb.connection, selection);
    nack::c_ptr<xcb_get_selection_owner_reply_t> reply(
        xcb_get_selection_owner_reply(nack__xcb.connection, cookie, NULL));
    bool ok = reply && reply->owner == nack__xcb.helper;

    if (!ok)
        return state.fail(NACK_ERROR_PLATFORM, "failed to take selection ownership");
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

        if (nack__win_time_seconds() >= deadline)
            return NULL;

        xcb_flush(nack__xcb.connection);
        struct pollfd pfd = { xcb_get_file_descriptor(nack__xcb.connection), POLLIN, 0 };
        double remaining = (deadline - nack__win_time_seconds()) * 1000.0;
        if (remaining < 0.0)
            remaining = 0.0;
        if (remaining > 50.0)
            remaining = 50.0;   /* re-check the deadline regularly */
        poll(&pfd, 1, (int)remaining);
    }
}

static std::optional<std::string> nack__xcb_read_incr(void)
{
    std::string buffer;

    /* Deleting the property signals the owner to post the next chunk. */
    xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                        nack__xcb.atom.NACK_SELECTION);
    xcb_flush(nack__xcb.connection);

    for (;;) {
        double deadline = nack__win_time_seconds() + NACK_SELECTION_TIMEOUT;
        nack::c_ptr<xcb_generic_event_t> event(
            nack__xcb_wait_for(XCB_PROPERTY_NOTIFY, deadline));
        if (!event)
            return std::nullopt;

        xcb_property_notify_event_t *notify =
            (xcb_property_notify_event_t *)event.get();
        if (notify->window != nack__xcb.helper ||
            notify->atom != nack__xcb.atom.NACK_SELECTION ||
            notify->state != XCB_PROPERTY_NEW_VALUE) {
            /*
             * Not our chunk. It may well be a requestor asking us for the
             * next chunk of a selection we are serving, so hand it on rather
             * than dropping it - otherwise their transfer stalls for as long
             * as ours takes.
             */
            nack__xcb_dispatch(event.get());
            continue;
        }

        xcb_get_property_cookie_t cookie =
            xcb_get_property(nack__xcb.connection, 0, nack__xcb.helper,
                             nack__xcb.atom.NACK_SELECTION, XCB_GET_PROPERTY_TYPE_ANY,
                             0, UINT32_MAX / 4);
        nack::c_ptr<xcb_get_property_reply_t> reply(
            xcb_get_property_reply(nack__xcb.connection, cookie, NULL));
        if (!reply)
            return std::nullopt;

        int chunk_len = xcb_get_property_value_length(reply.get());
        if (chunk_len == 0) {          /* zero-length chunk ends the transfer */
            xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                                nack__xcb.atom.NACK_SELECTION);
            xcb_flush(nack__xcb.connection);
            break;
        }

        buffer.append((const char *)xcb_get_property_value(reply.get()),
                      (size_t)chunk_len);

        xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                            nack__xcb.atom.NACK_SELECTION);
        xcb_flush(nack__xcb.connection);
    }

    return buffer;
}

static std::optional<std::string> nack__xcb_read_selection(xcb_atom_t selection)
{
    /* Serving ourselves avoids a pointless round trip through the server. */
    xcb_get_selection_owner_cookie_t owner_cookie =
        xcb_get_selection_owner(nack__xcb.connection, selection);
    nack::c_ptr<xcb_get_selection_owner_reply_t> owner(
        xcb_get_selection_owner_reply(nack__xcb.connection, owner_cookie, NULL));

    if (owner && owner->owner == nack__xcb.helper) {
        std::optional<std::string> *owned = nack__xcb_owned_slot(selection);
        return owned ? *owned : std::nullopt;
    }
    if (owner && owner->owner == XCB_WINDOW_NONE)
        return std::nullopt;

    xcb_atom_t targets[2] = { nack__xcb.atom.UTF8_STRING, XCB_ATOM_STRING };
    for (int t = 0; t < 2; ++t) {
        xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                            nack__xcb.atom.NACK_SELECTION);
        xcb_convert_selection(nack__xcb.connection, nack__xcb.helper, selection,
                              targets[t], nack__xcb.atom.NACK_SELECTION,
                              XCB_CURRENT_TIME);
        xcb_flush(nack__xcb.connection);

        double deadline = nack__win_time_seconds() + NACK_SELECTION_TIMEOUT;
        nack::c_ptr<xcb_generic_event_t> event(
            nack__xcb_wait_for(XCB_SELECTION_NOTIFY, deadline));
        if (!event)
            continue;

        xcb_selection_notify_event_t *notify =
            (xcb_selection_notify_event_t *)event.get();
        if (notify->property == XCB_ATOM_NONE)
            continue;                  /* the owner has no such target */

        xcb_get_property_cookie_t cookie =
            xcb_get_property(nack__xcb.connection, 0, nack__xcb.helper,
                             nack__xcb.atom.NACK_SELECTION, XCB_GET_PROPERTY_TYPE_ANY,
                             0, UINT32_MAX / 4);
        nack::c_ptr<xcb_get_property_reply_t> reply(
            xcb_get_property_reply(nack__xcb.connection, cookie, NULL));
        if (!reply)
            continue;

        /*
         * An INCR reply carries the total size, not the data: the transfer
         * itself is a conversation, and read_incr has it.
         */
        if (reply->type == nack__xcb.atom.INCR) {
            reply.reset();
            std::optional<std::string> result = nack__xcb_read_incr();
            if (result)
                return result;
            continue;
        }

        int len = xcb_get_property_value_length(reply.get());
        std::optional<std::string> result;
        if (len > 0)
            result.emplace((const char *)xcb_get_property_value(reply.get()),
                          (size_t)len);
        reply.reset();
        xcb_delete_property(nack__xcb.connection, nack__xcb.helper,
                            nack__xcb.atom.NACK_SELECTION);
        if (result)
            return result;
    }
    return std::nullopt;
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
    nack__xcb.clipboard_received = nack__xcb_read_selection(nack__xcb.atom.CLIPBOARD);
    return nack__xcb.clipboard_received ? nack__xcb.clipboard_received->c_str() : NULL;
}

bool nack__xcb_primary_set(const char *utf8)
{
    return nack__xcb_own_selection(XCB_ATOM_PRIMARY, utf8);
}

const char *nack__xcb_primary_get(void)
{
    nack__xcb.primary_received = nack__xcb_read_selection(XCB_ATOM_PRIMARY);
    return nack__xcb.primary_received ? nack__xcb.primary_received->c_str() : NULL;
}

void nack__xcb_clipboard_shutdown(void)
{
    for (int i = 0; i < NACK_INCR_SENDS; ++i)
        nack__xcb_incr_end(&nack__incr_sends[i]);

    nack__xcb.clipboard_owned.reset();
    nack__xcb.primary_owned.reset();
    nack__xcb.clipboard_received.reset();
    nack__xcb.primary_received.reset();
}
