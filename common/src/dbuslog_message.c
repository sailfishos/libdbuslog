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

#include "dbuslog_message.h"

#include <gutil_macros.h>
#include <gutil_log.h>

#include <glib/gprintf.h>

typedef struct dbus_log_message_priv {
    DBusLogMessage pub;
    gint ref_count;
} DBusLogMessagePriv;

static
inline
DBusLogMessagePriv*
dbus_log_message_cast(
    DBusLogMessage* msg)
{
    return G_CAST(msg, DBusLogMessagePriv, pub);
}

static
DBusLogMessagePriv*
dbus_log_message_alloc()
{
    DBusLogMessagePriv* priv = g_slice_new0(DBusLogMessagePriv);
    priv->ref_count = 1;
    return priv;
}

DBusLogMessage*
dbus_log_message_new(
    const char* str)
{
    DBusLogMessagePriv* priv = dbus_log_message_alloc();
    DBusLogMessage* msg = &priv->pub;
    if (str) {
        msg->length = strlen(str);
        msg->string = g_new(char, msg->length + 1);
        memcpy(msg->string, str, msg->length + 1);
    }
    return msg;
}

DBusLogMessage*
dbus_log_message_new_va(
    const char* format,
    va_list args)
{
    DBusLogMessagePriv* priv = dbus_log_message_alloc();
    DBusLogMessage* msg = &priv->pub;
    msg->length = g_vasprintf(&msg->string, format, args);
    return msg;
}

static
void
dbus_log_message_finalize(
    DBusLogMessagePriv* priv)
{
    g_free(priv->pub.string);
    g_slice_free(DBusLogMessagePriv, priv);
}

DBusLogMessage*
dbus_log_message_ref(
    DBusLogMessage* msg)
{
    if (G_LIKELY(msg)) {
        DBusLogMessagePriv* priv = dbus_log_message_cast(msg);
        GASSERT(priv->ref_count > 0);
        g_atomic_int_inc(&priv->ref_count);
    }
    return msg;
}

void
dbus_log_message_unref(
    DBusLogMessage* msg)
{
    if (G_LIKELY(msg)) {
        DBusLogMessagePriv* priv = dbus_log_message_cast(msg);
        GASSERT(priv->ref_count > 0);
        if (g_atomic_int_dec_and_test(&priv->ref_count)) {
            dbus_log_message_finalize(priv);
        }
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
