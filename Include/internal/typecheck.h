#ifndef ASTER_TYPECHECK_H
#define ASTER_TYPECHECK_H

#include "internal/arena.h"
#include "internal/ast.h"

/*
 * 모듈을 정적으로 검사한다.
 * 0이면 문제 없음, 1이면 오류를 표준 오류에 출력했다.
 */
int TypeCheck_Module(Arena *arena, mod_ty mod, const char *filename);

#endif
