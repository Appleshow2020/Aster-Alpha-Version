import glob

M = {
    r"메모리가 부족합니다\n": r"out of memory\n",
    "이름": "a name",
    "기본 타입": "a primitive type",
    "선언된 타입 이름": "a declared type name",
    "정수 리터럴이 너무 큽니다": "integer literal is too large",
    "잘못된 문자열 리터럴: %s": "invalid string literal: %s",
    "f-string에 짝이 맞지 않는 '}'가 있습니다": "unmatched '}' in f-string",
    "f-string의 중괄호가 비었습니다": "empty braces in f-string",
    "f-string에서 '}'가 필요합니다": "'}' expected in f-string",
    "f-string 변환은 !r 또는 !s만 쓸 수 있습니다": "f-string conversion must be !r or !s",
    "f-string 식이 잘못되었습니다: %s": "invalid f-string expression: %s",
    "변환에는 인자를 하나만 넘길 수 있습니다": "conversion takes exactly one argument",
    "식": "an expression",
    "증감할 수 있는 대상": "an incrementable target",
    "대입할 수 있는 좌변": "an assignable target",
    "setter의 매개변수 이름은 value여야 합니다 ('%s')": "setter parameter must be named 'value' (got '%s')",
    "구조체 생성자의 이름은 구조체 이름과 같아야 합니다: '%s'": "struct constructor name must match the struct name: '%s'",
    "범위 for의 변수 이름이 같아야 합니다: '%s'를 선언하고 '%s'에 대입했습니다":
        "range for variable names must match: declared '%s' but assigned '%s'",
    "'파일 끝'": "'end of file'",
    "문법 오류: 줄 끝에 ';'가 빠졌습니다": "syntax error: missing ';' at end of line",
    "문법 오류: 예상하지 못한 파일 끝": "syntax error: unexpected end of file",
    "문법 오류: 예상하지 못한 '%.*s'": "syntax error: unexpected '%.*s'",
    " (%s 중 하나가 필요합니다)": " (%s expected)",
    "       aster [--ast|--tokens|--check] -    (표준 입력에서 읽는다)":
        "       aster [--ast|--tokens|--check] -    (read from stdin)",
    "       aster pkg <init|add|remove|list|check>   (패키지 관리자)":
        "       aster pkg <init|add|remove|list|check>   (package manager)",
    "  (기본)      파일을 실행한다 (실행 전에 타입 검사를 한다)":
        "  (default)   run a file (type-checks before running)",
    "  --ast       파싱한 AST를 출력한다": "  --ast       print the parsed AST",
    "  --tokens    토큰 목록을 출력한다": "  --tokens    print the token list",
    "  --check     정적 타입 검사만 수행한다": "  --check     type-check only",
    "  --no-check  타입 검사 없이 실행한다": "  --no-check  run without type checking",
    "  --version   버전을 출력한다": "  --version   print the version",
    "  init [name]        현재 디렉터리에 aster.pkg를 만든다":
        "  init [name]        create aster.pkg in the current directory",
    "  add NAME PATH      의존성을 추가한다 (PATH는 패키지의 부모 디렉터리)":
        "  add NAME PATH      add a dependency (PATH is the package's parent directory)",
    "  remove NAME        의존성을 제거한다": "  remove NAME        remove a dependency",
    "  list               의존성 목록을 출력한다": "  list               list dependencies",
    "  check              의존성이 실제로 있는지 검사한다":
        "  check              verify that dependencies exist",
    "aster.pkg가 이미 있습니다": "aster.pkg already exists",
    r"aster.pkg를 쓸 수 없습니다\n": r"cannot write aster.pkg\n",
    r"생성: aster.pkg (name = %s)\n": r"created: aster.pkg (name = %s)\n",
    r"디렉터리가 없습니다: %s\n": r"no such directory: %s\n",
    r"%s의 부모 디렉터리를 찾을 수 없습니다\n": r"cannot find the parent directory of %s\n",
    "%s에서 '%s' 패키지를 찾을 수 없습니다 ": "cannot find package '%s' under %s ",
    r"(%s/%s.ast 또는 %s/%s/__init__.ast 필요)\n": r"(expected %s/%s.ast or %s/%s/__init__.ast)\n",
    r"추가: %s -> %s\n": r"added: %s -> %s\n",
    r"aster.pkg가 없습니다\n": r"aster.pkg not found\n",
    r"그런 의존성이 없습니다: %s\n": r"no such dependency: %s\n",
    r"제거: %s\n": r"removed: %s\n",
    r"aster.pkg가 없습니다 (aster pkg init)\n": r"aster.pkg not found (run 'aster pkg init')\n",
    "의존성 없음": "no dependencies",
    r"fail %s = %s (모듈을 찾을 수 없음)\n": r"fail %s = %s (module not found)\n",
    r"내부 오류: %s의 필수 필드 '%s'가 비어 있습니다\n":
        r"internal error: required field '%s' of %s is null\n",
    "'%s'에는 인자가 %zu개 필요하지만 %zu개가 넘어왔습니다":
        "'%s' takes %zu argument(s) but %zu were given",
    "호출 깊이가 너무 깊습니다 (무한 재귀일 수 있습니다)":
        "call depth exceeded (possible infinite recursion)",
    "'break'와 'continue'는 반복문 안에서만 쓸 수 있습니다":
        "'break' and 'continue' are only allowed inside loops",
    "호출할 수 없는 값입니다": "value is not callable",
    "'%s'에 인자 %zu개에 맞는 정의가 없습니다": "no definition of '%s' accepts %zu argument(s)",
    "상위 클래스에 생성자가 없습니다": "base class has no constructor",
    "'%s'을(를) 만들 때는 new를 사용하세요": "use 'new' to create '%s'",
    "값을 함수처럼 호출할 수 없습니다": "value is not callable",
    "'%s' 객체에 '%s' 메서드가 없습니다": "'%s' object has no method '%s'",
    "제너레이터 실행 컨텍스트를 만들 수 없습니다": "cannot create a generator execution context",
    "정수 연산 결과가 int 범위를 벗어났습니다": "integer arithmetic overflowed int range",
    "시퀀스 반복 횟수는 정수여야 합니다": "sequence repeat count must be an integer",
    "시퀀스가 너무 커집니다": "sequence is too large",
    "산술 연산에는 숫자가 필요합니다": "arithmetic requires numbers",
    "0으로 나눌 수 없습니다": "division by zero",
    "null 값에서 '%s'에 접근했습니다": "accessed '%s' on a null value",
    "프로퍼티 '%s'에 getter가 없습니다": "property '%s' has no getter",
    "'%s'에는 '%s' 속성이 없습니다": "'%s' has no attribute '%s'",
    "null 포인터에서 '%s'에 접근했습니다": "accessed '%s' through a null pointer",
    "'%s' 구조체에 '%s' 필드가 없습니다": "struct '%s' has no field '%s'",
    "'%s' 객체에는 '%s' 속성이 없습니다": "'%s' object has no attribute '%s'",
    "str 객체에는 '%s' 속성이 없습니다": "str has no attribute '%s'",
    "모듈 '%s'에 '%s'이(가) 없습니다": "module '%s' has no member '%s'",
    "상위 클래스에 '%s' 메서드가 없습니다": "base class has no method '%s'",
    "클래스 '%s'에는 정적 멤버 '%s'이(가) 없습니다": "class '%s' has no static member '%s'",
    "enum 값에는 '%s' 속성이 없습니다": "enum value has no attribute '%s'",
    "enum '%s'에 '%s'이(가) 없습니다": "enum '%s' has no member '%s'",
    "예외 값에는 '%s' 속성이 없습니다": "exception value has no attribute '%s'",
    "이 값에는 '%s' 속성이 없습니다": "this value has no attribute '%s'",
    "구조체 초기화는 구조체 타입에만 쓸 수 있습니다":
        "struct initializers can only be used with struct types",
    "'%s' 초기화 값이 너무 많습니다": "too many initializers for '%s'",
    "'%s'에 '%s' 필드가 없습니다": "'%s' has no field '%s'",
    "기반 클래스 '%s'을(를) 찾을 수 없습니다": "cannot find base class '%s'",
    "'%s'에는 생성자가 없습니다": "'%s' has no constructor",
    "'%s'에 맞는 생성자가 없습니다": "no constructor of '%s' matches the arguments",
    "extern 함수 '%s'은(는) 인자를 최대 4개까지 지원합니다":
        "extern function '%s' supports at most 4 arguments",
    "extern 함수 '%s'에 연결된 네이티브 구현이 없습니다":
        "extern function '%s' has no native implementation",
    "extern 함수 '%s'은(는) 인자 %zu개가 필요합니다":
        "extern function '%s' requires %zu argument(s)",
    "extern 함수 '%s'의 %zu번째 인자는 str이어야 합니다":
        "argument %zu of extern function '%s' must be str",
    "extern 함수 '%s': float 인자와 다른 인자를 섞을 수 없습니다":
        "extern function '%s': cannot mix float and non-float arguments",
    "%s 타입 변수에 %s 값을 넣을 수 없습니다": "cannot assign %s to a variable of type %s",
    "값 %zu개를 이름 %zu개로 분해할 수 없습니다": "cannot unpack %zu values into %zu names",
    "단언이 실패했습니다": "assertion failed",
    "null 값의 '%s'에 접근했습니다": "accessed '%s' on a null value",
    "클래스 '%s'에는 정적 필드 '%s'이(가) 없습니다": "class '%s' has no static field '%s'",
    "'%s'에는 '%s' 필드가 없습니다": "'%s' has no field '%s'",
    "이 값에는 '%s'에 접근할 수 없습니다": "cannot access '%s' on this value",
    "이름 '%s'이(가) 정의되지 않았습니다": "name '%s' is not defined",
    "null 포인터를 역참조했습니다": "dereferenced a null pointer",
    "역참조할 수 없는 값입니다": "value cannot be dereferenced",
    "역참조할 수 없는 값에 대입했습니다": "cannot assign through a non-dereferenceable value",
    "슬라이스 대입은 아직 지원하지 않습니다": "slice assignment is not supported",
    "대입할 수 없는 대상입니다": "invalid assignment target",
    "프로퍼티 '%s'에 setter가 없습니다": "property '%s' has no setter",
    "슬라이스 경계는 int여야 합니다": "slice bounds must be int",
    "슬라이스 대입은 리스트에만 쓸 수 있습니다":
        "slice assignment is only allowed on lists",
    "슬라이스 대입의 step은 1이어야 합니다": "slice assignment step must be 1",
    "슬라이스 대입에는 순회 가능한 값이 필요합니다":
        "slice assignment requires an iterable value",
    "슬라이스 대입에는 '='만 쓸 수 있습니다": "slice assignment only supports '='",
    "구조 분해에는 '='만 쓸 수 있습니다": "destructuring only supports '='",
    "%s 값은 구조 분해할 수 없습니다": "cannot destructure a %s value",
    "구조 분해 대상은 이름이어야 합니다": "destructuring targets must be names",
    "const 변수 '%s'에는 대입할 수 없습니다": "cannot assign to const variable '%s'",
    "범위 for의 간격은 0일 수 없습니다": "range for step cannot be zero",
    "모듈 '%s'을(를) 찾을 수 없습니다": "cannot find module '%s'",
    "모듈 '%s'을(를) 읽을 수 없습니다: %s": "cannot read module '%s': %s",
    "yield는 제너레이터 함수 안에서만 쓸 수 있습니다":
        "'yield' is only allowed inside generator functions",
    "throw에는 문자열이나 예외 값이 필요합니다":
        "'throw' requires a string or an exception value",
    "타입 '%s'을(를) 찾을 수 없습니다": "cannot find type '%s'",
    "'%s'은(는) new로 만들 수 있는 타입이 아닙니다": "'%s' cannot be created with 'new'",
    "슬라이스 인덱스는 정수여야 합니다": "slice indices must be integers",
    "'%s' 객체는 슬라이스를 지원하지 않습니다": "'%s' object does not support slicing",
    "'self'는 메서드 안에서만 쓸 수 있습니다": "'self' is only allowed inside methods",
    "딕셔너리는 딕셔너리로만 펼칠 수 있습니다": "dicts can only be spread into dicts",
    "이 위치에서는 펼치기를 쓸 수 없습니다": "spread is not allowed here",
    "%s 값은 증감할 수 없습니다": "cannot increment/decrement a %s value",
    "단항 '-'에는 숫자가 필요합니다": "unary '-' requires a number",
    "컬렉션 원소의 주소는 구할 수 없습니다": "cannot take the address of a collection element",
    "프로퍼티의 주소는 구할 수 없습니다": "cannot take the address of a property",
    "배열 리터럴은 아직 지원하지 않습니다": "array literals are not supported",
    "슬라이스는 대괄호 안에서만 쓸 수 있습니다": "slicing is only allowed inside brackets",
    "알 수 없는 식 노드입니다": "unknown expression node",
    "null 값에는 len()을 쓸 수 없습니다": "cannot call len() on null",
    "__len__은 정수를 돌려주어야 합니다": "__len__ must return an integer",
    "'%s'에는 len()을 쓸 수 없습니다": "cannot call len() on '%s'",
    "'%s'은(는) 순회할 수 없습니다": "'%s' is not iterable",
    "null 값은 순회할 수 없습니다": "cannot iterate over null",
    "'%s'의 __iter__가 __next__를 가진 값을 돌려주지 않았습니다":
        "__iter__ of '%s' did not return a value with __next__",
    "'%s' 객체는 __iter__/__next__가 없어 순회할 수 없습니다":
        "'%s' object is not iterable (missing __iter__/__next__)",
    "잘못된 순회 커서입니다": "invalid iteration cursor",
    "구조체 기본값이 너무 깊습니다": "struct default values are nested too deeply",
    "타입 기본값이 너무 깊습니다": "type default values are nested too deeply",
    "구조체 타입 '%s'을(를) 찾을 수 없습니다": "cannot find struct type '%s'",
    "구조체 크기 계산이 너무 깊습니다": "struct size computation is nested too deeply",
    "타입 크기 계산이 너무 깊습니다": "type size computation is nested too deeply",
    "크기를 비교할 수 없는 값입니다": "value has no comparable size",
    "인덱스는 정수여야 합니다 (받은 값: '%s')": "index must be an integer (got '%s')",
    "null 값은 인덱싱할 수 없습니다": "cannot index null",
    "'%s' 객체는 인덱싱할 수 없습니다": "'%s' object cannot be indexed",
    "'%s' 객체는 값을 바꿀 수 없습니다": "'%s' object is immutable",
    "'set' 객체는 인덱스 대입을 지원하지 않습니다":
        "'set' does not support index assignment",
    "'str' 객체는 값을 바꿀 수 없습니다": "'str' object is immutable",
    "'%s' 객체는 인덱스 대입을 지원하지 않습니다":
        "'%s' object does not support index assignment",
    "null 값에는 'in'을 쓸 수 없습니다": "cannot use 'in' with null",
    "'in <string>'의 왼쪽은 문자열이어야 합니다":
        "the left operand of 'in <string>' must be a string",
    "'%s' 객체는 iterable이 아닙니다": "'%s' object is not iterable",
    "문자열이 너무 커집니다": "string is too large",
    "문자열을 float로 바꿀 수 없습니다: '%s'": "cannot convert string to float: '%s'",
    "숫자가 아닌 값을 float로 바꿀 수 없습니다": "cannot convert a non-number to float",
    "float 값을 int 범위로 바꿀 수 없습니다": "float value is out of int range",
    "문자열을 int로 바꿀 수 없습니다: '%s'": "cannot convert string to int: '%s'",
    "정수 범위를 넘었습니다": "integer overflow",
    "숫자가 아닌 값을 int로 바꿀 수 없습니다": "cannot convert a non-number to int",
    "%s는 문자열을 돌려주어야 합니다": "%s must return a string",
    "이 값은 memset으로 초기화할 수 없습니다": "this value cannot be initialized with memset",
    "memset은 인자 3개가 필요합니다": "memset requires 3 arguments",
    "memset의 첫 인자는 포인터여야 합니다": "the first argument of memset must be a pointer",
    "memset은 지금 0으로만 초기화할 수 있습니다":
        "memset currently only supports zero initialization",
    "null 값은 iterable이 아닙니다": "null is not iterable",
    "인자가 너무 많습니다": "too many arguments",
    "len은 인자 1개가 필요합니다": "len takes exactly 1 argument",
    "typeName은 인자 1개가 필요합니다": "typeName takes exactly 1 argument",
    "repr는 인자 1개가 필요합니다": "repr takes exactly 1 argument",
    "chr은 인자 1개가 필요합니다": "chr takes exactly 1 argument",
    "chr 인자는 0과 0x10FFFF 사이여야 합니다": "chr argument must be between 0 and 0x10FFFF",
    "ord는 문자열 인자 1개가 필요합니다": "ord takes exactly 1 string argument",
    "ord는 한 글자 문자열이 필요합니다": "ord requires a single-character string",
    "nowMillis는 인자를 받지 않습니다": "nowMillis takes no arguments",
    "sleepMillis는 인자 1개가 필요합니다": "sleepMillis takes exactly 1 argument",
    "cwd는 인자를 받지 않습니다": "cwd takes no arguments",
    "현재 디렉터리를 읽을 수 없습니다": "cannot read the current directory",
    "pid는 인자를 받지 않습니다": "pid takes no arguments",
    "platform은 인자를 받지 않습니다": "platform takes no arguments",
    "setenv는 문자열 인자 2개가 필요합니다": "setenv takes exactly 2 string arguments",
    "next는 제너레이터 인자 1개가 필요합니다": "next takes exactly 1 generator argument",
    "제너레이터가 끝났습니다": "generator is exhausted",
    "dict는 (키, 값) 쌍의 iterable이 필요합니다":
        "dict requires an iterable of (key, value) pairs",
    "dict 원소의 길이가 2가 아닙니다": "dict element does not have length 2",
    "인덱스는 정수여야 합니다": "index must be an integer",
    "append()는 인자 1개가 필요합니다": "append() takes exactly 1 argument",
    "extend()는 인자 1개가 필요합니다": "extend() takes exactly 1 argument",
    "insert()는 인자 2개가 필요합니다": "insert() takes exactly 2 arguments",
    "pop()은 인자를 최대 1개 받습니다": "pop() takes at most 1 argument",
    "빈 리스트에서 pop할 수 없습니다": "cannot pop from an empty list",
    "remove()는 인자 1개가 필요합니다": "remove() takes exactly 1 argument",
    "index()는 인자 1개가 필요합니다": "index() takes exactly 1 argument",
    "count()는 인자 1개가 필요합니다": "count() takes exactly 1 argument",
    "get()은 인자를 1개 또는 2개 받습니다": "get() takes 1 or 2 arguments",
    "pop()은 인자를 1개 또는 2개 받습니다": "pop() takes 1 or 2 arguments",
    "setdefault()는 인자를 1개 또는 2개 받습니다": "setdefault() takes 1 or 2 arguments",
    "update()는 인자 1개가 필요합니다": "update() takes exactly 1 argument",
    "update()에는 (키, 값) 쌍이 필요합니다": "update() requires (key, value) pairs",
    "add()는 인자 1개가 필요합니다": "add() takes exactly 1 argument",
    "discard()는 인자 1개가 필요합니다": "discard() takes exactly 1 argument",
    "union()은 인자 1개가 필요합니다": "union() takes exactly 1 argument",
    "intersection()은 인자 1개가 필요합니다": "intersection() takes exactly 1 argument",
    "difference()는 인자 1개가 필요합니다": "difference() takes exactly 1 argument",
    "issubset()은 인자 1개가 필요합니다": "issubset() takes exactly 1 argument",
    "issuperset()은 인자 1개가 필요합니다": "issuperset() takes exactly 1 argument",
    "%s은(는) 문자열이어야 합니다": "%s must be a string",
    "split()은 인자를 최대 1개 받습니다": "split() takes at most 1 argument",
    "split 구분자": "split separator",
    "빈 구분자로는 나눌 수 없습니다": "cannot split with an empty separator",
    "join()은 인자 1개가 필요합니다": "join() takes exactly 1 argument",
    "join()의 원소는 문자열이어야 합니다": "join() elements must be strings",
    "replace()는 인자 2개 또는 3개가 필요합니다": "replace() takes 2 or 3 arguments",
    "replace의 old": "replace() old value",
    "replace의 new": "replace() new value",
    "startswith/endswith는 인자 1개가 필요합니다":
        "startswith/endswith take exactly 1 argument",
    "인자": "argument",
    "find()는 인자 1개가 필요합니다": "find() takes exactly 1 argument",
    "find의 인자": "find() argument",
    "count의 인자": "count() argument",
    "charAt()는 인자 1개가 필요합니다": "charAt() takes exactly 1 argument",
    "문자열 인덱스가 범위를 벗어났습니다": "string index out of range",
    "substring()은 인자 1개 또는 2개가 필요합니다": "substring() takes 1 or 2 arguments",
    "substring 범위가 잘못되었습니다": "invalid substring range",
    "range()는 인자 1개에서 3개가 필요합니다": "range() takes 1 to 3 arguments",
    "range()의 간격은 0일 수 없습니다": "range() step cannot be zero",
    "range() 결과가 너무 큽니다": "range() result is too large",
    "파일 경로는 문자열이어야 합니다": "file path must be a string",
    "입력이 끝났습니다": "end of input",
    "정수로 읽을 수 없습니다: %s": "cannot read an integer: %s",
    "수로 읽을 수 없습니다: %s": "cannot read a number: %s",
    "Files.readString()은 인자 1개가 필요합니다":
        "Files.readString() takes exactly 1 argument",
    "파일을 열 수 없습니다: %s": "cannot open file: %s",
    "Files.writeString()은 인자 2개가 필요합니다":
        "Files.writeString() takes exactly 2 arguments",
    "파일 내용": "file contents",
    "파일을 쓸 수 없습니다: %s": "cannot write file: %s",
    "Files.exists()는 인자 1개가 필요합니다": "Files.exists() takes exactly 1 argument",
    "Files.delete()는 인자 1개가 필요합니다": "Files.delete() takes exactly 1 argument",
    "파일을 지울 수 없습니다: %s": "cannot delete file: %s",
    "System.exit()는 인자를 최대 1개 받습니다": "System.exit() takes at most 1 argument",
    "%s: %s 타입이 필요하지만 %s 값입니다": "%s: expected %s but got %s",
    "모듈 '%s' 구문 오류: %s:%d:%d: %s": "syntax error in module '%s': %s:%d:%d: %s",
    "모듈 '%s' 파일을 읽을 수 없습니다: %s": "cannot read module file '%s': %s",
    "함수에는 인자가 %zu개 필요하지만 %zu개가 넘어왔습니다":
        "function takes %zu argument(s) but %zu were given",
    "str에는 '%s' 속성이 없습니다": "str has no attribute '%s'",
    "'%s'의 '%s'은(는) private 멤버라 '%s' 밖에서 쓸 수 없습니다":
        "'%s' of '%s' is private and cannot be used outside '%s'",
    "'%s'의 '%s'은(는) protected 멤버라 '%s'와 그 하위 클래스에서만 쓸 수 있습니다":
        "'%s' of '%s' is protected and cannot be used outside '%s' and its subclasses",
    "'%s' 클래스에는 '%s' 멤버가 없습니다": "class '%s' has no member '%s'",
    "인자 %zu개에 맞는 함수 정의가 없습니다": "no function definition accepts %zu argument(s)",
    "생성자 인자": "constructor argument",
    "'%s' 생성자에는 인자가 %zu개 필요합니다": "constructor of '%s' takes %zu argument(s)",
    "정의되지 않은 이름 '%s'입니다": "undefined name '%s'",
    "'%s' 클래스가 인터페이스 '%s'의 메서드 '%s'을(를) 구현하지 않았습니다":
        "class '%s' does not implement method '%s' of interface '%s'",
    "'%s'의 '%s' 메서드 서명이 인터페이스 '%s'와 맞지 않습니다":
        "method '%s' of '%s' does not match interface '%s'",
    "필드 초기값": "field initializer",
    "대입": "assignment",
    "프로퍼티 대입": "property assignment",
    "필드 대입": "field assignment",
    "슬라이스 대입": "slice assignment",
    "원소 대입": "element assignment",
    "역참조 대입": "dereference assignment",
    "변수 초기값": "variable initializer",
    "for-in 변수: %s 타입이 필요하지만 %s 값을 순회합니다":
        "for-in variable: expected %s but iterating over %s",
    "yield는 함수 안에서만 쓸 수 있습니다": "'yield' is only allowed inside functions",
    "반환값": "return value",
    "void 함수에서는 값을 반환할 수 없습니다":
        "cannot return a value from a void function",
    "%s 값을 반환해야 합니다": "must return a value of type %s",
}


