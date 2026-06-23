CC = gcc
CFLAGS = -Wall -g
TARGETS = shell editline tags

all: $(TARGETS)

%: %.c %.h
	$(CC) $(CFLAGS) -DDEBUG_PRINT $< -o $@

.PHONY: tags
tags:
	ctags *.[ch]
