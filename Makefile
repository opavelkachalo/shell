CC = gcc
CFLAGS = -Wall -g
TARGETS = shell tags
OBJS = shell.o editline.o

all: $(TARGETS)

%: %.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -DDEBUG_PRINT -c $< -o $@

shell: $(OBJS)

.PHONY: tags
tags:
	ctags *.[ch]
