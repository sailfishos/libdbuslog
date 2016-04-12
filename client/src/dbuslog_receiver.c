/*
 * Copyright (C) 2016 Jolla Ltd.
 * Contact: Slava Monich <slava.monich@jolla.com>
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the name of Jolla Ltd nor the names of its contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "dbuslog_receiver.h"
#include "dbuslog_protocol.h"
#include "dbuslog_client_log.h"

#include <gutil_misc.h>

/* Log module */
GLOG_MODULE_DEFINE("dbuslog");

/* Object definition */
struct dbus_log_receiver {
    GObject object;
    GIOChannel* io;
    int paused;
    guint read_watch_id;
    guint32 last_message_index;
    gboolean message_received;
    guchar packet[DBUSLOG_PACKET_MAX_FIXED_PART];
    guint packet_size;
    guint packet_fixed_part;
    guint packet_read;
    char* packet_buffer;
};

typedef GObjectClass DBusLogReceiverClass;
G_DEFINE_TYPE(DBusLogReceiver, dbus_log_receiver, G_TYPE_OBJECT)
#define PARENT_CLASS (dbus_log_receiver_parent_class)
#define DBUSLOG_RECEIVER_TYPE (dbus_log_receiver_get_type())
#define DBUSLOG_RECEIVER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUSLOG_RECEIVER_TYPE, DBusLogReceiver))

enum dbus_log_receiver_signal {
    DBUSLOG_RECEIVER_SIGNAL_MESSAGE,
    DBUSLOG_RECEIVER_SIGNAL_SKIP,
    DBUSLOG_RECEIVER_SIGNAL_CLOSED,
    DBUSLOG_RECEIVER_SIGNAL_COUNT
};

#define DBUSLOG_RECEIVER_SIGNAL_MESSAGE_NAME    "dbuslog-receiver-message"
#define DBUSLOG_RECEIVER_SIGNAL_SKIP_NAME       "dbuslog-receiver-skip"
#define DBUSLOG_RECEIVER_SIGNAL_CLOSED_NAME     "dbuslog-receiver-closed"

