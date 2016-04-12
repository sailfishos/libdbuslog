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

#include "dbuslog_server_p.h"
#include "dbuslog_server_log.h"

#include <gutil_misc.h>

enum dbus_log_server_core_signal {
    DBUSLOG_CORE_SIGNAL_DEFAULT_LEVEL,
    DBUSLOG_CORE_SIGNAL_CATEGORY_ADDED,
    DBUSLOG_CORE_SIGNAL_CATEGORY_REMOVED,
    DBUSLOG_CORE_SIGNAL_CATEGORY_FLAGS,
    DBUSLOG_CORE_SIGNAL_CATEGORY_LEVEL,
    DBUSLOG_CORE_SIGNAL_COUNT
};

typedef struct dbus_log_server_peer {
    gulong watch_id;
    DBusLogSender* sender;
    DBusLogServer* server;
} DBusLogServerPeer;

struct dbus_log_server_priv {
    char* path;
    GHashTable* peers;
    gulong core_signal_id[DBUSLOG_CORE_SIGNAL_COUNT];
};

G_DEFINE_TYPE(DBusLogServer, dbus_log_server, G_TYPE_OBJECT)
#define PARENT_CLASS (dbus_log_server_parent_class)
#define DBUSLOG_SERVER_GET_CLASS(obj) (G_TYPE_INSTANCE_GET_CLASS((obj), \
    DBUSLOG_SERVER_TYPE, DBusLogServerClass))

enum dbus_log_server_signal {
    SIGNAL_CATEGORY_ENABLED,
    SIGNAL_CATEGORY_DISABLED,
    SIGNAL_CATEGORY_LEVEL,
    SIGNAL_COUNT
};

#define SIGNAL_CATEGORY_ENABLED_NAME  "dbuslog-server-category-enabled"
#define SIGNAL_CATEGORY_DISABLED_NAME "dbuslog-server-category-disabled"
#define SIGNAL_CATEGORY_LEVEL_NAME    "dbuslog-server-category-level"

static guint dbus_log_server_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
dbus_log_server_peer_destroy(
    gpointer user_data)
{
    DBusLogServerPeer* peer = user_data;
    DBusLogServer* server = peer->server;
    DBusLogServerClass* klass = DBUSLOG_SERVER_GET_CLASS(server);
    if (klass->unwatch_name) {
        klass->unwatch_name(server, peer->watch_id);
    }
    dbus_log_sender_close(peer->sender, TRUE);
    dbus_log_sender_unref(peer->sender);
    g_slice_free(DBusLogServerPeer, peer);
}

static
void
dbus_log_server_default_level_changed(
    DBusLogCore* core,
    gpointer user_data)
{
    DBusLogServer* self = DBUSLOG_SERVER(user_data);
    if (self->started) {
        DBUSLOG_SERVER_GET_CLASS(self)->emit_default_level_changed(self);
    }
}

static
void
dbus_log_server_category_added(
    DBusLogCore* core,
    DBusLogCategory* category,
    gpointer user_data)
{
    DBusLogServer* self = DBUSLOG_SERVER(user_data);
    if (self->started) {
        DBUSLOG_SERVER_GET_CLASS(self)->emit_category_added(self,
            category->name, category->id, category->flags);
    }
}

static
void
dbus_log_server_category_removed(
    DBusLogCore* core,
    DBusLogCategory* category,
    gpointer user_data)
{
    DBusLogServer* self = DBUSLOG_SERVER(user_data);
    if (self->started) {
        DBUSLOG_SERVER_GET_CLASS(self)->emit_category_removed(self,
            category->id);
    }
}

static
void
dbus_log_server_category_flags_changed(
    DBusLogCore* core,
    DBusLogCategory* category,
    guint mask,
    gpointer user_data)
{
    DBusLogServer* self = DBUSLOG_SERVER(user_data);
    if (mask & DBUSLOG_CATEGORY_FLAG_ENABLED) {
        g_signal_emit(self, dbus_log_server_signals[
            (category->flags & DBUSLOG_CATEGORY_FLAG_ENABLED) ?
            SIGNAL_CATEGORY_ENABLED : SIGNAL_CATEGORY_DISABLED], 0,
            category->name);
    }
    if (self->started) {
        DBusLogServerClass* klass = DBUSLOG_SERVER_GET_CLASS(self);
        if (klass->emit_category_flags_changed) {
            klass->emit_category_flags_changed(self,
                category->id, category->flags);
        }
    }
}

static
void
dbus_log_server_category_level_changed(
    DBusLogCore* core,
    DBusLogCategory* cat,
    gpointer user_data)
{
    DBusLogServer* self = DBUSLOG_SERVER(user_data);
    g_signal_emit(self, dbus_log_server_signals[SIGNAL_CATEGORY_LEVEL], 0,
        cat->name, cat->level);
    if (self->started) {
        DBusLogServerClass* klass = DBUSLOG_SERVER_GET_CLASS(self);
        if (klass->emit_category_level_changed) {
            klass->emit_category_level_changed(self, cat->id, cat->level);
        }
    }
}

