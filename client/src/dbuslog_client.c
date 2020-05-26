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

#include "dbuslog_client.h"
#include "dbuslog_client_log.h"
#include "dbuslog_receiver.h"

#include <gutil_misc.h>
#include <gutil_strv.h>

#include <gio/gunixfdlist.h>

/* Generated headers */
#include "org.nemomobile.Logger.h"

typedef
gboolean
(*DBusLogClientCallFinishFunc)(
    OrgNemomobileLogger* proxy,
    GAsyncResult* result,
    GError** error);

struct dbus_log_client_call {
    DBusLogClient* client;
    DBusLogClientCallFinishFunc finish;
    DBusLogClientCallFunc fn;
    gpointer user_data;
    GCancellable* cancel;
};

enum dbus_log_client_proxy_signal {
    PROXY_SIGNAL_CATEGORY_ADDED,
    PROXY_SIGNAL_CATEGORY_REMOVED,
    PROXY_SIGNAL_CATEGORY_FLAGS_CHANGED,
    PROXY_SIGNAL_COUNT
};

enum dbus_log_client_receiver_signal {
    RECEIVER_SIGNAL_MESSAGE,
    RECEIVER_SIGNAL_SKIP,
    RECEIVER_SIGNAL_CLOSED,
    RECEIVER_SIGNAL_COUNT
};

/* Initialization context */
typedef struct dbus_log_client_init {
    GDBusConnection* bus;
    DBusLogClient* client;
    GHashTable* categories;
    GCancellable* cancel;
    OrgNemomobileLogger* proxy;
} DBusLogClientInit;

/* Object definition */
struct dbus_log_client_priv {
    char* path;
    guint flags;
    int cookie;
    GHashTable* categories;
    guint name_watch_id;
    DBusLogClientInit* init;
    DBusLogClientCall* autostart;
    OrgNemomobileLogger* proxy;
    gulong proxy_signal_id[PROXY_SIGNAL_COUNT];
    DBusLogReceiver* receiver;
    gulong receiver_signal_id[RECEIVER_SIGNAL_COUNT];
};

typedef GObjectClass DBusLogClientClass;
G_DEFINE_TYPE(DBusLogClient, dbus_log_client, G_TYPE_OBJECT)
#define PARENT_CLASS (dbus_log_client_parent_class)
#define DBUSLOG_CLIENT_TYPE (dbus_log_client_get_type())
#define DBUSLOG_CLIENT(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUSLOG_CLIENT_TYPE, DBusLogClient))

enum dbus_log_client_signal {
    SIGNAL_CONNECT_ERROR,
    SIGNAL_CONNECTED_CHANGED,
    SIGNAL_BACKLOG_CHANGED,
    SIGNAL_CATEGORY_ADDED,
    SIGNAL_CATEGORY_REMOVED,
    SIGNAL_CATEGORY_FLAGS,
    SIGNAL_LOG_START_ERROR,
    SIGNAL_LOG_STARTED_CHANGED,
    SIGNAL_LOG_MESSAGE,
    SIGNAL_LOG_SKIP,
    SIGNAL_COUNT
};

#define SIGNAL_CONNECT_ERROR_NAME       "dbuslog-client-connect-error"
#define SIGNAL_CONNECTED_CHANGED_NAME   "dbuslog-client-connected-changed"
#define SIGNAL_BACKLOG_CHANGED_NAME     "dbuslog-client-backlog-changed"
#define SIGNAL_CATEGORY_ADDED_NAME      "dbuslog-client-category-added"
#define SIGNAL_CATEGORY_REMOVED_NAME    "dbuslog-client-category-removed"
#define SIGNAL_CATEGORY_FLAGS_NAME      "dbuslog-client-category-flags"
#define SIGNAL_LOG_START_ERROR_NAME     "dbuslog-client-log-start-error"
#define SIGNAL_LOG_STARTED_CHANGED_NAME "dbuslog-client-log-started-changed"
#define SIGNAL_LOG_MESSAGE_NAME         "dbuslog-client-log-message"
#define SIGNAL_LOG_SKIP_NAME            "dbuslog-client-log-skip"

static guint dbus_log_client_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
dbus_log_client_emit(
    DBusLogClient* self,
    guint signal_id,
    ...)
{
    va_list va;
    va_start(va, signal_id);
    dbus_log_client_ref(self);
    g_signal_emit_valist(self, dbus_log_client_signals[signal_id], 0, va);
    dbus_log_client_unref(self);
    va_end(va);
}

