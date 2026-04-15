CFLAGS := -Wall

.PHONY=all client server

all : client server
client : build/client
server : build/server

build/server : src/server.c src/net.c
	gcc -o build/server src/server.c src/net.c $(CFLAGS) -Iinclude

build/client : src/client.c src/net.c
	gcc -o build/client src/client.c src/net.c $(CFLAGS) -Iinclude
