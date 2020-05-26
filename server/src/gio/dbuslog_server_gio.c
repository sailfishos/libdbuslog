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

#include "dbuslog_server_gio.h"
#include "dbuslog_server_p.h"
#include "dbuslog_server_log.h"

#include <gutil_misc.h>

#include <gio/gunixfdlist.h>
#include <errno.h>

/* Generated headers */
#include "org.nemomobile.Logger.h"

enum dbus_log_skeleton_method {
    DBUSLOG_METHOD_GET_INTERFACE_VERSION,
    DBUSLOG_METHOD_GET_ALL,
    DBUSLOG_METHOD_SET_DEFAULT_LEVEL,
    DBUSLOG_METHOD_SET_CATEGORY_LEVEL,
    DBUSLOG_METHOD_OPEN,
    DBUSLOG_METHOD_CLOSE,
    DBUSLOG_METHOD_ENABLE,
    DBUSLOG_METHOD_ENABLE_PATTERN,
    DBUSLOG_METHOD_DISABLE,
    DBUSLOG_METHOD_DISABLE_PATTERN,
    DBUSLOG_METHOD_GET_ALL2,
    DBUSLOG_METHOD_SET_BACKLOG,
    DBUSLOG_METHOD_COUNT
};

/* Object definition */
typedef struct dbus_log_server_gio {
    DBusLogServer server;
    OrgNemomobileLogger* iface;
    gulong iface_method_id[DBUSLOG_METHOD_COUNT];
    GDBusConnection* bus;
    guint name_watch_id;
} DBusLogServerGio;

typedef DBusLogServerClass DBusLogServerGioClass;
GType dbus_log_server_gio_get_type() G_GNUC_INTERNAL;
G_DEFINE_TYPE(DBusLogServerGio, dbus_log_server_gio, DBUSLOG_SERVER_TYPE)
#define PARENT_CLASS (dbus_log_server_gio_parent_class)
#define DBUSLOG_SERVER_GIO_TYPE (dbus_log_server_gio_get_type())
#define DBUSLOG_SERVER_GIO(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUSLOG_SERVER_GIO_TYPE, DBusLogServerGio))

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
dbus_log_server_gio_peer_vanished(
    GDBusConnection* bus,
    const gchar* name,
    gpointer user_data)
{
    GDEBUG("Name '%s' has disappeared", name);
    dbus_log_server_peer_vanished(DBUSLOG_SERVER(user_data), name);
}

static
gulong
dbus_log_server_gio_watch_name(
    DBusLogServer* server,
    const char* name)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->bus) {
        return g_bus_watch_name_on_connection(self->bus, name,
            G_BUS_NAME_WATCHER_FLAGS_NONE, NULL,
            dbus_log_server_gio_peer_vanished, self, NULL);
    }
    return 0;
}

static
void
dbus_log_server_gio_unwatch_name(
    DBusLogServer* server,
    gulong id)
{
    g_bus_unwatch_name(id);
}

static
gboolean
dbus_log_server_gio_export(
    DBusLogServer* server)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->bus) {
        GError* error = NULL;
        if (g_dbus_interface_skeleton_export(G_DBUS_INTERFACE_SKELETON(
            self->iface), self->bus, server->path, &error)) {
            return TRUE;
        } else {
            GERR("Could not export D-Bus object: %s", GERRMSG(error));
            g_error_free(error);
        }
    }
    return FALSE;
}

static
void
dbus_log_server_gio_unexport(
    DBusLogServer* server)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    g_dbus_interface_skeleton_unexport(G_DBUS_INTERFACE_SKELETON(self->iface));
}

static
void
dbus_log_server_gio_emit_default_level_changed(
    DBusLogServer* server)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->iface) {
        org_nemomobile_logger_emit_default_level_changed(self->iface,
            dbus_log_core_default_level(server->core));
    }
}

static
void
dbus_log_server_gio_emit_category_level_changed(
    DBusLogServer* server,
    guint id,
    DBUSLOG_LEVEL level)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->iface) {
        org_nemomobile_logger_emit_category_level_changed(self->iface,
            id, level);
    }
}

static
void
dbus_log_server_gio_emit_category_added(
    DBusLogServer* server,
    const char* name,
    guint id,
    guint flags)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->iface) {
        org_nemomobile_logger_emit_category_added(self->iface,
            name, id, flags);
    }
}

static
void
dbus_log_server_gio_emit_category_removed(
    DBusLogServer* server,
    guint id)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->iface) {
        org_nemomobile_logger_emit_category_removed(self->iface, id);
    }
}