static guint dbus_log_receiver_signals[DBUSLOG_RECEIVER_SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gboolean
dbus_log_receiver_read_chars(
    DBusLogReceiver* self,
    void* buf,
    gsize count,
    gsize* bytes_read)
{
    GError* error = NULL;
    GIOStatus status = g_io_channel_read_chars(self->io, buf, count,
        bytes_read, &error);
    if (error) {
        GERR("Read failed: %s", error->message);
        g_error_free(error);
        return FALSE;
    } else if (status == G_IO_STATUS_EOF) {
        GDEBUG("End of stream");
        return FALSE;
    } else {
        return TRUE;
    }
}

inline static
guint32
dbus_log_receiver_get_uint32(
    DBusLogReceiver* self,
    guint offset)
{
    const guchar* ptr = self->packet + offset;
    return ((guint32)(ptr[3]) << 24) |
        ((guint32)(ptr[2]) << 16) |
        ((guint32)(ptr[1]) << 8) |
        ptr[0];
}

inline static
guint64
dbus_log_receiver_get_uint64(
    DBusLogReceiver* self,
    guint offset)
{
    return ((guint64)dbus_log_receiver_get_uint32(self, offset)) |
        (((guint64)dbus_log_receiver_get_uint32(self, offset + 4)) << 32);
}

static
gboolean
dbus_log_receiver_read(
    DBusLogReceiver* self)
{
    gsize bytes_read;

    /* Read the header */
    if (self->packet_read < DBUSLOG_PACKET_HEADER_SIZE) {
        if (!dbus_log_receiver_read_chars(self,
            self->packet + self->packet_read,
            DBUSLOG_PACKET_HEADER_SIZE - self->packet_read,
            &bytes_read)) {
            return FALSE;
        }
        self->packet_read += bytes_read;
        if (self->packet_read < DBUSLOG_PACKET_HEADER_SIZE) {
            /* Have to wait */
            return TRUE;
        }

        self->packet_size = dbus_log_receiver_get_uint32(self,
            DBUSLOG_PACKET_SIZE_OFFSET) + DBUSLOG_PACKET_HEADER_SIZE;

        switch (self->packet[DBUSLOG_PACKET_TYPE_OFFSET]) {
        case DBUSLOG_PACKET_TYPE_PING:
        case DBUSLOG_PACKET_TYPE_BYE:
            GASSERT(self->packet_size == DBUSLOG_PACKET_HEADER_SIZE);
            self->packet_fixed_part = DBUSLOG_PACKET_HEADER_SIZE;
            break;
        case DBUSLOG_PACKET_TYPE_MESSAGE:
            self->packet_fixed_part = DBUSLOG_PACKET_HEADER_SIZE +
                DBUSLOG_MESSAGE_PREFIX_SIZE;
            break;
        default:
            self->packet_fixed_part = DBUSLOG_PACKET_MAX_FIXED_PART;
            break;
        }
    }

    /* Read the fixed part of the packet */
    if (self->packet_read < self->packet_fixed_part) {
        if (!dbus_log_receiver_read_chars(self,
            self->packet + self->packet_read,
            self->packet_fixed_part - self->packet_read,
            &bytes_read)) {
            return FALSE;
        }
        self->packet_read += bytes_read;
        if (self->packet_read < self->packet_fixed_part) {
            /* Have to wait */
            return TRUE;
        }

        if (self->packet_size > self->packet_fixed_part) {
            /* Allocate extra byte for NULL terminator */
            self->packet_buffer = g_malloc(self->packet_size -
                self->packet_fixed_part + 1);
        }
    }

    /* Read the rest of the data */
    if (self->packet_read < self->packet_size) {
        if (!dbus_log_receiver_read_chars(self, self->packet_buffer +
            (self->packet_read - self->packet_fixed_part),
            self->packet_size - self->packet_read,
            &bytes_read)) {
            return FALSE;
        }
        self->packet_read += bytes_read;
        if (self->packet_read < self->packet_size) {
            /* Have to wait */
            return TRUE;
        }
    }

    /* We have received the entire packet */
    if (self->packet[DBUSLOG_PACKET_TYPE_OFFSET] ==
        DBUSLOG_PACKET_TYPE_MESSAGE) {
        DBusLogMessage* msg = dbus_log_message_new(NULL);
        msg->timestamp = dbus_log_receiver_get_uint64(self,
            DBUSLOG_MESSAGE_TIMESTAMP_OFFSET);
        msg->index = dbus_log_receiver_get_uint32(self,
            DBUSLOG_MESSAGE_INDEX_OFFSET);
        msg->category = dbus_log_receiver_get_uint32(self,
            DBUSLOG_MESSAGE_CATEGORY_OFFSET);
        msg->level = self->packet[DBUSLOG_MESSAGE_LEVEL_OFFSET];

        /* Transfer buffer ownership to DBusLogMessage */
        if (self->packet_buffer) {
            msg->length = self->packet_size - self->packet_fixed_part;
            msg->string = self->packet_buffer;
            msg->string[msg->length] = 0; /* We allocated the extra byte */
            GASSERT(strlen(msg->string) == msg->length);
            self->packet_buffer = NULL;
        }

        if (self->message_received) {
            const guint32 expected = self->last_message_index + 1;
            if (msg->index != expected) {
                guint32 skipped = (msg->index > expected) ?
                    (msg->index - expected) : (expected - msg->index);
                g_signal_emit(self, dbus_log_receiver_signals[
                    DBUSLOG_RECEIVER_SIGNAL_SKIP], 0, skipped);
            }
        }

        self->message_received = TRUE;
        self->last_message_index = msg->index;
        self->packet_size = self->packet_fixed_part = self->packet_read = 0;
        g_signal_emit(self, dbus_log_receiver_signals[
            DBUSLOG_RECEIVER_SIGNAL_MESSAGE], 0, msg);
        dbus_log_message_unref(msg);
    } else {
        self->packet_size = self->packet_fixed_part = self->packet_read = 0;
        if (self->packet_buffer) {
            g_free(self->packet_buffer);
            self->packet_buffer = NULL;
        }
        switch (self->packet[DBUSLOG_PACKET_TYPE_OFFSET]) {
        case DBUSLOG_PACKET_TYPE_PING:
            GDEBUG("Ping");
            break;
        case DBUSLOG_PACKET_TYPE_BYE:
            GDEBUG("Bye");
            return FALSE;
        default:
            GDEBUG("Unexpected packet type %u",
                self->packet[DBUSLOG_PACKET_TYPE_OFFSET]);
            break;
        }
    }

    return TRUE;
}

static
gboolean
dbus_log_receiver_read_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data)
{
    DBusLogReceiver* self = DBUSLOG_RECEIVER(data);
    gboolean disposition;
    dbus_log_receiver_ref(self);
    if ((condition & G_IO_IN) && dbus_log_receiver_read(self)) {
        disposition = G_SOURCE_CONTINUE;
    } else {
        self->read_watch_id = 0;
        dbus_log_receiver_close(self);
        disposition = G_SOURCE_REMOVE;
    }
    dbus_log_receiver_unref(self);
    return disposition;
}

