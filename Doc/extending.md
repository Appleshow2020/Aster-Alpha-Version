# Aster 확장 안내

이 문서는 Aster에 **새 키워드**, **새 전역 내장 함수**, **새 표준 라이브러리 모듈**,
**새 C 내장 모듈**, **새 토큰/연산자**를 추가하는 방법을 실제 파일과 함수 이름으로
설명합니다. 설치와 빌드는 [README](../README.md)를 봅니다.

## 0. 전체 구조

Aster는 CPython과 비슷한 구조의 C11 트리 워킹 인터프리터입니다.

```text
소스 텍스트
   │  Parser/lexer/lexer.c          (토큰화)
   ▼
토큰 목록
   │  Parser/parser.c               (재귀 하강 파싱)
   ▼
AST  ── Parser/Aster.asdl           (노드 정의, make regen으로 코드 생성)
   │
   ├─ Python/typecheck.c            (정적 타입 검사)
   ├─ Python/interp.c               (실행)
   ├─ Python/formatter.c            (--format)
   └─ Python/linter.c               (--lint)
```

런타임 값과 내장 함수는 `Python/runtime.c`, 값 표현은
`Include/internal/value.h`와 `Objects/object.c`에 있습니다.

### 파일 지도

| 파일 | 역할 |
| --- | --- |
| `Grammar/Tokens` | 토큰(연산자) 목록. `make regen`의 입력 |
| `Grammar/aster.gram` | 사람이 읽는 문법 문서 |
| `Parser/Aster.asdl` | AST 노드 정의. `make regen`의 입력 |
| `Parser/parser.c` | 파서, 하드 키워드 목록, 문장/식 파싱 |
| `Parser/lexer/lexer.c` | 손으로 쓴 어휘 분석기. 키워드 표는 없음 |
| `Include/internal/token.h`, `Parser/token.c` | `make regen`이 생성. 직접 수정 금지 |
| `Include/internal/ast.h`, `Python/Aster-ast.c` | `make regen`이 생성. 직접 수정 금지 |
| `Python/typecheck.c` | 타입 검사기, 내장 함수 타입 등록 |
| `Python/interp.c` | 문장 실행, 디버거, 환경 재사용 판단 |
| `Python/runtime.c` | 런타임 내장 함수, 내장 모듈 등록 |
| `Python/formatter.c` | `--format` 출력 |
| `Python/linter.c` | `--lint` 경고 |
| `Lib/aster/**` | Aster로 작성한 표준 라이브러리 |
| `Lib/test/**` | 고정 테스트와 문법 테스트 |
| `Tools/random_suite.py` | 무작위 검증 프로그램 생성기 |
| `Tools/check_*.py` | 검증 실행기 |

## 1. 새 키워드 추가

키워드는 렉서에 하드코딩되어 있지 않습니다. 렉서는 모든 식별자를 `NAME` 토큰으로
만들고, `Parser/parser.c`가 토큰의 **문자열**을 비교해 키워드로 취급합니다.
따라서 키워드 추가는 대부분 파서와 AST 작업입니다.

### 1.1 하드 키워드와 소프트 키워드

- **하드 키워드**: 어디서든 식별자로 쓸 수 없습니다. `Parser/parser.c`의
  `HARD_KEYWORDS` 배열에 넣습니다. `if`, `for`, `class`처럼 문법의 뼈대를 이루는
  말에 씁니다.
- **소프트 키워드**: 특정 위치에서만 키워드입니다. 배열에 넣지 않고, 해당 위치를
  파싱하는 함수에서만 문자열을 확인합니다. `assert`, `enum`, `property`처럼
  기존 코드의 변수 이름과 충돌할 수 있는 말에 씁니다.

이 절에서는 `repeat (n) { ... }` 문장을 **소프트 키워드**로 추가한다고 가정합니다.
`repeat`은 변수 이름으로도 쓸 수 있어야 하기 때문입니다.

### 1.2 문법 문서 갱신

`Grammar/aster.gram`의 문장 규칙에 새 문장을 추가합니다.

