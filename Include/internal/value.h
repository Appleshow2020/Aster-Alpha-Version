#ifndef ASTER_VALUE_H
#define ASTER_VALUE_H
/* Aster의 값. null/bool/int/float은 힙을 쓰지 않고, 나머지는 Object 포인터다. */
#include <stdint.h>

typedef struct Object Object;

typedef enum { VT_NULL = 0, VT_BOOL, VT_INT, VT_FLOAT, VT_OBJ } ValueTag;

typedef struct Value {
    ValueTag tag;
    union { int64_t i; double f; Object *o; } u;
} Value;

static inline Value Value_Null(void)       { Value v; v.tag = VT_NULL;  v.u.i = 0; return v; }
static inline Value Value_Bool(int b)      { Value v; v.tag = VT_BOOL;  v.u.i = b ? 1 : 0; return v; }
static inline Value Value_Int(int64_t i)   { Value v; v.tag = VT_INT;   v.u.i = i; return v; }
static inline Value Value_Float(double f)  { Value v; v.tag = VT_FLOAT; v.u.f = f; return v; }
static inline Value Value_Obj(Object *o)   { Value v; v.tag = VT_OBJ;   v.u.o = o; return v; }

#endif
