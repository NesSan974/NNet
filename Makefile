
# cd ${0%/*}
.PHONY : reset all server client help

# help :
# 	@echo "Utilisation : make command"
# 	@echo "Construit le projet"
# 	@echo " all"
# 	@printf "\t ca fait tout\n\n"
# 	@echo " client"
# 	@printf "\t client descr\n\n"
# 	@echo " server"
# 	@printf "\t server descr\n\n"

all : server client

server : build/server

client : build/client

clean :
	rm -r build/*

build/server : build/obj/net_server.o build/obj/server.o
	mkdir -p build
	gcc -o $@ -g -O0 build/obj/net_server.o build/obj/server.o -fno-omit-frame-pointer -Iinclude

build/obj/server.o : src/server.c
	gcc -o $@ -g -O0 -c src/server.c -Iinclude -fno-omit-frame-pointer

build/client : build/obj/net_client.o build/obj/client.o
	mkdir -p build
	gcc -o $@ -g -O0 build/obj/client.o build/obj/net_client.o -fno-omit-frame-pointer -Iinclude

build/obj/client.o : src/client.c
	gcc -o $@ -g -O0 -c src/client.c -Iinclude -fno-omit-frame-pointer


build/obj/net_server.o :
	mkdir -p build/obj
	gcc -o $@ -g -O0 -c src/net.c -fno-omit-frame-pointer -Iinclude

build/obj/net_client.o :
	mkdir -p build/obj
	gcc -DBUDGET=16384 -o $@ -g -O0 -c src/net.c -fno-omit-frame-pointer -Iinclude
