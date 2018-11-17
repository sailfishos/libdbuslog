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
#include "dbuslog_receiver.h"
#include "dbuslog_protocol.h"
#include "gutil_log.h"

#include <unistd.h>

#define RET_OK       (0)
#define RET_ERR      (1)
#define RET_TIMEOUT  (2)

#define TEST_TIMEOUT (10) /* seconds */

typedef struct test_desc {
    const char* name;
    int (*run)(GMainLoop* loop);
} TestDesc;

static
void
test_sendv(
    DBusLogCore* core,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* format,
    ...) G_GNUC_PRINTF(4,5);

static
void
test_sendv(
    DBusLogCore* core,
    DBUSLOG_LEVEL level,
    const char* category,
    const char* format,
    ...)
{
    va_list va;
    va_start(va, format);
    dbus_log_core_logv(core, level, category, format, va);
    va_end(va);
}

#define test_send(core,level,category,message) \
    dbus_log_core_log(core, level, category, message)

/*==========================================================================*
 * Basic
 *==========================================================================*/

typedef struct _test_basic {
    GMainLoop* loop;
    DBusLogSender* sender;
    int default_level_changed;
    int backlog_changed;
    int received;
    int received_ok;
    int ret;
} TestBasic;

static const char* test_basic_msg [] = {
    "Test message 1",
    "Test message 2"
};

static
void
test_basic_default_level_changed(
    DBusLogCore* core,
    gpointer user_data)
{
    TestBasic* test = user_data;
    test->default_level_changed++;
    GVERBOSE_("%d", dbus_log_core_default_level(core));
}

static
void
test_basic_backlog_changed(
    DBusLogCore* core,
    gpointer user_data)
{
    TestBasic* test = user_data;
    test->backlog_changed++;
    GVERBOSE_("%d", dbus_log_core_backlog(core));
}

static
void
test_basic_message_received(
    DBusLogReceiver* receiver,
    DBusLogMessage* msg,
    gpointer user_data)
{
    TestBasic* test = user_data;
    GDEBUG("%s", msg->string);
    if (test->received < G_N_ELEMENTS(test_basic_msg)) {
        if (!g_strcmp0(msg->string, test_basic_msg[test->received])) {
            if (test->received_ok == msg->index) {
                test->received_ok++;
            } else {
                GERR("Unexpected index %u", msg->index);
            }
        } else {
            GERR("Expected \"%s\", got \"%s\"", test_basic_msg[test->received],
                msg->string);
        }
    }
    test->received++;
    dbus_log_sender_close(test->sender, TRUE);
}

static
void
test_basic_receiver_closed(
    DBusLogReceiver* receiver,
    gpointer user_data)
{
    TestBasic* test = user_data;
    GDEBUG("Closed");
    if (test->received == test->received_ok &&
        test->received == G_N_ELEMENTS(test_basic_msg)) {
        test->ret = RET_OK;
    }
    g_main_loop_quit(test->loop);
}

