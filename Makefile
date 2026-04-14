CFLAGS := -Wall

build/main.o : src/main.c
	gcc src/main.c $(CFLAGS) -Iinclude -Llib -o build/main.o