/*==========================================================================*
 * D-Bus method helpers
 *==========================================================================*/

int
dbus_log_server_open(
    DBusLogServer* self,
    const char* name)
{
    DBusLogSender* sender = dbus_log_core_new_sender(self->core, name);
    if (sender) {
        DBusLogServerPriv* priv = self->priv;
        DBusLogServerPeer* peer = g_slice_new0(DBusLogServerPeer);
        DBusLogServerClass* klass = DBUSLOG_SERVER_GET_CLASS(self);
        peer->sender = sender;
        peer->server = self;
        if (klass->watch_name) {
            peer->watch_id = klass->watch_name(self, name);
        }
        g_hash_table_replace(priv->peers, (gpointer)sender->name, peer);
        return sender->readfd;
    }
    return -1;
}

void
dbus_log_server_close(
    DBusLogServer* self,
    const char* name,
    guint cookie)
{
    /* Cookie is ignored at the moment */
    DBusLogServerPriv* priv = self->priv;
    g_hash_table_remove(priv->peers, name);
}

void
dbus_log_server_set_names_enabled(
    DBusLogServer* self,
    const GStrV* names,
    gboolean enable)
{
    if (G_LIKELY(self) && names) {
        const GStrV* ptr;
        for (ptr = names; *ptr; ptr++) {
            dbus_log_core_set_category_enabled(self->core, *ptr, enable);
        }
    }
}

void
dbus_log_server_set_pattern_enabled(
    DBusLogServer* self,
    const char* pattern,
    gboolean enable)
{
    guint i;
    GPtrArray* cats = dbus_log_core_find_categories(self->core, pattern);
    for (i=0; i<cats->len; i++) {
        DBusLogCategory* cat = g_ptr_array_index(cats, i);
        dbus_log_core_set_category_enabled(self->core, cat->name, enable);
    }
}

/*==========================================================================*
 * API
 *==========================================================================*/

void
dbus_log_server_initialize(
    DBusLogServer* self,
    const char* path)
{
    DBusLogServerPriv* priv = self->priv;
    DBusLogServerClass* klass = DBUSLOG_SERVER_GET_CLASS(self);
    self->path = priv->path = g_strdup(path);

    /* Attach to the core signals */
    self->core = dbus_log_core_new(0);
    if (klass->emit_default_level_changed) {
        priv->core_signal_id[DBUSLOG_CORE_SIGNAL_DEFAULT_LEVEL] =
            dbus_log_core_add_default_level_handler(self->core,
                dbus_log_server_default_level_changed, self);
    }
    if (klass->emit_category_added) {
        priv->core_signal_id[DBUSLOG_CORE_SIGNAL_CATEGORY_ADDED] =
            dbus_log_core_add_category_added_handler(self->core,
                dbus_log_server_category_added, self);
    }
    if (klass->emit_category_removed) {
        priv->core_signal_id[DBUSLOG_CORE_SIGNAL_CATEGORY_REMOVED] =
            dbus_log_core_add_category_removed_handler(self->core,
                dbus_log_server_category_removed, self);
    }
    priv->core_signal_id[DBUSLOG_CORE_SIGNAL_CATEGORY_FLAGS] =
        dbus_log_core_add_category_flags_handler(self->core,
            dbus_log_server_category_flags_changed, self);
    priv->core_signal_id[DBUSLOG_CORE_SIGNAL_CATEGORY_LEVEL] =
        dbus_log_core_add_category_level_handler(self->core,
            dbus_log_server_category_level_changed, self);
}

DBusLogServer*
dbus_log_server_ref(
    DBusLogServer* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(DBUSLOG_SERVER(self));
        return self;
    } else {
        return NULL;
    }
}

void
dbus_log_server_unref(
    DBusLogServer* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(DBUSLOG_SERVER(self));
    }
}

void
dbus_log_server_start(
    DBusLogServer* self)
{
    if (G_LIKELY(self) && !self->started) {
        DBusLogServerClass* klass = DBUSLOG_SERVER_GET_CLASS(self);
        self->started = TRUE;
        if (klass->export) {
            self->exported = klass->export(self);
        }
    }
}

void
dbus_log_server_stop(
    DBusLogServer* self)
{
    if (G_LIKELY(self) && self->started) {
        self->started = FALSE;
        if (self->exported) {
            DBusLogServerClass* klass = DBUSLOG_SERVER_GET_CLASS(self);
            self->exported = FALSE;
            if (klass->unexport) {
                klass->unexport(self);
            }
        }
    }
}

