D-Bus based logging
-------------------

This repository contains both server and client code. Each log message can
be (optionally) associated with a log category and log level (priority).
Client side API allows to manipulate (enable/disable) log categories and
receive log messages. Multiple clients are supported.

D-Bus calls are used for configuring log categories and getting the log pipe
handle. The actual log messages are sent over a pipe using a little custom
wire [protocol](PROTOCOL).