def rewrite(path):
    with open(path, encoding='utf-8') as f:
        s = f.read()
    out = []
    i = 0
    n = len(s)
    changed = 0
    while i < n:
        c = s[i]
        if c == '/' and i + 1 < n and s[i + 1] == '/':
            j = s.find('\n', i)
            if j < 0:
                j = n
            out.append(s[i:j])
            i = j
            continue
        if c == '/' and i + 1 < n and s[i + 1] == '*':
            j = s.find('*/', i + 2)
            j = n if j < 0 else j + 2
            out.append(s[i:j])
            i = j
            continue
        if c == "'":
            j = i + 1
            while j < n:
                if s[j] == '\\' and j + 1 < n:
                    j += 2
                    continue
                if s[j] == "'":
                    j += 1
                    break
                if s[j] == '\n':
                    break
                j += 1
            out.append(s[i:j])
            i = j
            continue
        if c == '"':
            j = i + 1
            buf = []
            while j < n:
                if s[j] == '\\' and j + 1 < n:
                    buf.append(s[j:j + 2])
                    j += 2
                    continue
                if s[j] == '"':
                    break
                buf.append(s[j])
                j += 1
            content = ''.join(buf)
            if content in M:
                out.append('"' + M[content] + '"')
                changed += 1
            else:
                out.append(s[i:j + 1])
            i = j + 1
        else:
            out.append(c)
            i += 1
    if changed:
        with open(path, 'w', encoding='utf-8', newline='') as f:
            f.write(''.join(out))
    return changed


total = 0
for path in sorted(glob.glob('Programs/*.c') + glob.glob('Parser/*.c') +
                   glob.glob('Parser/lexer/*.c') + glob.glob('Python/*.c') +
                   glob.glob('Objects/*.c')):
    c = rewrite(path)
    if c:
        print(path, c)
        total += c
print('replaced', total)