```text
repeat_stmt: 'repeat' '(' expression ')' '{' statement* '}'
```

`Doc/language.md` 2절의 하드/소프트 키워드 목록과 해당 문법 절도 함께 고칩니다.
문서와 실제 파서가 어긋나면 다음 사람이 잘못된 코드를 쓰게 됩니다.

### 1.3 AST 노드 추가

`Parser/Aster.asdl`의 `stmt`에 노드를 추가합니다.

```text
| Repeat(expr count, stmt* body)
```

그다음 생성 코드를 다시 만듭니다.

```sh
make regen
```

`make regen`은 다음 두 쌍을 다시 생성합니다.

- `Grammar/Tokens` → `Include/internal/token.h`, `Parser/token.c`
- `Parser/Aster.asdl` → `Include/internal/ast.h`, `Python/Aster-ast.c`

이 파일들은 생성물이므로 직접 고치지 않습니다. `make regen`을 실행하면
`_Aster_Repeat(...)` 생성자와 `Repeat_kind`, `s->v.Repeat.count` 같은 필드가
자동으로 생깁니다.

### 1.4 파서에 문장 추가

`Parser/parser.c`에 파싱 함수를 추가합니다. 기존 `p_if_stmt`와 같은 패턴을
따릅니다. 중요한 점은 **실패하면 시작 위치로 되돌리는 것**입니다. 소프트
키워드는 일반 식과 겹칠 수 있으므로, 뒤쪽 모양이 맞지 않으면 파싱을 포기하고
호출자가 다른 해석을 시도하게 해야 합니다.

```c
static stmt_ty p_repeat_stmt(Parser *p) {
    size_t start = p->pos;
    expr_ty count;
    asdl_seq *body;
    if (!(accept(p, "repeat") && accept(p, "("))) {
        p->pos = start;
        return NULL;
    }
    count = p_expression(p);
    if (!count || !accept(p, ")")) {
        p->pos = start;
        return NULL;
    }
    body = p_block(p);
    if (!body) {
        p->pos = start;
        return NULL;
    }
    return _Aster_Repeat(count, body, ATTRS(p, start), p->arena);
}
```

`ATTRS(p, start)`는 `lineno`, `col_offset`, `end_lineno`, `end_col_offset`를
한 번에 채우는 매크로입니다. 모든 노드에 위치 정보를 넣어야 오류 메시지와
디버거가 제대로 동작합니다.

문장 파싱 진입점은 두 곳입니다.

- `p_compound_stmt`: 블록으로 끝나는 문장(`if`, `while`, `class` 등)
- `p_simple_stmt`: `;`로 끝나는 단순 문장

`repeat`은 블록을 가지므로 `p_compound_stmt`에서 다른 블록 문장보다 먼저
시도합니다. 실패하면 `NULL`을 돌려주므로 다음 파서가 이어서 시도합니다.

```c
static stmt_ty p_compound_stmt(Parser *p) {
    stmt_ty s = p_export_decl(p);
    if (s)
        return s;
    s = p_extern_def(p);
    if (s)
        return s;
    /* ... 기존 항목 ... */
    s = p_repeat_stmt(p);
    if (s)
        return s;
    s = p_if_stmt(p);
    if (s)
        return s;
    /* ... */
}
```

하드 키워드로 만들려면 `HARD_KEYWORDS` 배열에 이름을 넣습니다. 그러면
`p_name`이 그 이름을 식별자로 거부하고, `repeat`을 변수 이름으로 쓸 수 없게
됩니다. 소프트 키워드라면 배열에 넣지 않습니다.

선언을 새로 추가하는 키워드(예: 새 종류의 타입 선언)라면 `prescan` 단계도
확인해야 합니다. `prescan`은 본문을 검사하기 전에 클래스/구조체/타입 별칭
이름을 모으므로, 전방 참조를 지원하려면 새 선언도 여기서 등록해야 합니다.

### 1.5 타입 검사기 연결

`Python/typecheck.c`의 `check_stmt`에 `case`를 추가합니다. 문장이 블록을
가지면 스코프를 열고 닫고, 반복문이면 `loop_depth`를 올려 `break`/`continue`가
허용되게 합니다.

