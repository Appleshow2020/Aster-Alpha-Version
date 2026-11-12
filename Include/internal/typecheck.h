#ifndef ASTER_TYPECHECK_H
#define ASTER_TYPECHECK_H

#include <stddef.h>
#include "internal/arena.h"
#include "internal/ast.h"

/* import된 모듈 파일을 찾을 때 쓰는 검색 경로. */
typedef struct TypeCheckEnv {
    const char *base_dir;
    const char *stdlib_dir;
    const char **module_dirs;
    size_t nmodule_dirs;
} TypeCheckEnv;

/*
 * 모듈을 정적으로 검사한다.
 * 0이면 문제 없음, 1이면 오류를 표준 오류에 출력했다.
 * env가 NULL이면 import된 모듈을 읽지 않는다.
 */
int TypeCheck_Module(Arena *arena, mod_ty mod, const char *filename);
int TypeCheck_ModuleEx(Arena *arena, mod_ty mod, const char *filename,
                       const TypeCheckEnv *env);

#endif
