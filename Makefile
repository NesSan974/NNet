CFLAGS := -Wall

.PHONY=all

all : build/server.o build/client.o

build/server.o : src/server.c
	gcc src/server.c $(CFLAGS) -Iinclude -Llib -o build/server.o

build/client.o : src/client.c
	gcc src/client.c $(CFLAGS) -Iinclude -Llib -o build/client.o