```c
case Repeat_kind: {
    Ty *count = infer(c, s->v.Repeat.count);
    require_assignable(c, ty_int(c), count,
                       s->v.Repeat.count, "repeat count");
    push_scope(c);
    c->loop_depth++;
    check_body(c, s->v.Repeat.body);
    c->loop_depth--;
    pop_scope(c);
    break;
}
```

`require_assignable`의 마지막 문자열은 오류 메시지의 위치 설명입니다
(`"variable initializer"`, `"return value"`처럼). 사용자에게 보이는 영어
문장이므로 짧고 명확하게 씁니다.

문장이 항상 함수를 빠져나가는지(`return`/`throw`만 있는지)에 따라 뒤 코드의
도달 가능성이 달라지는 문장이라면 `always_exits`에도 `case`를 추가합니다.
그렇지 않으면 “함수가 값을 반환하지 않을 수 있다”는 오탐이나 미탐이 생깁니다.

### 1.6 인터프리터 연결

`Python/interp.c`의 `exec_stmt`에 실행 분기를 추가합니다.

```c
case Repeat_kind:
    return exec_repeat(in, s, env);
```

실행 함수는 `Signal`을 돌려줘야 합니다. 블록 실행은 `exec_block`을 쓰고,
루프라면 `SIG_BREAK`/`SIG_CONTINUE`를 처리합니다. 환경을 새로 만들 때는
`Env_New`/`Env_AcquireBlock`을 쓰며, 블록 환경 재사용 규칙을 지켜야 합니다.

같은 파일에서 문장 종류를 다루는 곳이 더 있습니다.

- `debug_stmt_name`: `--debug`가 출력하는 짧은 이름
- `stmt_escapes_env`: 블록 환경을 재사용해도 안전한지 판단. 새 문장이 클로저,
  람다, `&지역변수`, 제너레이터, 중첩 선언을 만들 수 있으면 `1`을 반환해
  재사용을 막습니다. 확신이 없으면 보수적으로 `1`을 반환합니다.

`stmt_escapes_env`를 빠뜨리면 환경 재사용 때문에 클로저가 잘못된 값을 볼 수
있습니다. 새 문장이 값을 밖으로 흘릴 가능성이 있으면 반드시 추가합니다.

### 1.7 포매터와 린터 연결

`--format`은 AST를 다시 소스로 출력하므로 `Python/formatter.c`의 `fmt_stmt`에
`case`를 추가해야 합니다. 출력이 다시 파싱되는지(`--format`의 고정점)도
확인합니다.

`--lint`는 `Python/linter.c`의 `walk_stmt`에서 하위 식과 블록을 순회합니다.
새 문장 안의 변수 사용을 빠뜨리면 미사용 변수 경고가 틀리게 나옵니다.

### 1.8 문서와 테스트

키워드를 추가하면 다음을 함께 합니다.

1. `Doc/language.md`: 키워드 목록과 문법 설명
2. `Grammar/aster.gram`: 문법 규칙
3. `Lib/test/test_grammar/valid/`: 새 문장이 파싱되는 예
4. `Lib/test/test_grammar/invalid/`: 잘못된 형태가 거부되는 예
5. `Lib/test/test_run/*.ast`와 같은 이름의 `.out`: 실행 결과
6. `Lib/test/test_format/*.ast`와 `.fmt`: 포매터 출력이 필요하면 추가
7. `Tools/random_suite.py`: 새 문법을 무작위로 생성하려면 카테고리 추가
8. `Tools/random_suite.py`의 부정 케이스: 새 규칙 위반이 거부되는지 확인

### 1.9 체크리스트

