CC = gcc
CFLAGS = -Wall -D_POSIX_C_SOURCE=200112L -g
LDFLAGS = -pthread
OBJ = rpc.o

# Define default target
all: rpc.a

# Define rule for building the static library rpc.a
rpc.a: rpc.o
	ar rcs $@ $^

# Define rule for building the object file rpc.o
rpc.o: rpc.c rpc.h
	$(CC) $(CFLAGS) -c -o $@ $< $(LDFLAGS)

clean:
	rm -f rpc.a $(OBJ)

# Mark 'all' and 'clean' targets as phony, as they don't produce any files
.PHONY: all clean