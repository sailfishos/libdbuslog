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

#include <dbuslog_client.h>
#include <gutil_macros.h>
#include <gutil_log.h>

#include <glib-unix.h>

#define RET_OK      (0)
#define RET_ERR     (1)
#define RET_CANCEL  (2)
#define RET_TIMEOUT (3)

typedef struct app_action AppAction;
typedef DBusLogClientCall* (*AppActionRunFunc)(AppAction* action);
typedef void (*AppActionFreeFunc)(AppAction* action);

typedef struct app {
    GMainLoop* loop;
    DBusLogClient* client;
    DBusLogClientCall* call;
    AppAction* actions;
    gint timeout;
    guint timeout_id;
    guint sigterm_id;
    guint sigint_id;
    int ret;
} App;

struct app_action {
    AppAction* next;
    App* app;
    AppActionRunFunc fn_run;
    AppActionFreeFunc fn_free;
};

typedef struct app_action_str {
    AppAction action;
    char* str;
} AppActionStr;

static
void
app_quit(
    App* app)
{
    g_idle_add((GSourceFunc)g_main_loop_quit, app->loop);
}

static
void
app_next_action(
    App* app)
{
    while (app->actions && !app->call) {
        AppAction* action = app->actions;
        app->actions = action->next;
        action->next = NULL;
        app->call = action->fn_run(action);
    }
    if (!app->call) {
        app_quit(app);
    }
}

static
void
app_add_action(
    App* app,
    AppAction* action)
{
    if (app->actions) {
        AppAction* last = app->actions;
        while (last->next) {
            last = last->next;
        }
        last->next = action;
    } else {
        app->actions = action;
    }
}

static
void
app_action_free(
    AppAction* action)
{
    g_free(action);
}

static
void
app_action_str_free(
    AppAction* action)
{
    AppActionStr* str_action = G_CAST(action, AppActionStr, action);
    g_free(str_action->str);
    g_free(str_action);
}

static
AppAction*
app_action_new(
    App* app,
    AppActionRunFunc run)
{
    AppAction* action = g_new0(AppAction, 1);
    action->app = app;
    action->fn_run = run;
    action->fn_free = app_action_free;
    return action;
}

static
AppAction*
app_action_str_new(
    App* app,
    AppActionRunFunc run,
    const char* str)
{
    AppActionStr* str_action = g_new0(AppActionStr, 1);
    AppAction* action = &str_action->action;
    action->app = app;
    action->fn_run = run;
    action->fn_free = app_action_str_free;
    str_action->str = g_strdup(str);
    return action;
}

static
DBusLogClientCall*
app_action_list(
    AppAction* action)
{
    guint i;
    GPtrArray* list = action->app->client->categories;
    for (i=0; i<list->len; i++) {
        DBusLogCategory* cat = g_ptr_array_index(list, i);
        printf("%s: %s%s\n", cat->name,
            (cat->flags & DBUSLOG_CATEGORY_FLAG_ENABLED) ? "on" : "off",
            (cat->flags & DBUSLOG_CATEGORY_FLAG_ENABLED_BY_DEFAULT) ?
                "|default" : "");
    }
    action->fn_free(action);
    return NULL;
}

static
void
app_action_call_done(
    DBusLogClientCall* call,
    const GError* error,
    gpointer data)
{
    AppAction* action = data;
    App* app = action->app;
    GASSERT(app->call == call);
    app->call = NULL;
    action->fn_free(action);
    if (error) {
        GERR("%s", error->message);
        app->ret = RET_ERR;
        app_quit(app);
    } else {
        app_next_action(app);
    }
}

static
DBusLogClientCall*
app_action_enable(
    AppAction* action)
{
    AppActionStr* str_action = G_CAST(action,AppActionStr,action);
    return dbus_log_client_enable_pattern(action->app->client,
        str_action->str, app_action_call_done, action);
}

static
DBusLogClientCall*
app_action_disable(
    AppAction* action)
{
    AppActionStr* str_action = G_CAST(action,AppActionStr,action);
    return dbus_log_client_disable_pattern(action->app->client,
        str_action->str, app_action_call_done, action);
}

static
void
client_connected(
    App* app)
{
    GDEBUG("Connected!");
    app_next_action(app);
}

static
void
client_message(
    DBusLogClient* client,
    DBusLogCategory* category,
    DBusLogMessage* message,
    gpointer user_data)
{
    if (category && !(category->flags & DBUSLOG_CATEGORY_FLAG_HIDE_NAME)) {
        printf("%s: %s\n", category->name, message->string);
    } else {
        printf("%s\n", message->string);
    }
}

static
void
client_connected_cb(
    DBusLogClient* client,
    gpointer user_data)
{
    App* app = user_data;
    if (client->connected) {
        client_connected(app);
    } else {
        GDEBUG("Disconnected!");
    }
}

static
void
client_connect_error(
    DBusLogClient* client,
    const GError* error,
    gpointer user_data)
{
    App* app = user_data;
    app->ret = RET_ERR;
    app_quit(app);
}

static
gboolean
app_timeout(
    gpointer user_data)
{
    App* app = user_data;
    GERR("Timeout");
    app->timeout_id = 0;
    app->ret = RET_TIMEOUT;
    app_quit(app);
    return G_SOURCE_REMOVE;
}

