#ifndef ASTER_REGEX_H
#define ASTER_REGEX_H

/*
 * Python re 호환 정규식 엔진.
 * 패턴을 바이트코드로 컴파일한 뒤 백트래킹 VM으로 실행한다.
 * 문법은 Python re와 같고, 매치 결과는 RMatch에 담긴다.
 */

#include <stddef.h>
#include "internal/runtime.h"

typedef struct AsterRegex AsterRegex;
typedef struct RMatch RMatch;

/* 컴파일. 실패하면 NULL을 돌려주고 err에 메시지를 쓴다. */
AsterRegex *Regex_Compile(Interp *in, const char *pattern, size_t len, int flags,
                          char *err, size_t errcap);

/* mode: 0=search, 1=match(앞에서), 2=fullmatch */
int Regex_Exec(Interp *in, AsterRegex *re, AsterString *subject, size_t start_cp,
               int mode, RMatch **out);

/* id 레지스트리 */
int Regex_Intern(Interp *in, AsterRegex *re);
AsterRegex *Regex_Get(Interp *in, int id);
int RMatch_Intern(Interp *in, RMatch *m);
RMatch *RMatch_Get(Interp *in, int id);

const char *Regex_PatternText(const AsterRegex *re);
int Regex_Flags(const AsterRegex *re);
int Regex_GroupCount(const AsterRegex *re);
int Regex_GroupIndex(const AsterRegex *re, const char *name, size_t namelen);

/* 매치 정보 */
size_t RMatch_Start(const RMatch *m, int group); /* 바이트 오프셋, 미참여면 SIZE_MAX */
size_t RMatch_End(const RMatch *m, int group);
AsterString *RMatch_Group(Interp *in, const RMatch *m, int group);
size_t Regex_PatternLength(const AsterRegex *re);

/* 매치 레지스트리에서 해제 (Matcher가 더 이상 보관하지 않을 때) */
void Regex_ReleaseMatch(Interp *in, int id);

#define REGEX_MODE_SEARCH 0
#define REGEX_MODE_MATCH 1
#define REGEX_MODE_FULLMATCH 2

/* 정규식 내부 모듈(aster.util.regex.internal)의 함수를 등록한다. */
void Regex_Install(Interp *in);

#endif
