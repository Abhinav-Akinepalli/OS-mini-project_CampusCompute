CC=gcc
CFLAGS=-Wall -Wextra -g -Iinclude
LDFLAGS=-pthread -lrt
SRC=$(wildcard src/*.c)
HDR=$(wildcard include/*.h)

campus: $(SRC) $(HDR)
	$(CC) $(CFLAGS) $(SRC) -o campus $(LDFLAGS)

clean:
	rm -f campus logs/*.log

.PHONY: clean test