static
void
dbus_log_client_call_free(
    DBusLogClientCall* call)
{
    if (G_LIKELY(call)) {
        dbus_log_client_unref(call->client);
        g_object_unref(call->cancel);
        g_slice_free(DBusLogClientCall, call);
    }
}

static
DBusLogClientCall*
dbus_log_client_call_new(
    DBusLogClient* client,
    DBusLogClientCallFinishFunc finish,
    DBusLogClientCallFunc fn,
    gpointer user_data)
{
    DBusLogClientCall* call = g_slice_new0(DBusLogClientCall);
    call->client = dbus_log_client_ref(client);
    call->finish = finish;
    call->fn = fn;
    call->user_data = user_data;
    call->cancel = g_cancellable_new();
    return call;
}

static
DBusLogCategory*
dbus_log_client_category(
    DBusLogClient* self,
    guint id)
{
    DBusLogClientPriv* priv = self->priv;
    return id ? g_hash_table_lookup(priv->categories, GINT_TO_POINTER(id)) :
        NULL;
}

static
gint
dbus_log_client_category_index(
    DBusLogClient* self,
    DBusLogCategory* category)
{
    if (category) {
        guint i;
        for (i=0; i<self->categories->len; i++) {
            if (g_ptr_array_index(self->categories, i) == category) {
                return i;
            }
        }
    }
    return -1;
}

static
void
dbus_log_client_stop(
    DBusLogClient* self,
    gboolean emit_signals)
{
    DBusLogClientPriv* priv = self->priv;
    if (priv->autostart) {
        dbus_log_client_call_cancel(priv->autostart);
        priv->autostart = NULL;
    }
    if (priv->receiver) {
        dbus_log_receiver_remove_handlers(priv->receiver,
            priv->receiver_signal_id, G_N_ELEMENTS(priv->receiver_signal_id));
        dbus_log_receiver_close(priv->receiver);
        dbus_log_receiver_unref(priv->receiver);
        priv->receiver = NULL;
    }
    if (self->started) {
        self->started = FALSE;
        if (emit_signals) {
            dbus_log_client_emit(self, SIGNAL_LOG_STARTED_CHANGED);
        }
    }
}

static
void
dbus_log_client_disconnect(
    DBusLogClient* self,
    gboolean emit_signals)
{
    DBusLogClientPriv* priv = self->priv;
    dbus_log_client_stop(self, emit_signals);
    g_hash_table_remove_all(priv->categories);
    if (self->categories->len) {
        g_ptr_array_unref(self->categories);
        self->categories = g_ptr_array_new_with_free_func(
            dbus_log_category_free);
    }
    if (priv->proxy) {
        gutil_disconnect_handlers(priv->proxy,
            priv->proxy_signal_id, G_N_ELEMENTS(priv->proxy_signal_id));
        g_object_unref(priv->proxy);
        priv->proxy = NULL;
    }
    if (self->connected) {
        self->connected = FALSE;
        if (emit_signals) {
            dbus_log_client_emit(self, SIGNAL_CONNECTED_CHANGED);
        }
    }
}

static
gboolean
dbus_log_client_category_remove(
    DBusLogClient* self,
    guint id)
{
    DBusLogCategory* category = dbus_log_client_category(self, id);
    const int index = dbus_log_client_category_index(self, category);
    if (index >= 0) {
        guint i;
        DBusLogClientPriv* priv = self->priv;
        GPtrArray* old_array = self->categories;
        GPtrArray* new_array = g_ptr_array_new_full(old_array->len-1,
            dbus_log_category_free);
        GDEBUG_("%d (%s)", index, category->name);
        for (i=0; i<(guint)index; i++) {
            g_ptr_array_add(new_array, dbus_log_category_ref(
                g_ptr_array_index(old_array, i)));
        }
        for (i++; i<old_array->len; i++) {
            g_ptr_array_add(new_array, dbus_log_category_ref(
                g_ptr_array_index(old_array, i)));
        }
        self->categories = new_array;
        GVERIFY(g_hash_table_remove(priv->categories, GINT_TO_POINTER(id)));
        dbus_log_category_ref(category);
        dbus_log_client_emit(self, SIGNAL_CATEGORY_REMOVED, category, index);
        dbus_log_category_unref(category);
        g_ptr_array_unref(old_array);
        return TRUE;
    } else {
        return FALSE;
    }
}

