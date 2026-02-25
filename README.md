# Shell in C

This is my attempt at implementing a Unix-like shell. For now, it is
capable of:

* executing commands, both in the foreground and in the background
* redirecting standard streams (via `<`, `>` and `>>` tokens)

Features like pipes, command editing, and history are yet to come.

To build the shell, just run `make shell`.
