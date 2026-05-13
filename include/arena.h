#ifndef __ARENA_H__
#define __ARENA_H__

#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>

typedef struct {
    uint8_t *buffer;
    size_t capacity;
    size_t offset;
} Arena;

// créer l'arena
void arena_create(Arena *arena, size_t size) {
    arena->buffer = malloc(size);
    arena->capacity = (size);
    arena->offset = 0;
}

// allouer dans l'arena
void *arena_alloc(Arena *arena, size_t size) {
    if (arena->offset + size > arena->capacity) {
        return NULL; // plus de place
    }

    void *ptr = arena->buffer + arena->offset;
    arena->offset += size;

    return ptr;
}

void arena_reset(Arena* arena){
    arena->offset = 0;
}

// destruction
void arena_destroy(Arena *arena) {
    free(arena->buffer);
    arena->buffer = NULL;
    arena->capacity = 0;
    arena->offset = 0;
}

#endif //__ARENA_H__
