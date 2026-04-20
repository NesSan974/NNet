

.PHONY : all server client clean

all : server client

server : build/server

client : build/client

clean :
	rm -v build/obj/net.o build/server build/client

build/server : build/obj/net.o src/server.c
	gcc -o $@ build/obj/net.o src/server.c  -Iinclude

build/obj/net.o :
	gcc -o $@ -c src/net.c -Iinclude

build/client : build/obj/net.o src/client.c
	gcc -o $@ src/client.c -Iinclude