- [ ] `Grammar/aster.gram`과 `Doc/language.md` 갱신
- [ ] `Parser/Aster.asdl`에 노드 추가 후 `make regen`
- [ ] 하드 키워드면 `HARD_KEYWORDS`, 소프트면 파서 함수에서만 처리
- [ ] `p_*` 함수가 실패 시 `p->pos = start`로 되돌림
- [ ] `ATTRS(p, start)`로 위치 정보 채움
- [ ] `check_stmt`에 타입 검사 추가
- [ ] `exec_stmt`에 실행 추가
- [ ] `debug_stmt_name`, `stmt_escapes_env` 갱신
- [ ] `fmt_stmt`, `walk_stmt` 갱신
- [ ] 테스트와 문서 추가 후 `make check`

## 2. 새 전역 내장 함수 추가

전역 내장 함수는 `import` 없이 모든 모듈에서 보입니다. `len`, `range`,
`sorted`가 여기에 해당합니다.

### 2.1 런타임 구현

`Python/runtime.c`의 기존 `bi_len`, `bi_chr` 근처에 함수를 추가합니다.
시그니처는 항상 다음과 같습니다.

```c
static RValue *bi_twice(Interp *in, RValue *self, RValue **args, size_t nargs) {
    int ok;
    int64_t value;
    (void)self;
    if (nargs != 1)
        return RT_ThrowValue(in, "TypeError", "twice takes exactly 1 argument");
    value = RValue_AsInt(in, args[0], &ok);
    if (!ok)
        return NULL;
    return RValue_Int(in, value * 2);
}
```

규칙:

- 인자 수가 틀리면 `RT_ThrowValue(in, "TypeError", ...)`를 반환합니다.
- 타입 변환 실패는 `RValue_AsInt` 같은 헬퍼가 이미 예외를 던지고 `NULL`을
  돌려줍니다. 그대로 `return NULL` 합니다.
- 값을 만들 때 `RValue_Int`, `RValue_Str`, `RValue_List` 등 생성자를 씁니다.
- `self`는 메서드가 아닌 전역 함수에서는 쓰지 않습니다.

### 2.2 전역 환경에 등록

`Interp_Init`의 기존 `Env_Define` 줄 옆에 추가합니다.

```c
Env_Define(in, in->globals, "twice",
           RValue_Builtin(in, Builtin_New(in, "twice", bi_twice)));
```

### 2.3 타입 검사기에 서명 등록

`Python/typecheck.c`의 `init_builtins`에 서명을 추가합니다. 인자 타입을
정확히 알린다면 `method_params`로 타입 배열을 만들고 `varargs`를 `0`으로
둡니다. 인자를 자유롭게 받는 함수라면 `ty_func(c, NULL, 0, 반환타입, 1)`을
씁니다.

```c
{
    Ty **params = method_params(c, 1);
    params[0] = ty_int(c);
    define_builtin(c, "twice", ty_func(c, params, 1, ty_int(c), 0));
}
```

이 등록을 빠뜨리면 실행은 되지만 타입 검사기에서 이름을 찾지 못해
`undefined name` 오류가 납니다.

### 2.4 문서와 테스트

- `Doc/stdlib.md` 1절의 전역 내장 함수 표에 한 줄 추가
- `Lib/test/test_run/builtins_global.ast`와 `.out`에 실행 예 추가
- 잘못된 인자 수/타입은 `Lib/test/test_grammar/invalid/`에 추가
- `make`로 다시 빌드하고 `make check`

## 3. 새 표준 라이브러리 모듈 추가

C를 고치지 않고 Aster로만 작성하는 방법입니다. 대부분의 라이브러리는 이
방식이 좋습니다. 별도 빌드가 필요 없고, 타입 검사기가 모듈 소스를 직접 읽어
정확한 타입을 제공합니다.

### 3.1 위치와 이름

모듈은 `Lib/aster/` 아래에 두고, import 경로는 디렉터리 경로와 같습니다.

```text
Lib/aster/text/Slug.ast    →  import aster.text.Slug;
```

패키지는 디렉터리와 `__init__.ast`로 만들 수 있습니다.

```text
Lib/aster/text/__init__.ast  →  import aster.text;
```

### 3.2 공개 선언

모듈 밖에서 쓸 이름에는 `export`를 붙입니다.