static
int
test_basic(GMainLoop* loop)
{
    TestBasic test;
    DBusLogCore* core;
    DBusLogReceiver* receiver;
    DBusLogSender* dummy;
    DBusLogMessage* dummy_msg = dbus_log_message_new(NULL);
    gulong message_id, closed_id, default_level_id, backlog_id;
    guint i;

    memset(&test, 0, sizeof(test));
    test.ret = RET_ERR;
    test.loop = loop;
    core = dbus_log_core_new(0);
    test.sender = dbus_log_core_new_sender(core, "Test");

    default_level_id = dbus_log_core_add_default_level_handler(core,
      test_basic_default_level_changed, &test);
    backlog_id = dbus_log_core_add_backlog_handler(core,
      test_basic_backlog_changed, &test);
    g_assert(backlog_id);

    dummy = dbus_log_core_new_sender(core, "Dummy1");
    dbus_log_core_remove_sender(core, dummy);
    dbus_log_core_remove_sender(core, dummy);
    dbus_log_sender_unref(dummy);
    dbus_log_sender_unref(dbus_log_core_new_sender(core, "Dummy2"));

    receiver = dbus_log_receiver_new(dup(test.sender->readfd), TRUE);
    message_id = dbus_log_receiver_add_message_handler(receiver,
        test_basic_message_received, &test);
    closed_id = dbus_log_receiver_add_closed_handler(receiver,
        test_basic_receiver_closed, &test);

    dbus_log_sender_ping(test.sender);
    for (i=0; i<G_N_ELEMENTS(test_basic_msg); i++) {
        test_send(core, DBUSLOG_LEVEL_INFO, NULL, test_basic_msg[i]);
    }

    dbus_log_receiver_pause(receiver);
    dbus_log_receiver_pause(receiver);
    dbus_log_receiver_resume(receiver);
    dbus_log_receiver_resume(receiver);

    /* Test resistance to NULL and invalid parameters */
    dbus_log_core_ref(NULL);
    dbus_log_core_unref(NULL);
    dbus_log_core_new_sender(NULL, NULL);
    dbus_log_core_remove_sender(NULL, NULL);
    dbus_log_core_remove_sender(NULL, test.sender);
    dbus_log_core_remove_sender(core, NULL);
    dbus_log_core_new_category(NULL, NULL, DBUSLOG_LEVEL_UNDEFINED, 0);
    dbus_log_core_new_category(core, NULL, DBUSLOG_LEVEL_UNDEFINED, 0);
    dbus_log_core_new_category(NULL, "Foo", DBUSLOG_LEVEL_UNDEFINED, 0);
    dbus_log_core_find_category(NULL, NULL);
    dbus_log_core_find_category(core, NULL);
    dbus_log_core_find_category(NULL, "Bar");
    dbus_log_core_find_categories(NULL, NULL);
    dbus_log_core_find_categories(core, NULL);
    dbus_log_core_find_categories(NULL, "Bar");
    dbus_log_core_set_category_level(NULL, NULL, DBUSLOG_LEVEL_UNDEFINED);
    dbus_log_core_set_category_level(core, NULL, DBUSLOG_LEVEL_UNDEFINED);
    dbus_log_core_set_category_level(core, "Foo", DBUSLOG_LEVEL_UNDEFINED);
    dbus_log_core_set_category_level(core, "Foo", DBUSLOG_LEVEL_COUNT);
    dbus_log_core_get_categories(NULL);
    dbus_log_core_remove_category(NULL, NULL);
    dbus_log_core_remove_category(core, NULL);
    dbus_log_core_remove_all_categories(NULL);
    dbus_log_core_add_default_level_handler(NULL, NULL, NULL);
    dbus_log_core_add_default_level_handler(core, NULL, NULL);
    dbus_log_core_add_default_level_handler(NULL,
        test_basic_default_level_changed, NULL);
    dbus_log_core_add_category_level_handler(NULL, NULL, NULL);
    dbus_log_core_add_category_level_handler(core, NULL, NULL);
    dbus_log_core_set_category_enabled(NULL, NULL, FALSE);
    dbus_log_core_set_category_enabled(core, NULL, FALSE);
    dbus_log_core_add_category_added_handler(NULL, NULL, NULL);
    dbus_log_core_add_category_added_handler(core, NULL, NULL);
    dbus_log_core_add_category_removed_handler(NULL, NULL, NULL);
    dbus_log_core_add_category_removed_handler(core, NULL, NULL);
    dbus_log_core_add_category_flags_handler(NULL, NULL, NULL);
    dbus_log_core_add_category_flags_handler(core, NULL, NULL);

    g_assert(!dbus_log_core_add_backlog_handler(NULL, NULL, NULL));
    g_assert(!dbus_log_core_add_backlog_handler(core, NULL, NULL));
    g_assert(!dbus_log_core_backlog(NULL));
    g_assert(dbus_log_core_backlog(core));
    dbus_log_core_set_backlog(NULL, 0);
    g_assert(!test.backlog_changed);
    dbus_log_core_set_backlog(core, 1);
    g_assert(test.backlog_changed == 1);
    dbus_log_core_set_backlog(core, 1);
    g_assert(test.backlog_changed == 1);
    dbus_log_core_set_backlog(core, 2);
    g_assert(test.backlog_changed == 2);

    dbus_log_core_remove_handler(NULL, 0);
    dbus_log_core_remove_handler(core, 0);
    dbus_log_message_ref(NULL);
    dbus_log_message_unref(NULL);
    dbus_log_sender_ref(NULL);
    dbus_log_sender_unref(NULL);
    dbus_log_sender_ping(NULL);
    dbus_log_sender_send(NULL, NULL);
    dbus_log_sender_send(test.sender, NULL);
    dbus_log_sender_send(NULL, dummy_msg);
    dbus_log_sender_close(NULL, FALSE);
    dbus_log_sender_shutdown(NULL, FALSE);
    dbus_log_sender_add_closed_handler(NULL, NULL, NULL);
    dbus_log_sender_add_closed_handler(test.sender, NULL, NULL);
    dbus_log_sender_remove_handler(NULL, 0);
    dbus_log_sender_remove_handler(test.sender, 0);
    dbus_log_receiver_ref(NULL);
    dbus_log_receiver_unref(NULL);
    dbus_log_receiver_pause(NULL);
    dbus_log_receiver_resume(NULL);
    dbus_log_receiver_close(NULL);
    dbus_log_receiver_add_message_handler(NULL, NULL, NULL);
    dbus_log_receiver_add_message_handler(receiver, NULL, NULL);
    dbus_log_receiver_add_message_handler(NULL, test_basic_message_received,
        NULL);
    dbus_log_receiver_add_skip_handler(NULL, NULL, NULL);
    dbus_log_receiver_add_skip_handler(receiver, NULL, NULL);
    dbus_log_receiver_add_closed_handler(NULL, NULL, NULL);
    dbus_log_receiver_add_closed_handler(receiver, NULL, NULL);
    dbus_log_receiver_add_closed_handler(NULL, test_basic_receiver_closed,
        NULL);
    dbus_log_receiver_remove_handler(receiver, 0);
    dbus_log_receiver_remove_handler(NULL, 0);
    dbus_log_receiver_remove_handler(NULL, 1);

    dbus_log_core_set_default_level(core, DBUSLOG_LEVEL_VERBOSE);
    dbus_log_core_set_default_level(core, DBUSLOG_LEVEL_VERBOSE);
    GASSERT(test.default_level_changed == 1);

    g_main_loop_run(loop);

    if (dbus_log_core_default_level(NULL) != DBUSLOG_LEVEL_UNDEFINED ||
        dbus_log_core_set_default_level(NULL, DBUSLOG_LEVEL_ALWAYS) ||
        dbus_log_core_set_default_level(core, DBUSLOG_LEVEL_UNDEFINED) ||
        dbus_log_core_set_default_level(core, DBUSLOG_LEVEL_COUNT) ||
        dbus_log_core_default_level(core) != DBUSLOG_LEVEL_VERBOSE ||
        test.default_level_changed != 1) {
        test.ret = RET_ERR;
    }

    dbus_log_sender_send(test.sender, dummy_msg); /* Will fail */
    dbus_log_receiver_remove_handler(receiver, message_id);
    dbus_log_receiver_remove_handler(receiver, closed_id);
    dbus_log_receiver_unref(receiver);
    dbus_log_sender_unref(test.sender);
    dbus_log_core_remove_handler(core, default_level_id);
    dbus_log_core_remove_handler(core, backlog_id);
    dbus_log_core_unref(core);
    dbus_log_message_unref(dummy_msg);

    return test.ret;
}

