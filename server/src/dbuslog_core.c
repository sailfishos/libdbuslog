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

#include "dbuslog_core.h"
#include "dbuslog_protocol.h"
#include "dbuslog_server_log.h"

#include <gutil_idlepool.h>
#include <gutil_misc.h>

/* Log module */
GLOG_MODULE_DEFINE("dbuslog");

/* Object definition */
struct dbus_log_core {
    GObject object;
    guint backlog;
    GUtilIdlePool* pool;
    GPtrArray* senders;
    GHashTable* categories;
    GHashTable* sender_signal_ids;
    guint last_cid;
    guint next_msg_index;
    DBUSLOG_LEVEL default_level;
};

typedef GObjectClass DBusLogCoreClass;
G_DEFINE_TYPE(DBusLogCore, dbus_log_core, G_TYPE_OBJECT)
#define PARENT_CLASS (dbus_log_core_parent_class)
#define DBUSLOG_CORE_TYPE (dbus_log_core_get_type())
#define DBUSLOG_CORE(obj) (G_TYPE_CHECK_INSTANCE_CAST((obj), \
        DBUSLOG_CORE_TYPE, DBusLogCore))

enum dbus_log_core_signal {
    SIGNAL_BACKLOG,
    SIGNAL_DEFAULT_LEVEL,
    SIGNAL_CATEGORY_ADDED,
    SIGNAL_CATEGORY_REMOVED,
    SIGNAL_CATEGORY_LEVEL,
    SIGNAL_CATEGORY_FLAGS,
    SIGNAL_COUNT
};

#define SIGNAL_BACKLOG_NAME             "dbuslog-core-backlog"
#define SIGNAL_DEFAULT_LEVEL_NAME       "dbuslog-core-default-level"
#define SIGNAL_CATEGORY_ADDED_NAME      "dbuslog-core-category-added"
#define SIGNAL_CATEGORY_REMOVED_NAME    "dbuslog-core-category-removed"
#define SIGNAL_CATEGORY_LEVEL_NAME      "dbuslog-core-category-level"
#define SIGNAL_CATEGORY_FLAGS_NAME      "dbuslog-core-category-flags"

static guint dbus_log_core_signals[SIGNAL_COUNT] = { 0 };

/*==========================================================================*
 * Implementation
 *==========================================================================*/

static
void
dbus_log_core_free_sender(
    gpointer data)
{
    dbus_log_sender_unref(data);
}

static
void
dbus_log_core_sender_closed(
    DBusLogSender* sender,
    gpointer user_data)
{
    dbus_log_core_remove_sender(DBUSLOG_CORE(user_data), sender);
}

static
void
dbus_log_core_emit_signal(
    DBusLogCore* self,
    DBusLogCategory* category,
    enum dbus_log_core_signal signal)
{
    dbus_log_category_ref(category);
    g_signal_emit(self, dbus_log_core_signals[signal], 0, category);
    dbus_log_category_unref(category);
}

/*==========================================================================*
 * API
 *==========================================================================*/

DBusLogCore*
dbus_log_core_new(
    guint backlog)
{
    DBusLogCore* self = g_object_new(DBUSLOG_CORE_TYPE, NULL);
    self->backlog = dbus_log_sender_normalize_backlog(backlog);
    return self;
}

DBusLogCore*
dbus_log_core_ref(
    DBusLogCore* self)
{
    if (G_LIKELY(self)) {
        g_object_ref(DBUSLOG_CORE(self));
        return self;
    } else {
        return NULL;
    }
}

void
dbus_log_core_unref(
    DBusLogCore* self)
{
    if (G_LIKELY(self)) {
        g_object_unref(DBUSLOG_CORE(self));
    }
}

DBusLogSender*
dbus_log_core_new_sender(
    DBusLogCore* self,
    const char* name)
{
    DBusLogSender* sender = NULL;
    if (G_LIKELY(self)) {
        sender = dbus_log_sender_new(name, self->backlog);
        if (sender) {
            /*
             * Replace the complete array in case if this function is
             * indirectly invoked by dbus_log_core_logv.
             */
            guint i;
            gulong signal_id;
            GPtrArray* old = self->senders;
            GPtrArray* new_array = g_ptr_array_new_full(old->len + 1,
                dbus_log_core_free_sender);

            /* Copy the old ones */
            for (i=0; i<self->senders->len; i++) {
                g_ptr_array_add(new_array,
                    dbus_log_sender_ref(g_ptr_array_index(old, i)));
            }

            /* Add the new one */
            g_ptr_array_add(new_array, dbus_log_sender_ref(sender));

            /* Register for close notifications */
            signal_id = dbus_log_sender_add_closed_handler(sender,
                dbus_log_core_sender_closed, self);
            g_hash_table_replace(self->sender_signal_ids, sender,
                (gpointer)signal_id);

            /* Swap the arrays */
            self->senders = new_array;
            g_ptr_array_unref(old);
        }
    }
    return sender;
}

