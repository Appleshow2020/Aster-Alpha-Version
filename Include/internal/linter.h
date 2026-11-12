#ifndef ASTER_LINTER_H
#define ASTER_LINTER_H
#include <stdio.h>
#include "internal/ast.h"

/* 사용하지 않는 지역 변수와 도달할 수 없는 코드를 찾아 out에 경고를 쓴다.
   반환값은 경고 개수다. */
int Aster_LintModule(FILE *out, mod_ty mod, const char *filename);

#endif
