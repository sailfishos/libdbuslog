/*
 * Copyright (C) 2016-2021 Jolla Ltd.
 * Copyright (C) 2016-2021 Slava Monich <slava.monich@jolla.com>
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

#include "dbuslog_server_dbus.h"
#include "dbuslog_server_p.h"
#include "dbuslog_server_log.h"

#include <gutil_strv.h>
#include <unistd.h>
#include <errno.h>

typedef struct dbus_log_server_dbus DBusLogServerDbus;
typedef struct dbus_log_name_watch {
    int ref_count;
    char* name;
    char* rule;
    DBusLogServerDbus* server;
    DBusPendingCall* get_name_owner_pending;
    gboolean vanished;
} DBusLogNameWatch;

/* Object definition */
struct dbus_log_server_dbus {
    DBusLogServer server;
    DBusConnection* conn;
    gulong last_watch_id;
    GHashTable* name_watch_table;
    GHashTable* id_watch_table;
};

typedef DBusLogServerClass DBusLogServerDbusClass;
GType dbus_log_server_dbus_get_type() G_GNUC_INTERNAL;
G_DEFINE_TYPE(DBusLogServerDbus, dbus_log_server_dbus, DBUSLOG_SERVER_TYPE)
#define PARENT_CLASS (dbus_log_server_dbus_parent_class)
#define DBUSLOG_SERVER_DBUS_TYPE (dbus_log_server_dbus_get_type())
#define DBUSLOG_SERVER_DBUS(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUSLOG_SERVER_DBUS_TYPE, DBusLogServerDbus))

#define DBUSLOG_INTERFACE "org.nemomobile.Logger"
#define DBUS_NAME_OWNER_CHANGED "NameOwnerChanged"
#define DBUS_GET_NAME_OWNER "GetNameOwner"

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
dbus_log_server_dbus_watch_unref(
    gpointer data)
{
    DBusLogNameWatch* watch = data;
    if (!(--(watch->ref_count))) {
        if (watch->get_name_owner_pending) {
            dbus_pending_call_cancel(watch->get_name_owner_pending);
            dbus_pending_call_unref(watch->get_name_owner_pending);
            watch->get_name_owner_pending = NULL;
        }
        dbus_bus_remove_match(watch->server->conn, watch->rule, NULL);
        g_hash_table_remove(watch->server->name_watch_table, watch->name);
        g_free(watch->name);
        g_free(watch->rule);
        g_slice_free(DBusLogNameWatch, watch);
    }
}

static
void
dbus_log_server_dbus_get_name_owner_reply(
    DBusPendingCall* call,
    void* user_data)
{
    DBusMessage* reply = dbus_pending_call_steal_reply(call);
    DBusLogNameWatch* watch = user_data;
    const char* owner = NULL;

    GASSERT(watch->get_name_owner_pending == call);
    dbus_pending_call_unref(watch->get_name_owner_pending);
    watch->get_name_owner_pending = NULL;

    if (dbus_message_get_type(reply) != DBUS_MESSAGE_TYPE_METHOD_RETURN ||
        !dbus_message_get_args(reply, NULL, DBUS_TYPE_STRING, &owner,
        DBUS_TYPE_INVALID) || !owner || !owner[0]) {
        if (!watch->vanished) {
            watch->vanished = TRUE;
            dbus_log_server_peer_vanished(&watch->server->server, watch->name);
        }
    }

    dbus_message_unref(reply);
}