static
void
dbus_log_server_gio_emit_flags_changed(
    DBusLogServer* server,
    guint id,
    guint flags)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->iface) {
        org_nemomobile_logger_emit_category_flags_changed(self->iface,
            id, flags);
    }
}

static
void
dbus_log_server_gio_emit_backlog_changed(
    DBusLogServer* server,
    int backlog)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(server);
    if (self->iface) {
        org_nemomobile_logger_emit_backlog_changed(self->iface, backlog);
    }
}

static
void
dbus_log_server_bus_acquired(
    GDBusConnection* bus,
    const char* name,
    gpointer user_data)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(user_data);
    DBusLogServer* server = &self->server;
    GDEBUG("Bus acquired");
    GASSERT(!self->bus);
    g_object_ref(self->bus = bus);
    if (server->started) {
        server->exported = dbus_log_server_gio_export(server);
    }
}

static
void
dbus_log_server_name_acquired(
    GDBusConnection* bus,
    const char* name,
    gpointer user_data)
{
    GDEBUG("Acquired service name '%s'", name);
}

static
void
dbus_log_server_name_lost(
    GDBusConnection* bus,
    const char* name,
    gpointer user_data)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(user_data);
    GWARN("'%s' service already running or access denied", name);
    if (self->bus) {
        g_dbus_interface_skeleton_unexport(
            G_DBUS_INTERFACE_SKELETON(self->iface));
        g_object_unref(self->bus);
        self->bus = NULL;
    }
}

static
void
dbus_log_server_return_error(
    GDBusMethodInvocation* call,
    int err)
{
    guint code;
    const char* message;
    switch (err) {
    case -EACCES:
        code = G_DBUS_ERROR_ACCESS_DENIED;
        message = "Access denied";
        break;
    case -EINVAL:
        code = G_DBUS_ERROR_INVALID_ARGS;
        message = "Invalid argument(s)";
        break;
    default:
        code = G_DBUS_ERROR_FAILED;
        message = "Internal error";
        break;
    }
    g_dbus_method_invocation_return_error_literal(call, G_DBUS_ERROR,
        code, message);
}

/*==========================================================================*
 * D-Bus methods
 *==========================================================================*/

static
gboolean
dbus_log_server_handle_get_interface_version(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    DBusLogServerGio* self)
{
    org_nemomobile_logger_complete_get_interface_version(proxy, call,
        DBUSLOG_INTERFACE_VERSION);
    return TRUE;
}

static
GVariant* /* floating */
dbus_log_server_get_categories_as_variant(
    DBusLogCore* core)
{
    GPtrArray* cats = dbus_log_core_get_categories(core);
    GVariantBuilder vb;
    guint i;
    g_variant_builder_init(&vb, G_VARIANT_TYPE("a(suui)"));
    for (i = 0; i < cats->len; i++) {
        DBusLogCategory* cat = g_ptr_array_index(cats, i);
        g_variant_builder_add(&vb, "(suui)", cat->name, cat->id, cat->flags,
            cat->level);
    }
    return g_variant_builder_end(&vb);
}

static
gboolean
dbus_log_server_handle_get_all(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    DBusLogServerGio* self)
{
    DBusLogCore* core = self->server.core;
    org_nemomobile_logger_complete_get_all(proxy, call,
        DBUSLOG_INTERFACE_VERSION, dbus_log_core_default_level(core),
        dbus_log_server_get_categories_as_variant(core));
    return TRUE;
}

static
gboolean
dbus_log_server_handle_get_all2(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    DBusLogServerGio* self)
{
    DBusLogCore* core = self->server.core;
    org_nemomobile_logger_complete_get_all2(proxy, call,
        DBUSLOG_INTERFACE_VERSION, dbus_log_core_default_level(core),
        dbus_log_server_get_categories_as_variant(core),
        dbus_log_core_backlog(core));
    return TRUE;
}

static
gboolean
dbus_log_server_handle_set_default_level(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    gint level,
    DBusLogServerGio* self)
{
    int err = dbus_log_server_call_set_default_level(&self->server,
        g_dbus_method_invocation_get_sender(call), level);
    if (err) {
        dbus_log_server_return_error(call, err);
    } else {
        org_nemomobile_logger_complete_set_default_level(proxy, call);
    }
    return TRUE;
}

static
gboolean
dbus_log_server_handle_set_category_level(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    const char* name,
    gint level,
    DBusLogServerGio* self)
{
    int err = dbus_log_server_call_set_category_level(&self->server,
        g_dbus_method_invocation_get_sender(call), name, level);
    if (err) {
        dbus_log_server_return_error(call, err);
    } else {
        org_nemomobile_logger_complete_set_category_level(proxy, call);
    }
    return TRUE;
}

