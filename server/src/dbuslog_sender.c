/*
 * Copyright (C) 2016-2020 Jolla Ltd.
 * Copyright (C) 2016-2020 Slava Monich <slava.monich@jolla.com>
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
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
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

#define GLIB_DISABLE_DEPRECATION_WARNINGS

#include "dbuslog_sender.h"
#include "dbuslog_protocol.h"
#include "dbuslog_server_log.h"

#include <gutil_ring.h>

#include <unistd.h>
#include <errno.h>

#define DBUSLOG_SENDER_DEFAULT_BACKLOG (1000)

/* Object definition */
struct dbus_log_sender_priv {
    gboolean done;
    gboolean bye;
    char* name;
    GIOChannel* io;
    guint write_watch_id;
    GUtilRing* buffer;
    guchar packet[DBUSLOG_PACKET_MAX_FIXED_PART];
    guint packet_size;
    guint packet_fixed_part;
    guint packet_written;
    DBusLogMessage* current_message;
    GMainContext* context;
    GMutex mutex;
};

typedef GObjectClass DBusLogSenderClass;
G_DEFINE_TYPE(DBusLogSender, dbus_log_sender, G_TYPE_OBJECT)
#define PARENT_CLASS (dbus_log_sender_parent_class)
#define DBUSLOG_SENDER_TYPE (dbus_log_sender_get_type())
#define DBUSLOG_SENDER(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUSLOG_SENDER_TYPE, DBusLogSender))

enum dbus_log_sender_signal {
    DBUSLOG_SENDER_SIGNAL_CLOSED,
    DBUSLOG_SENDER_SIGNAL_COUNT
};

#define DBUSLOG_SENDER_SIGNAL_CLOSED_NAME   "dbuslog-sender-closed"

static guint dbus_log_sender_signals[DBUSLOG_SENDER_SIGNAL_COUNT] = { 0 };

static
void
dbus_log_sender_prepare_bye(
    DBusLogSender* self);

static
void
dbus_log_sender_prepare_current_message(
    DBusLogSender* self);

static
void
dbus_log_sender_schedule_write(
    DBusLogSender* self);

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
gboolean
dbus_log_sender_write(
    DBusLogSender* self)
{
    DBusLogSenderPriv* priv = self->priv;
    GError* error = NULL;

    if (priv->packet_written < priv->packet_size) {
        gsize bytes_written;

        /* Fixed part */
        if (priv->packet_written < priv->packet_fixed_part) {
            bytes_written = 0;
            g_io_channel_write_chars(priv->io,
                (void*)(priv->packet + priv->packet_written),
                priv->packet_fixed_part - priv->packet_written,
                &bytes_written, &error);
            if (error) {
                GDEBUG("%s write failed: %s", priv->name, error->message);
                g_error_free(error);
                priv->write_watch_id = 0;
                dbus_log_sender_shutdown(self, FALSE);
                return FALSE;
            }
            priv->packet_written += bytes_written;
            if (priv->packet_written < priv->packet_fixed_part) {
                /* Will have to wait */
                return TRUE;
            }
        }

        /* Message data */
        if (priv->packet_written < priv->packet_size) {
            GASSERT(priv->current_message);
            bytes_written = 0;
            g_io_channel_write_chars(priv->io, priv->current_message->string +
                (priv->packet_written - priv->packet_fixed_part),
                priv->packet_size - priv->packet_written,
                &bytes_written, &error);
            if (error) {
                GDEBUG("%s write failed: %s", priv->name, error->message);
                g_error_free(error);
                priv->write_watch_id = 0;
                dbus_log_sender_shutdown(self, FALSE);
                return FALSE;
            }
            priv->packet_written += bytes_written;
            if (priv->packet_written < priv->packet_size) {
                /* Will have to wait */
                return TRUE;
            }
        }
    }

    /* The entire packet has been sent, pick the next one */

    /* Lock */
    g_mutex_lock(&priv->mutex);
    dbus_log_message_unref(priv->current_message);
    priv->current_message = gutil_ring_get(priv->buffer);
    priv->packet_written = priv->packet_size = priv->packet_fixed_part = 0;
    if (priv->current_message) {
        dbus_log_sender_prepare_current_message(self);
        g_mutex_unlock(&priv->mutex);
        /* Unlock */
        dbus_log_sender_schedule_write(self);
        return TRUE;
    } else if (priv->bye) {
        dbus_log_sender_prepare_bye(self);
        g_mutex_unlock(&priv->mutex);
        /* Unlock */
        dbus_log_sender_schedule_write(self);
        return TRUE;
    } else {
        g_mutex_unlock(&priv->mutex);
        /* Unlock */
        priv->write_watch_id = 0;
        if (priv->done) {
            GVERBOSE("%s done", priv->name);
            dbus_log_sender_shutdown(self, TRUE);
        } else {
            GVERBOSE("%s queue empty", priv->name);
        }
        return FALSE;
    }
}