/*==========================================================================*
 * Categories
 *==========================================================================*/

typedef struct _test_cat {
    GMainLoop* loop;
    DBusLogSender* sender;
    DBusLogReceiver* receiver;
    DBusLogCategory* enabled;
    DBusLogCategory* verbose;
    int msg_count;
    int add_count;
    int remove_count;
    int flags_count;
    int ret;
} TestCat;

static
void
test_cat_added(
    DBusLogCore* core,
    DBusLogCategory* category,
    gpointer user_data)
{
    TestCat* test = user_data;
    GDEBUG("%s added", category->name);
    test->add_count++;
}

static
void
test_cat_removed(
    DBusLogCore* core,
    DBusLogCategory* category,
    gpointer user_data)
{
    TestCat* test = user_data;
    GDEBUG("%s removed", category->name);
    test->remove_count++;
}

static
void
test_cat_flags(
    DBusLogCore* core,
    DBusLogCategory* category,
    guint32 mask,
    gpointer user_data)
{
    TestCat* test = user_data;
    GDEBUG("%s flags 0x%04x mask 0x%04x", category->name,
        (guint)category->flags, mask);
    if (mask == DBUSLOG_CATEGORY_FLAG_ENABLED) {
        test->flags_count++;
    }
}

static
void
test_cat_message_received(
    DBusLogReceiver* receiver,
    DBusLogMessage* msg,
    gpointer user_data)
{
    TestCat* test = user_data;
    GDEBUG("%s", msg->string);
    if (msg->category == test->enabled->id ||
        msg->category == test->verbose->id ) {
        test->ret = RET_OK;
        test->msg_count++;
    } else {
        if (!msg->category) {
            test->ret = RET_OK;
            test->msg_count++;
         } else {
            GERR("Unexpected category %u", msg->category);
            test->ret = RET_ERR;
        }
        dbus_log_receiver_close(test->receiver);
        dbus_log_sender_close(test->sender, TRUE);
   }
}