gboolean
dbus_log_core_remove_sender(
    DBusLogCore* self,
    DBusLogSender* sender)
{
    gboolean removed = FALSE;
    if (G_LIKELY(self) && G_LIKELY(sender)) {
        int index = -1;
        guint i;

        /* Find the sender */
        for (i=0; i<self->senders->len; i++) {
            if (self->senders->pdata[i] == sender) {
                index = i;
                break;
            }
        }

        if (index >= 0) {
            /*
             * Replace the complete array in case if this function is
             * indirectly invoked by dbus_log_core_logv.
             */
            GPtrArray* old = self->senders;
            GPtrArray* new_array = g_ptr_array_new_full(old->len - 1,
                dbus_log_core_free_sender);

            for (i=0; i<(guint)index; i++) {
                g_ptr_array_add(new_array,
                    dbus_log_sender_ref(g_ptr_array_index(old, i)));
            }

            /* Skip the one we are removing */
            for (i++; i<self->senders->len; i++) {
                 g_ptr_array_add(new_array,
                    dbus_log_sender_ref(g_ptr_array_index(old, i)));
            }

            /* Unregister the handler */
            dbus_log_sender_remove_handler(sender, (gulong)
                g_hash_table_lookup(self->sender_signal_ids, sender));
            GVERIFY(g_hash_table_remove(self->sender_signal_ids, sender));

            /* Swap the arrays */
            self->senders = new_array;
            g_ptr_array_unref(old);
            removed = TRUE;
        }
    }
    return removed;
}

DBUSLOG_LEVEL
dbus_log_core_default_level(
    DBusLogCore* self)
{
    return G_LIKELY(self) ? self->default_level : DBUSLOG_LEVEL_UNDEFINED;
}

gboolean
dbus_log_core_set_default_level(
    DBusLogCore* self,
    DBUSLOG_LEVEL level)
{
    if (G_LIKELY(self) &&
        G_LIKELY(level >= DBUSLOG_LEVEL_ALWAYS) &&
        G_LIKELY(level < DBUSLOG_LEVEL_COUNT)) {
        if (self->default_level != level) {
            self->default_level = level;
            g_signal_emit(self, dbus_log_core_signals[SIGNAL_DEFAULT_LEVEL], 0);
        }
        return TRUE;
    }
    return FALSE;
}

int
dbus_log_core_backlog(
    DBusLogCore* self)
{
    return G_LIKELY(self) ? self->backlog : 0;
}

void
dbus_log_core_set_backlog(
    DBusLogCore* self,
    int backlog)
{
    if (G_LIKELY(self)) {
        backlog = dbus_log_sender_normalize_backlog(backlog);
        if (self->backlog != backlog) {
            GPtrArray* senders = self->senders;
            guint i;

            self->backlog = backlog;
            for (i = 0; i < senders->len; i++) {
                dbus_log_sender_set_backlog(g_ptr_array_index(senders, i),
                    backlog);
            }
            g_signal_emit(self, dbus_log_core_signals[SIGNAL_BACKLOG], 0);
        }
    }
}

DBusLogCategory*
dbus_log_core_new_category(
    DBusLogCore* self,
    const char* name,
    DBUSLOG_LEVEL level,
    gulong flags)
{
    DBusLogCategory* cat = NULL;
    if (G_LIKELY(self) && G_LIKELY(name)) {
        cat = g_hash_table_lookup(self->categories, name);
        if (!cat) {
            self->last_cid++;
            if (!self->last_cid) self->last_cid++;
            cat = dbus_log_category_new(name, self->last_cid);
            if (G_LIKELY(level >= DBUSLOG_LEVEL_UNDEFINED) &&
                G_LIKELY(level < DBUSLOG_LEVEL_COUNT)) {
                cat->level = level;
            }
            cat->flags = (flags & DBUSLOG_CATEGORY_FLAG_MASK);
            if (flags & DBUSLOG_CATEGORY_FLAG_ENABLED) {
                cat->flags |= DBUSLOG_CATEGORY_FLAG_ENABLED_BY_DEFAULT;
            }
            g_hash_table_replace(self->categories, (void*)cat->name, cat);
            dbus_log_core_emit_signal(self, cat, SIGNAL_CATEGORY_ADDED);
        }
        dbus_log_category_ref(cat);
    }
    return cat;
}

