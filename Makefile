CC = gcc
CFLAGS = -Wall -g

tags: shell.c
	ctags *.c

shell: shell.c
	$(CC) $(CFLAGS) $< -o $@
