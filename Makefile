CC = g++
CFLAGS = -Wall -Wextra -O2 -g
SRC = server client

all: $(SRC)

strace:
	strace ./server >/dev/null

server: server.cpp
	$(CC) $(CFLAGS) $< -o $@

client: client.cpp
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(SRC)