static
gulong
dbus_log_server_dbus_watch_name(
    DBusLogServer* server,
    const char* name)
{
    gulong id;
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    DBusLogNameWatch* watch = g_hash_table_lookup(self->name_watch_table, name);

    /* Create DBusLogNameWatch if it's the first time we see this name */
    if (!watch) {
        DBusMessage* get_name_owner;
        watch = g_slice_new0(DBusLogNameWatch);
        watch->server = self;
        watch->name = g_strdup(name);
        watch->rule = g_strconcat("type='signal',interface='",
            DBUS_INTERFACE_DBUS, "',sender='", DBUS_SERVICE_DBUS, "',path='",
            DBUS_PATH_DBUS, "',member='", DBUS_NAME_OWNER_CHANGED,
            "',arg0='", name, "'", NULL);
        g_hash_table_replace(self->name_watch_table, watch->name, watch);

        /* Register to receive the signal */
        dbus_bus_add_match(self->conn, watch->rule, NULL);

        /* Request the current state (if may have missed the signal already) */
        get_name_owner = dbus_message_new_method_call(DBUS_SERVICE_DBUS,
            DBUS_PATH_DBUS, DBUS_INTERFACE_DBUS, DBUS_GET_NAME_OWNER);
        dbus_message_append_args(get_name_owner,
            DBUS_TYPE_STRING, &name,
            DBUS_TYPE_INVALID);
        dbus_connection_send_with_reply(self->conn, get_name_owner,
            &watch->get_name_owner_pending, DBUS_TIMEOUT_INFINITE);
        dbus_message_unref(get_name_owner);
        if (watch->get_name_owner_pending) {
            dbus_pending_call_set_notify(watch->get_name_owner_pending,
                dbus_log_server_dbus_get_name_owner_reply, watch, NULL);
        }
    }

    /* Bump the reference */
    watch->ref_count++;

    /* Generate unique id */
    id = (++(self->last_watch_id));
    while (!id || g_hash_table_contains(self->id_watch_table,
        GSIZE_TO_POINTER(id))) {
        id = (++(self->last_watch_id));
    }
    g_hash_table_replace(self->id_watch_table, GSIZE_TO_POINTER(id), watch);
    GASSERT(!watch->vanished);
    return id;
}

static
void
dbus_log_server_dbus_unwatch_name(
    DBusLogServer* server,
    gulong id)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    g_hash_table_remove(self->id_watch_table, GSIZE_TO_POINTER(id));
}

static
gboolean
dbus_log_server_dbus_handle_name_owner_changed(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    const char* name = NULL;
    const char* old = NULL;
    const char* new = NULL;
    if (dbus_message_get_args(msg, NULL,
        DBUS_TYPE_STRING, &name,
        DBUS_TYPE_STRING, &old,
        DBUS_TYPE_STRING, &new,
        DBUS_TYPE_INVALID) &&
        name && (!new || !new[0])) {
        DBusLogNameWatch* watch =
            g_hash_table_lookup(self->name_watch_table, name);
        if (watch && !watch->vanished) {
            watch->vanished = TRUE;
            dbus_log_server_peer_vanished(&self->server, name);
        }
        return TRUE;
    }
    return FALSE;
}

static
DBusMessage*
dbus_log_server_dbus_handle_get_interface_version(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    const dbus_int32_t version = DBUSLOG_INTERFACE_VERSION;
    DBusMessage* reply = dbus_message_new_method_return(msg);
    DBusMessageIter it;
    dbus_message_iter_init_append(reply, &it);
    dbus_message_iter_append_basic(&it, DBUS_TYPE_INT32, &version);
    return reply;
}

static
void
dbus_log_server_dbus_append_get_all(
    DBusLogServerDbus* self,
    DBusMessageIter* it)
{
    DBusLogCore* core = self->server.core;
    GPtrArray* cats = dbus_log_core_get_categories(core);
    DBusMessageIter a;
    guint i;
    const dbus_int32_t version = DBUSLOG_INTERFACE_VERSION;
    const dbus_int32_t loglevel = dbus_log_core_default_level(core);
    dbus_message_iter_append_basic(it, DBUS_TYPE_INT32, &version);
    dbus_message_iter_append_basic(it, DBUS_TYPE_INT32, &loglevel);
    dbus_message_iter_open_container(it, DBUS_TYPE_ARRAY, "(suui)", &a);
    for (i=0; i<cats->len; i++) {
        DBusMessageIter s;
        DBusLogCategory* cat = g_ptr_array_index(cats, i);
        const dbus_uint32_t id = cat->id;
        const dbus_uint32_t flags = cat->flags;
        const dbus_int32_t level = cat->level;
        dbus_message_iter_open_container(&a, DBUS_TYPE_STRUCT, NULL, &s);
        dbus_message_iter_append_basic(&s, DBUS_TYPE_STRING, &cat->name);
        dbus_message_iter_append_basic(&s, DBUS_TYPE_UINT32, &id);
        dbus_message_iter_append_basic(&s, DBUS_TYPE_UINT32, &flags);
        dbus_message_iter_append_basic(&s, DBUS_TYPE_INT32, &level);
        dbus_message_iter_close_container(&a, &s);
    }
    dbus_message_iter_close_container(it, &a);
}

