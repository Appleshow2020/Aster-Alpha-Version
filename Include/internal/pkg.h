#ifndef ASTER_PKG_H
#define ASTER_PKG_H

#include "internal/arena.h"
#include "internal/runtime.h"

/*
 * 패키지 관리자.
 * manifest(aster.pkg)는 다음 형식이다.
 *
 *   name = myapp
 *   version = 0.1.0
 *
 *   [deps]
 *   geo = vendor/geo
 *
 * 의존성 경로는 패키지 디렉터리의 부모다. import geo.util 은
 * vendor/geo/geo/util.ast 또는 vendor/geo/geo/__init__.ast 에서 찾는다.
 */

/* 'aster pkg ...' 하위 명령을 실행하고 종료 코드를 돌려준다. */
int Pkg_Main(int argc, char **argv);

/* base_dir에서 위로 올라가며 aster.pkg를 찾아 의존성 경로를 검색 루트에 추가한다. */
void Pkg_LoadDeps(Arena *arena, const char *base_dir, Interp *in);

#endif
