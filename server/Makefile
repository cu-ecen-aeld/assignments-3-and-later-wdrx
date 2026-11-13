CC := gcc

all: aesdsocket

default: aesdsocket

aesdsocket: aesdsocket.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o aesdsocket aesdsocket.c

clean:
	rm -f aesdsocket *.o