static
DBusMessage*
dbus_log_server_dbus_handle_get_all(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    DBusMessageIter it;
    DBusMessage* reply = dbus_message_new_method_return(msg);
    dbus_message_iter_init_append(reply, &it);
    dbus_log_server_dbus_append_get_all(self, &it);
    return reply;
}

static
void
dbus_log_server_dbus_append_get_all2(
    DBusLogServerDbus* self,
    DBusMessageIter* it)
{
    DBusLogCore* core = self->server.core;
    const dbus_int32_t backlog = dbus_log_core_backlog(core);
    dbus_log_server_dbus_append_get_all(self, it);
    dbus_message_iter_append_basic(it, DBUS_TYPE_INT32, &backlog);
}

static
DBusMessage*
dbus_log_server_dbus_handle_get_all2(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    DBusMessageIter it;
    DBusMessage* reply = dbus_message_new_method_return(msg);
    dbus_message_iter_init_append(reply, &it);
    dbus_log_server_dbus_append_get_all2(self, &it);
    return reply;
}

static
DBusMessage*
dbus_log_server_error(
    DBusMessage* msg,
    int err)
{
    const char* type;
    const char* message;
    switch (err) {
    case -EACCES:
        type = DBUS_ERROR_ACCESS_DENIED;
        message = "Access denied";
        break;
    case -EINVAL:
        type = DBUS_ERROR_INVALID_ARGS;
        message = "Invalid argument(s)";
        break;
    default:
        type = DBUS_ERROR_FAILED;
        message = "Internal error";
        break;
    }
    return dbus_message_new_error(msg, type, message);
}

static
DBusMessage*
dbus_log_server_return(
    DBusMessage* msg,
    int err)
{
    return (err < 0) ? dbus_log_server_error(msg, err) :
        dbus_message_new_method_return(msg);
}

static
DBusMessage*
dbus_log_server_dbus_handle_set_default_level(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    int err = -EINVAL;
    dbus_int32_t level = DBUSLOG_LEVEL_UNDEFINED;
    if (dbus_message_get_args(msg, NULL,
        DBUS_TYPE_INT32, &level,
        DBUS_TYPE_INVALID)) {
        err = dbus_log_server_call_set_default_level(&self->server,
            dbus_message_get_sender(msg), level);
    }
    return dbus_log_server_return(msg, err);
}

static
DBusMessage*
dbus_log_server_dbus_handle_set_category_level(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    int err = -EINVAL;
    const char* name = NULL;
    dbus_int32_t level = DBUSLOG_LEVEL_UNDEFINED;
    if (dbus_message_get_args(msg, NULL,
        DBUS_TYPE_STRING, &name,
        DBUS_TYPE_INT32, &level,
        DBUS_TYPE_INVALID)) {
        err = dbus_log_server_call_set_category_level(&self->server, name,
            dbus_message_get_sender(msg), level);
    }
    return dbus_log_server_return(msg, err);
}

static
DBusMessage*
dbus_log_server_dbus_handle_log_open(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    int fd = dbus_log_server_call_log_open(&self->server,
        dbus_message_get_sender(msg));
    if (fd >= 0) {
        DBusMessageIter it;
        const dbus_uint32_t cookie = DBUSLOG_LOG_COOKIE;
        DBusMessage* reply = dbus_message_new_method_return(msg);
        dbus_message_iter_init_append(reply, &it);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_UNIX_FD, &fd);
        dbus_message_iter_append_basic(&it, DBUS_TYPE_UINT32, &cookie);
        return reply;
    } else {
        return dbus_log_server_error(msg, fd);
    }
}

static
DBusMessage*
dbus_log_server_dbus_handle_log_close(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    DBusMessageIter it;
    dbus_uint32_t cookie;
    dbus_message_iter_init(msg, &it);
    dbus_message_iter_get_basic(&it, &cookie);
    dbus_log_server_call_log_close(&self->server,
        dbus_message_get_sender(msg), cookie);
    return dbus_message_new_method_return(msg);
}

