/*
 * Copyright (C) 2020 Jolla Ltd.
 * Copyright (C) 2020 Slava Monich <slava.monich@jolla.com>
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

#include "dbuslog_util.h"

#include <gutil_log.h>

/*==========================================================================*
 * log_level_from_gutil
 *==========================================================================*/

static
void
test_log_level_from_gutil(
    void)
{
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_ALWAYS), == ,(DBUSLOG_LEVEL_ALWAYS));
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_ERR), == ,(DBUSLOG_LEVEL_ERROR));
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_WARN), == ,(DBUSLOG_LEVEL_WARNING));
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_INFO), == ,(DBUSLOG_LEVEL_INFO));
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_DEBUG), == ,(DBUSLOG_LEVEL_DEBUG));
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_VERBOSE), == ,(DBUSLOG_LEVEL_VERBOSE));
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_NONE), == ,(DBUSLOG_LEVEL_UNDEFINED));
    g_assert_cmpint(dbus_log_level_from_gutil
        (GLOG_LEVEL_INHERIT), == ,(DBUSLOG_LEVEL_UNDEFINED));
    g_assert_cmpint(dbus_log_level_from_gutil
        (999), == ,(DBUSLOG_LEVEL_UNDEFINED));
}

/*==========================================================================*
 * log_level_to_gutil
 *==========================================================================*/

static
void
test_log_level_to_gutil(
    void)
{
    g_assert_cmpint(dbus_log_level_to_gutil
        (DBUSLOG_LEVEL_ALWAYS), == ,(GLOG_LEVEL_ALWAYS));
    g_assert_cmpint(dbus_log_level_to_gutil
        (DBUSLOG_LEVEL_ERROR), == ,(GLOG_LEVEL_ERR));
    g_assert_cmpint(dbus_log_level_to_gutil
        (DBUSLOG_LEVEL_WARNING), == ,(GLOG_LEVEL_WARN));
    g_assert_cmpint(dbus_log_level_to_gutil
        (DBUSLOG_LEVEL_INFO), == ,(GLOG_LEVEL_INFO));
    g_assert_cmpint(dbus_log_level_to_gutil
        (DBUSLOG_LEVEL_DEBUG), == ,(GLOG_LEVEL_DEBUG));
    g_assert_cmpint(dbus_log_level_to_gutil
        (DBUSLOG_LEVEL_VERBOSE), == ,(GLOG_LEVEL_VERBOSE));
    g_assert_cmpint(dbus_log_level_to_gutil
        (DBUSLOG_LEVEL_UNDEFINED), == ,(GLOG_LEVEL_NONE));
    g_assert_cmpint(dbus_log_level_to_gutil
        (999), == ,(GLOG_LEVEL_NONE));
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_(name) "/util/" name

int main(int argc, char* argv[])
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func(TEST_("log_level_from_gutil"), test_log_level_from_gutil);
    g_test_add_func(TEST_("log_level_to_gutil"), test_log_level_to_gutil);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
