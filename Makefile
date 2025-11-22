CC = gcc
CFLAGS = -Wall -g

shell: shell.c
	$(CC) $(CFLAGS) $< -o $@