static
DBusMessage*
dbus_log_server_dbus_set_enabled(
    DBusLogServerDbus* self,
    DBusMessage* msg,
    gboolean enable)
{
    int err;
    GStrV* names = NULL;
    DBusMessageIter it, array;
    dbus_message_iter_init(msg, &it);
    dbus_message_iter_recurse(&it, &array);
    while (dbus_message_iter_get_arg_type(&array) == DBUS_TYPE_STRING) {
        const char* name = NULL;
        dbus_message_iter_get_basic(&array, &name);
        names = gutil_strv_add(names, name);
        dbus_message_iter_next(&array);
    }
    err = dbus_log_server_call_set_names_enabled(&self->server,
        dbus_message_get_sender(msg), names, enable);
    g_strfreev(names);
    return dbus_log_server_return(msg, err);
}

static
DBusMessage*
dbus_log_server_dbus_set_enabled_pattern(
    DBusLogServerDbus* self,
    DBusMessage* msg,
    gboolean enable)
{
    int err = -EINVAL;
    const char* pattern = NULL;
    if (dbus_message_get_args(msg, NULL,
        DBUS_TYPE_STRING, &pattern,
        DBUS_TYPE_INVALID)) {
        err = dbus_log_server_call_set_pattern_enabled(&self->server,
            dbus_message_get_sender(msg), pattern, enable);
    }
    return dbus_log_server_return(msg, err);
}

static
DBusMessage*
dbus_log_server_dbus_handle_category_enable(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    return dbus_log_server_dbus_set_enabled(self, msg, TRUE);
}

static
DBusMessage*
dbus_log_server_dbus_handle_category_enable_pattern(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    return dbus_log_server_dbus_set_enabled_pattern(self, msg, TRUE);
}

static
DBusMessage*
dbus_log_server_dbus_handle_category_disable(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    return dbus_log_server_dbus_set_enabled(self, msg, FALSE);
}

static
DBusMessage*
dbus_log_server_dbus_handle_category_disable_pattern(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    return dbus_log_server_dbus_set_enabled_pattern(self, msg, FALSE);
}

static
DBusMessage*
dbus_log_server_dbus_handle_set_backlog(
    DBusLogServerDbus* self,
    DBusMessage* msg)
{
    int err = -EINVAL;
    dbus_int32_t backlog = 0;
    if (dbus_message_get_args(msg, NULL,
        DBUS_TYPE_INT32, &backlog,
        DBUS_TYPE_INVALID)) {
        err = dbus_log_server_call_set_backlog(&self->server,
            dbus_message_get_sender(msg), backlog);
    }
    return dbus_log_server_return(msg, err);
}

static
void
dbus_log_server_dbus_emit_default_level_changed(
    DBusLogServer* server)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    DBusMessage* signal = dbus_message_new_signal(server->path,
        DBUSLOG_INTERFACE, "DefaultLevelChanged");
    if (signal) {
        const dbus_int32_t level = dbus_log_core_default_level(server->core);
        if (dbus_message_append_args(signal, DBUS_TYPE_INT32, &level,
            DBUS_TYPE_INVALID)) {
            dbus_connection_send(self->conn, signal, NULL);
        }
        dbus_message_unref(signal);
    }
}

static
void
dbus_log_server_dbus_emit_category_level_changed(
    DBusLogServer* server,
    guint id,
    DBUSLOG_LEVEL level)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    DBusMessage* signal = dbus_message_new_signal(server->path,
        DBUSLOG_INTERFACE, "CategoryLevelChanged");
    if (signal) {
        const dbus_uint32_t arg_id = id;
        const dbus_int32_t arg_level = level;
        if (dbus_message_append_args(signal,
            DBUS_TYPE_UINT32, &arg_id,
            DBUS_TYPE_INT32, &arg_level,
            DBUS_TYPE_INVALID)) {
            dbus_connection_send(self->conn, signal, NULL);
        }
        dbus_message_unref(signal);
    }
}

static
void
dbus_log_server_dbus_emit_category_added(
    DBusLogServer* server,
    const char* name,
    guint id,
    guint flags)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    DBusMessage* signal = dbus_message_new_signal(server->path,
        DBUSLOG_INTERFACE, "CategoryAdded");
    if (signal) {
        const dbus_uint32_t id_arg = id;
        const dbus_uint32_t flags_arg = flags;
        if (dbus_message_append_args(signal,
            DBUS_TYPE_STRING, &name,
            DBUS_TYPE_UINT32, &id_arg,
            DBUS_TYPE_UINT32, &flags_arg,
            DBUS_TYPE_INVALID)) {
            dbus_connection_send(self->conn, signal, NULL);
        }
        dbus_message_unref(signal);
    }
}

