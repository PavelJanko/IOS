CC=gcc
SRC=proj2.c
BIN=proj2
CFLAGS= -std=gnu99 -Wall -Wextra -Werror -pedantic
LDFLAGS=-lpthread

all: $(BIN)

$(BIN): $(SRC)
	$(CC) $(CFLAGS) $(SRC) -o $(BIN) $(LDFLAGS)
