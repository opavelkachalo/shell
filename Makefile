CC = gcc
CFLAGS = -Wall -g
TARGETS = shell tags
OBJS = shell.o editline.o containers.o

all: $(TARGETS)

%: %.o
	$(CC) $(CFLAGS) $^ -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

shell: $(OBJS)

.PHONY: tags clean
tags:
	ctags *.[ch]

clean:
	rm -f $(TARGETS) $(OBJS)