static
void
test_cat_receiver_closed(
    DBusLogReceiver* receiver,
    gpointer user_data)
{
    TestCat* test = user_data;
    GDEBUG("Closed");
    g_main_loop_quit(test->loop);
}

static
int
test_cat(GMainLoop* loop)
{
    TestCat test;
    DBusLogCore* core;
    DBusLogCategory* disabled;
    gulong id[2], cid[3];

    memset(&test, 0, sizeof(test));
    test.ret = RET_ERR;
    test.loop = loop;
    core = dbus_log_core_new(0);

    test_sendv(core, DBUSLOG_LEVEL_INFO, "Ignore", "Test message (ignore)");
    test_send(core, DBUSLOG_LEVEL_INFO, "Ignore", "Test message (ignore)");

    cid[0] = dbus_log_core_add_category_added_handler(core,
        test_cat_added, &test);
    cid[1] = dbus_log_core_add_category_removed_handler(core,
        test_cat_removed, &test);
    cid[2] = dbus_log_core_add_category_flags_handler(core,
        test_cat_flags, &test);

    test.sender = dbus_log_core_new_sender(core, "Test");
    test.receiver = dbus_log_receiver_new(dup(test.sender->readfd), TRUE);
    test.enabled = dbus_log_core_new_category(core, "Enabled",
        DBUSLOG_LEVEL_UNDEFINED, 0);
    test.verbose = dbus_log_core_new_category(core, "Verbose",
        DBUSLOG_LEVEL_VERBOSE, DBUSLOG_CATEGORY_FLAG_ENABLED);
    disabled = dbus_log_core_new_category(core, "Disabled",
        DBUSLOG_LEVEL_UNDEFINED, DBUSLOG_CATEGORY_FLAG_ENABLED);

    GASSERT(test.verbose->level == DBUSLOG_LEVEL_VERBOSE);

    /* Actually enable/disable them */
    dbus_log_core_set_category_enabled(core, "Enabled", TRUE);
    dbus_log_core_set_category_enabled(core, "Enabled", TRUE);
    dbus_log_core_set_category_enabled(core, "Disabled", FALSE);
    dbus_log_core_set_category_enabled(core, "Disabled", FALSE);
    dbus_log_core_set_category_enabled(core, "Non-existent", TRUE);

    id[0] = dbus_log_receiver_add_message_handler(test.receiver,
        test_cat_message_received, &test);
    id[1] = dbus_log_receiver_add_closed_handler(test.receiver,
        test_cat_receiver_closed, &test);

    test_send(core, DBUSLOG_LEVEL_INFO, "Disabled", "Test message (disabled)");
    test_send(core, DBUSLOG_LEVEL_INFO, "Enabled", "Test message (enabled)");
    test_send(core, DBUSLOG_LEVEL_VERBOSE, "Verbose", "Test message (verbose)");
    test_sendv(core, DBUSLOG_LEVEL_INFO, "Disabled", "Test message (disabled)");
    test_sendv(core, DBUSLOG_LEVEL_INFO, "Non-existent", "Test message (stop)");
    test_send(core, DBUSLOG_LEVEL_INFO, "Non-existent", "Missed message");
    test_send(core, DBUSLOG_LEVEL_VERBOSE, "Enabled", "Dropped message");
    test_send(NULL, DBUSLOG_LEVEL_INFO, NULL, "Missed message");
    test_send(NULL, DBUSLOG_LEVEL_INFO, NULL, "Missed message");

    g_main_loop_run(loop);

    if (test.msg_count != 3) {
        GERR("Unexpected message count %d", test.msg_count);
        test.ret = RET_ERR;
    }

    if (test.ret == RET_OK) {
        DBusLogCategory* cat = dbus_log_core_new_category(core, "Enabled",
            DBUSLOG_LEVEL_UNDEFINED, DBUSLOG_CATEGORY_FLAG_ENABLED);
        if (cat != test.enabled) {
            GERR("Second category with the same name?");
            test.ret = RET_ERR;
        }
        dbus_log_category_unref(cat);
        if (!test.enabled->id || !disabled->id ||
            test.enabled->id == disabled->id) {
            GERR("Unexpected category ids");
            test.ret = RET_ERR;
        }
        if (dbus_log_core_find_category(core, "Enabled") != test.enabled) {
            GERR("Category not found");
            test.ret = RET_ERR;
        }
        if (dbus_log_core_get_categories(core)->len != 3) {
            GERR("Unexpected number of categories");
            test.ret = RET_ERR;
        }
        if (dbus_log_core_find_categories(core, "*")->len != 3) {
            GERR("Unexpected number of categories found");
            test.ret = RET_ERR;
        }
        if (dbus_log_core_find_categories(core, "*abled")->len != 2) {
            GERR("Failed to find *abled");
            test.ret = RET_ERR;
        }
        if (dbus_log_core_find_categories(core, "Disabled")->len != 1) {
            GERR("Failed to find Disabled");
            test.ret = RET_ERR;
        }
        if (!dbus_log_core_remove_category(core, "Enabled")) {
            GERR("Failed to find Enabled");
            test.ret = RET_ERR;
        }
        if (dbus_log_core_remove_category(core, "Non-existent")) {
            GERR("Removed non-existent category?");
            test.ret = RET_ERR;
        }
    }

    dbus_log_category_unref(test.enabled);
    dbus_log_category_unref(test.verbose);
    dbus_log_category_unref(disabled);

    dbus_log_receiver_remove_handlers(test.receiver, id, G_N_ELEMENTS(id));
    dbus_log_receiver_unref(test.receiver);
    dbus_log_sender_unref(test.sender);

    if (test.add_count != 3 ||
        test.remove_count != 1 ||
        test.flags_count != 2) {
        GERR("Unexpected signal counts %d/%d/%d", test.add_count,
             test.remove_count, test.flags_count);
        test.ret = RET_ERR;
    }

    dbus_log_core_remove_all_categories(core);
    dbus_log_core_remove_all_categories(core);

    if (test.remove_count != 3) {
        GERR("Unexpected remove signal count %d", test.remove_count);
        test.ret = RET_ERR;
    }

    dbus_log_core_remove_handler(core, cid[0]);
    dbus_log_core_remove_handlers(core, cid + 1, G_N_ELEMENTS(cid) - 1);

    dbus_log_category_unref(dbus_log_core_new_category(core, "Dummy",
        DBUSLOG_LEVEL_UNDEFINED, 0));
    dbus_log_core_remove_all_categories(core);
    if (test.add_count != 3 || test.remove_count != 3) {
        GERR("Unexpected signal counts %d/%d",
             test.add_count, test.remove_count);
        test.ret = RET_ERR;
    }

    dbus_log_core_ref(core);
    dbus_log_core_unref(core);
    dbus_log_core_unref(core);

    /* Test NULL resistance */
    dbus_log_category_ref(NULL);
    dbus_log_category_unref(NULL);
    if (dbus_log_category_values(NULL)) {
        test.ret = RET_ERR;
    }

    return test.ret;
}