static
gboolean
dbus_log_sender_write_callback(
    GIOChannel* source,
    GIOCondition condition,
    gpointer data)
{
    DBusLogSender* self = DBUSLOG_SENDER(data);
    DBusLogSenderPriv* priv = self->priv;
    gboolean disposition;
    dbus_log_sender_ref(self);
    if (condition & G_IO_OUT) {
        if (dbus_log_sender_write(self)) {
            disposition = G_SOURCE_CONTINUE;
        } else {
            GASSERT(!priv->write_watch_id);
            disposition = G_SOURCE_REMOVE;
        }
    } else {
        priv->write_watch_id = 0;
        dbus_log_sender_shutdown(self, FALSE);
        disposition = G_SOURCE_REMOVE;
    }
    dbus_log_sender_unref(self);
    return disposition;
}

static
void
dbus_log_sender_schedule_write(
    DBusLogSender* self)
{
    DBusLogSenderPriv* priv = self->priv;
    if (priv->io && !priv->write_watch_id) {
        if (dbus_log_sender_write(self)) {
            /* Something was left to write */
            GVERBOSE("%s scheduling write", priv->name);
            priv->write_watch_id = g_io_add_watch(priv->io,
                G_IO_OUT | G_IO_ERR | G_IO_HUP | G_IO_NVAL,
                dbus_log_sender_write_callback, self);
        }
    }
}

static
gboolean
dbus_log_sender_schedule_write_in_context(
    gpointer user_data)
{
    dbus_log_sender_schedule_write(DBUSLOG_SENDER(user_data));
    return G_SOURCE_REMOVE;
}

inline static
void
dbus_log_sender_put_uint32(
    DBusLogSender* self,
    guint offset,
    guint32 data)
{
    guchar* ptr = self->priv->packet + offset;
    *ptr++ = data & 0xff;
    *ptr++ = (data >> 8) & 0xff;
    *ptr++ = (data >> 16) & 0xff;
    *ptr = (data >> 24) & 0xff;
}

inline static
void
dbus_log_sender_put_uint64(
    DBusLogSender* self,
    guint offset,
    guint64 data)
{
    dbus_log_sender_put_uint32(self, offset, (guint32)(data));
    dbus_log_sender_put_uint32(self, offset + 4, (guint32)(data >> 32));
}

static
void
dbus_log_sender_fill_header(
    DBusLogSender* self,
    guint payload,
    DBUSLOG_PACKET_TYPE type)
{
    DBusLogSenderPriv* priv = self->priv;
    dbus_log_sender_put_uint32(self, DBUSLOG_PACKET_SIZE_OFFSET, payload);
    priv->packet[DBUSLOG_PACKET_TYPE_OFFSET] = type;
    priv->packet_size = DBUSLOG_PACKET_HEADER_SIZE + payload;
    priv->packet_written = 0;
    priv->packet_fixed_part = MIN(sizeof(priv->packet), priv->packet_size);
}

static
void
dbus_log_sender_prepare_bye(
    DBusLogSender* self)
{
    DBusLogSenderPriv* priv = self->priv;

    GASSERT(priv->done);
    GASSERT(!priv->current_message);
    GASSERT(!gutil_ring_size(priv->buffer));
    GASSERT(priv->packet_size == priv->packet_written);

    priv->bye = FALSE;
    dbus_log_sender_fill_header(self, 0, DBUSLOG_PACKET_TYPE_BYE);
}

