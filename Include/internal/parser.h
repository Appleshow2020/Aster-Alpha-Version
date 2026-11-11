#ifndef ASTER_PARSER_H
#define ASTER_PARSER_H
#include <stddef.h>
#include "internal/arena.h"
#include "internal/ast.h"
#include "internal/lexer.h"

/* 소스 전체를 읽어 Module 노드를 만든다. 오류면 NULL이고 err에 위치와 이유가 담긴다. */
mod_ty Aster_ParseFile(const char *source, size_t length, Arena *arena, AsterError *err);

/* 식 하나를 읽는다 (f-string의 중괄호 안 같은 곳에서 쓴다). */
expr_ty Aster_ParseExpression(const char *source, size_t length, Arena *arena, AsterError *err);

#endif