static
void
dbus_log_server_dbus_emit_category_removed(
    DBusLogServer* server,
    guint id)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    DBusMessage* signal = dbus_message_new_signal(server->path,
        DBUSLOG_INTERFACE, "CategoryRemoved");
    if (signal) {
        const dbus_uint32_t id_arg = id;
        if (dbus_message_append_args(signal,
            DBUS_TYPE_UINT32, &id_arg,
            DBUS_TYPE_INVALID)) {
            dbus_connection_send(self->conn, signal, NULL);
        }
        dbus_message_unref(signal);
    }
}

static
void
dbus_log_server_dbus_emit_category_flags_changed(
    DBusLogServer* server,
    guint id,
    guint flags)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    DBusMessage* signal = dbus_message_new_signal(server->path,
        DBUSLOG_INTERFACE, "CategoryFlagsChanged");
    if (signal) {
        const dbus_uint32_t id_arg = id;
        const dbus_uint32_t flags_arg = flags;
        if (dbus_message_append_args(signal,
            DBUS_TYPE_UINT32, &id_arg,
            DBUS_TYPE_UINT32, &flags_arg,
            DBUS_TYPE_INVALID)) {
            dbus_connection_send(self->conn, signal, NULL);
        }
        dbus_message_unref(signal);
    }
}

static
void
dbus_log_server_dbus_emit_backlog_changed(
    DBusLogServer* server,
    int backlog)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    DBusMessage* signal = dbus_message_new_signal(server->path,
        DBUSLOG_INTERFACE, "BacklogChanged");
    if (signal) {
        const dbus_int32_t value = backlog;
        if (dbus_message_append_args(signal,
            DBUS_TYPE_INT32, &value,
            DBUS_TYPE_INVALID)) {
            dbus_connection_send(self->conn, signal, NULL);
        }
        dbus_message_unref(signal);
    }
}

static
DBusHandlerResult
dbus_log_server_dbus_filter(
    DBusConnection* conn,
    DBusMessage* msg,
    void* user_data)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(user_data);
    const char* iface = dbus_message_get_interface(msg);
    const int type = dbus_message_get_type(msg);
    if (type == DBUS_MESSAGE_TYPE_METHOD_CALL) {
        if (self->server.started && !g_strcmp0(iface, DBUSLOG_INTERFACE)) {
            static const struct dbus_log_server_dbus_method {
                const char* method;
                const char* signature;
                DBusMessage* (*fn)(DBusLogServerDbus* self, DBusMessage* msg);
            } methods[] = {
                {
                    "GetInterfaceVersion", "",
                    dbus_log_server_dbus_handle_get_interface_version
                },{
                    "GetAll", "",
                    dbus_log_server_dbus_handle_get_all
                },{
                    "GetAll2", "",
                    dbus_log_server_dbus_handle_get_all2
                },{
                    "SetDefaultLevel", "i",
                    dbus_log_server_dbus_handle_set_default_level
                },{
                    "SetCategoryLevel", "si",
                    dbus_log_server_dbus_handle_set_category_level
                },{
                    "LogOpen", "",
                    dbus_log_server_dbus_handle_log_open
                },{
                    "LogClose", "u",
                    dbus_log_server_dbus_handle_log_close
                },{
                    "CategoryEnable", "as",
                    dbus_log_server_dbus_handle_category_enable
                },{
                    "CategoryEnablePattern", "s",
                    dbus_log_server_dbus_handle_category_enable_pattern
                },{
                    "CategoryDisable", "as",
                    dbus_log_server_dbus_handle_category_disable
                },{
                    "CategoryDisablePattern", "s",
                    dbus_log_server_dbus_handle_category_disable_pattern
                },{
                    "SetBacklog", "i",
                    dbus_log_server_dbus_handle_set_backlog
                }
            };
            guint i;
            const char* signature = dbus_message_get_signature(msg);
            const char* method = dbus_message_get_member(msg);
            for (i=0; i<G_N_ELEMENTS(methods); i++) {
                if (!g_strcmp0(method, methods[i].method) &&
                    !g_strcmp0(signature, methods[i].signature)) {
                    DBusMessage* reply = methods[i].fn(self, msg);
                    dbus_connection_send(self->conn, reply, NULL);
                    dbus_message_unref(reply);
                    return DBUS_HANDLER_RESULT_HANDLED;
                }
            }
        }
    } else if (type == DBUS_MESSAGE_TYPE_SIGNAL) {
        if (!g_strcmp0(iface, DBUS_INTERFACE_DBUS) &&
            !g_strcmp0(dbus_message_get_signature(msg), "sss") &&
            !g_strcmp0(dbus_message_get_member(msg), DBUS_NAME_OWNER_CHANGED) &&
            dbus_log_server_dbus_handle_name_owner_changed(self, msg)) {
            return DBUS_HANDLER_RESULT_HANDLED;
        }
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

static
gboolean
dbus_log_server_dbus_export(
    DBusLogServer* server)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    dbus_connection_add_filter(self->conn,
        dbus_log_server_dbus_filter, self, NULL);
    return TRUE;
}