static
gboolean
dbus_log_server_handle_open(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    GUnixFDList* fdlist,
    DBusLogServerGio* self)
{
    int err = -EFAULT;
    GASSERT(self->bus);
    if (self->bus) {
        const gint fd = dbus_log_server_call_log_open(&self->server,
            g_dbus_method_invocation_get_sender(call));
        if (fd >= 0) {
            GUnixFDList* fdl = g_unix_fd_list_new_from_array(&fd, 1);
            org_nemomobile_logger_complete_log_open(proxy, call, fdl,
                g_variant_new_handle(fd), DBUSLOG_LOG_COOKIE);
            g_object_unref(fdl);
            return TRUE;
        }
        err = fd;
    }
    dbus_log_server_return_error(call, err);
    return TRUE;
}

static
gboolean
dbus_log_server_handle_close(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    guint cookie,
    DBusLogServerGio* self)
{
    dbus_log_server_call_log_close(&self->server,
        g_dbus_method_invocation_get_sender(call), cookie);
    org_nemomobile_logger_complete_log_close(proxy, call);
    return TRUE;
}

static
gboolean
dbus_log_server_handle_enable(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    const GStrV* names,
    DBusLogServerGio* self)
{
    const int err = dbus_log_server_call_set_names_enabled(&self->server,
        g_dbus_method_invocation_get_sender(call), names, TRUE);
    if (err) {
        dbus_log_server_return_error(call, err);
    } else {
        org_nemomobile_logger_complete_category_enable(proxy, call);
    }
    return TRUE;
}

static
gboolean
dbus_log_server_handle_enable_pattern(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    const char* pattern,
    DBusLogServerGio* self)
{
    const int err = dbus_log_server_call_set_pattern_enabled(&self->server,
        g_dbus_method_invocation_get_sender(call), pattern, TRUE);
    if (err) {
        dbus_log_server_return_error(call, err);
    } else {
        org_nemomobile_logger_complete_category_enable_pattern(proxy, call);
    }
    return TRUE;
}

static
gboolean
dbus_log_server_handle_disable(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    const GStrV* names,
    DBusLogServerGio* self)
{
    const int err = dbus_log_server_call_set_names_enabled(&self->server,
        g_dbus_method_invocation_get_sender(call), names, FALSE);
    if (err) {
        dbus_log_server_return_error(call, err);
    } else {
        org_nemomobile_logger_complete_category_enable(proxy, call);
    }
    return TRUE;
}

static
gboolean
dbus_log_server_handle_disable_pattern(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    const char* pattern,
    DBusLogServerGio* self)
{
    const int err = dbus_log_server_call_set_pattern_enabled(&self->server,
        g_dbus_method_invocation_get_sender(call), pattern, FALSE);
    if (err) {
        dbus_log_server_return_error(call, err);
    } else {
        org_nemomobile_logger_complete_category_enable_pattern(proxy, call);
    }
    return TRUE;
}

static
gboolean
dbus_log_server_handle_set_backlog(
    OrgNemomobileLogger* proxy,
    GDBusMethodInvocation* call,
    gint backlog,
    DBusLogServerGio* self)
{
    const int err = dbus_log_server_call_set_backlog(&self->server,
        g_dbus_method_invocation_get_sender(call), backlog);
    if (err) {
        dbus_log_server_return_error(call, err);
    } else {
        org_nemomobile_logger_complete_set_backlog(proxy, call);
    }
    return TRUE;
}

/*==========================================================================*
 * API
 *==========================================================================*/

