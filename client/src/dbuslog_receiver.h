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

#ifndef DBUSLOG_RECEIVER_H
#define DBUSLOG_RECEIVER_H

#include "dbuslog_client_types.h"
#include "dbuslog_message.h"

typedef struct dbus_log_receiver DBusLogReceiver;

typedef
void
(*DBusLogReceiverMessageFunc)(
    DBusLogReceiver* receiver,
    DBusLogMessage* message,
    gpointer user_data);

typedef
void
(*DBusLogReceiverSkipFunc)(
    DBusLogReceiver* receiver,
    guint count,
    gpointer user_data);

typedef
void
(*DBusLogReceiverFunc)(
    DBusLogReceiver* receiver,
    gpointer user_data);

DBusLogReceiver*
dbus_log_receiver_new(
    int fd,
    gboolean close_when_done);

DBusLogReceiver*
dbus_log_receiver_ref(
    DBusLogReceiver* receiver);

void
dbus_log_receiver_unref(
    DBusLogReceiver* receiver);

void
dbus_log_receiver_pause(
    DBusLogReceiver* receiver);

void
dbus_log_receiver_resume(
    DBusLogReceiver* receiver);

void
dbus_log_receiver_close(
    DBusLogReceiver* receiver);

gulong
dbus_log_receiver_add_message_handler(
    DBusLogReceiver* receiver,
    DBusLogReceiverMessageFunc fn,
    gpointer user_data);

gulong
dbus_log_receiver_add_skip_handler(
    DBusLogReceiver* receiver,
    DBusLogReceiverSkipFunc fn,
    gpointer user_data);

gulong
dbus_log_receiver_add_closed_handler(
    DBusLogReceiver* receiver,
    DBusLogReceiverFunc fn,
    gpointer user_data);

void
dbus_log_receiver_remove_handler(
    DBusLogReceiver* receiver,
    gulong id);

void
dbus_log_receiver_remove_handlers(
    DBusLogReceiver* receiver,
    gulong* ids,
    guint count);

#endif /* DBUSLOG_RECEIVER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