static
void
dbus_log_client_category_removed(
    OrgNemomobileLogger* proxy,
    guint id,
    gpointer user_data)
{
    GDEBUG_("%u", id);
    GVERIFY(dbus_log_client_category_remove(DBUSLOG_CLIENT(user_data), id));
}

static
void
dbus_log_client_category_added(
    OrgNemomobileLogger* proxy,
    const char* name,
    guint id,
    guint flags,
    gpointer user_data)
{
    DBusLogClient* self = DBUSLOG_CLIENT(user_data);
    DBusLogClientPriv* priv = self->priv;
    DBusLogCategory* category = dbus_log_category_new(name, id);
    GPtrArray* old_array;
    GPtrArray* new_array;
    guint i;
    int index = -1;
    GDEBUG_("%s %u 0x%04x", name, id, flags);
    GVERIFY_FALSE(dbus_log_client_category_remove(self, id));

    /* Hashtable takes ownership of this DBusLogCategory */
    category->flags = flags;
    g_hash_table_replace(priv->categories, GINT_TO_POINTER(id), category);

    /* Replace the public array */
    old_array = self->categories;
    new_array = dbus_log_category_values(priv->categories);
    g_ptr_array_sort(new_array, dbus_log_category_sort_name);
    self->categories = new_array;

    /* Find the index of the new category */
    for (i=0; i<new_array->len; i++) {
        if (g_ptr_array_index(new_array, i) !=
            g_ptr_array_index(old_array, i)) {
            index = i;
            break;
        }
    }

    /* Notify the listeners */
    GASSERT(index >= 0);
    dbus_log_category_ref(category);
    dbus_log_client_emit(self, SIGNAL_CATEGORY_ADDED, category, index);
    dbus_log_category_unref(category);
    g_ptr_array_unref(old_array);
}

static
void
dbus_log_client_category_flags_changed(
    OrgNemomobileLogger* proxy,
    guint id,
    guint flags,
    gpointer user_data)
{
    DBusLogClient* self = DBUSLOG_CLIENT(user_data);
    DBusLogCategory* category = dbus_log_client_category(self, id);
    const int index = dbus_log_client_category_index(self, category);
    GVERBOSE_("%u (%d) 0x%04x", id, index, flags);
    GASSERT(index >= 0);
    if (index >= 0) {
        category->flags = flags;
        dbus_log_category_ref(category);
        dbus_log_client_emit(self, SIGNAL_CATEGORY_FLAGS, category, index);
        dbus_log_category_unref(category);
   }
}

static
void
dbus_log_client_receiver_message(
    DBusLogReceiver* receiver,
    DBusLogMessage* msg,
    gpointer user_data)
{
    DBusLogClient* self = DBUSLOG_CLIENT(user_data);
    DBusLogCategory* category = dbus_log_client_category(self, msg->category);
    GVERBOSE_("%s", msg->string);
    dbus_log_category_ref(category);
    dbus_log_client_emit(self, SIGNAL_LOG_MESSAGE, category, msg);
    dbus_log_category_unref(category);
}

static
void
dbus_log_client_receiver_skip(
    DBusLogReceiver* receiver,
    guint count,
    gpointer user_data)
{
    GVERBOSE_("%u", count);
    dbus_log_client_emit(DBUSLOG_CLIENT(user_data), SIGNAL_LOG_SKIP, count);
}

static
void
dbus_log_client_receiver_closed(
    DBusLogReceiver* receiver,
    gpointer user_data)
{
    GDEBUG_("");
    dbus_log_client_stop(DBUSLOG_CLIENT(user_data), TRUE);
}