DBusLogServer*
dbus_log_server_new(
    GBusType bus_type,
    const char* service,
    const char* path)
{
    DBusLogServerGio* self = g_object_new(DBUSLOG_SERVER_GIO_TYPE, NULL);
    DBusLogServer* server = &self->server;
    dbus_log_server_initialize(server, (bus_type == G_BUS_TYPE_SYSTEM) ?
        DBUSLOG_BUS_SYSTEM : DBUSLOG_BUS_SESSION, path);

    /* Attach to the D-Bus signals */
    self->iface = org_nemomobile_logger_skeleton_new();
    self->iface_method_id[DBUSLOG_METHOD_GET_INTERFACE_VERSION] =
        g_signal_connect(self->iface, "handle-get-interface-version",
        G_CALLBACK(dbus_log_server_handle_get_interface_version), self);
    self->iface_method_id[DBUSLOG_METHOD_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all",
        G_CALLBACK(dbus_log_server_handle_get_all), self);
    self->iface_method_id[DBUSLOG_METHOD_GET_ALL] =
        g_signal_connect(self->iface, "handle-get-all2",
        G_CALLBACK(dbus_log_server_handle_get_all2), self);
    self->iface_method_id[DBUSLOG_METHOD_SET_DEFAULT_LEVEL] =
        g_signal_connect(self->iface, "handle-set-default-level",
        G_CALLBACK(dbus_log_server_handle_set_default_level), self);
    self->iface_method_id[DBUSLOG_METHOD_SET_CATEGORY_LEVEL] =
        g_signal_connect(self->iface, "handle-set-category-level",
        G_CALLBACK(dbus_log_server_handle_set_category_level), self);
    self->iface_method_id[DBUSLOG_METHOD_OPEN] =
        g_signal_connect(self->iface, "handle-log-open",
        G_CALLBACK(dbus_log_server_handle_open), self);
    self->iface_method_id[DBUSLOG_METHOD_OPEN] =
        g_signal_connect(self->iface, "handle-log-close",
        G_CALLBACK(dbus_log_server_handle_close), self);
    self->iface_method_id[DBUSLOG_METHOD_ENABLE] =
        g_signal_connect(self->iface, "handle-category-enable",
        G_CALLBACK(dbus_log_server_handle_enable), self);
    self->iface_method_id[DBUSLOG_METHOD_ENABLE_PATTERN] =
        g_signal_connect(self->iface, "handle-category-enable-pattern",
        G_CALLBACK(dbus_log_server_handle_enable_pattern), self);
    self->iface_method_id[DBUSLOG_METHOD_DISABLE] =
        g_signal_connect(self->iface, "handle-category-disable",
        G_CALLBACK(dbus_log_server_handle_disable), self);
    self->iface_method_id[DBUSLOG_METHOD_DISABLE_PATTERN] =
        g_signal_connect(self->iface, "handle-category-disable-pattern",
        G_CALLBACK(dbus_log_server_handle_disable_pattern), self);
    self->iface_method_id[DBUSLOG_METHOD_SET_BACKLOG] =
        g_signal_connect(self->iface, "handle-set-backlog",
        G_CALLBACK(dbus_log_server_handle_set_backlog), self);

    /* And start watching the requested name */
    if (service) {
        self->name_watch_id = g_bus_own_name(bus_type, service,
            G_BUS_NAME_OWNER_FLAGS_REPLACE, dbus_log_server_bus_acquired,
            dbus_log_server_name_acquired, dbus_log_server_name_lost, self,
            NULL);
    } else {
        GError* error = NULL;
        self->bus = g_bus_get_sync(bus_type, NULL, &error);
        if (!self->bus) {
            GERR("Could not get D-Bus connection: %s", GERRMSG(error));
            g_error_free(error);
        }
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
dbus_log_server_gio_init(
    DBusLogServerGio* self)
{
}

/**
 * Final stage of deinitialization
 */
static
void
dbus_log_server_gio_finalize(
    GObject* object)
{
    DBusLogServerGio* self = DBUSLOG_SERVER_GIO(object);
    if (self->name_watch_id) {
        g_bus_unown_name(self->name_watch_id);
    }
    if (self->bus) {
        g_object_unref(self->bus);
    }

    gutil_disconnect_handlers(self->iface, self->iface_method_id,
        G_N_ELEMENTS(self->iface_method_id));
    g_object_unref(self->iface);

    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
dbus_log_server_gio_class_init(
    DBusLogServerGioClass* klass)
{
    klass->watch_name = dbus_log_server_gio_watch_name;
    klass->unwatch_name = dbus_log_server_gio_unwatch_name;
    klass->export = dbus_log_server_gio_export;
    klass->unexport = dbus_log_server_gio_unexport;
    klass->emit_default_level_changed =
        dbus_log_server_gio_emit_default_level_changed;
    klass->emit_category_level_changed =
        dbus_log_server_gio_emit_category_level_changed;
    klass->emit_category_added = dbus_log_server_gio_emit_category_added;
    klass->emit_category_removed = dbus_log_server_gio_emit_category_removed;
    klass->emit_category_flags_changed = dbus_log_server_gio_emit_flags_changed;
    klass->emit_backlog_changed = dbus_log_server_gio_emit_backlog_changed;
    G_OBJECT_CLASS(klass)->finalize = dbus_log_server_gio_finalize;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
