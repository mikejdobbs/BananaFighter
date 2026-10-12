CC      ?= cc
CFLAGS  ?= -O2 -Wall -Wextra -std=gnu11
SRCS    = main.c util.c state.c combat.c sim.c net.c
OBJS    = $(SRCS:.c=.o)

server: $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

%.o: %.c server.h ai.h
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f server $(OBJS)