static
void
dbus_log_client_started(
    DBusLogClient* self,
    int fd,
    int cookie)
{
    DBusLogClientPriv* priv = self->priv;
    const gboolean was_started = self->started;
    dbus_log_client_stop(self, FALSE);
    priv->cookie = cookie;
    priv->receiver = dbus_log_receiver_new(fd, TRUE);
    self->started = (priv->receiver != NULL);
    if (priv->receiver) {
        priv->receiver_signal_id[RECEIVER_SIGNAL_MESSAGE] =
            dbus_log_receiver_add_message_handler(priv->receiver,
                dbus_log_client_receiver_message, self);
        priv->receiver_signal_id[RECEIVER_SIGNAL_SKIP] =
            dbus_log_receiver_add_skip_handler(priv->receiver,
                dbus_log_client_receiver_skip, self);
        priv->receiver_signal_id[RECEIVER_SIGNAL_CLOSED] =
            dbus_log_receiver_add_closed_handler(priv->receiver,
                dbus_log_client_receiver_closed, self);
    }
    if (self->started != was_started) {
        dbus_log_client_emit(self, SIGNAL_LOG_STARTED_CHANGED);
    }
}

static
void
dbus_log_client_autostart_finished(
    DBusLogClientCall* call,
    const GError* error,
    gpointer user_data)
{
    DBusLogClient* self = DBUSLOG_CLIENT(user_data);
    DBusLogClientPriv* priv = self->priv;
    if (priv->autostart == call) {
        priv->autostart = NULL;
    }
    if (!error) {
        GDEBUG("Autostart OK");
    }
}

static
void
dbus_log_client_connected(
    DBusLogClient* self,
    OrgNemomobileLogger* proxy)
{
    DBusLogClientPriv* priv = self->priv;
    const gboolean was_connected = self->connected;

    dbus_log_client_stop(self, TRUE);
    g_object_ref(priv->proxy = proxy);

    /* Register signal handlers */
    priv->proxy_signal_id[PROXY_SIGNAL_CATEGORY_FLAGS_CHANGED] =
        g_signal_connect(priv->proxy, "category-flags-changed",
            G_CALLBACK(dbus_log_client_category_flags_changed), self);
    priv->proxy_signal_id[PROXY_SIGNAL_CATEGORY_ADDED] =
        g_signal_connect(priv->proxy, "category-added",
            G_CALLBACK(dbus_log_client_category_added), self);
    priv->proxy_signal_id[PROXY_SIGNAL_CATEGORY_REMOVED] =
        g_signal_connect(priv->proxy, "category-removed",
            G_CALLBACK(dbus_log_client_category_removed), self);

    self->connected = TRUE;
    GASSERT(!self->started);
    if (self->connected != was_connected) {
        dbus_log_client_emit(self, SIGNAL_CONNECTED_CHANGED);
    }
    if (priv->flags & DBUSLOG_CLIENT_FLAG_AUTOSTART) {
        priv->autostart = dbus_log_client_start(self,
            dbus_log_client_autostart_finished, self);
    }
}

static
void
dbus_log_client_init_free(
    DBusLogClientInit* init,
    GError* error)
{
    if (init) {
        DBusLogClient* self = init->client;
        DBusLogClientPriv* priv = self->priv;
        if (priv->init == init) {
            priv->init = NULL;
            if (error) {
                /* Failed to connect */
                GERR("%s", GERRMSG(error));
                dbus_log_client_emit(self, SIGNAL_CONNECT_ERROR, error);
            } else {
                /* Replace the public array */
                g_ptr_array_unref(self->categories);
                self->categories = dbus_log_category_values(init->categories);
                g_ptr_array_sort(self->categories, dbus_log_category_sort_name);
                if (priv->categories) {
                    g_hash_table_destroy(priv->categories);
                }
                priv->categories = init->categories;
                init->categories = NULL;
                /* Connected */
                dbus_log_client_connected(self, init->proxy);
            }
        }
        if (init->proxy) {
            g_object_unref(init->proxy);
        }
        if (init->categories) {
            g_hash_table_destroy(init->categories);
        }
        if (error) {
            g_error_free(error);
        }
        g_object_unref(init->bus);
        g_object_unref(init->cancel);
        g_slice_free(DBusLogClientInit, init);
        dbus_log_client_unref(self);
    }
}

static
void
dbus_log_client_decode_categories(
    DBusLogClientInit* init,
    GVariant* cats)
{
    GVariantIter it;
    GVariant* child;
    GDEBUG("%u categories", (guint)g_variant_n_children(cats));
    for (g_variant_iter_init(&it, cats);
         (child = g_variant_iter_next_value(&it)) != NULL;
         g_variant_unref(child)) {
        DBusLogCategory* cat;
        const char* name = NULL;
        guint id = 0, flags = 0;
        gint level = 0;
        g_variant_get(child, "(&suui)", &name, &id, &flags, &level);
        cat = dbus_log_category_new(name, id);
        cat->flags = flags;
        cat->level = level;
        /* Hashtable takes ownership of this DBusLogCategory */
        g_hash_table_replace(init->categories, GINT_TO_POINTER(id), cat);
    }
}