/*==========================================================================*
 * Queue
 *==========================================================================*/

typedef struct _test_queue {
    GMainLoop* loop;
    int sent;
    int received;
    int skipped;
    int ret;
} TestQueue;

static
void
test_queue_message_received(
    DBusLogReceiver* receiver,
    DBusLogMessage* msg,
    gpointer user_data)
{
    TestQueue* test = user_data;
    GDEBUG("%s", msg->string);
    test->received++;
}

static
void
test_queue_message_skipped(
    DBusLogReceiver* receiver,
    guint count,
    gpointer user_data)
{
    TestQueue* test = user_data;
    GDEBUG("%u message(s) skipped", count);
    test->skipped += count;
}

static
void
test_queue_receiver_closed(
    DBusLogReceiver* receiver,
    gpointer user_data)
{
    TestQueue* test = user_data;
    GDEBUG("Closed");
    if (test->skipped > 0 &&
        test->sent == (test->received + test->skipped)) {
        test->ret = RET_OK;
    }
    g_main_loop_quit(test->loop);
}

static
int
test_queue(GMainLoop* loop)
{
    TestQueue test;
    DBusLogCore* core;
    DBusLogSender* sender;
    DBusLogReceiver* receiver;
    gulong id[3];
    guint i;

    memset(&test, 0, sizeof(test));
    test.ret = RET_ERR;
    test.loop = loop;
    core = dbus_log_core_new(5);
    sender = dbus_log_core_new_sender(core, "Test");
    receiver = dbus_log_receiver_new(dup(sender->readfd), TRUE);
    id[0] = dbus_log_receiver_add_message_handler(receiver,
        test_queue_message_received, &test);
    id[1] = dbus_log_receiver_add_skip_handler(receiver,
        test_queue_message_skipped, &test);
    id[2] = dbus_log_receiver_add_closed_handler(receiver,
        test_queue_receiver_closed, &test);

    /* Fill the buffer */
    while (dbus_log_sender_ping(sender)) {
        test_sendv(core, DBUSLOG_LEVEL_INFO, NULL, "%u", test.sent);
        test.sent++;
    }
    for (i=0; i<10; i++) {
        test_sendv(core, DBUSLOG_LEVEL_INFO, "No-existent", "%u", test.sent);
        test.sent++;
    }
    dbus_log_sender_close(sender, TRUE);

    g_main_loop_run(loop);

    dbus_log_receiver_remove_handlers(receiver, id, G_N_ELEMENTS(id));
    dbus_log_receiver_unref(receiver);
    dbus_log_sender_unref(sender);
    dbus_log_core_unref(core);

    return test.ret;
}

