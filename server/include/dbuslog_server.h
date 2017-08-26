/*
 * Copyright (C) 2016-2017 Jolla Ltd.
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

#ifndef DBUSLOG_SERVER_H
#define DBUSLOG_SERVER_H

#include "dbuslog_server_types.h"
#include "dbuslog_protocol.h"

G_BEGIN_DECLS

typedef struct dbus_log_server DBusLogServer;

typedef
void
(*DBusLogServerCategoryFunc)(
    DBusLogServer* server,
    const char* category,
    gpointer user_data);

typedef
void
(*DBusLogServerCategoryLevelFunc)(
    DBusLogServer* server,
    const char* category,
    DBUSLOG_LEVEL level,
    gpointer user_data);

DBusLogServer*
dbus_log_server_ref(
    DBusLogServer* server);

void
dbus_log_server_unref(
    DBusLogServer* server);

void
dbus_log_server_start(
    DBusLogServer* server);

void
dbus_log_server_stop(
    DBusLogServer* server);

gboolean
dbus_log_server_set_access_policy(
    DBusLogServer* server,
    const char* spec);

DBUSLOG_LEVEL
dbus_log_server_default_level(
    DBusLogServer* server);

gboolean
dbus_log_server_set_default_level(
    DBusLogServer* server,
    DBUSLOG_LEVEL level);

void
dbus_log_server_add_category(
    DBusLogServer* server,
    const char* name,
    DBUSLOG_LEVEL level,
    gulong flags);

gboolean
dbus_log_server_remove_category(
    DBusLogServer* server,
    const char* name);

void
dbus_log_server_remove_all_categories(
    DBusLogServer* server);

gboolean
dbus_log_server_log(
    DBusLogServer* server,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* message);

gboolean
dbus_log_server_logv(
    DBusLogServer* server,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* format,
    va_list args);

/* Signals */

gulong
dbus_log_server_add_category_enabled_handler(
    DBusLogServer* server,
    DBusLogServerCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_server_add_category_disabled_handler(
    DBusLogServer* server,
    DBusLogServerCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_server_add_category_level_handler(
    DBusLogServer* server,
    DBusLogServerCategoryLevelFunc fn,
    gpointer user_data);

void
dbus_log_server_remove_handler(
    DBusLogServer* server,
    gulong id);

void
dbus_log_server_remove_handlers(
    DBusLogServer* server,
    gulong* ids,
    guint count);

G_END_DECLS

#endif /* DBUSLOG_SERVER_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