static
void
dbus_log_client_init_get_all_finished(
    GObject* proxy,
    GAsyncResult* result,
    gpointer data)
{
    DBusLogClientInit* init = data;
    GError* error = NULL;
    gint version;
    gint default_level;
    GVariant* cats = NULL;
    GASSERT(ORG_NEMOMOBILE_LOGGER(proxy) == init->proxy);
    if (org_nemomobile_logger_call_get_all_finish(init->proxy, &version,
        &default_level, &cats, result, &error)) {
        dbus_log_client_decode_categories(init, cats);
        g_variant_unref(cats);
        init->client->default_level = default_level;
        /* Done with init. This emits the CONNECTED signal */
        dbus_log_client_init_free(init, NULL);
    } else {
        dbus_log_client_init_free(init, error);
    }
}

static
void
dbus_log_client_init_get_all2_finished(
    GObject* proxy,
    GAsyncResult* result,
    gpointer data)
{
    DBusLogClientInit* init = data;
    GError* error = NULL;
    gint version, default_level, backlog;
    GVariant* cats = NULL;
    GASSERT(ORG_NEMOMOBILE_LOGGER(proxy) == init->proxy);
    if (org_nemomobile_logger_call_get_all2_finish(init->proxy, &version,
        &default_level, &cats, &backlog, result, &error)) {
        DBusLogClient* client = init->client;
        dbus_log_client_decode_categories(init, cats);
        g_variant_unref(cats);
        client->default_level = default_level;
        client->backlog = backlog;
        /* Done with init. This emits the CONNECTED signal */
        dbus_log_client_init_free(init, NULL);
    } else {
        dbus_log_client_init_free(init, error);
    }
}

static
void
dbus_log_client_init_get_interface_version_finished(
    GObject* proxy,
    GAsyncResult* result,
    gpointer data)
{
    DBusLogClientInit* init = data;
    GError* error = NULL;
    gint version;
    GASSERT(ORG_NEMOMOBILE_LOGGER(proxy) == init->proxy);
    if (org_nemomobile_logger_call_get_interface_version_finish(init->proxy,
        &version,result, &error)) {
        init->client->api_version = version;
        switch (version) {
        case 1:
            org_nemomobile_logger_call_get_all(init->proxy, init->cancel,
                dbus_log_client_init_get_all_finished, init);
            break;
        case 2:
        default:
            org_nemomobile_logger_call_get_all2(init->proxy, init->cancel,
                dbus_log_client_init_get_all2_finished, init);
            break;
        }
    } else {
        dbus_log_client_init_free(init, error);
    }
}

static
void
dbus_log_client_init_proxy_created(
    GObject* bus,
    GAsyncResult* result,
    gpointer data)
{
    GError* error = NULL;
    DBusLogClientInit* init = data;
    init->proxy = org_nemomobile_logger_proxy_new_finish(result, &error);
    if (init->proxy) {
        org_nemomobile_logger_call_get_interface_version(init->proxy,
            init->cancel, dbus_log_client_init_get_interface_version_finished,
            init);
    } else {
        dbus_log_client_init_free(init, error);
    }
}

static
DBusLogClientInit*
dbus_log_client_init_new(
    DBusLogClient* client,
    GDBusConnection* bus,
    const gchar* service)
{
    DBusLogClientInit* init = g_slice_new0(DBusLogClientInit);
    DBusLogClientPriv* priv = client->priv;
    g_object_ref(init->bus = bus);
    init->client = dbus_log_client_ref(client);
    init->cancel = g_cancellable_new();
    init->categories = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, dbus_log_category_free);
    org_nemomobile_logger_proxy_new(bus,
        G_DBUS_PROXY_FLAGS_DO_NOT_LOAD_PROPERTIES, service, priv->path,
        init->cancel, dbus_log_client_init_proxy_created, init);
    return init;
}

static
void
dbus_log_client_init_cancel(
    DBusLogClientInit* init)
{
    if (init) {
        g_cancellable_cancel(init->cancel);
    }
}

