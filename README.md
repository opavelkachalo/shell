# Shell in C

This is my implementation of a Unix shell. For now, it is capable of:

* executing commands, both in the foreground and in the background
* redirecting standard streams (via `<`, `>` and `>>` tokens)
* handling pipelines of arbitrary length (stdin redirection always applies
  to the first member of the pipeline, stdout redirection always applies to
  the last member of the pipeline)

Features like stderr redirection, command editing, command history, and
operators like `&&`, `||`, `;` are yet to come.

N.B. The lexer currently only works with double qoutes (`"`) and treats a
single quote (`'`) as a regular character. This is also a subject to change
in the future.

To build the shell, just run `make shell` in the project directory.
