VERSION = $(shell git describe --tags --always --dirty)

CC = clang
LIBS = -lavformat -lavcodec -lavutil
CFLAGS = -std=c99 -Wall -Wextra -pedantic -g -DVERSION=\"$(VERSION)\"
LDFLAGS = -g

SRC = transcode.c
OBJ = $(SRC:.c=.o)

all: transcode

.c.o:
	$(CC) $(CFLAGS) -c $<

transcode: $(OBJ)
	$(CC) $(LDFLAGS) $(LIBS) -o $@ $(OBJ)

clean:
	rm -f transcode $(OBJ)

.PHONY: all clean
