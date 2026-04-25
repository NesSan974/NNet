
# cd ${0%/*}
.PHONY : all server client clean help

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
	rm -v build/obj/net.o build/server build/client

build/server : build/obj/net.o src/server.c
	mkdir -p build
	gcc -o $@ src/server.c  build/obj/net.o -Iinclude

build/obj/net.o :
	mkdir -p build/obj
	gcc -o $@ -c src/net.c -Iinclude

build/client : build/obj/net.o src/client.c
	mkdir -p build
	gcc -o $@ src/client.c -Iinclude
