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

#include <dbuslog_client.h>
#include <gutil_macros.h>
#include <gutil_strv.h>
#include <gutil_log.h>

#include <glib-unix.h>

#define RET_OK      (0)
#define RET_ERR     (1)
#define RET_CANCEL  (2)
#define RET_TIMEOUT (3)

enum {
    APP_EVENT_ERROR,
    APP_EVENT_CONNECT,
    APP_EVENT_MESSAGE,
    APP_N_EVENTS
};

typedef struct app_action AppAction;
typedef DBusLogClientCall* (*AppActionRunFunc)(AppAction* action);
typedef void (*AppActionFreeFunc)(AppAction* action);

typedef struct app {
    GMainLoop* loop;
    DBusLogClient* client;
    DBusLogClientCall* call;
    AppAction* actions;
    gboolean follow;
    gboolean datetime;
    gboolean timestamp;
    gboolean print_log_level;
    gboolean print_backlog;
    char* out_filename;
    FILE* out_file;
    gulong event_id[APP_N_EVENTS];
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

typedef struct app_action_int {
    AppAction action;
    int value;
} AppActionInt;

static
void
app_follow(
    App* app);

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
        if (app->follow) {
            app_follow(app);
        } else {
            app_quit(app);
        }
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
AppAction*
app_action_int_new(
    App* app,
    AppActionRunFunc run,
    int value)
{
    AppActionInt* int_action = g_new0(AppActionInt, 1);
    AppAction* action = &int_action->action;
    action->app = app;
    action->fn_run = run;
    action->fn_free = app_action_free;
    int_action->value = value;
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
app_action_set_level_level(
    AppAction* action)
{
    AppActionInt* int_action = G_CAST(action,AppActionInt,action);
    GDEBUG("Settings log level to %d", int_action->value);
    return dbus_log_client_set_default_level(action->app->client,
        int_action->value, app_action_call_done, action);
}

static
DBusLogClientCall*
app_action_set_backlog(
    AppAction* action)
{
    AppActionInt* int_action = G_CAST(action,AppActionInt,action);
    DBusLogClient* client = action->app->client;
    if (client->api_version > 1) {
        GDEBUG("Settings backlog to %d", int_action->value);
        return dbus_log_client_set_backlog(action->app->client,
            int_action->value, app_action_call_done, action);
    } else {
        GERR("Backlog API is not supported by the remote");
        return NULL;
    }
}

static
DBusLogClientCall*
app_action_enable_all(
    AppAction* action)
{
    GDEBUG("Enabling all categories");
    return dbus_log_client_enable_pattern(action->app->client, "*",
        app_action_call_done, action);
}

static
DBusLogClientCall*
app_action_disable_all(
    AppAction* action)
{
    GDEBUG("Disabling all categories");
    return dbus_log_client_disable_pattern(action->app->client, "*",
        app_action_call_done, action);
}

static
DBusLogClientCall*
app_action_enable(
    AppAction* action)
{
    AppActionStr* str_action = G_CAST(action,AppActionStr,action);
    GDEBUG("Enabling '%s'", str_action->str);
    return dbus_log_client_enable_pattern(action->app->client,
        str_action->str, app_action_call_done, action);
}

static
DBusLogClientCall*
app_action_disable(
    AppAction* action)
{
    AppActionStr* str_action = G_CAST(action,AppActionStr,action);
    GDEBUG("Disabling '%s'", str_action->str);
    return dbus_log_client_disable_pattern(action->app->client,
        str_action->str, app_action_call_done, action);
}

static
DBusLogClientCall*
app_action_reset(
    AppAction* action)
{
    DBusLogClient* client = action->app->client;
    GStrV* enable = NULL;
    GStrV* disable = NULL;
    guint i;

    GDEBUG("Resetting categories...");
    for (i=0; i<client->categories->len; i++) {
        DBusLogCategory* cat = g_ptr_array_index(client->categories, i);
        if (cat->flags & DBUSLOG_CATEGORY_FLAG_ENABLED_BY_DEFAULT) {
            if (!(cat->flags & DBUSLOG_CATEGORY_FLAG_ENABLED)) {
                GVERBOSE(" enable %s", cat->name);
                enable = gutil_strv_add(enable, cat->name);
            }
        } else {
            if (cat->flags & DBUSLOG_CATEGORY_FLAG_ENABLED) {
                GVERBOSE("  disable %s", cat->name);
                disable = gutil_strv_add(disable, cat->name);
            }
        }
    }

    if (enable || disable) {
        DBusLogClientCall* call;
        if (enable && disable) {
            /* Register callback for the second call only */
            dbus_log_client_enable_categories(client, enable, NULL, NULL);
            call = dbus_log_client_disable_categories(client, disable,
                app_action_call_done, action);
        } else if (enable) {
            call = dbus_log_client_enable_categories(client, enable,
                app_action_call_done, action);
        } else {
            call = dbus_log_client_disable_categories(client, disable,
                app_action_call_done, action);
        }
        g_strfreev(enable);
        g_strfreev(disable);
        return call;
    } else {
        GDEBUG("Nothing to reset");
        return NULL;
    }
}

static
void
client_connected(
    App* app)
{
    DBusLogClient* client = app->client;
    GDEBUG("Connected!");
    if (app->print_log_level) {
        app->print_log_level = FALSE;
        printf("%d\n", client->default_level);
    }
    if (app->print_backlog) {
        app->print_backlog = FALSE;
        if (client->api_version > 1) {
            printf("%d\n", client->backlog);
        } else {
            GERR("Backlog API is not supported by the remote");
        }
    }
    app_next_action(app);
}

static
void
app_print(
    App* app,
    const char* format,
    ...)
{
    va_list va;
    va_start(va, format);
    vprintf(format, va);
    va_end(va);
    if (app->out_file) {
        va_start(va, format);
        if (vfprintf(app->out_file, format, va) < 0) {
            GERR("Error writing %s: %s", app->out_filename, strerror(errno));
            fclose(app->out_file);
            app->out_file = NULL;
        }
        va_end(va);
    }
}

static
void
client_message(
    DBusLogClient* client,
    DBusLogCategory* category,
    DBusLogMessage* message,
    gpointer user_data)
{
    App* app = user_data;
    const char* prefix;
    char buf[32];
    if (app->timestamp || app->datetime) {
        const char* format = app->datetime ? "%F %T" : "%T";
        const time_t t = (time_t)(message->timestamp/1000000);
        const int ms = (int)(message->timestamp%1000000)/1000;
        struct tm tm;
        gsize len;
        localtime_r(&t, &tm);
        buf[0] = buf[G_N_ELEMENTS(buf)-1] = 0;
        strftime(buf, G_N_ELEMENTS(buf)-1, format, &tm);
        len = strlen(buf);
        snprintf(buf + len, G_N_ELEMENTS(buf) - len - 1, ".%03d ", ms);
        prefix = buf;
    } else{
        prefix = "";
    }
    if (category && !(category->flags & DBUSLOG_CATEGORY_FLAG_HIDE_NAME)) {
        app_print(app, "%s%s: %s\n", prefix, category->name, message->string);
    } else {
        app_print(app, "%s%s\n", prefix, message->string);
    }
}

static
void
app_follow(
    App* app)
{
    if (!app->event_id[APP_EVENT_MESSAGE]) {
        app->event_id[APP_EVENT_MESSAGE] =
            dbus_log_client_add_message_handler(app->client,
                client_message, app);
    }
    if (!app->client->started) {
        GDEBUG("Starting live capture...");
        dbus_log_client_start(app->client, NULL, NULL);
        if (app->out_filename && !app->out_file) {
            app->out_file = fopen(app->out_filename, "w");
            if (app->out_file) {
                GDEBUG("Writing %s", app->out_filename);
            } else {
                GERR("Can't open %s: %s", app->out_filename, strerror(errno));
                g_free(app->out_filename);
                app->out_filename = NULL;
            }
        }
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
    gboolean run_loop = TRUE;

    app->loop = g_main_loop_new(NULL, FALSE);
    app->sigterm_id = g_unix_signal_add(SIGTERM, app_sigterm, app);
    app->sigint_id = g_unix_signal_add(SIGINT, app_sigint, app);
    app->event_id[APP_EVENT_ERROR] =
        dbus_log_client_add_connect_error_handler(app->client,
            client_connect_error, app);
    app->event_id[APP_EVENT_CONNECT] =
        dbus_log_client_add_connected_handler(app->client,
            client_connected_cb, app);
    if (app->client->connected) {
        client_connected(app);
        run_loop = app->follow;
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

    dbus_log_client_remove_handlers(app->client, app->event_id, APP_N_EVENTS);
    dbus_log_client_unref(app->client);
    g_main_loop_unref(app->loop);
    return app->ret;
}

static
gboolean
app_option_enable_all(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    App* app = data;
    app_add_action(app, app_action_new(app, app_action_enable_all));
    return TRUE;
}

static
gboolean
app_option_disable_all(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    App* app = data;
    app_add_action(app, app_action_new(app, app_action_disable_all));
    return TRUE;
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
app_option_reset(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    App* app = data;
    app_add_action(app, app_action_new(app, app_action_reset));
    return TRUE;
}

static
gboolean
app_option_log_level(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    App* app = data;
    if (value) {
        const int level = atoi(value);
        if (level > DBUSLOG_LEVEL_UNDEFINED && level < DBUSLOG_LEVEL_COUNT) {
            app_add_action(app, app_action_int_new(app,
                    app_action_set_level_level, level));
            return TRUE;
        } else {
            g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                "Invalid log level \'%s\'", value);
            return FALSE;
        }
    } else {
        app->print_log_level = TRUE;
        return TRUE;
    }
}

static
gboolean
app_option_backlog(
    const gchar* name,
    const gchar* value,
    gpointer data,
    GError** error)
{
    App* app = data;
    if (value) {
        const int level = atoi(value);
        if (level > 0) {
            app_add_action(app, app_action_int_new(app,
                    app_action_set_backlog, level));
            return TRUE;
        } else {
            g_set_error(error, G_OPTION_ERROR, G_OPTION_ERROR_BAD_VALUE,
                "Invalid backlog \'%s\'", value);
            return FALSE;
        }
    } else {
        app->print_backlog = TRUE;
        return TRUE;
    }
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
        { "follow", 'f', 0, G_OPTION_ARG_NONE, &app->follow,
          "Print log messages to stdout (default action)", NULL },
        { "write", 'w', 0, G_OPTION_ARG_FILENAME, &app->out_filename,
          "Write message to file too (requires -f)", "FILE" },
        { "timestamp", 'T', 0, G_OPTION_ARG_NONE, &app->timestamp,
          "Print message time (use -D to print the date too)", NULL },
        { "date", 'D', 0, G_OPTION_ARG_NONE, &app->datetime,
          "Print message time and date", NULL },
        { "categories", 'c', 0, G_OPTION_ARG_NONE, &list,
          "List log categories", NULL },
        { "print-log-level", 'L', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
           app_option_log_level, "Show the current log level", NULL },
        { "log-level", 'l', 0, G_OPTION_ARG_CALLBACK, app_option_log_level,
          "Set the log level (1..8)", "LEVEL" },
        { "print-backlog", 'B', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
           app_option_backlog, "Show current backlog", NULL },
        { "backlog", 'b', 0, G_OPTION_ARG_CALLBACK, app_option_backlog,
          "Set the backlog", "COUNT" },
        { "all", 'a', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
           app_option_enable_all, "Enable all log categories", NULL },
        { "none", 'n', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
           app_option_disable_all, "Disable all log categories", NULL },
        { "enable", 'e', 0, G_OPTION_ARG_CALLBACK, app_option_enable,
          "Enable log categories (repeatable)", "PATTERN" },
        { "disable", 'd', 0, G_OPTION_ARG_CALLBACK, app_option_disable,
          "Disable log categories (repeatable)", "PATTERN" },
        { "reset", 'r', G_OPTION_FLAG_NO_ARG, G_OPTION_ARG_CALLBACK,
           app_option_reset, "Reset log categories to default", NULL },
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
                G_BUS_TYPE_SESSION : G_BUS_TYPE_SYSTEM, service, path, 0);
            if (list) {
                app_add_action(app, app_action_new(app, app_action_list));
            }
            if (!app->actions && !app->print_log_level && !app->print_backlog) {
                /* Default action */
                app->follow = TRUE;
            }
            if (app->out_filename && !app->follow) {
                GWARN("Ignoring -w option (it requires -f)");
                g_free(app->out_filename);
                app->out_filename = NULL;
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

static
void
app_destroy(
    App* app)
{
    if (app->out_file) {
        fclose(app->out_file);
        app->out_file = NULL;
    }
    if (app->out_filename) {
        g_free(app->out_filename);
        app->out_filename = NULL;
    }
    while (app->actions) {
        AppAction* action = app->actions;
        app->actions = action->next;
        action->fn_free(action);
    }
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
    }
    app_destroy(&app);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
