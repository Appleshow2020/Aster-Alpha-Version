/* AST용 단순 할당기 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal/arena.h"
#include "internal/asdl.h"

#define BLOCK_SIZE (64 * 1024)

typedef struct Block { struct Block *next; size_t used, cap; char data[]; } Block;
struct Arena { Block *head; };

static Block *block_new(size_t cap, Block *next)
{
    Block *b = malloc(sizeof(Block) + cap);
    if (!b) { fputs("out of memory\n", stderr); abort(); }
    b->next = next; b->used = 0; b->cap = cap;
    return b;
}

Arena *Arena_New(void)
{
    Arena *a = malloc(sizeof(Arena));
    if (!a) abort();
    a->head = block_new(BLOCK_SIZE, NULL);
    return a;
}

void *Arena_Alloc(Arena *a, size_t n)
{
    n = (n + 7) & ~(size_t)7;
    if (n > BLOCK_SIZE / 4) {                        /* 큰 덩어리는 따로 둔다 */
        Block *b = block_new(n, a->head->next);
        a->head->next = b;
        memset(b->data, 0, n);
        b->used = n;
        return b->data;
    }
    if (a->head->used + n > a->head->cap)
        a->head = block_new(BLOCK_SIZE, a->head);
    void *p = a->head->data + a->head->used;
    a->head->used += n;
    memset(p, 0, n);
    return p;
}

char *Arena_Strndup(Arena *a, const char *s, size_t n)
{
    char *p = Arena_Alloc(a, n + 1);
    memcpy(p, s, n);
    p[n] = 0;
    return p;
}

void Arena_Free(Arena *a)
{
    Block *b = a->head;
    while (b) { Block *next = b->next; free(b); b = next; }
    free(a);
}

asdl_seq *_Aster_seq_new(size_t size, Arena *arena)
{
    asdl_seq *s = Arena_Alloc(arena, sizeof(asdl_seq) + size * sizeof(void *));
    s->size = size;
    return s;
}