/*==========================================================================*
 * Skip
 *==========================================================================*/

typedef struct _test_skip {
    GMainLoop* loop;
    DBusLogSender* sender;
    int received;
    int received_ok;
    int skip_index;
    int skip_count;
    int ret;
} TestSkip;

static const struct _test_skip_msg {
    guint32 index;
    const char* text;
} test_skip_msg [] = {
    {0, "Test message 1"},
    {1, "Test message 2"},
    {3, "Test message 3"}
};

static
void
test_skip_message_received(
    DBusLogReceiver* receiver,
    DBusLogMessage* msg,
    gpointer user_data)
{
    TestSkip* test = user_data;
    GDEBUG("%s", msg->string);
    if (test->received < G_N_ELEMENTS(test_skip_msg)) {
        if (!g_strcmp0(msg->string, test_skip_msg[test->received].text)) {
            if (test_skip_msg[test->received].index == msg->index) {
                test->received_ok++;
            } else {
                GERR("Unexpected index %u", msg->index);
            }
        } else {
            GERR("Expected text \"%s\", got \"%s\"",
                test_skip_msg[test->received].text, msg->string);
        }
    }
    test->received++;
}

static
void
test_skip_message_skipped(
    DBusLogReceiver* receiver,
    guint count,
    gpointer user_data)
{
    TestSkip* test = user_data;
    GDEBUG("%u message(s) skipped", count);
    test->skip_index = test->received;
    test->skip_count += count;
    dbus_log_sender_close(test->sender, TRUE);
}

static
void
test_skip_receiver_closed(
    DBusLogReceiver* receiver,
    gpointer user_data)
{
    TestSkip* test = user_data;
    GDEBUG("Closed");
    if (test->received == test->received_ok &&
        test->received == G_N_ELEMENTS(test_skip_msg) &&
        test->skip_index == 2 &&
        test->skip_count == 1) {
        test->ret = RET_OK;
    }
    g_main_loop_quit(test->loop);
}