static
void
dbus_log_server_dbus_unexport(
    DBusLogServer* server)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(server);
    dbus_connection_remove_filter(self->conn,
        dbus_log_server_dbus_filter,self);
}

static
DBusLogServer*
dbus_log_server_new_full(
    DBusConnection* conn,
    DBusBusType type,
    const char* path)
{
    DBusLogServerDbus* self = g_object_new(DBUSLOG_SERVER_DBUS_TYPE, NULL);
    DBusLogServer* server = &self->server;
    dbus_log_server_initialize(server, (type == DBUS_BUS_SYSTEM) ?
        DBUSLOG_BUS_SYSTEM : DBUSLOG_BUS_SESSION, path);
    self->conn = dbus_connection_ref(conn);
    return server;
}

/*==========================================================================*
 * API
 *==========================================================================*/

DBusLogServer*
dbus_log_server_new(
    DBusConnection* conn,
    const char* path)
{
    /* Assume system bus */
    return dbus_log_server_new_full(conn, DBUS_BUS_SYSTEM, path);
}

DBusLogServer*
dbus_log_server_new_type(
    DBusBusType type,
    const char* path)
{
    DBusLogServer* server = NULL;
    DBusConnection* conn = dbus_bus_get(type, NULL);
    if (conn) {
        server = dbus_log_server_new_full(conn, type, path);
        dbus_connection_unref(conn);
    }
    return server;
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

/**
 * Per instance initializer
 */
static
void
dbus_log_server_dbus_init(
    DBusLogServerDbus* self)
{
    self->name_watch_table = g_hash_table_new_full(g_str_hash, g_str_equal,
        NULL, NULL);
    self->id_watch_table = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, dbus_log_server_dbus_watch_unref);
}

/**
 * Final stage of deinitialization
 */
static
void
dbus_log_server_dbus_finalize(
    GObject* object)
{
    DBusLogServerDbus* self = DBUSLOG_SERVER_DBUS(object);
    dbus_connection_unref(self->conn);
    g_hash_table_destroy(self->name_watch_table);
    g_hash_table_destroy(self->id_watch_table);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
dbus_log_server_dbus_class_init(
    DBusLogServerDbusClass* klass)
{
    klass->watch_name = dbus_log_server_dbus_watch_name;
    klass->unwatch_name = dbus_log_server_dbus_unwatch_name;
    klass->export = dbus_log_server_dbus_export;
    klass->unexport = dbus_log_server_dbus_unexport;
    klass->emit_default_level_changed =
        dbus_log_server_dbus_emit_default_level_changed;
    klass->emit_category_level_changed =
        dbus_log_server_dbus_emit_category_level_changed;
    klass->emit_category_added = dbus_log_server_dbus_emit_category_added;
    klass->emit_category_removed = dbus_log_server_dbus_emit_category_removed;
    klass->emit_category_flags_changed =
        dbus_log_server_dbus_emit_category_flags_changed;
    klass->emit_backlog_changed = dbus_log_server_dbus_emit_backlog_changed;
    G_OBJECT_CLASS(klass)->finalize = dbus_log_server_dbus_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