static
void
dbus_log_sender_prepare_current_message(
    DBusLogSender* self)
{
    DBusLogSenderPriv* priv = self->priv;
    DBusLogMessage* msg = priv->current_message;

    GASSERT(priv->packet_size == priv->packet_written);
    GASSERT(priv->current_message);

    dbus_log_sender_fill_header(self,
        DBUSLOG_MESSAGE_PREFIX_SIZE + msg->length,
        DBUSLOG_PACKET_TYPE_MESSAGE);
    dbus_log_sender_put_uint64(self,
        DBUSLOG_MESSAGE_TIMESTAMP_OFFSET,
        msg->timestamp);
    dbus_log_sender_put_uint32(self,
        DBUSLOG_MESSAGE_INDEX_OFFSET,
        msg->index);
    dbus_log_sender_put_uint32(self,
        DBUSLOG_MESSAGE_CATEGORY_OFFSET,
        msg->category);
    priv->packet[DBUSLOG_MESSAGE_LEVEL_OFFSET] = msg->level;
}

static
void
dbus_log_sender_buffer_free_func(
    gpointer data)
{
    dbus_log_message_unref(data);
}

/*==========================================================================*
 * API
 *==========================================================================*/

DBusLogSender*
dbus_log_sender_new(
    const char* name,
    int backlog)
{
    int pipefd[2];
    if (pipe(pipefd) < 0) {
        GERR("Can't create pipe: %s", strerror(errno));
    } else {
        DBusLogSender* self = g_object_new(DBUSLOG_SENDER_TYPE, NULL);
        DBusLogSenderPriv* priv = self->priv;
        int writefd = pipefd[1];
        self->readfd = pipefd[0];
        priv->buffer = gutil_ring_new_full(0,
            dbus_log_sender_normalize_backlog(backlog),
            dbus_log_sender_buffer_free_func);
        self->name = priv->name = g_strdup(name);
        priv->io = g_io_channel_unix_new(writefd);
        if (priv->io) {
            g_io_channel_set_flags(priv->io, G_IO_FLAG_NONBLOCK, NULL);
            g_io_channel_set_encoding(priv->io, NULL, NULL);
            g_io_channel_set_buffered(priv->io, FALSE);
            g_io_channel_set_close_on_unref(priv->io, TRUE);
            return self;
        } else {
            dbus_log_sender_unref(self);
        }
        close(pipefd[0]);
        close(pipefd[1]);
    }
    return NULL;
}

DBusLogSender*
dbus_log_sender_ref(
    DBusLogSender* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(DBUSLOG_SENDER(self));
        return self;
    } else {
        return NULL;
    }
}

void
dbus_log_sender_unref(
    DBusLogSender* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(DBUSLOG_SENDER(self));
    }
}

void
dbus_log_sender_set_backlog(
    DBusLogSender* self,
    int backlog)
{
    if (G_LIKELY(self)) {
        DBusLogSenderPriv* priv = self->priv;

        gutil_ring_set_max_size(priv->buffer,
            dbus_log_sender_normalize_backlog(backlog));
    }
}

gboolean
dbus_log_sender_ping(
    DBusLogSender* self)
{
    if (G_LIKELY(self)) {
        DBusLogSenderPriv* priv = self->priv;
        /* Only ping if we have nothing pending */
        if (!priv->done && !priv->current_message &&
            !gutil_ring_size(priv->buffer)) {
            GASSERT(priv->packet_size == priv->packet_written);
            dbus_log_sender_fill_header(self, 0, DBUSLOG_PACKET_TYPE_PING);
            dbus_log_sender_schedule_write(self);
            return TRUE;
        }
    }
    return FALSE;
}

void
dbus_log_sender_send(
    DBusLogSender* self,
    DBusLogMessage* msg)
{
    if (G_LIKELY(self) && G_LIKELY(msg)) {
        DBusLogSenderPriv* priv = self->priv;
        /* Lock */
        g_mutex_lock(&priv->mutex);
        if (!priv->done) {
            if (priv->packet_size == priv->packet_written) {
                GASSERT(!priv->current_message);
                priv->current_message = dbus_log_message_ref(msg);
                dbus_log_sender_prepare_current_message(self);
                g_mutex_unlock(&priv->mutex);
                /* Unlock */
                g_main_context_invoke_full(priv->context, G_PRIORITY_DEFAULT,
                    dbus_log_sender_schedule_write_in_context,
                    dbus_log_sender_ref(self), g_object_unref);
                return;
            } else {
                /* Queue the message */
                if (!gutil_ring_can_put(priv->buffer, 1)) {
                    /* Buffer is full, drop the last half */
                    GDEBUG("%s queue full", priv->name);
                    gutil_ring_drop_last(priv->buffer,
                        gutil_ring_size(priv->buffer)/2);
                }
                if (gutil_ring_put(priv->buffer, msg)) {
                    dbus_log_message_ref(msg);
                }
            }
        }
        g_mutex_unlock(&priv->mutex);
        /* Unlock */
    }
}

