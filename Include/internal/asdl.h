#ifndef ASTER_ASDL_H
#define ASTER_ASDL_H
#include <stddef.h>
#include "internal/arena.h"

/* ASDL의 'T*' (시퀀스). 원소는 void*이고 쓰는 쪽에서 타입을 안다. */
typedef struct { size_t size; void *elements[]; } asdl_seq;

asdl_seq *_Aster_seq_new(size_t size, Arena *arena);
#define asdl_seq_LEN(s)    ((s) ? (s)->size : (size_t)0)
#define asdl_seq_GET(s, i) ((s)->elements[(i)])
#define asdl_seq_SET(s, i, v) ((s)->elements[(i)] = (v))

typedef const char *identifier;     /* 이름은 intern된 문자열이라 포인터 비교가 가능하다 */

#endif