DBusLogCategory*
dbus_log_core_find_category(
    DBusLogCore* self,
    const char* name)
{
    return (G_LIKELY(self) && G_LIKELY(name)) ?
        g_hash_table_lookup(self->categories, name) :
        NULL;
}

GPtrArray*
dbus_log_core_find_categories(
    DBusLogCore* self,
    const char* pattern)
{
    GPtrArray* array = NULL;
    if (G_LIKELY(self)) {
        if (pattern && pattern[0] && strcmp(pattern, "*")) {
            GHashTableIter it;
            gpointer value;
            array = g_ptr_array_new_full(0, dbus_log_category_free);
            g_hash_table_iter_init(&it, self->categories);
            while (g_hash_table_iter_next(&it, NULL, &value)) {
                DBusLogCategory* cat = value;
                if (g_pattern_match_simple(pattern, cat->name)) {
                    g_ptr_array_add(array, dbus_log_category_ref(cat));
                }
            }
            g_ptr_array_sort(array, dbus_log_category_sort_name);
            gutil_idle_pool_add_ptr_array(self->pool, array);
        } else {
            array = dbus_log_core_get_categories(self);
        }
    }
    return array;
}

GPtrArray*
dbus_log_core_get_categories(
    DBusLogCore* self)
{
    GPtrArray* array = NULL;
    if (G_LIKELY(self)) {
        array = dbus_log_category_values(self->categories);
        g_ptr_array_sort(array, dbus_log_category_sort_name);
        gutil_idle_pool_add_ptr_array(self->pool, array);
    }
    return array;
}

gboolean
dbus_log_core_remove_category(
    DBusLogCore* self,
    const char* name)
{
    gboolean removed = FALSE;
    if (G_LIKELY(self) && G_LIKELY(name)) {
        DBusLogCategory* cat = g_hash_table_lookup(self->categories, name);
        if (cat) {
            removed = TRUE;
            dbus_log_category_ref(cat);
            GVERIFY(g_hash_table_remove(self->categories, name));
            dbus_log_core_emit_signal(self, cat, SIGNAL_CATEGORY_REMOVED);
            dbus_log_category_unref(cat);
        }
    }
    return removed;
}

void
dbus_log_core_remove_all_categories(
    DBusLogCore* self)
{
    if (G_LIKELY(self)) {
        guint i, count = g_hash_table_size(self->categories);
        if (count > 0) {
            if (g_signal_has_handler_pending(self, dbus_log_core_signals[ 
                SIGNAL_CATEGORY_REMOVED], 0, FALSE)) {
                GPtrArray* cats = dbus_log_core_get_categories(self);
                g_ptr_array_ref(cats);
                g_hash_table_remove_all(self->categories);
                for (i=0; i<cats->len; i++) {
                    dbus_log_core_emit_signal(self, g_ptr_array_index(cats, i),
                        SIGNAL_CATEGORY_REMOVED);
                }
                g_ptr_array_unref(cats);
            } else {
                g_hash_table_remove_all(self->categories);
            }
        }
    }
}

void
dbus_log_core_set_category_enabled(
    DBusLogCore* self,
    const char* name,
    gboolean enable)
{
    if (G_LIKELY(self) && G_LIKELY(name)) {
        DBusLogCategory* cat = g_hash_table_lookup(self->categories, name);
        if (cat) {
            gboolean changed;
            if ((cat->flags & DBUSLOG_CATEGORY_FLAG_ENABLED) && !enable) {
                cat->flags &= ~DBUSLOG_CATEGORY_FLAG_ENABLED;
                changed = TRUE;
            } else {
                if (!(cat->flags & DBUSLOG_CATEGORY_FLAG_ENABLED) && enable) {
                    cat->flags |= DBUSLOG_CATEGORY_FLAG_ENABLED;
                    changed = TRUE;
                } else {
                    changed = FALSE;
                }
            }
            if (changed) {
                dbus_log_category_ref(cat);
                g_signal_emit(self, dbus_log_core_signals[
                    SIGNAL_CATEGORY_FLAGS], 0,
                    cat, DBUSLOG_CATEGORY_FLAG_ENABLED);
                dbus_log_category_unref(cat);
            }
        }
    }
}