static
void
dbus_log_client_name_appeared(
    GDBusConnection* bus,
    const gchar* service,
    const gchar* owner,
    gpointer data)
{
    DBusLogClient* self = DBUSLOG_CLIENT(data);
    DBusLogClientPriv* priv = self->priv;
    GDEBUG("Name '%s' is owned by %s", service, owner);

    /* Start the initialization sequence */
    dbus_log_client_init_cancel(priv->init);
    priv->init = dbus_log_client_init_new(self, bus, service);
}

static
void
dbus_log_client_name_vanished(
    GDBusConnection* bus,
    const gchar* service,
    gpointer data)
{
    DBusLogClient* self = DBUSLOG_CLIENT(data);
    DBusLogClientPriv* priv = self->priv;
    GDEBUG("Name '%s' has disappeared", service);
    if (priv->init) {
        dbus_log_client_init_cancel(priv->init);
        priv->init = NULL;
    }
    dbus_log_client_disconnect(self, TRUE);
}

/*==========================================================================*
 * API
 *==========================================================================*/

DBusLogClient*
dbus_log_client_new(
    GBusType bus,
    const char* service,
    const char* path,
    guint flags)
{
    DBusLogClient* self = g_object_new(DBUSLOG_CLIENT_TYPE, NULL);
    DBusLogClientPriv* priv = self->priv;
    priv->path = g_strdup(path);
    priv->flags = flags;
    priv->name_watch_id = g_bus_watch_name(bus, service,
        G_BUS_NAME_WATCHER_FLAGS_NONE, dbus_log_client_name_appeared,
        dbus_log_client_name_vanished, self, NULL);
    return self;
}

DBusLogClient*
dbus_log_client_ref(
    DBusLogClient* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(DBUSLOG_CLIENT(self));
        return self;
    } else {
        return NULL;
    }
}

void
dbus_log_client_unref(
    DBusLogClient* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(DBUSLOG_CLIENT(self));
    }
}

static
void
dbus_log_client_start_finished(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    DBusLogClientCall* call = user_data;
    GVariant* fd = NULL;
    guint cookie;
    GUnixFDList* fdl = NULL;
    GError* error = NULL;
    if (org_nemomobile_logger_call_log_open_finish(
        ORG_NEMOMOBILE_LOGGER(proxy), &fd, &cookie, &fdl, result, &error)) {
        if (g_unix_fd_list_get_length(fdl) == 1) {
            gint* fds = g_unix_fd_list_steal_fds(fdl, NULL);
            dbus_log_client_started(call->client, fds[0], cookie);
            g_free(fds);
        }
        g_variant_unref(fd);
        g_object_unref(fdl);
    } else {
        GERR("Failed to start logging: %s", GERRMSG(error));
        dbus_log_client_emit(call->client, SIGNAL_LOG_START_ERROR, error);
    }
    if (call->fn) {
        call->fn(call, error, call->user_data);
    }
    if (error) {
        g_error_free(error);
    }
    dbus_log_client_call_free(call);
}

DBusLogClientCall*
dbus_log_client_start(
    DBusLogClient* self,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self)) {
        DBusLogClientPriv* priv = self->priv;
        if (priv->proxy) {
            call = dbus_log_client_call_new(self, NULL, fn, data);
            org_nemomobile_logger_call_log_open(priv->proxy, NULL,
                call->cancel, dbus_log_client_start_finished, call);
        }
    }
    return call;
}

static
void
dbus_log_client_generic_call_finished(
    GObject* proxy,
    GAsyncResult* result,
    gpointer user_data)
{
    DBusLogClientCall* call = user_data;
    GError* error = NULL;
    if (!call->finish(ORG_NEMOMOBILE_LOGGER(proxy), result, &error)) {
        GERR("%s", GERRMSG(error));
    }
    if (call->fn) {
        call->fn(call, error, call->user_data);
    }
    if (error) {
        g_error_free(error);
    }
    dbus_log_client_call_free(call);
}

DBusLogClientCall*
dbus_log_client_set_default_level(
    DBusLogClient* self,
    DBUSLOG_LEVEL level,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self)) {
        DBusLogClientPriv* priv = self->priv;
        if (priv->proxy) {
            call = dbus_log_client_call_new(self,
                org_nemomobile_logger_call_set_default_level_finish,
                fn, data);
            org_nemomobile_logger_call_set_default_level(priv->proxy, level,
                call->cancel, dbus_log_client_generic_call_finished, call);
        }
    }
    return call;
}

