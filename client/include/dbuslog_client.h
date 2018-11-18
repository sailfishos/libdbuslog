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

#ifndef DBUSLOG_CLIENT_H
#define DBUSLOG_CLIENT_H

#include "dbuslog_client_types.h"
#include "dbuslog_category.h"
#include "dbuslog_message.h"

#include <gutil_types.h>

#include <gio/gio.h>

G_BEGIN_DECLS

/* dbus_log_client_new flags */
#define DBUSLOG_CLIENT_FLAG_AUTOSTART (0x01)

typedef struct dbus_log_client_priv DBusLogClientPriv;
typedef struct dbus_log_client_call DBusLogClientCall;

struct dbus_log_client {
    GObject object;
    DBusLogClientPriv* priv;
    GPtrArray* categories;
    DBUSLOG_LEVEL default_level;
    gboolean connected;
    gboolean started;
    int api_version;
    int backlog;
};

typedef
void
(*DBusLogClientFunc)(
    DBusLogClient* client,
    gpointer user_data);

typedef
void
(*DBusLogClientErrorFunc)(
    DBusLogClient* client,
    const GError* error,
    gpointer user_data);

typedef
void
(*DBusLogClientCallFunc)(
    DBusLogClientCall* call,
    const GError* error,
    gpointer user_data);

typedef
void
(*DBusLogClientCategoryFunc)(
    DBusLogClient* client,
    DBusLogCategory* category,
    guint index,
    gpointer user_data);

typedef
void
(*DBusLogClientMessageFunc)(
    DBusLogClient* client,
    DBusLogCategory* category,
    DBusLogMessage* message,
    gpointer user_data);

typedef
void
(*DBusLogClientSkipFunc)(
    DBusLogClient* client,
    guint count,
    gpointer user_data);

DBusLogClient*
dbus_log_client_new(
    GBusType bus,
    const char* service,
    const char* path,
    guint flags);

DBusLogClient*
dbus_log_client_ref(
    DBusLogClient* client);

void
dbus_log_client_unref(
    DBusLogClient* client);

DBusLogClientCall*
dbus_log_client_set_default_level(
    DBusLogClient* client,
    DBUSLOG_LEVEL level,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_set_backlog(
    DBusLogClient* client,
    int backlog,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_start(
    DBusLogClient* client,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_enable_category(
    DBusLogClient* client,
    const char* name,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_enable_categories(
    DBusLogClient* client,
    const GStrV* names,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_disable_category(
    DBusLogClient* client,
    const char* name,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_disable_categories(
    DBusLogClient* client,
    const GStrV* names,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_enable_pattern(
    DBusLogClient* client,
    const char* pattern,
    DBusLogClientCallFunc fn,
    gpointer user_data);

DBusLogClientCall*
dbus_log_client_disable_pattern(
    DBusLogClient* client,
    const char* pattern,
    DBusLogClientCallFunc fn,
    gpointer user_data);

void
dbus_log_client_call_cancel(
    DBusLogClientCall* call);

/* Signals */

gulong
dbus_log_client_add_connected_handler(
    DBusLogClient* client,
    DBusLogClientFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_connect_error_handler(
    DBusLogClient* client,
    DBusLogClientErrorFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_backlog_handler(
    DBusLogClient* client,
    DBusLogClientFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_category_added_handler(
    DBusLogClient* client,
    DBusLogClientCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_category_removed_handler(
    DBusLogClient* client,
    DBusLogClientCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_category_flags_handler(
    DBusLogClient* client,
    DBusLogClientCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_start_error_handler(
    DBusLogClient* client,
    DBusLogClientErrorFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_started_handler(
    DBusLogClient* client,
    DBusLogClientFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_message_handler(
    DBusLogClient* client,
    DBusLogClientMessageFunc fn,
    gpointer user_data);

gulong
dbus_log_client_add_skip_handler(
    DBusLogClient* client,
    DBusLogClientSkipFunc fn,
    gpointer user_data);

void
dbus_log_client_remove_handler(
    DBusLogClient* client,
    gulong id);

void
dbus_log_client_remove_handlers(
    DBusLogClient* client,
    gulong* ids,
    guint count);

G_END_DECLS

#endif /* DBUSLOG_CLIENT_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
