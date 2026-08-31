CC := gcc

all: writer

writer: writer.c
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -o writer writer.c

clean:
	rm -f writer *.o