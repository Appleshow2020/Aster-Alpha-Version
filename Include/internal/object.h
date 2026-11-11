#ifndef ASTER_OBJECT_H
#define ASTER_OBJECT_H
#include <stddef.h>
#include <stdio.h>
#include "internal/value.h"

/* 지금은 문자열만 힙 객체다. int/float/bool/null은 Value 안에 직접 들어간다. */
typedef enum { ASTER_OBJ_STRING = 1 } AsterObjKind;

struct Object {
    AsterObjKind kind;
};

typedef struct AsterString {
    struct Object ob;
    size_t length;          /* 바이트 수 (NUL 제외) */
    char data[];
} AsterString;

AsterString *AsterString_FromUTF8(const char *s, size_t n);
AsterString *AsterString_FromCStr(const char *s);

/* ast.dump와 같은 표기로 값을 출력한다 (str은 따옴표로 감싼다). */
void Aster_Value_Repr(FILE *out, Value v);

/* 왕복 변환이 되는 가장 짧은 float 표기를 buf에 쓴다. */
void Aster_FormatDouble(char *buf, size_t size, double value);
void Aster_StringRepr(FILE *out, const char *s, size_t n);

#endif
