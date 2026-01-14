CC = gcc
CFLAGS = -Wall -g

shell: shell.c
	ctags *.c
	$(CC) $(CFLAGS) $< -o $@