```aster
# Lib/aster/text/Slug.ast
export fn slug(str text) -> str {
    return text.lower().replace(" ", "-");
}

export class Counter {
    private int value;

    public fn add(int amount) -> int {
        self->value = self->value + amount;
        return self->value;
    }

    public fn get() -> int {
        return self->value;
    }
}
```

`export`가 없는 선언은 모듈 내부에서만 보입니다. `import aster.text.Slug;`로
가져오면 `Slug.slug(...)`, `new Slug.Counter()`처럼 모듈 이름을 붙여 씁니다.

### 3.3 라이브러리 작성 규칙

- **인터페이스에는 제네릭을 쓸 수 있습니다.** 계약은
  `interface Mapper<T> { fn apply(T value) -> T; }`처럼 선언합니다.
- **람다는 대응하는 인터페이스에 대입할 수 있습니다.** 예:
  `Mapper<int> m = (int x) -> int => x + 1;` (`fn` 타입 변수 선언은 불가)
- **생성자 오버로딩은 제한적입니다.** 여러 입력 형태가 필요하면
  `Optional.empty()`, `Optional.of(v)` 같은 정적 팩토리를 함께 제공합니다.
- **`char`/`byte` 타입은 없습니다.** 문자는 한 글자 문자열, 바이트는
  `list<int>`(0..255)로 다룹니다.
- **비트 연산자는 없습니다.** 필요한 연산은 산술 연산으로 구현합니다.
- **enum에는 메서드를 둘 수 있습니다.** 정적 헬퍼는 컴패니언 함수나 별도
  모듈 함수로 제공합니다(정적 메서드 호출은 지원하지 않음).
- **인터페이스는 값 타입으로 쓸 수 있습니다.** 구현 인스턴스를 인터페이스
  타입 변수에 담아 호출할 수 있습니다.

### 3.4 테스트와 문서

1. `Lib/test/test_run/stdlib_slug.ast`
2. 같은 이름의 `Lib/test/test_run/stdlib_slug.out`
3. `Doc/stdlib.md`의 해당 표 또는 새 절
4. `make check`

`Lib/test/test_run`의 `.ast`는 자동으로 발견되므로 목록 파일을 고칠 필요가
없습니다.

## 4. 새 C 내장 모듈 추가

파일 시스템, 시간, 프로세스처럼 C 런타임이 필요한 모듈은 `.ast` 파일 대신
내장 모듈로 등록할 수 있습니다. `System`과 `Files`가 이 방식입니다.

### 4.1 런타임 등록

`Python/runtime.c`의 `Interp_Init`에서 모듈을 만들고 값을 정의합니다.

```c
AstModule *mod = Module_RegisterBuiltin(in, "AsterHost");
Env_Define(in, mod->env, "clock",
           RValue_Builtin(in, Builtin_New(in, "clock", bi_host_clock)));
```

`Module_RegisterBuiltin`은 같은 이름의 모듈이 이미 있으면 그 모듈을 돌려줍니다.
하위 모듈이 필요하면 `"AsterHost.clock"`처럼 점이 들어간 이름으로 따로
등록합니다.

### 4.2 타입 검사기에 opaque 모듈로 알리기

`Python/typecheck.c`의 `is_opaque_module`에 모듈 이름을 추가합니다.

```c
static int is_opaque_module(const char *name) {
    return !strcmp(name, "System") || !strncmp(name, "System.", 7) ||
           !strcmp(name, "Files") ||
           !strcmp(name, "AsterHost") ||
           !strcmp(name, "aster.util.regex.Native");
}
```

opaque 모듈은 소스 파일을 읽지 않으므로 멤버 타입이 기본적으로 `any`입니다.
정확한 타입이 필요하면 `init_builtins`/`handle_import`/`member_type`에
`System.stdin` 같은 특례를 추가해야 합니다. 특례가 많아질 것 같으면 Aster
모듈로 작성하는 편이 유지보수에 좋습니다.

### 4.3 테스트

`Lib/test/test_run/`에 모듈을 쓰는 프로그램과 `.out`을 추가합니다. 내장
모듈은 `import AsterHost;`로 가져와야 하며, import 없이 이름을 쓰면 타입
검사기와 실행기 모두에서 거부되어야 합니다.