DBUSLOG_LEVEL
dbus_log_server_default_level(
    DBusLogServer* self)
{
    return G_LIKELY(self) ? dbus_log_core_default_level(self->core) :
        DBUSLOG_LEVEL_UNDEFINED;
}

gboolean
dbus_log_server_set_default_level(
    DBusLogServer* self,
    DBUSLOG_LEVEL level)
{
    return G_LIKELY(self) && dbus_log_core_set_default_level(self->core, level);
}

void
dbus_log_server_add_category(
    DBusLogServer* self,
    const char* name,
    DBUSLOG_LEVEL level,
    gulong flags)
{
    if (G_LIKELY(self)) {
        dbus_log_category_unref(dbus_log_core_new_category(self->core,
            name, level, flags));
    }
}

gboolean
dbus_log_server_remove_category(
    DBusLogServer* self,
    const char* name)
{
    if (G_LIKELY(self)) {
        return dbus_log_core_remove_category(self->core, name);
    }    
    return FALSE;
}

void
dbus_log_server_remove_all_categories(
    DBusLogServer* self)
{
    if (G_LIKELY(self)) {
        dbus_log_core_remove_all_categories(self->core);
    }
}

gboolean
dbus_log_server_log(
    DBusLogServer* self,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* message)
{
    if (G_LIKELY(self)) {
        return dbus_log_core_log(self->core, level, category, message);
    }
    return TRUE;
}

gboolean
dbus_log_server_logv(
    DBusLogServer* self,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* format,
    va_list args)
{
    if (G_LIKELY(self)) {
        return dbus_log_core_logv(self->core, level, category, format, args);
    }
    return TRUE;
}

gulong
dbus_log_server_add_category_enabled_handler(
    DBusLogServer* self,
    DBusLogServerCategoryFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_ENABLED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
dbus_log_server_add_category_disabled_handler(
    DBusLogServer* self,
    DBusLogServerCategoryFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_DISABLED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
dbus_log_server_add_category_level_handler(
    DBusLogServer* self,
    DBusLogServerCategoryLevelFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_LEVEL_NAME, G_CALLBACK(fn), data) : 0;
}

void
dbus_log_server_remove_handler(
    DBusLogServer* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
dbus_log_server_remove_handlers(
    DBusLogServer* self,
    gulong* ids,
    guint count)
{
    gutil_disconnect_handlers(self, ids, count);
}

/*==========================================================================*
 * Internal API
 *==========================================================================*/

void
dbus_log_server_peer_vanished(
    DBusLogServer* self,
    const gchar* name)
{
    DBusLogServerPriv* priv = self->priv;
    g_hash_table_remove(priv->peers, name);
}

/*==========================================================================*
 * Internals
 *==========================================================================*/

/**
 * Per instance initializer
 */
static
void
dbus_log_server_init(
    DBusLogServer* self)
{
    DBusLogServerPriv* priv = G_TYPE_INSTANCE_GET_PRIVATE(self,
        DBUSLOG_SERVER_TYPE, DBusLogServerPriv);
    self->priv = priv;
    priv->peers = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
        dbus_log_server_peer_destroy);
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
dbus_log_server_dispose(
    GObject* object)
{
    DBusLogServer* self = DBUSLOG_SERVER(object);
    DBusLogServerPriv* priv = self->priv;
    dbus_log_server_stop(self);
    g_hash_table_remove_all(priv->peers);
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
dbus_log_server_finalize(
    GObject* object)
{
    DBusLogServer* self = DBUSLOG_SERVER(object);
    DBusLogServerPriv* priv = self->priv;
    dbus_log_core_remove_handlers(self->core, priv->core_signal_id,
        G_N_ELEMENTS(priv->core_signal_id));
    dbus_log_core_unref(self->core);
    g_hash_table_destroy(priv->peers);
    g_free(priv->path);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
dbus_log_server_class_init(
    DBusLogServerClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GType class_type = G_OBJECT_CLASS_TYPE(klass);
    object_class->dispose = dbus_log_server_dispose;
    object_class->finalize = dbus_log_server_finalize;
    g_type_class_add_private(klass, sizeof(DBusLogServerPriv));
    dbus_log_server_signals[SIGNAL_CATEGORY_ENABLED] =
        g_signal_new(SIGNAL_CATEGORY_ENABLED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_STRING);
    dbus_log_server_signals[SIGNAL_CATEGORY_DISABLED] =
        g_signal_new(SIGNAL_CATEGORY_DISABLED_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_STRING);
    dbus_log_server_signals[SIGNAL_CATEGORY_LEVEL] =
        g_signal_new(SIGNAL_CATEGORY_LEVEL_NAME, class_type,
            G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_STRING, G_TYPE_INT);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
