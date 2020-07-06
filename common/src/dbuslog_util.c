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

DBUSLOG_LEVEL
dbus_log_level_from_gutil(
    int level) /* Since 1.0.19 */
{
    switch (level) {
    case GLOG_LEVEL_ALWAYS:
        return DBUSLOG_LEVEL_ALWAYS;
    case GLOG_LEVEL_ERR:
        return DBUSLOG_LEVEL_ERROR;
    case GLOG_LEVEL_WARN:
        return DBUSLOG_LEVEL_WARNING;
    case GLOG_LEVEL_INFO:
        return DBUSLOG_LEVEL_INFO;
    case GLOG_LEVEL_DEBUG:
        return DBUSLOG_LEVEL_DEBUG;
    case GLOG_LEVEL_VERBOSE:
        return DBUSLOG_LEVEL_VERBOSE;
    default:
        return DBUSLOG_LEVEL_UNDEFINED;
    }
}

int
dbus_log_level_to_gutil(
    DBUSLOG_LEVEL level) /* Since 1.0.19 */
{
    switch (level) {
    case DBUSLOG_LEVEL_ALWAYS:
        return GLOG_LEVEL_ALWAYS;
    case DBUSLOG_LEVEL_ERROR:
        return GLOG_LEVEL_ERR;
    case DBUSLOG_LEVEL_WARNING:
        return GLOG_LEVEL_WARN;
    case DBUSLOG_LEVEL_INFO:
        return GLOG_LEVEL_INFO;
    case DBUSLOG_LEVEL_DEBUG:
        return GLOG_LEVEL_DEBUG;
    case DBUSLOG_LEVEL_VERBOSE:
        return GLOG_LEVEL_VERBOSE;
    default:
        return GLOG_LEVEL_NONE;
    }
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