## 5. 새 토큰/연산자 추가

키워드와 달리 연산자는 렉서가 직접 인식해야 합니다.

### 5.1 `Grammar/Tokens`에 추가

정확히 그 문자열 하나로 이루어진 토큰은 이름 뒤에 문자열을 씁니다.

```text
TILDE '~'
```

### 5.2 생성 코드 갱신

```sh
make regen
```

`Tools/build/generate_token.py`가 `TOK_TILDE`, 토큰 이름 표, 정확 일치 표를
만듭니다. 렉서는 `_AsterToken_Exact`를 가장 긴 문자열부터 대조하므로, 새
연산자는 자동으로 인식됩니다. `ASTER_MAX_OPERATOR_LENGTH`도 함께 갱신됩니다.

### 5.3 파서에 우선순위 추가

식 파서는 우선순위가 낮은 것부터 높은 순서로 함수를 호출합니다.
`p_or_expr` → `p_and_expr` → `p_not_expr` → `p_comparison` → … 순서를 보고
새 연산자가 들어갈 자리에 파싱을 추가합니다. 기존 연산자와 같은 우선순위라면
해당 함수의 루프에 분기를 넣습니다.

### 5.4 AST, 타입 검사, 실행, 도구

새 식 노드가 필요하면 `Parser/Aster.asdl`의 `expr`에 추가하고 `make regen`
합니다. 그 뒤 다음을 모두 갱신합니다.

- `Python/typecheck.c`: `infer`의 식 분기, 필요하면 `shallow_type`(var 추론)
- `Python/interp.c`: `eval`의 식 분기
- `Python/formatter.c`: `fmt_expr`
- `Python/linter.c`: `walk_expr`
- `Python/Aster-ast.c`는 생성물이므로 직접 수정하지 않음

### 5.5 테스트

- `Lib/test/test_grammar/valid/`: 새 연산자 식
- `Lib/test/test_run/*.ast` + `.out`: 계산 결과
- `Lib/test/test_format/*.ast` + `.fmt`: 포매터 출력
- `Tools/random_suite.py`: 무작위 생성에 넣을 가치가 있는 연산자면 카테고리 추가

## 6. 검증 명령

```sh
make                 # C 코드 빌드
make regen           # 토큰/AST 생성 코드 갱신 (ASDL/Tokens 수정 시)
make check           # 전체 검증
```

특정 검증만 돌리려면 다음과 같이 실행합니다.

```sh
python3 Tools/check_frontend.py
python3 Tools/check_runtime.py
python3 Tools/check_tools.py
python3 Tools/check_random.py --seed 1234 --keep
```

Windows MSYS2에서는 `python3` 대신 `python`을 쓰거나
`make check PYTHON=python`으로 지정합니다. 실패 산출물은
`build/test-failures/`에 남습니다.

## 7. 흔한 실수

- **생성 파일 직접 수정**: `Include/internal/token.h`, `Parser/token.c`,
  `Include/internal/ast.h`, `Python/Aster-ast.c`는 `make regen`이 덮어씁니다.
  원본(`Grammar/Tokens`, `Parser/Aster.asdl`)을 고칩니다.
- **ASDL만 고치고 `make regen`을 잊음**: 새 생성자가 없어 컴파일이 깨집니다.
- **파서 롤백 누락**: 소프트 키워드 파서가 실패했는데 위치를 되돌리지 않으면
  뒤의 정상 식을 삼킵니다.
- **`stmt_escapes_env` 누락**: 환경 재사용으로 클로저가 오래된 값을 봅니다.
  확신이 없으면 `1`을 반환합니다.
- **`always_exits` 누락**: 반환 누락 검사가 틀립니다.
- **`loop_depth` 누락**: 루프 안의 `break`/`continue`가 거부됩니다.
- **포매터 미연결**: `--format`이 새 문장을 출력하지 못합니다.
- **문서 미갱신**: `Doc/language.md`, `Doc/stdlib.md`, `Grammar/aster.gram`이
  실제 동작과 어긋납니다.
