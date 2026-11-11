#ifndef ASTER_LEXER_H
#define ASTER_LEXER_H
#include <stddef.h>
#include "internal/token.h"

typedef struct {
    int type;               /* TOK_* */
    const char *start;      /* AsterTokenList.source 안을 가리킨다 */
    int length;             /* 바이트 수 */
    int line, col;          /* 시작 위치. 줄은 1부터, 열은 0부터 (문자 단위) */
    int end_line, end_col;
} AsterToken;

typedef struct {
    AsterToken *items;
    size_t count, cap;
    char *source;           /* CRLF/CR 정규화, BOM 제거, NUL 종료 */
    size_t source_length;
} AsterTokenList;

typedef struct {
    char message[256];
    int line, col;          /* 1부터 / 0부터 (문자 단위) */
    int length;             /* ^^^ 길이 */
} AsterError;

/* 성공 1, 어휘 오류 0. 오류일 때도 out->source는 채워져 있으므로 줄을 보여 줄 수 있다. */
int Aster_Tokenize(const char *source, size_t length, AsterTokenList *out, AsterError *err);
void Aster_TokenList_Clear(AsterTokenList *list);

/* STRING 토큰(따옴표 포함)을 UTF-8 바이트로 푼다. 실패하면 NULL. */
char *Aster_DecodeString(const char *raw, int length, int *out_length, AsterError *err);

#endif