void
dbus_log_core_set_category_level(
    DBusLogCore* self,
    const char* name,
    DBUSLOG_LEVEL level)
{
    if (G_LIKELY(self) && G_LIKELY(name) &&
        G_LIKELY(level >= DBUSLOG_LEVEL_UNDEFINED) &&
        G_LIKELY(level < DBUSLOG_LEVEL_COUNT)) {
        DBusLogCategory* cat = g_hash_table_lookup(self->categories, name);
        if (cat && cat->level != level) {
            cat->level = level;
            dbus_log_core_emit_signal(self, cat, SIGNAL_CATEGORY_LEVEL);
        }
    }
}

static
void
dbus_log_core_send(
    DBusLogCore* self,
    DBusLogCategory* category,
    DBusLogMessage* message)
{
    guint i;
    GPtrArray* senders = g_ptr_array_ref(self->senders);

    message->timestamp = g_get_real_time();
    message->index = self->next_msg_index++;

    if (category) {
        message->category = category->id;
    }

    for (i=0; i<senders->len; i++) {
        dbus_log_sender_send(g_ptr_array_index(senders, i), message);
    }

    g_ptr_array_unref(senders);
}

static
gboolean
dbus_log_core_should_log(
    DBusLogCore* self,
    DBUSLOG_LEVEL level,
    const char* cname,
    DBusLogCategory** cat)
{
    if (G_LIKELY(self) && self->senders->len) {
        gboolean send = (self->default_level <= DBUSLOG_LEVEL_UNDEFINED) ||
            (level <= self->default_level);
        if (cname) {
            *cat = g_hash_table_lookup(self->categories, cname);
            if (*cat) {
                if ((*cat)->flags & DBUSLOG_CATEGORY_FLAG_ENABLED) {
                    if ((*cat)->level > DBUSLOG_LEVEL_UNDEFINED) {
                        /* Category has non-default log level */
                        send = (level <= (*cat)->level);
                    }
                } else {
                    /* Category is disabled */
                    send = FALSE;
                }
            }
        } else {
            *cat = NULL;
        }
        return send;
    } else {
        return FALSE;
    }
}

gboolean
dbus_log_core_log(
    DBusLogCore* self,
    DBUSLOG_LEVEL level,
    const char* cname,
    const char* message)
{
    DBusLogCategory* cat;
    if (dbus_log_core_should_log(self, level, cname, &cat)) {
        DBusLogMessage* msg = dbus_log_message_new(message);
        msg->level = level;
        dbus_log_core_send(self, cat, msg);
        dbus_log_message_unref(msg);
        return TRUE;
    }
    return FALSE;
}

gboolean
dbus_log_core_logv(
    DBusLogCore* self,
    DBUSLOG_LEVEL level,
    const char* cname,
    const char* format,
    va_list args)
{
    DBusLogCategory* cat;
    if (dbus_log_core_should_log(self, level, cname, &cat)) {
        DBusLogMessage* msg = dbus_log_message_new_va(format, args);
        msg->level = level;
        dbus_log_core_send(self, cat, msg);
        dbus_log_message_unref(msg);
        return TRUE;
    }
    return FALSE;
}

