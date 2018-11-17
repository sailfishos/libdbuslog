/*
 * Copyright (C) 2016-2018 Jolla Ltd.
 * Copyright (C) 2016-2018 Slava Monich <slava.monich@jolla.com>
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

#ifndef DBUSLOG_SENDER_H
#define DBUSLOG_SENDER_H

#include "dbuslog_server_types.h"
#include "dbuslog_message.h"

#include <glib-object.h>

typedef struct dbus_log_sender_priv DBusLogSenderPriv;

typedef struct dbus_log_sender {
    GObject object;
    DBusLogSenderPriv* priv;
    const char* name;
    int readfd;
} DBusLogSender;

typedef
void
(*DBusLogSenderFunc)(
    DBusLogSender* sender,
    gpointer user_data);

DBusLogSender*
dbus_log_sender_new(
    const char* name,
    int backlog);

DBusLogSender*
dbus_log_sender_ref(
    DBusLogSender* sender);

void
dbus_log_sender_unref(
    DBusLogSender* sender);

void
dbus_log_sender_set_backlog(
    DBusLogSender* sender,
    int backlog);

gboolean
dbus_log_sender_ping(
    DBusLogSender* sender);

void
dbus_log_sender_send(
    DBusLogSender* sender,
    DBusLogMessage* message);

void
dbus_log_sender_close(
    DBusLogSender* sender,
    gboolean bye);

void
dbus_log_sender_shutdown(
    DBusLogSender* sender,
    gboolean flush);

gulong
dbus_log_sender_add_closed_handler(
    DBusLogSender* sender,
    DBusLogSenderFunc fn,
    gpointer user_data);

void
dbus_log_sender_remove_handler(
    DBusLogSender* sender,
    gulong id);

int
dbus_log_sender_normalize_backlog(
    int backlog);

#endif /* DBUSLOG_SENDER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
