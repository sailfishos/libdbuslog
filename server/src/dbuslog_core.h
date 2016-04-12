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

#ifndef DBUSLOG_CORE_H
#define DBUSLOG_CORE_H

#include "dbuslog_server_types.h"
#include "dbuslog_category.h"
#include "dbuslog_sender.h"

typedef struct dbus_log_core DBusLogCore;

typedef
void
(*DBusLogCoreFunc)(
    DBusLogCore* core,
    gpointer user_data);

typedef
void
(*DBusLogCoreCategoryFunc)(
    DBusLogCore* core,
    DBusLogCategory* category,
    gpointer user_data);

typedef
void
(*DBusLogCoreCategoryFlagsFunc)(
    DBusLogCore* core,
    DBusLogCategory* category,
    guint mask,
    gpointer user_data);

DBusLogCore*
dbus_log_core_new(
    guint backlog);

DBusLogCore*
dbus_log_core_ref(
    DBusLogCore* core);

void
dbus_log_core_unref(
    DBusLogCore* core);

DBusLogSender*
dbus_log_core_new_sender(
    DBusLogCore* core,
    const char* name);

gboolean
dbus_log_core_remove_sender(
    DBusLogCore* core,
    DBusLogSender* sender);

DBUSLOG_LEVEL
dbus_log_core_default_level(
    DBusLogCore* core);

gboolean
dbus_log_core_set_default_level(
    DBusLogCore* core,
    DBUSLOG_LEVEL level);

DBusLogCategory*
dbus_log_core_new_category(
    DBusLogCore* core,
    const char* name,
    DBUSLOG_LEVEL level,
    gulong flags);

DBusLogCategory*
dbus_log_core_find_category(
    DBusLogCore* core,
    const char* name);

GPtrArray*
dbus_log_core_find_categories(
    DBusLogCore* core,
    const char* pattern);

GPtrArray*
dbus_log_core_get_categories(
    DBusLogCore* core);

gboolean
dbus_log_core_remove_category(
    DBusLogCore* core,
    const char* name);

void
dbus_log_core_remove_all_categories(
    DBusLogCore* core);

void
dbus_log_core_set_category_enabled(
    DBusLogCore* core,
    const char* name,
    gboolean enabled);

void
dbus_log_core_set_category_level(
    DBusLogCore* core,
    const char* name,
    DBUSLOG_LEVEL level);

gboolean
dbus_log_core_log(
    DBusLogCore* core,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* message);

gboolean
dbus_log_core_logv(
    DBusLogCore* core,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* format,
    va_list args);

/* Signals */

gulong
dbus_log_core_add_default_level_handler(
    DBusLogCore* core,
    DBusLogCoreFunc fn,
    gpointer user_data);

gulong
dbus_log_core_add_category_added_handler(
    DBusLogCore* core,
    DBusLogCoreCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_core_add_category_removed_handler(
    DBusLogCore* core,
    DBusLogCoreCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_core_add_category_level_handler(
    DBusLogCore* core,
    DBusLogCoreCategoryFunc fn,
    gpointer user_data);

gulong
dbus_log_core_add_category_flags_handler(
    DBusLogCore* core,
    DBusLogCoreCategoryFlagsFunc fn,
    gpointer user_data);

void
dbus_log_core_remove_handler(
    DBusLogCore* core,
    gulong id);

void
dbus_log_core_remove_handlers(
    DBusLogCore* core,
    gulong* ids,
    guint count);

#endif /* DBUSLOG_CORE_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