gulong
dbus_log_core_add_backlog_handler(
    DBusLogCore* self,
    DBusLogCoreFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_BACKLOG_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
dbus_log_core_add_default_level_handler(
    DBusLogCore* self,
    DBusLogCoreFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_DEFAULT_LEVEL_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
dbus_log_core_add_category_added_handler(
    DBusLogCore* self,
    DBusLogCoreCategoryFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_ADDED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
dbus_log_core_add_category_removed_handler(
    DBusLogCore* self,
    DBusLogCoreCategoryFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_REMOVED_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
dbus_log_core_add_category_level_handler(
    DBusLogCore* self,
    DBusLogCoreCategoryFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_LEVEL_NAME, G_CALLBACK(fn), data) : 0;
}

gulong
dbus_log_core_add_category_flags_handler(
    DBusLogCore* self,
    DBusLogCoreCategoryFlagsFunc fn,
    gpointer data)
{
    return (G_LIKELY(self) && G_LIKELY(fn)) ? g_signal_connect(self,
        SIGNAL_CATEGORY_FLAGS_NAME, G_CALLBACK(fn), data) : 0;
}

void
dbus_log_core_remove_handler(
    DBusLogCore* self,
    gulong id)
{
    if (G_LIKELY(self) && G_LIKELY(id)) {
        g_signal_handler_disconnect(self, id);
    }
}

void
dbus_log_core_remove_handlers(
    DBusLogCore* self,
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
dbus_log_core_init(
    DBusLogCore* self)
{
    self->default_level = DBUSLOG_LEVEL_INFO;
    self->pool = gutil_idle_pool_new();
    self->senders = g_ptr_array_new_with_free_func(dbus_log_core_free_sender);
    self->categories = g_hash_table_new_full(g_str_hash, g_str_equal, NULL,
        dbus_log_category_free);
    self->sender_signal_ids = g_hash_table_new_full(g_direct_hash,
        g_direct_equal, NULL, NULL);
}

/**
 * First stage of deinitialization (release all references).
 * May be called more than once in the lifetime of the object.
 */
static
void
dbus_log_core_dispose(
    GObject* object)
{
    DBusLogCore* self = DBUSLOG_CORE(object);
    guint i;

    /*
     * Unregister the handlers first, to prevent dbus_log_core_sender_closed
     * from being invoked when we clear self->senders array.
     */
    for (i=0; i<self->senders->len; i++) {
        DBusLogSender* sender = g_ptr_array_index(self->senders, i);
        dbus_log_sender_remove_handler(sender, (gulong)
            g_hash_table_lookup(self->sender_signal_ids, sender));
        GVERIFY(g_hash_table_remove(self->sender_signal_ids, sender));
    }
    GASSERT(!g_hash_table_size(self->sender_signal_ids));
    g_ptr_array_set_size(self->senders, 0);
    g_hash_table_remove_all(self->categories);
    gutil_idle_pool_drain(self->pool);
    G_OBJECT_CLASS(PARENT_CLASS)->dispose(object);
}

/**
 * Final stage of deinitialization
 */
static
void
dbus_log_core_finalize(
    GObject* object)
{
    DBusLogCore* self = DBUSLOG_CORE(object);
    g_ptr_array_unref(self->senders);
    g_hash_table_destroy(self->categories);
    g_hash_table_destroy(self->sender_signal_ids);
    gutil_idle_pool_unref(self->pool);
    G_OBJECT_CLASS(PARENT_CLASS)->finalize(object);
}

/**
 * Per class initializer
 */
static
void
dbus_log_core_class_init(
    DBusLogCoreClass* klass)
{
    GObjectClass* object_class = G_OBJECT_CLASS(klass);
    GType class_type = G_OBJECT_CLASS_TYPE(klass);
    object_class->dispose = dbus_log_core_dispose;
    object_class->finalize = dbus_log_core_finalize;
    dbus_log_core_signals[SIGNAL_DEFAULT_LEVEL] =
        g_signal_new(SIGNAL_DEFAULT_LEVEL_NAME,
            class_type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    dbus_log_core_signals[SIGNAL_BACKLOG] =
        g_signal_new(SIGNAL_BACKLOG_NAME,
            class_type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 0);
    dbus_log_core_signals[SIGNAL_CATEGORY_ADDED] =
        g_signal_new(SIGNAL_CATEGORY_ADDED_NAME,
            class_type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_POINTER);
    dbus_log_core_signals[SIGNAL_CATEGORY_REMOVED] =
        g_signal_new(SIGNAL_CATEGORY_REMOVED_NAME,
            class_type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_POINTER);
    dbus_log_core_signals[SIGNAL_CATEGORY_LEVEL] =
        g_signal_new(SIGNAL_CATEGORY_LEVEL_NAME,
            class_type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 1, G_TYPE_POINTER);
    dbus_log_core_signals[SIGNAL_CATEGORY_FLAGS] =
        g_signal_new(SIGNAL_CATEGORY_FLAGS_NAME,
            class_type, G_SIGNAL_RUN_FIRST, 0, NULL, NULL, NULL,
            G_TYPE_NONE, 2, G_TYPE_POINTER, G_TYPE_UINT);
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
