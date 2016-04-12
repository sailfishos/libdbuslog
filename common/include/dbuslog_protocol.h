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

#ifndef DBUSLOG_PROTOCOL_H
#define DBUSLOG_PROTOCOL_H

/*
 * Header (5 bytes)
 *
 * 0..3   Size of the packet body (not including header)
 * 4      Packet type:
 *        0: Ping (no payload)
 *        1: Message (>= 17 bytes)
 *        2: Bye (no payload and no more data to follow)
 */

#define DBUSLOG_PACKET_HEADER_SIZE      (5)
#define DBUSLOG_PACKET_SIZE_OFFSET      (0)
#define DBUSLOG_PACKET_TYPE_OFFSET      (4)

typedef enum dbus_log_packet_type {
    DBUSLOG_PACKET_TYPE_PING,
    DBUSLOG_PACKET_TYPE_MESSAGE,
    DBUSLOG_PACKET_TYPE_BYE,
    DBUSLOG_PACKET_TYPE_COUNT
} DBUSLOG_PACKET_TYPE;

/*
 * Message payload [type 1]
 *
 * 0..7   Timestamp (microseconds since 1970-01-01 00:00:00 UTC)
 * 8..11  Message index
 * 12..15 Log category id
 * 16     Log level
 * 17...  UTF-8 encoded string (not including NULL terminator)
 */

#define DBUSLOG_MESSAGE_TIMESTAMP_OFFSET    (DBUSLOG_PACKET_HEADER_SIZE + 0)
#define DBUSLOG_MESSAGE_INDEX_OFFSET        (DBUSLOG_PACKET_HEADER_SIZE + 8)
#define DBUSLOG_MESSAGE_CATEGORY_OFFSET     (DBUSLOG_PACKET_HEADER_SIZE + 12)
#define DBUSLOG_MESSAGE_LEVEL_OFFSET        (DBUSLOG_PACKET_HEADER_SIZE + 16)

#define DBUSLOG_MESSAGE_PREFIX_SIZE         (17)
#define DBUSLOG_PACKET_MAX_FIXED_PART (\
    DBUSLOG_PACKET_HEADER_SIZE + \
    DBUSLOG_MESSAGE_PREFIX_SIZE)

typedef enum dbus_log_level {
    DBUSLOG_LEVEL_UNDEFINED,
    DBUSLOG_LEVEL_ALWAYS,
    DBUSLOG_LEVEL_CRITICAL,
    DBUSLOG_LEVEL_ERROR,
    DBUSLOG_LEVEL_WARNING,
    DBUSLOG_LEVEL_NOTICE,
    DBUSLOG_LEVEL_INFO,
    DBUSLOG_LEVEL_DEBUG,
    DBUSLOG_LEVEL_VERBOSE,
    DBUSLOG_LEVEL_COUNT
} DBUSLOG_LEVEL;

/*
 * Category flags.
 *
 * DBUSLOG_CATEGORY_FLAG_ENABLED_BY_DEFAULT is immutable and can be used
 * by the client to implement "reset to default" feature.
 */
#define DBUSLOG_CATEGORY_FLAG_ENABLED               (0x01)
#define DBUSLOG_CATEGORY_FLAG_ENABLED_BY_DEFAULT    (0x02)
#define DBUSLOG_CATEGORY_FLAG_HIDE_NAME             (0x04)
#define DBUSLOG_CATEGORY_FLAG_MASK                  (0x07)

#endif /* DBUSLOG_PROTOCOL_H */

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