DBusLogClientCall*
dbus_log_client_set_backlog(
    DBusLogClient* self,
    int backlog,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self)) {
        DBusLogClientPriv* priv = self->priv;
        if (priv->proxy) {
            call = dbus_log_client_call_new(self,
                org_nemomobile_logger_call_set_backlog_finish, fn, data);
            org_nemomobile_logger_call_set_backlog(priv->proxy, backlog,
                call->cancel, dbus_log_client_generic_call_finished, call);
        }
    }
    return call;
}

DBusLogClientCall*
dbus_log_client_enable_category(
    DBusLogClient* self,
    const char* name,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self) && G_LIKELY(name)) {
        GStrV* names = gutil_strv_add(NULL, name);
        call = dbus_log_client_enable_categories(self, names, fn, data);
        g_strfreev(names);
    }
    return call;
}

DBusLogClientCall*
dbus_log_client_enable_categories(
    DBusLogClient* self,
    const GStrV* names,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self) && G_LIKELY(names) && G_LIKELY(names[0])) {
        DBusLogClientPriv* priv = self->priv;
        if (priv->proxy) {
            call = dbus_log_client_call_new(self,
                org_nemomobile_logger_call_category_enable_finish,
                fn, data);
            org_nemomobile_logger_call_category_enable(priv->proxy,
                (const gchar* const*)names, call->cancel,
                dbus_log_client_generic_call_finished, call);
        }
    }
    return call;
}

DBusLogClientCall*
dbus_log_client_disable_category(
    DBusLogClient* self,
    const char* name,
    DBusLogClientCallFunc fn,
    gpointer user_data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self) && G_LIKELY(name)) {
        GStrV* names = gutil_strv_add(NULL, name);
        call = dbus_log_client_disable_categories(self, names, fn, user_data);
        g_strfreev(names);
    }
    return call;
}

DBusLogClientCall*
dbus_log_client_disable_categories(
    DBusLogClient* self,
    const GStrV* names,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self) && G_LIKELY(names) && G_LIKELY(names[0])) {
        DBusLogClientPriv* priv = self->priv;
        if (priv->proxy) {
            call = dbus_log_client_call_new(self,
                org_nemomobile_logger_call_category_disable_finish,
                fn, data);
            org_nemomobile_logger_call_category_disable(priv->proxy,
                (const gchar* const*)names, call->cancel,
                dbus_log_client_generic_call_finished, call);
        }
    }
    return call;
}

DBusLogClientCall*
dbus_log_client_enable_pattern(
    DBusLogClient* self,
    const char* pattern,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self) && G_LIKELY(pattern)) {
        DBusLogClientPriv* priv = self->priv;
        if (priv->proxy) {
            call = dbus_log_client_call_new(self,
                org_nemomobile_logger_call_category_enable_pattern_finish,
                fn, data);
            org_nemomobile_logger_call_category_enable_pattern(priv->proxy,
                pattern, call->cancel, dbus_log_client_generic_call_finished,
                call);
        }
    }
    return call;
}

DBusLogClientCall*
dbus_log_client_disable_pattern(
    DBusLogClient* self,
    const char* pattern,
    DBusLogClientCallFunc fn,
    gpointer data)
{
    DBusLogClientCall* call = NULL;
    if (G_LIKELY(self) && G_LIKELY(pattern)) {
        DBusLogClientPriv* priv = self->priv;
        if (priv->proxy) {
            call = dbus_log_client_call_new(self,
                org_nemomobile_logger_call_category_disable_pattern_finish,
                fn, data);
            org_nemomobile_logger_call_category_disable_pattern(priv->proxy,
                pattern, call->cancel, dbus_log_client_generic_call_finished,
                call);
        }
    }
    return call;
}

void
dbus_log_client_call_cancel(
    DBusLogClientCall* call)
{
    if (G_LIKELY(call)) {
        call->fn = NULL;
        call->user_data = NULL;
        g_cancellable_cancel(call->cancel);
    }
}

