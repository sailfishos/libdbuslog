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

#include "dbuslog_category.h"

#include <gutil_macros.h>
#include <gutil_log.h>

typedef struct dbus_log_category_priv {
    DBusLogCategory pub;
    gint ref_count;
    char* name;
} DBusLogCategoryPriv;

static
inline
DBusLogCategoryPriv*
dbus_log_category_cast(
    DBusLogCategory* cat)
{
    return G_CAST(cat, DBusLogCategoryPriv, pub);
}

DBusLogCategory*
dbus_log_category_new(
    const char* name,
    guint id)
{
    DBusLogCategoryPriv* priv = g_slice_new0(DBusLogCategoryPriv);
    DBusLogCategory* cat = &priv->pub;
    priv->ref_count = 1;
    cat->level = DBUSLOG_LEVEL_UNDEFINED;
    cat->name = priv->name = g_strdup(name);
    cat->id = id;
    return cat;
}

static
void
dbus_log_category_finalize(
    DBusLogCategoryPriv* priv)
{
    g_free(priv->name);
    g_slice_free(DBusLogCategoryPriv, priv);
}

DBusLogCategory*
dbus_log_category_ref(
    DBusLogCategory* cat)
{
    if (G_LIKELY(cat)) {
        DBusLogCategoryPriv* priv = dbus_log_category_cast(cat);
        GASSERT(priv->ref_count > 0);
        g_atomic_int_inc(&priv->ref_count);
    }
    return cat;
}

void
dbus_log_category_unref(
    DBusLogCategory* cat)
{
    if (G_LIKELY(cat)) {
        DBusLogCategoryPriv* priv = dbus_log_category_cast(cat);
        GASSERT(priv->ref_count > 0);
        if (g_atomic_int_dec_and_test(&priv->ref_count)) {
            dbus_log_category_finalize(priv);
        }
    }
}

void
dbus_log_category_free(
    gpointer data)
{
    dbus_log_category_unref(data);
}

gint
dbus_log_category_sort_name(
    gconstpointer p1,
    gconstpointer p2)
{
    DBusLogCategory* const* c1 = p1;
    DBusLogCategory* const* c2 = p2;
    return g_strcmp0((*c1)->name, (*c2)->name);
}

GPtrArray*
dbus_log_category_values(
    GHashTable* table)
{
    GPtrArray* array = NULL;
    if (G_LIKELY(table)) {
        GHashTableIter it;
        gpointer value;
        array = g_ptr_array_new_full(g_hash_table_size(table),
            dbus_log_category_free);
        g_hash_table_iter_init(&it, table);
        while (g_hash_table_iter_next(&it, NULL, &value)) {
            g_ptr_array_add(array, dbus_log_category_ref(value));
        }
    }
    return array;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