static
int
test_skip(GMainLoop* loop)
{
    TestSkip test;
    DBusLogReceiver* receiver;
    gulong id[3];
    guint i;

    memset(&test, 0, sizeof(test));
    test.ret = RET_ERR;
    test.loop = loop;
    test.sender = dbus_log_sender_new("Test", -1);
    receiver = dbus_log_receiver_new(dup(test.sender->readfd), TRUE);
    id[0] = dbus_log_receiver_add_message_handler(receiver,
        test_skip_message_received, &test);
    id[1] = dbus_log_receiver_add_skip_handler(receiver,
        test_skip_message_skipped, &test);
    id[2] = dbus_log_receiver_add_closed_handler(receiver,
        test_skip_receiver_closed, &test);

    for (i=0; i<G_N_ELEMENTS(test_skip_msg); i++) {
        DBusLogMessage* msg = dbus_log_message_new(test_skip_msg[i].text);
        msg->index = test_skip_msg[i].index;
        dbus_log_sender_send(test.sender, msg);
        dbus_log_message_unref(msg);
    }

    g_main_loop_run(loop);

    dbus_log_receiver_remove_handlers(receiver, id, G_N_ELEMENTS(id));
    dbus_log_receiver_unref(receiver);
    dbus_log_sender_unref(test.sender);

    return test.ret;
}

/*==========================================================================*
 * Common
 *==========================================================================*/

static const TestDesc all_tests[] = {
    {
        "Basic",
        test_basic
    },{
        "Categories",
        test_cat
    },{
        "Queue",
        test_queue
    },{
        "Skip",
        test_skip
    }
};

typedef struct test_run_context {
    const TestDesc* desc;
    GMainLoop* loop;
    guint timeout_id;
    gboolean timeout_occured;
} TestRun;

static
gboolean
test_timer(
    gpointer param)
{
    TestRun* run = param;
    GERR("%s TIMEOUT", run->desc->name);
    run->timeout_id = 0;
    run->timeout_occured = TRUE;
    g_main_loop_quit(run->loop);
    return G_SOURCE_REMOVE;
}

static
int
test_run_once(
    const TestDesc* desc,
    gboolean debug)
{
    TestRun run;
    int ret;

    memset(&run, 0, sizeof(run));
    run.loop = g_main_loop_new(NULL, TRUE);
    run.desc = desc;
    if (!debug) {
        run.timeout_id = g_timeout_add_seconds(TEST_TIMEOUT, test_timer, &run);
    }

    ret = desc->run(run.loop);

    if (run.timeout_occured) {
        ret = RET_TIMEOUT;
    }
    if (run.timeout_id) {
        g_source_remove(run.timeout_id);
    }
    g_main_loop_unref(run.loop);
    GINFO("%s: %s", (ret == RET_OK) ? "OK" : "FAILED", desc->name);
    return ret;
}

static
int
test_run(
    const char* name,
    gboolean debug)
{
    int i, ret;
    if (name) {
        const TestDesc* found = NULL;
        for (i=0, ret = RET_ERR; i<G_N_ELEMENTS(all_tests); i++) {
            const TestDesc* test = all_tests + i;
            if (!strcmp(test->name, name)) {
                ret = test_run_once(test, debug);
                found = test;
                break;
            }
        }
        if (!found) GERR("No such test: %s", name);
    } else {
        for (i=0, ret = RET_OK; i<G_N_ELEMENTS(all_tests); i++) {
            int test_status = test_run_once(all_tests + i, debug);
            if (ret == RET_OK && test_status != RET_OK) ret = test_status;
        }
    }
    return ret;
}

int main(int argc, char* argv[])
{
    int ret = RET_ERR;
    gboolean verbose = FALSE;
    gboolean debug = FALSE;
    GError* error = NULL;
    GOptionContext* options;
    GOptionEntry entries[] = {
        { "verbose", 'v', 0, G_OPTION_ARG_NONE, &verbose,
          "Enable verbose output", NULL },
        { "debug", 'd', 0, G_OPTION_ARG_NONE, &debug,
          "Disable timeout for debugging", NULL },
        { NULL }
    };

    options = g_option_context_new("[TEST]");
    g_option_context_add_main_entries(options, entries, NULL);
    if (g_option_context_parse(options, &argc, &argv, &error)) {
        gutil_log_timestamp = FALSE;
        if (verbose) {
            gutil_log_default.level = GLOG_LEVEL_VERBOSE;
        }

        if (argc < 2) {
            ret = test_run(NULL, debug);
        } else {
            int i;
            for (i=1, ret = RET_OK; i<argc; i++) {
                int test_status =  test_run(argv[i], debug);
                if (ret == RET_OK && test_status != RET_OK) ret = test_status;
            }
        }
    } else {
        fprintf(stderr, "%s\n", GERRMSG(error));
        g_error_free(error);
        ret = RET_ERR;
    }
    g_option_context_free(options);
    return ret;
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
