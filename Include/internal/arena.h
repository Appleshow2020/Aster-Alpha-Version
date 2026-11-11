#ifndef ASTER_ARENA_H
#define ASTER_ARENA_H
#include <stddef.h>

/* AST 노드를 위한 할당기. 노드를 하나씩 해제하지 않고 Arena_Free로 한꺼번에 해제한다 (CPython의 PyArena와 같다). */
typedef struct Arena Arena;

Arena *Arena_New(void);
void *Arena_Alloc(Arena *a, size_t n);                  /* 0으로 채워서 돌려준다 */
char *Arena_Strndup(Arena *a, const char *s, size_t n);
void Arena_Free(Arena *a);

#endif