void
dbus_log_sender_close(
    DBusLogSender* self,
    gboolean bye)
{
    if (G_LIKELY(self)) {
        DBusLogSenderPriv* priv = self->priv;
        if (!priv->done) {
            /* Lock */
            g_mutex_lock(&priv->mutex);
            /* Non-main threads are not supposed to change the 'done'
             * flag, they only check it. This function is supposed to
             * be called by the main thread. Therefore, there's no need
             * to re-check the flag under mutex. */
            priv->done = TRUE;
            if (priv->packet_size == priv->packet_written &&
                !gutil_ring_size(priv->buffer)) {
                dbus_log_sender_prepare_bye(self);
                g_mutex_unlock(&priv->mutex);
                /* Unlock */
                dbus_log_sender_schedule_write(self);
            } else {
                /* Will send it after flushing pending messages */
                priv->bye = TRUE;
                g_mutex_unlock(&priv->mutex);
                /* Unlock */
            }
        }
    }
}

void
dbus_log_sender_shutdown(
    DBusLogSender* self,
    gboolean flush)
{
    if (G_LIKELY(self)) {
        DBusLogSenderPriv* priv = self->priv;
        priv->packet_size = priv->packet_written = 0;
        priv->done = TRUE;
        priv->bye = FALSE;
        if (self->readfd >= 0) {
            close(self->readfd);
            self->readfd = -1;
        }
        if (priv->write_watch_id) {
            g_source_remove(priv->write_watch_id);
            priv->write_watch_id = 0;
        }
        if (priv->io) {
            g_io_channel_shutdown(priv->io, flush, NULL);
            g_io_channel_unref(priv->io);
            priv->io = NULL;
            g_signal_emit(self, dbus_log_sender_signals
                [DBUSLOG_SENDER_SIGNAL_CLOSED], 0);
        }
    }
}

gulong
dbus_log_sender_add_closed_handler(
    DBusLogSender* self,
    DBusLogSenderFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        DBUSLOG_SENDER_SIGNAL_CLOSED_NAME, G_CALLBACK(fn), user_data) : 0;
}

void
dbus_log_sender_remove_handler(
    DBusLogSender* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

int
dbus_log_sender_normalize_backlog(
    int backlog)
{
    return (backlog > 0) ? backlog :
        (backlog == 0) ? DBUSLOG_SENDER_DEFAULT_BACKLOG :
        GUTIL_RING_UNLIMITED_SIZE;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

/**
 * Per instance initializer
 */
static
void
dbus_log_sender_init(
    DBusLogSender* self)
{
    DBusLogSenderPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        DBUSLOG_SENDER_TYPE, DBusLogSenderPriv);
    g_mutex_init(&priv->mutex);
    priv->context = g_main_context_default();
    self->priv = priv;
    self->readfd = -1;
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
dbus_log_sender_dispose(
    GObject* object)
{
    DBusLogSender* self = DBUSLOG_SENDER(object);
    DBusLogSenderPriv* priv = self->priv;
    dbus_log_sender_shutdown(self, FALSE);
    gutil_ring_clear(priv->buffer);
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
dbus_log_sender_finalize(
    GObject* object)
{
    DBusLogSender* self = DBUSLOG_SENDER(object);
    DBusLogSenderPriv* priv = self->priv;
    dbus_log_message_unref(priv->current_message);
    gutil_ring_unref(priv->buffer);
    g_mutex_clear(&priv->mutex);
    g_free(priv->name);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
dbus_log_sender_class_init(
    DBusLogSenderClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GType class_type = G_OBJECT_CLASS_TYPE(klass);
    object_class->dispose = dbus_log_sender_dispose;
    object_class->finalize = dbus_log_sender_finalize;
    g_type_class_add_private(klass, sizeof(DBusLogSenderPriv));
    dbus_log_sender_signals[DBUSLOG_SENDER_SIGNAL_CLOSED] =
        g_signal_new(DBUSLOG_SENDER_SIGNAL_CLOSED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);

    /*
     * There seems to be no way to stop write() from generating
     * SIGPIPE signal, other than to disable SIGPIPE handling for
     * the entire process.
     */
    signal(SIGPIPE, SIG_IGN);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