gulong
dbus_log_client_add_connect_error_handler(
    DBusLogClient* self,
    DBusLogClientErrorFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CONNECT_ERROR_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_connected_handler(
    DBusLogClient* self,
    DBusLogClientFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CONNECTED_CHANGED_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_backlog_handler(
    DBusLogClient* self,
    DBusLogClientFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_BACKLOG_CHANGED_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_category_added_handler(
    DBusLogClient* self,
    DBusLogClientCategoryFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_ADDED_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_category_removed_handler(
    DBusLogClient* self,
    DBusLogClientCategoryFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_REMOVED_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_category_flags_handler(
    DBusLogClient* self,
    DBusLogClientCategoryFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_FLAGS_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_start_error_handler(
    DBusLogClient* self,
    DBusLogClientErrorFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_LOG_START_ERROR_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_started_handler(
    DBusLogClient* self,
    DBusLogClientFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_LOG_STARTED_CHANGED_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_message_handler(
    DBusLogClient* self,
    DBusLogClientMessageFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_LOG_MESSAGE_NAME, G_CALLBACK(fn), user_data) : 0;
}

gulong
dbus_log_client_add_skip_handler(
    DBusLogClient* self,
    DBusLogClientSkipFunc fn,
    gpointer user_data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_LOG_SKIP_NAME, G_CALLBACK(fn), user_data) : 0;
}

void
dbus_log_client_remove_handler(
    DBusLogClient* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
dbus_log_client_remove_handlers(
    DBusLogClient* self,
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
dbus_log_client_init(
    DBusLogClient* self)
{
    DBusLogClientPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        DBUSLOG_CLIENT_TYPE, DBusLogClientPriv);
    self->priv = priv;
    self->default_level = DBUSLOG_LEVEL_UNDEFINED;
    self->categories = g_ptr_array_new_with_free_func(dbus_log_category_free);
    priv->categories = g_hash_table_new_full(g_direct_hash, g_direct_equal,
        NULL, dbus_log_category_free);
}

/**
 * Final stage of deinitialization
 */
static
void
dbus_log_client_finalize(
    GObject* object)
{
    DBusLogClient* self = DBUSLOG_CLIENT(object);
    DBusLogClientPriv* priv = self->priv;
    GASSERT(!priv->init);
    GASSERT(!priv->autostart);
    dbus_log_client_disconnect(self, FALSE);
    g_ptr_array_unref(self->categories);
    g_hash_table_destroy(priv->categories);
    if (priv->name_watch_id) {
        g_bus_unwatch_name(priv->name_watch_id);
    }
    if (priv->proxy) {
        g_object_unref(priv->proxy);
    }
    g_free(priv->path);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
dbus_log_client_class_init(
    DBusLogClientClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GType class_type = G_OBJECT_CLASS_TYPE(klass);
    object_class->finalize = dbus_log_client_finalize;
    g_type_class_add_private(klass, sizeof(DBusLogClientPriv));
    dbus_log_client_signals[SIGNAL_CONNECT_ERROR] =
        g_signal_new(SIGNAL_CONNECT_ERROR_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_ERROR);
    dbus_log_client_signals[SIGNAL_CONNECTED_CHANGED] =
        g_signal_new(SIGNAL_CONNECTED_CHANGED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    dbus_log_client_signals[SIGNAL_BACKLOG_CHANGED] =
        g_signal_new(SIGNAL_BACKLOG_CHANGED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    dbus_log_client_signals[SIGNAL_CATEGORY_ADDED] =
        g_signal_new(SIGNAL_CATEGORY_ADDED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
    dbus_log_client_signals[SIGNAL_CATEGORY_REMOVED] =
        g_signal_new(SIGNAL_CATEGORY_REMOVED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
    dbus_log_client_signals[SIGNAL_CATEGORY_FLAGS] =
        g_signal_new(SIGNAL_CATEGORY_FLAGS_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
    dbus_log_client_signals[SIGNAL_LOG_START_ERROR] =
        g_signal_new(SIGNAL_LOG_START_ERROR_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_ERROR);
    dbus_log_client_signals[SIGNAL_LOG_STARTED_CHANGED] =
        g_signal_new(SIGNAL_LOG_STARTED_CHANGED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    dbus_log_client_signals[SIGNAL_LOG_MESSAGE] =
        g_signal_new(SIGNAL_LOG_MESSAGE_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_POINTER);
    dbus_log_client_signals[SIGNAL_LOG_SKIP] =
        g_signal_new(SIGNAL_LOG_SKIP_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_UINT);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
