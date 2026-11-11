#ifndef ASTER_INTERP_H
#define ASTER_INTERP_H

#include "internal/arena.h"
#include "internal/ast.h"
#include "internal/runtime.h"

/* 모듈을 실행한다. 0이면 정상 종료, 1이면 처리되지 않은 예외. */
int Interp_RunModule(Interp *in, mod_ty mod);

#endif
