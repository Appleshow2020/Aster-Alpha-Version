#ifndef ASTER_FORMATTER_H
#define ASTER_FORMATTER_H
#include <stdio.h>
#include "internal/ast.h"

/* 모듈을 표준 형식(4칸 들여쓰기)으로 출력한다.
   AST 기반이라 주석과 빈 줄은 보존하지 않는다. */
void Aster_FormatModule(FILE *out, mod_ty mod);

#endif
