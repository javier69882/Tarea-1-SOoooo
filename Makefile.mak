CC=gcc
CFLAGS=-Wall -Wextra -O2 -g

all: mishell

mishell: mishell.c
	$(CC) $(CFLAGS) -o mishell mishell.c

clean:
	rm -f mishell

.PHONY: all clean
