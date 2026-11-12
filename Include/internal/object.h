#ifndef ASTER_OBJECT_H
#define ASTER_OBJECT_H
#include <stddef.h>
#include <stdint.h>
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
    size_t cp_length;       /* 코드 포인트 수 */
    char data[];
} AsterString;

AsterString *AsterString_FromUTF8(const char *s, size_t n);
AsterString *AsterString_FromCStr(const char *s);

/* UTF-8 코드 포인트 단위 도우미. 잘못된 바이트는 1바이트 문자로 취급한다. */
uint32_t Aster_UTF8_Decode(const char *s, size_t n, size_t *advance);
size_t Aster_UTF8_Encode(uint32_t cp, char out[4]);
size_t AsterString_ByteOffset(const AsterString *s, size_t cp_index);
size_t AsterString_CodePointIndex(const AsterString *s, size_t byte_offset);

uint32_t Aster_ToUpperCP(uint32_t cp);
uint32_t Aster_ToLowerCP(uint32_t cp);
int Aster_IsSpaceCP(uint32_t cp);

/* ast.dump와 같은 표기로 값을 출력한다 (str은 따옴표로 감싼다). */
void Aster_Value_Repr(FILE *out, Value v);

/* 왕복 변환이 되는 가장 짧은 float 표기를 buf에 쓴다. */
void Aster_FormatDouble(char *buf, size_t size, double value);
void Aster_StringRepr(FILE *out, const char *s, size_t n);

#endif