static
gboolean
app_signal(
    App* app)
{
    GINFO("Caught signal, shutting down...");
    app->ret = RET_CANCEL;
    if (app->call) {
        dbus_log_client_call_cancel(app->call);
        app->call = NULL;
        return G_SOURCE_CONTINUE;
    } else {
        app_quit(app);
        return G_SOURCE_REMOVE;
    }
}

static
gboolean
app_sigterm(
    gpointer user_data)
{
    App* app = user_data;
    gboolean result = app_signal(app);
    if (result == G_SOURCE_REMOVE) {
        app->sigterm_id = 0;
    }
    return result;
}

static
gboolean
app_sigint(
    gpointer user_data)
{
    App* app = user_data;
    gboolean result = app_signal(app);
    if (result == G_SOURCE_REMOVE) {
        app->sigint_id = 0;
    }
    return result;
}

static
int
app_run(
    App* app)
{
    int n = 0;
    gulong id[2];
    gboolean run_loop = TRUE;

    app->loop = g_main_loop_new(NULL, FALSE);
    app->sigterm_id = g_unix_signal_add(SIGTERM, app_sigterm, app);
    app->sigint_id = g_unix_signal_add(SIGINT, app_sigint, app);
    id[n++] = dbus_log_client_add_connect_error_handler(app->client,
        client_connect_error, app);
    if (app->actions) {
        if (app->client->connected) {
            client_connected(app);
            run_loop = FALSE;
        } else {
            id[n++] = dbus_log_client_add_connected_handler(app->client,
                client_connected_cb, app);
        }
    } else {
        id[n++] = dbus_log_client_add_message_handler(app->client,
            client_message, app);
    }

    if (app->timeout > 0) {
        GDEBUG("Timeout %d sec", app->timeout);
        app->timeout_id = g_timeout_add_seconds(app->timeout, app_timeout, app);
    }

    if (run_loop) {
        g_main_loop_run(app->loop);
    }

    if (app->sigterm_id) g_source_remove(app->sigterm_id);
    if (app->sigint_id) g_source_remove(app->sigint_id);
    if (app->timeout_id) g_source_remove(app->timeout_id);

    dbus_log_client_remove_handlers(app->client, id, n);
    dbus_log_client_unref(app->client);
    g_main_loop_unref(app->loop);
    return app->ret;
}

static
gboolean
app_option_enable(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    App* app = data;
    app_add_action(app, app_action_str_new(app, app_action_enable, value));
    return TRUE;
}

static
gboolean
app_option_disable(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    App* app = data;
    app_add_action(app, app_action_str_new(app, app_action_disable, value));
    return TRUE;
}

static
gboolean
app_init(
    App* app,
    int argc,
    char* argv[])
{
    gboolean ok = FALSE;
    gboolean session_bus = FALSE;
    gboolean verbose = FALSE;
    gboolean list = FALSE;
    GOptionEntry entries[] = {
        { "session", 0, 0, G_OPTION_ARG_NONE, &session_bus,
          "Use session bus (default is system)", NULL },
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "timeout", 't', 0, G_OPTION_ARG_INT, &app->timeout,
          "Timeout in seconds", "SEC" },
        { NULL }
    };
    GOptionEntry action_entries[] = {
        { "list", 'l', 0, G_OPTION_ARG_NONE, &list,
          "List log categories", NULL },
        { "enable", 'e', 0, G_OPTION_ARG_CALLBACK, app_option_enable,
          "Enable log categories (repeatable)", "PATTERN" },
        { "disable", 'd', 0, G_OPTION_ARG_CALLBACK, app_option_disable,
          "Disable log categories (repeatable)", "PATTERN" },
        { NULL }
    };
    GError* error = NULL;
    GOptionContext* options = g_option_context_new("[PATH] SERVICE");
    GOptionGroup* actions = g_option_group_new("actions",
        "Action Options:", "Show all actions", app, NULL);
    g_option_context_add_main_entries(options, entries, NULL);
    g_option_group_add_entries(actions, action_entries);
    g_option_context_add_group(options, actions);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        if (argc >= 2 && argc <= 3) {
            const char* path;
            const char* service;
            if (argc == 2) {
                path = "/";
                service = argv[1];
            } else {
                path = argv[1];
                service = argv[2];
            }
            if (verbose) gutil_log_default.level = GLOG_LEVEL_VERBOSE;
            app->client = dbus_log_client_new(session_bus ?
                G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM,
                service, path, app->actions ? 0 :
                DBUSLOG_CLIENT_FLAG_AUTOSTART);
            if (list) {
                app_add_action(app, app_action_new(app, app_action_list));
            }
            ok = TRUE;
        } else {
            char* help = g_option_context_get_help(options, TRUE, NULL);
            fprintf(stderr, "%s", help);
            g_free(help);
        }
    } else {
        GERR("%s", error->message);
        g_error_free(error);
    }
    g_option_context_free(options);
    return ok;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    App app;
    memset(&app, 0, sizeof(app));
    gutil_log_timestamp = FALSE;
    gutil_log_set_type(GLOG_TYPE_STDOUT, "dbuslog");
    gutil_log_default.level = GLOG_LEVEL_DEFAULT;
    if (app_init(&app, argc, argv)) {
        ret = app_run(&app);
        while (app.actions) {
            AppAction* action = app.actions;
            app.actions = action->next;
            action->fn_free(action);
        }
    }
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