/*==========================================================================*
 * API
 *==========================================================================*/

DBusLogReceiver*
dbus_log_receiver_new(
    int fd,
    gboolean do_close)
{
    GIOChannel* io = g_io_channel_unix_new(fd);
    if (io) {
        DBusLogReceiver* self = g_object_new(DBUSLOG_RECEIVER_TYPE, NULL);
        self->io = io;
        g_io_channel_set_flags(self->io, G_IO_FLAG_NONBLOCK, NULL);
        g_io_channel_set_encoding(self->io, NULL, NULL);
        g_io_channel_set_buffered(self->io, FALSE);
        g_io_channel_set_close_on_unref(self->io, do_close);
        self->paused = 1;
        dbus_log_receiver_resume(self);
        return self;
    } else {
        return NULL;
    }
}

DBusLogReceiver*
dbus_log_receiver_ref(
    DBusLogReceiver* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(DBUSLOG_RECEIVER(self));
        return self;
    } else {
        return NULL;
    }
}

void
dbus_log_receiver_unref(
    DBusLogReceiver* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(DBUSLOG_RECEIVER(self));
    }
}

void
dbus_log_receiver_pause(
    DBusLogReceiver* self)
{
    if (G_LIKELY(self)) {
        self->paused++;
        if (self->read_watch_id) {
            g_source_remove(self->read_watch_id);
            self->read_watch_id = 0;
        }
    }
}

void
dbus_log_receiver_resume(
    DBusLogReceiver* self)
{
    if (G_LIKELY(self)) {
        GASSERT(self->paused);
        if (self->paused > 0 && !(--(self->paused))) {
            GASSERT(!self->read_watch_id);
            if (self->io) {
                self->read_watch_id = g_io_add_watch(self->io,
                    G_IO_IN | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                    dbus_log_receiver_read_callback, self);
            }
        }
    }
}

void
dbus_log_receiver_close(
    DBusLogReceiver* self)
{
    if (G_LIKELY(self)) {
        self->packet_size = self->packet_fixed_part = self->packet_read = 0;
        if (self->packet_buffer) {
            g_free(self->packet_buffer);
            self->packet_buffer = NULL;
        }
        if (self->read_watch_id) {
            g_source_remove(self->read_watch_id);
            self->read_watch_id = 0;
        }
        if (self->io) {
            g_io_channel_shutdown(self->io, FALSE, NULL);
            g_io_channel_unref(self->io);
            self->io = NULL;
            g_signal_emit(self, dbus_log_receiver_signals[
                DBUSLOG_RECEIVER_SIGNAL_CLOSED], 0);
        }
    }
}

gulong
dbus_log_receiver_add_message_handler(
    DBusLogReceiver* self,
    DBusLogReceiverMessageFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        DBUSLOG_RECEIVER_SIGNAL_MESSAGE_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_receiver_add_skip_handler(
    DBusLogReceiver* self,
    DBusLogReceiverSkipFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        DBUSLOG_RECEIVER_SIGNAL_SKIP_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_receiver_add_closed_handler(
    DBusLogReceiver* self,
    DBusLogReceiverFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        DBUSLOG_RECEIVER_SIGNAL_CLOSED_NAME, G_CALLBACK(fn), user_data) : 0;
}

void
dbus_log_receiver_remove_handler(
    DBusLogReceiver* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
dbus_log_receiver_remove_handlers(
    DBusLogReceiver* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

/**
 * Per instance initializer
 */
static
void
dbus_log_receiver_init(
    DBusLogReceiver* self)
{
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
dbus_log_receiver_dispose(
    GObject* object)
{
    dbus_log_receiver_close(DBUSLOG_RECEIVER(object));
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

/**
 * Per class initializer
 */
static
void
dbus_log_receiver_class_init(
    DBusLogReceiverClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GType class_type = G_OBJECT_CLASS_TYPE(klass);
    object_class->dispose = dbus_log_receiver_dispose;
    dbus_log_receiver_signals[DBUSLOG_RECEIVER_SIGNAL_MESSAGE] =
        g_signal_new(DBUSLOG_RECEIVER_SIGNAL_MESSAGE_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_POINTER);
    dbus_log_receiver_signals[DBUSLOG_RECEIVER_SIGNAL_SKIP] =
        g_signal_new(DBUSLOG_RECEIVER_SIGNAL_SKIP_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_UINT);
    dbus_log_receiver_signals[DBUSLOG_RECEIVER_SIGNAL_CLOSED] =
        g_signal_new(DBUSLOG_RECEIVER_SIGNAL_CLOSED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
