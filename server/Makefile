CC := gcc
TARGET := aesdsocket
OBJS := main.o server.o packet_reader.o file_helper.o

all: $(TARGET)

default: $(TARGET)

$(TARGET): $(OBJS)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) $(LDFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c $(wildcard *.h)
	$(CROSS_COMPILE)$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) *.o