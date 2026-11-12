# Aster 언어 명세

이 문서는 Aster의 문법과 실행 의미를 설명합니다. 설치 방법은 [README](../README.md)를,
내장 함수와 표준 입출력은 [stdlib.md](stdlib.md), 패키지는 [packages.md](packages.md)를 봅니다.

문법의 형식 정의는 [Grammar/aster.gram](../Grammar/aster.gram)에 있습니다.

## 1. 실행

```sh
aster program.ast [인자...]
aster -                    # 표준 입력에서 읽는다
aster                      # 파일이 없으면 대화형 REPL
```

| 옵션              | 의미                                    |
| ----------------- | --------------------------------------- |
| (없음)            | 타입 검사 후 실행                       |
| `--check`         | 타입 검사만 수행 (실행하지 않음)        |
| `--no-check`      | 타입 검사를 건너뛰고 실행               |
| `--ast`           | 파싱한 AST를 출력                       |
| `--tokens`        | 토큰 목록을 출력                        |
| `--format`        | 정규화된 소스를 출력 (주석은 사라짐)    |
| `--lint`          | 미사용 변수와 도달 불가 코드를 보고      |
| `--debug`         | 문장 단위 디버거로 실행 (`s/c/p/b/q/h`) |
| `--version`, `-V` | 버전 출력                               |
| `--help`, `-h`    | 도움말                                  |

스크립트는 위에서부터 순서대로 실행됩니다. `main()`은 자동으로 호출되지 않으므로
필요하면 직접 호출합니다. 파일 이름 뒤의 인자는 `System.args`로 들어갑니다.

```aster
import System;
import System.stdout.*;

println("인자 수: " + str(len(System.args)));
```

`print`/`println`과 `System.*`는 전역이 아니라 내장 모듈입니다. 쓰려면 먼저
import해야 하며, import하지 않으면 `NameError`입니다.

```aster
import System;             # System.out / System.err / System.in / System.args ...
import System.stdout.*;    # print, println
import System.stderr.*;    # print, println (표준 에러로 출력)

println("hello");
System.out.println("hello");
System.err.println("error");
```

이 문서의 예제에서 `println`/`print`를 쓰려면 `import System.stdout.*;`가
필요합니다.

### 대화형 REPL

파일 없이 `aster`를 실행하면 REPL이 열립니다. 입력마다 타입 검사 후 바로
실행하며, 선언한 함수·변수는 다음 입력에서도 유지됩니다. 블록이 닫히지
않았으면 `... ` 프롬프트로 다음 줄을 이어 받습니다.

```text
>>> int x = 2;
>>> x * 3
6
>>> :quit
```

- `:help`: 명령 목록 출력
- `:quit` (또는 `:q`, `:exit`, Ctrl-D): 종료

표현식 문장은 값을 출력하고, 오류는 실행을 멈추지 않습니다.

### 포매터, 린터, 디버거

세 도구는 파일을 수정하지 않고 결과만 출력합니다.

- `--format`: AST를 기준으로 정규화한 소스를 표준 출력에 씁니다. 주석은 보존되지
  않습니다. `--format file.ast > file.formatted.ast`처럼 리다이렉트해서 씁니다.
- `--lint`: 미사용 변수와 도달 불가 코드를 표준 에러에 보고합니다. 경고가 하나라도
  있으면 종료 코드가 1, 없으면 0입니다(타입 검사는 하지 않습니다).
- `--debug`: 타입 검사 후 문장을 하나씩 멈춰 가며 실행합니다. 입력 명령은
  `s`(다음 문장), `c`(계속 실행), `p 표현식`(값 출력), `b 줄번호`(중단점 설정),
  `q`(종료), `h`(도움말)입니다. 디버그 프롬프트와 추적은 표준 에러로 나갑니다.

## 2. 어휘 규칙

- **주석**: `#`부터 줄 끝까지, 그리고 `/* ... */`.
- **문장 구분**: 단순 문장은 `;`로 끝납니다. 블록(`{ ... }`)으로 끝나는 문장에는
  `;`를 붙이지 않습니다.
- **문자열**: `"..."` 또는 `'...'`. 이스케이프는 `\n \t \r \a \b \f \v \\ \' \"`,
  8진수 `\ooo`, 16진수 `\xhh`, 유니코드 `\uXXXX`, `\UXXXXXXXX`.
- **숫자**: 10진 정수와 소수(`3`, `1.5`). 지수 표기는 지원하지 않습니다.
- **식별자**: 문자, 숫자, `_`로 구성되며 숫자로 시작할 수 없습니다.
- **하드 키워드**: `package import from as fn class extends implements interface new
self public protected private internal if else while for break continue return
throw try catch finally switch case default typedef struct and or not is in
true false sizeof int float bool str null void super exception export yield`
- **소프트 키워드**: `const static enum assert extern constructor getter setter property`.
  지정된 위치에서만 키워드로 취급되므로 변수 이름으로도 쓸 수 있습니다.
  (`yield`는 하드 키워드입니다.)
- **오버로드**: 별도 키워드가 없습니다. 같은 이름의 함수/메서드를 여러 번 선언하면
  오버로드 묶음이 됩니다.

## 3. 타입

### 기본 타입

| 타입    | 설명                |
| ------- | ------------------- |
| `int`   | 64비트 정수         |
| `float` | 배정밀도 부동소수점 |
| `bool`  | `true`, `false`     |
| `str`   | UTF-8 문자열        |
| `null`  | null 값의 타입      |
| `void`  | 반환 타입 전용      |

그 밖에 사용자가 선언한 클래스, 인터페이스, 구조체, 예외, 열거형, 제네릭 타입이 있습니다.

### 타입 수식어

```aster
int x;              # 값
int* p;             # 포인터 단계
int** pp;           # 두 단계
str? maybe;         # null 허용 (맨 끝에만)
Point*? node;       # 포인터이면서 null 허용
list<int> xs;       # 제네릭
dict<str, int> m;
```

- nullable 표시 `?`는 타입의 맨 끝에만 붙일 수 있습니다. `int?*`는 오류입니다.
- `void`는 변수, 필드, 매개변수 타입으로 쓸 수 없고 반환 타입에만 쓸 수 있습니다.
- 함수 타입은 `fn(매개변수 타입, ...) -> 반환 타입`으로 씁니다. 반환하지 않으면
  `void`입니다.

```aster
fn(int) -> int twice = double_number;   # 이름 있는 함수를 담는 함수 타입 변수
list<fn(int) -> bool> predicates = [];
```

함수 타입은 변수, 매개변수, 반환 타입, 제네릭 인자에 모두 쓸 수 있습니다. 다만
**람다를 선언할 때는 함수 타입을 붙이지 않습니다.** 람다는 `var`(또는 타입 생략)나
람다의 반환 타입을 씁니다.

```aster
var a = (int x) -> int => x * 2;
float b = (int x) -> float => (float)x;

fn(int) -> int c = (int x) -> int => x * 2;   # 오류
```

### 합집합 타입

`|`로 여러 타입 중 하나를 받는 타입을 씁니다. 변수, 매개변수, 반환 타입,
제네릭 인자 어디에나 쓸 수 있습니다.

```aster
int | str value = 42;      # OK
value = "text";            # OK
value = 1.5;               # 오류: float는 int | str에 속하지 않음

fn label(int | str v) -> str { ... }

list<int | str> mixed = [1, "two", 3];
int | str | null maybe = null;
```

- `|`는 타입 위치에서만 씁니다. 식에서 `|`를 쓰면 문법 오류입니다.
- 초기값을 생략하면 **첫 번째 타입**의 기본값을 씁니다(`int | str`이면 `0`).
- `null`을 허용하려면 `null`을 멤버로 넣거나 맨 끝에 `?`를 붙입니다:
  `int | str | null`, `int | str?`.

### 타입 추론과 `const`

```aster
var count = 3;          # int로 추론
var name = "aster";     # str로 추론
const int LIMIT = 3;    # 재대입 불가
var xs = [1, 2, 3];     # list<int>
```

`var`와 `any`는 "타입을 검사하지 않음"을 뜻합니다. `const`가 붙은 변수에 다시
대입하면 타입 검사 단계와 실행 단계 모두에서 오류가 납니다.

### 선언과 기본값

```aster
int x;                  # 0
float f;                # 0.0
bool b;                 # false
str s;                  # ""
Point* p;               # null
Point p2 = {1, 2};      # 구조체 위치 초기화
Point p3 = { .y = 2, .x = 1 };
```

## 4. 연산자

우선순위는 낮은 것부터:

| 연산자                         | 설명                               |
| ------------------------------ | ---------------------------------- |
| `params -> T => 식`            | 람다                               |
| `a \|> f`                      | 파이프 ( `f(a)` )                  |
| `a ?? b`                       | null 병합 (왼쪽이 null이면 오른쪽) |
| `or`                           | 논리 합 (단락 평가)                |
| `and`                          | 논리 곱 (단락 평가)                |
| `not`                          | 논리 부정                          |
| `is`, `is not`, `in`, `not in` | 타입 검사, 포함 검사               |
| `== != < <= > >=`              | 비교 (연쇄 불가)                   |
| `+ -`                          | 덧셈, 뺄셈, 문자열/리스트 연결     |
| `* / // %`                     | 곱셈, 나눗셈, 몫, 나머지           |
| `**`                           | 거듭제곱 (오른쪽 결합)             |
| `+ - ++ -- & * **`             | 단항                               |
| `. -> ?. () [] ++ --`          | 후위                               |

### 산술

- `/`는 정수끼리도 **버림 나눗셈이 아니라 0 방향 절단**입니다. `7 / 2 == 3`.
- `//`는 **바닥 나눗셈**입니다. `-7 // 2 == -4`.
- `%`는 나머지의 부호가 제수와 같습니다(파이썬식). `-7 % 2 == 1`.
- 0으로 나누면 `ZeroDivisionError`.
- `**`의 왼쪽 피연산자가 정수이고 지수가 음수이면 결과는 `float`입니다.
- `+`는 문자열과 리스트/튜플 연결에도 쓰입니다. `*`는 문자열/리스트 반복에도 쓰입니다.

### 비교와 논리

`true`/`false`, `null`, 숫자, 문자열은 `==`로 비교합니다. 객체는 같은 인스턴스일 때만
`==`가 참입니다. `is`는 값의 **타입**을 검사합니다.

```aster
if (value is int) { ... }
if (value is not null) { ... }
if ("x" in xs) { ... }
```

`and`/`or`는 단락 평가이며 마지막으로 평가한 값을 돌려줍니다. 조건문에서만
`bool`로 해석됩니다.

### null 처리

```aster
str? maybe = null;
str shown = maybe ?? "기본값";
int n = maybe?.length() ?? 0;
```

`?.`는 왼쪽이 null이면 오른쪽을 평가하지 않고 `null`을 돌려줍니다.

### 캐스트, 변환, sizeof

```aster
int x = (int) 3.9;         # C 스타일 캐스트 (절단)
Point* q = (Point*) raw;
int y = int("42");         # 기본 타입 변환 함수
str s = str(3.5);
bool t = bool(1);
int size = sizeof(struct Point);
int size2 = sizeof(int);
```

`int(x)`, `float(x)`, `str(x)`, `bool(x)`는 인자가 없으면 각각 `0`, `0.0`, `""`,
`false`를 만듭니다. 이름 타입의 캐스트 `(T) x`는 `T`가 이미 선언된 타입 이름일 때만
캐스트로 읽힙니다.

### 증감

```aster
i++;
--j;
```

전위/후위 `++`/`--`는 값을 1 늘리거나 줄입니다. 식의 값은 전위는 갱신 후,
후위는 갱신 전 값입니다.

### 포인터 연산

`&value`는 주소, `*ptr`은 역참조(한 단계), `**pp`는 두 단계 역참조입니다.
`a ** b`는 거듭제곱이고, `a * *b`는 곱셈과 역참조입니다.

## 5. 문장

### 블록과 대입

```aster
{
    int x = 1;
    x += 2;             # += -= *= /= //= %= **=
    x = x + 1;
}
```

대입 좌변은 이름, 속성(`a.b`, `p->b`), 역참조(`*p`, `**pp`), 서브스크립트
(`xs[i]`, `d[k]`), 슬라이스(`xs[1:3]`), 튜플 `(a, b)`만 가능합니다.

```aster
xs[1:3] = [9, 8, 7];    # 슬라이스 대입 (길이는 달라도 됨)
d["k"] = 1;
(a, b) = (b, a);        # 튜플 교환
```

### 조건문

```aster
if (x > 0) {
    println("양수");
} else if (x == 0) {
    println("0");
} else {
    println("음수");
}
```

괄호는 필수입니다. 3항 연산자(`? :`)는 없습니다.

### switch와 패턴

`switch`는 값 하나를 여러 패턴과 차례로 비교해 처음 맞는 가지만 실행합니다.
폴스루(다음 `case`로 넘어감)는 없고, 각 `case`는 자기 블록을 끝까지 실행한 뒤
`switch`를 빠져나갑니다.

```aster
int x = -1;
switch (x) {
    case 0 { println("zero"); }
    case -1 { println("minus one"); }
    default { println("other"); }
}

str s = "hi";
switch (s) {
    case "hi" { println("greeting"); }
    case "bye" { println("farewell"); }
}
```

패턴은 다음 세 가지입니다.

- **리터럴**: 정수(음수 포함), 실수, 문자열, `true`, `false`, `null`. `==`로 비교하며,
  타입 검사기는 리터럴이 주체 타입에 대입 가능한지도 확인합니다(예: `int` 주체에
  문자열 리터럴 `case`는 오류). `null`은 nullable 주체에서만 씁니다.
- **이름 바인딩**: 그 자리에 매칭되는 모든 값과 일치하고, 값을 그 이름에 묶습니다.
  `_`는 아무것도 묶지 않는 와일드카드이며 `default`와 같습니다.
- **열거형 변형**: `Type.Variant` 또는 `Type.Variant(하위패턴...)`. 타입·변형·페이로드
  개수가 모두 같아야 하고, 하위 패턴도 차례로 매칭됩니다.

```aster
enum Shape {
    Circle(float r),
    Rect(float w, float h),
    Empty
}

fn describe(Shape s) -> str {
    switch (s) {
        case Shape.Circle(r) { return "circle " + str(r); }
        case Shape.Rect(w, h) { return "rect " + str(w) + "x" + str(h); }
        case Shape.Empty { return "empty"; }
    }
    return "unknown";
}
```

- 주체 식은 한 번만 평가합니다. 각 `case`는 별도의 자식 스코프를 가지므로 패턴이
  묶은 이름은 그 블록 안에서만 보입니다.
- 매칭되는 패턴이 없으면 아무것도 하지 않고 다음 문장으로 넘어갑니다.
- `default`는 `_` 패턴과 같아서 위치상 그 뒤의 `case`가 있으면 안 됩니다. `default`를
  두 번 쓰면 문법 오류입니다.
- 변형 패턴의 경로가 열거형 변형을 가리키지 않으면 `TypeError`입니다.

### 반복문

```aster
for (int i = 0; i < 10; i++) { println(i); }       # C 스타일
for (int i in xs) { println(i); }                  # for-in
for (int i; i = 1..6..2) { println(i); }           # 범위 (1,3,5)
while (running) { ... }
```

- `for-in`은 리스트, 튜플, 딕셔너리(키), 세트, 문자열, 제너레이터를 순회합니다.
- 범위 구문은 `시작..끝` 또는 `시작..끝..간격`이며 `..`는 이 구문 안에서만 씁니다.
- `break`와 `continue`는 가장 가까운 반복문에 적용됩니다.

### 구조 분해

```aster
var (a, b) = (1, 2);
var (x, y) = [3, 4];
str (first, last) = pair;
```

타입은 오른쪽 값에서 추론됩니다. 이름이 하나뿐인 `T(x) = ...`는 호출문과 구분되지
않으므로 구조 분해로 읽지 않습니다.

### assert

```aster
assert x > 0;
assert x > 0, "x는 양수여야 합니다";
```

실패하면 `AssertionError`가 납니다. 두 번째 인자는 메시지입니다.

### return, yield

```aster
fn f() -> int { return 1; }
fn g() -> void { return; }
```

`yield`는 제너레이터 함수 안에서만 쓸 수 있습니다(6.5절).

## 6. 함수

### 선언과 호출

```aster
fn add(int a, int b) -> int {
    return a + b;
}

int r = add(1, 2);
```

반환 타입은 생략할 수 없습니다. 매개변수는 항상 `타입 이름` 형태입니다.
블록 안에 중첩 함수를 선언할 수 있습니다.

### 제네릭 함수

```aster
fn id<T>(T value) -> T { return value; }
int x = id(3);              # T는 호출 시 추론
```

타입 인자를 명시하는 호출 문법(`id<int>(3)`)은 없습니다.

### 람다

```aster
var double = x -> int => x * 2;
var add = (int a, int b) -> int => a + b;
var make = () -> int => 42;

double_it = (int x) -> int => x * 2;   # var 없이 선언해도 된다
float half = (int x) -> float => (float)x;   # 선언 타입 = 람다의 반환 타입
```

- 형식은 `매개변수 -> 반환타입 => 본문`입니다. 반환 타입은 필수입니다.
- 매개변수는 `(타입 이름, ...)` 괄호 목록입니다. 이름만 쓸 수도 있습니다.
- 본문은 식 하나입니다. 포인터 멤버 접근(`p->x`)도 그대로 쓸 수 있습니다.
- 람다를 변수에 넣을 때 선언 타입은 `var`이거나 **람다의 반환 타입**입니다.
  함수 타입 `fn(...) -> T`을 붙이면 오류입니다.
- 옛 문법 `x => 식 -> 타입`, `(int a, int b) => 식 -> 타입`도 계속 동작합니다.
  옛 문법에서 본문 최상위의 `->`는 반환 타입 화살표로 읽히므로 포인터 멤버
  접근은 괄호로 감쌉니다: `x => (p->v) -> int`.

### 함수 타입

`fn(...) -> T`는 값의 타입으로 쓸 수 있습니다. 변수, 매개변수, 반환 타입, 제네릭
인자 어디에나 올 수 있습니다.

```aster
fn apply(fn(int) -> int f, int v) -> int { return f(v); }

fn(int) -> int double_it = double_number;   # 이름 있는 함수
println(apply(double_it, 4));               # 8

var triple = (int x) -> int => x * 3;       # 람다는 var로
println(apply(triple, 4));                  # 12

list<fn(int) -> bool> predicates = [];
```

함수 타입 값에는 일반 함수, 람다, 메서드 참조를 넣을 수 있습니다. 다만 람다를
변수로 선언할 때 함수 타입을 붙이지 말고 `var`나 람다의 반환 타입을 씁니다.
타입 검사기는 매개변수 개수와 타입, 반환 타입이 같은지를 검사합니다
(`int`→`float` 허용 규칙은 함수 타입에도 적용됩니다).

### extern

```aster
extern fn puts(str text) -> int;
extern fn sqrt(float x) -> float;
```

`extern`은 본문 없이 C 함수를 선언합니다. 선언을 읽는 시점에 구현을 확인하며,
프로세스에서 그 이름의 C 심볼(또는 같은 이름의 내장 함수)을 찾지 못하면
`NotImplementedError: extern function '...' has no native implementation`이 납니다.
호출하면 찾은 심볼을 그대로 호출합니다.

- Windows: 현재 실행 파일, `msvcrt`/`ucrtbase`/`kernel32`에서 찾습니다.
- 그 밖의 플랫폼: `dlsym(RTLD_DEFAULT, 이름)`으로 찾습니다.

지원 인자/반환 타입은 `int`(포인터 크기 정수), `bool`, `float`(double), `str`
(`char*`로 전달)입니다. 인자는 최대 4개까지 넘길 수 있고, `float` 인자와 다른
인자를 섞을 수 없습니다. 심볼을 찾지 못하면 `NotImplementedError`가 납니다.
`Lib/aster/math.ast`가 이 방식으로 libm을 그대로 노출합니다.

### 오버로드

```aster
fn area(int side) -> int { return side * side; }
fn area(int w, int h) -> int { return w * h; }
fn area(float r) -> float { return 3.14 * r * r; }
```

같은 이름을 여러 번 선언하면 오버로드가 됩니다. 호출할 때 **인자 수**와
**매개변수 타입**으로 고릅니다. 정수는 `float` 매개변수에도 맞으므로, 정확히 맞는
정의가 우선입니다. 반환 타입만 다른 오버로드는 구분하지 않습니다. 클래스 메서드와
`static` 메서드에도 같은 규칙이 적용됩니다.

### 연산자 오버로딩

클래스에 특별한 이름의 메서드를 정의하면 연산자와 내장 함수가 그 메서드를
호출합니다.

| 메서드                                                                                         | 쓰이는 곳                            |
| ---------------------------------------------------------------------------------------------- | ------------------------------------ |
| `__add__ __sub__ __mul__ __div__ __floordiv__ __mod__ __pow__`                                  | 산술 `+ - * / // % **`               |
| `__radd__ __rsub__ __rmul__ __rdiv__ __rfloordiv__ __rmod__ __rpow__`                          | 오른쪽 피연산자일 때의 산술          |
| `__eq__ __lt__ __le__ __gt__ __ge__`                                                           | 비교                                 |
| `__neg__`                                                                                      | 단항 `-`                             |
| `__str__ __repr__`                                                                             | 문자열 변환, `print`/repr            |
| `__len__`                                                                                      | `len()`                              |
| `__getitem__ __setitem__`                                                                      | 인덱스 읽기/쓰기                     |
| `__contains__`                                                                                 | `in`                                 |
| `__iter__ __next__`                                                                            | `for-in`, 순회                       |

```aster
class Vec {
    public float x;
    public float y;
    constructor(float x, float y) { self->x = x; self->y = y; }

    public fn __add__(Vec other) -> Vec {
        return new Vec(self->x + other.x, self->y + other.y);
    }
    public fn __eq__(Vec other) -> bool { return self->x == other.x and self->y == other.y; }
    public fn __str__() -> str { return "Vec(" + str(self->x) + ", " + str(self->y) + ")"; }
    public fn __len__() -> int { return 2; }
}

Vec a = new Vec(1.0, 2.0);
Vec b = new Vec(3.0, 4.0);
println(a + b);            # Vec(4.0, 6.0)
println(a == new Vec(1.0, 2.0));   # true
```

- 왼쪽 피연산자 객체의 메서드를 먼저 찾고, 없으면 오른쪽의 `__r...__`를 씁니다.
- 비교 훅의 결과는 `bool`로 해석합니다.
- `__str__`/`__repr__`은 `str`을, `__len__`은 `int`를 돌려주어야 합니다.
- 왼쪽 객체가 `__add__`를 가지고 있으면 문자열 자동 변환보다 훅이 우선합니다.

### 제너레이터

```aster
fn odds(int n) -> list<int> {
    for (int i = 1; i <= n; i++) {
        if (i % 2 == 0) { continue; }
        yield i * i;
    }
}

for (int v in odds(5)) { println(v); }

var g = odds(3);
println(next(g));       # 1  (여기서 본문이 2번째 yield까지 실행된다)
println(list(g));       # [9]  (남은 값)
println(list(g));       # []
```

본문에 `yield`가 하나라도 있으면 그 함수는 제너레이터 함수가 됩니다. 호출하면
본문을 실행하지 않고 제너레이터 값을 돌려줍니다. `next()`나 `for-in`이 값을
요청할 때마다 다음 `yield`까지 본문이 **지연 실행**되고, 그 지점에서 멈춥니다.
남은 값이 없으면 `next()`가 `StopIteration`을 던집니다. `return`은 순회를 끝내고,
`break`/`continue`가 반복문 밖에서 함수를 빠져나가면 `RuntimeError`입니다.
최상위 문장의 `yield`는 타입 검사에서 거부됩니다. 제너레이터에는 `len()`을 쓸 수
없고, 남은 값은 `list(g)`로 꺼냅니다.

`for-in`은 다음 순서로 대상을 엽니다.

1. `list`/`tuple`/`dict`(키)/`set`/`frozenset`/`str`(코드포인트) 같은 내장 컬렉션
2. 제너레이터
3. `__next__`를 가진 객체, 또는 `__iter__`가 순회자를 돌려주는 객체

사용자 클래스로 순회자를 만들려면 `__iter__`(자기 자신 반환)와 `__next__`를
정의하고, 끝에서 `StopIteration`을 던집니다.

```aster
class Countdown {
    private int left;
    constructor(int n) { self->left = n; }
    public fn __iter__() -> Countdown { return self; }
    public fn __next__() -> int {
        if (self->left <= 0) { throw StopIteration(); }
        self->left -= 1;
        return self->left;
    }
}

for (int v in new Countdown(3)) { println(v); }   # 2, 1, 0
```

## 7. 클래스, 인터페이스, 구조체

### 클래스

```aster
class Point {
    private int x;
    private int y;

    constructor(int x, int y) {
        self->x = x;
        self->y = y;
    }

    public fn sum() -> int { return self->x + self->y; }
}

Point* p = new Point(1, 2);
println(p->sum());
```

- 필드와 메서드에는 `public`, `protected`, `private`, `internal`을 붙일 수 있습니다.
  접근 제어는 **명시한 경우에만** 타입 검사기가 강제합니다. 생략하면 제한이 없습니다.
  `private`는 선언한 클래스 안에서만, `protected`는 그 클래스와 하위 클래스에서만
  쓸 수 있습니다. `internal`은 모듈 밖에서도 쓸 수 있습니다(현재 검사 없음).
- 생성자는 `constructor(...)`로 쓰고 반환 타입을 쓰지 않습니다.
- 인스턴스에는 `self`, 멤버 접근에는 `->`를 씁니다. 타입 이름이 붙은 정적 멤버에는
  `.`을 씁니다.
- 상속은 `extends`, 인터페이스는 `implements`입니다.

### static

```aster
class Counter {
    static int total = 0;
    public static fn next() -> int {
        Counter.total = Counter.total + 1;
        return Counter.total;
    }
}
```

`static` 필드는 클래스당 하나이고, `static` 메서드는 인스턴스 없이 호출합니다.

### 프로퍼티

```aster
class Person {
    private str _name;

    public @property
    str name;

    getter name { return self->_name; }
    setter name(str value) { self->_name = value; }
}
```

`@property`로 선언하고 `getter`/`setter`로 접근자를 정의합니다. setter의 매개변수
이름은 반드시 `value`여야 합니다. 선언 없이 `getter`/`setter`만 쓸 수 없습니다.

### 상속과 super

```aster
class Animal {
    protected str name;
    constructor(str n) { self->name = n; }
    public fn speak() -> str { return self->name; }
}

class Dog extends Animal {
    constructor(str n) { super(n); }
    public fn speak() -> str { return super.speak() + "!"; }
}
```

`super(...)`는 부모 생성자, `super.method()`는 부모 메서드를 호출합니다.

### 인터페이스

```aster
interface Shape {
    fn area() -> float;
}

class Circle implements Shape {
    private float r;
    constructor(float r) { self->r = r; }
    public fn area() -> float { return 3.14 * self->r * self->r; }
}
```

인터페이스 메서드는 본문 없이 `;`로 끝납니다. 현재 구현은 **런타임 강제가 없는
계약**입니다. 인터페이스를 구현했다고 선언해도 메서드 존재 여부를 실행 시점에
검사하지 않습니다.

### 제네릭 클래스

```aster
class Box<T> {
    private T value;
    constructor(T v) { self->value = v; }
    public fn get() -> T { return self->value; }
}

Box<int>* b = new Box<int>(3);
```

제네릭 매개변수에 제약(`T extends ...`)은 없습니다. 타입은 실행 시 지워집니다.

### 구조체

구조체는 C 스타일 값 타입입니다. `typedef struct`로 별칭을 만들 수 있습니다.

```aster
typedef struct Point {
    int x;
    int y;
    Point(int a, int b): x(a), y(b) {}
} Point;

struct Point p = {10, 20};
Point q = { .y = 20, .x = 10 };
Point* r = new Point(1, 2);
memset(&p, 0, sizeof(struct Point));
```

- 초기화는 위치(`{10, 20}`), 지정(`{ .x = 10, .y = 20 }`) 두 방식이 있고 섞을 수
  없습니다. `{}`와 `{0}`도 허용합니다.
- 구조체 생성자는 구조체 이름(태그 또는 별칭)으로 씁니다.
- 구조체 생성자의 초기화 리스트 `: x(a), y(b)`는 선택입니다.
- 일반 코드에서는 `new`로 구조체 포인터를 만드는 것을 권장합니다.

## 8. 예외

```aster
exception AppError extends Exception;

try {
    throw AppError("잘못된 입력");
} catch (AppError e) {
    println(e.name + ": " + e.message);
} catch (Exception e) {
    println("기타: " + e.message);
} finally {
    println("정리");
}
```

`throw`의 형태:

| 형태                            | 결과                     |
| ------------------------------- | ------------------------ |
| `throw "메시지";`               | `Exception("메시지")`    |
| `throw "메시지" as ValueError;` | `ValueError("메시지")`   |
| `throw ValueError;`             | 메시지 없는 `ValueError` |
| `throw 예외값;`                 | 그 예외 값               |

`catch`는 예외 클래스 계층을 따라 매칭합니다. 잡힌 값에는 `.message`와 `.name`이
있습니다. `finally`는 정상 종료와 예외 모두에서 실행됩니다.

잡히지 않은 예외는 호출 스택과 함께 표준 에러로 출력되고 종료 코드는 1이 됩니다.

```
Traceback (most recent call last):
  at outer (line 10)
  at middle (line 6)
  at inner (line 2)
main.ast:14: error: ZeroDivisionError: 0으로 나눌 수 없습니다
```

마지막 줄이 예외가 최종적으로 드러난 위치이고, 그 위는 예외가 빠져나온 호출
경로입니다(오래된 호출이 위). `try`로 잡으면 그 지점까지의 프레임만 남습니다.

사용자 예외는 `exception 이름;` 또는 `exception 이름 extends 부모;`로 선언합니다.
부모를 생략하면 `Exception`을 상속합니다.

내장 예외 목록은 [stdlib.md](stdlib.md)를 봅니다.

## 9. 열거형

```aster
enum Color { RED, GREEN, BLUE }
enum Code { OK = 0, WARN = 10, ERROR = 20 }

println(Color.GREEN.name);     # GREEN
println(Color.BLUE.ordinal);   # 2
println(Code.WARN.value);      # 10
```

값을 생략하면 0부터의 순서가 됩니다. 열거형 멤버는 `타입.이름`으로 접근하고
`.name`, `.ordinal`, `.value`를 읽을 수 있습니다.

### 페이로드가 있는 변형

멤버에 매개변수를 붙이면 값을 담는 변형(variant)이 됩니다.

```aster
enum Shape {
    Circle(float r),
    Rect(float w, float h),
    Empty
}

Shape c = Shape.Circle(1.5);
Shape r = Shape.Rect(2.0, 3.0);
Shape e = Shape.Empty;

println(c);            # Shape.Circle(1.5)
println(c.variant);    # Circle
println(c.arity);      # 1
println(c.payload);    # [1.5]
println(r.payload);    # [2.0, 3.0]
println(e.arity);      # 0
```

- 매개변수가 있는 멤버는 `타입.멤버(인자...)`로 생성합니다. 인자 개수가 다르면
  `TypeError`입니다.
- 매개변수가 없는 멤버는 생성 호출 없이 `Shape.Empty`가 곧 값입니다.
- 공통 접근자: `.name`, `.ordinal`, `.value`, `.variant`(이름), `.arity`(페이로드
  개수), `.payload`(페이로드 리스트).
- `==`는 타입, 변형, 페이로드를 모두 비교합니다. 페이로드가 해시 가능하면
  딕셔너리 키나 세트 원소로도 쓸 수 있습니다.
- 분기는 `switch`와 변형 패턴(`case Shape.Circle(r) { ... }`)으로 합니다. 자세한
  문법은 §5의 “switch와 패턴”을 봅니다.

```aster
fn area(Shape s) -> float {
    if (s.variant == "Circle") {
        float r = s.payload[0];
        return 3.0 * r * r;
    }
    if (s.variant == "Rect") {
        return s.payload[0] * s.payload[1];
    }
    return 0.0;
}
```

## 10. 컬렉션

### 리터럴과 생성

```aster
var xs = [1, 2, 3];
var empty = [];
var t = (1, 2);          # 튜플
var one = (1,);          # 원소 1개 튜플
var d = {"a": 1, "b": 2};
var s = {1, 2, 3};       # 세트
var e = {};              # 빈 딕셔너리
var fs = frozenset(xs);  # 불변 세트 (리터럴 없음)
var nested = [[1], [2]];
```

`{}`는 빈 딕셔너리이고, 빈 세트는 `set()`으로 만듭니다.

### 컴프리헨션

```aster
var squares = [x * x for x in xs if x > 0];
var evens = {x for x in xs if x % 2 == 0};
var index = {x: x * 10 for x in xs};
```

### 펼치기

```aster
var more = [0, *xs, 4];
var pool = {*xs, 99};
var merged = {**d, "c": 3};
```

`*`는 리스트/세트, `**`는 딕셔너리 펼치기입니다.

### 범위와 map/filter

`a..b`는 양 끝을 포함하는 리스트를 만듭니다. `a..b..s`는 간격을 지정합니다.

```aster
list<int> xs = 1..5;          # [1, 2, 3, 4, 5]
list<int> odds = 1..9..2;     # [1, 3, 5, 7, 9]
list<int> down = 5..1..-2;    # [5, 3, 1]
list<int> squares = xs.map(x -> int => x * x);
list<int> big = squares.filter(x -> bool => x > 4);
```

`map`은 각 원소에 함수를 적용한 새 리스트를, `filter`는 조건이 true인 원소만 담은
새 리스트를 돌려줍니다. 원본 리스트는 바뀌지 않습니다.

### 인덱스와 슬라이스

```aster
println(xs[0]);
println(xs[-1]);         # 뒤에서 첫 번째
println(d["a"]);
var part = xs[1:3];      # [xs[1], xs[2]]
var step = xs[0:5:2];
xs[0] = 9;
xs[1:3] = [7, 7, 7];
```

범위를 벗어난 인덱스는 `IndexError`, 없는 키는 `KeyError`입니다. 세트와
frozenset은 인덱스로 읽을 수 없습니다. 슬라이스 경계는 생략할 수 있습니다.

### 순회

```aster
for (int x in xs) { ... }
for (str k in d) { ... }         # 키
for (int v in s) { ... }
for (str ch in "abc") { ... }    # 유니코드 코드포인트 단위
```

### 내장 함수와 메서드

`len`, `list`, `tuple`, `dict`, `set`, `frozenset`, `range`, `next`와 각 컬렉션의
메서드는 [stdlib.md](stdlib.md)에 정리되어 있습니다.

## 11. 문자열

```aster
str s = "Aster";
println(s.upper());              # ASTER
println(s.length());
println(s.substring(1, 3));
println(s.charAt(0));
println("  x  ".trim());
println("-".join(["a", "b"]));   # a-b
println("42".to_int());

str msg = f"{name}은 {count}살, 평균 {avg:.2f}";
```

f-string은 `f"..."`로 쓰고 `{식}`을 보간합니다. `{{`/`}}`는 중괄호 문자입니다.
`!r`은 repr, `!s`는 문자열 변환입니다. 포맷 지정자는 소수(`:.2f`), 지수(`:e`),
정수(`:d`), 16진수(`:x`)를 지원합니다.

문자열은 내부적으로 UTF-8이지만, `length()`, 인덱스, 슬라이스, `for-in`,
`substring`, `charAt`은 모두 **유니코드 코드포인트** 단위로 동작합니다. `s[0]`은
첫 번째 바이트가 아니라 첫 번째 문자(코드포인트)입니다.

## 12. 모듈

```aster
# helper.ast
export fn greet(str name) -> str { return "Hello, " + name; }
export int answer = 42;

# main.ast
import helper;
println(helper.greet("aster"));

import helper as h;
println(h.answer);

from helper import greet;
from helper import answer as a;
from helper import *;
```

`from`은 한 번에 이름 하나만 가져옵니다(콤마 목록은 지원하지 않습니다). 여러
이름이 필요하면 `from`을 여러 번 쓰거나 모듈 전체를 가져옵니다.

- 모듈 경로의 `.`은 디렉터리 구분자입니다. `import a.b`는 `a/b.ast` 또는
  `a/b/__init__.ast`를 찾습니다.
- `import a.b.C;`는 모듈 `a.b`가 공개한 이름 `C`를 **직접** 가져옵니다. Java의
  `import java.io.File;`과 같은 방식입니다. 모듈의 마지막 경로 이름과 같은
  공개 선언이 없을 때만 모듈 객체를 가져옵니다.
- 탐색 순서: 실행 파일이 있는 디렉터리 → 표준 라이브러리(`ASTER_HOME/Lib`
  또는 현재 디렉터리의 `Lib`) → 패키지 의존성 경로 → `ASTER_PATH`.
- `export`가 하나라도 있으면 **export한 이름만** import할 수 있습니다.
  `export`가 없으면 밑줄로 시작하지 않는 모든 이름을 공개합니다.
- 모듈은 이름별로 한 번만 로드되고 캐시됩니다.
- `import *`는 공개 이름을 현재 범위로 가져옵니다.
- `package 이름;` 문장은 파싱되지만 현재 로더 동작에는 영향을 주지 않습니다.

표준 라이브러리의 내장 클래스도 같은 방식으로 가져옵니다.

```aster
import aster.io.File;          # File 클래스
import aster.util.Deque;       # Deque<T> 클래스
import aster.util.regex.Pattern;

File f = new File("notes.txt");
Deque<int>* d = new Deque<int>();
```

패키지 관리자는 [packages.md](packages.md)를 봅니다.

## 13. 표준 입출력

Java 스타일 API를 씁니다.

```aster
System.out.println("Hello");
System.err.println("오류");
Scanner sc = new Scanner(System.in);
str line = sc.nextLine();
int n = sc.nextInt();
```

`System.out`/`System.err`에는 `print`와 `println`이 있습니다. `System`은 내장
모듈이라 `import System;`으로 가져와야 합니다. `import System.stdout.*;`는
`print`/`println`을 현재 범위에 바로 정의합니다. `Scanner`는
`nextLine`, `next`, `nextInt`, `nextFloat`, `hasNextLine`, `hasNext`, `close`를
제공합니다. `Files`는 `readString`, `writeString`, `readLines`, `exists`, `delete`를
제공합니다. 자세한 내용은 [stdlib.md](stdlib.md)를 봅니다.

## 14. 타입 검사기

Aster는 실행 전에 정적 타입 검사를 합니다.

- 기본값: 검사 후 실행. 오류가 있으면 실행하지 않습니다.
- `--check`: 검사만 하고 종료합니다.
- `--no-check`: 검사를 건너뜁니다.

검사기는 **보수적**입니다. 확실한 오류만 보고하고, 추론할 수 없는 값은 `any`로
처리합니다. 주 모듈과 함께, import한 모듈(표준 라이브러리 포함)도 읽어서
검사하고 공개 이름의 타입을 가져옵니다. 순환 import는 한 번만 검사합니다.

검사하는 것:

- 지역 변수, 함수, 클래스, 구조체 선언과 사용
- 함수 호출 인자 수/타입(오버로드 선택 포함), 반환 타입
- import한 모듈에 없는 이름 접근
- 인터페이스가 요구하는 메서드의 존재와 서명
- 명시한 `private`/`protected` 멤버의 접근 범위

검사하지 않는 것:

- 객체의 동적 속성 존재 여부
- 배열/딕셔너리 인덱스의 범위
- 함수 타입 값의 실제 대상 검사(대입 시 서명만 비교)

`extern` 선언의 구현 확인은 타입 검사가 아니라 로드(선언 처리) 단계에서 합니다.
오류 메시지는 모두 영어로 출력됩니다.

## 15. 실행 모델과 제한

- 트리 워킹 인터프리터이며, 값은 태그가 붙은 `RValue`로 표현됩니다.
- AST 노드는 아레나에 할당되고 프로그램 종료 시 한꺼번에 해제됩니다. 실행 중
  만들어진 값(리스트, 인스턴스 등)은 참조 카운팅 없이 프로세스가 끝날 때 반환됩니다.
- 호출 환경과 매 반복마다 새로 만들어지는 블록 환경은 재사용 풀에서 꺼내 씁니다.
  함수/블록 본문이 환경을 밖으로 흘리지 않는다고 정적으로 판단될 때만 재사용하므로,
  클로저·람다·`&지역변수`·제너레이터·중첩 선언이 있는 곳은 항상 새 환경을 씁니다.
  이는 의미를 바꾸지 않으면서 반복 호출의 할당량을 줄입니다.
- 클래스 인스턴스는 참조 의미, 구조체는 값 의미입니다.
- 제너레이터는 지연 실행이며 별도 스택(Windows 파이버 / POSIX ucontext)에서 돕니다.
- 호출 깊이 제한 기본값은 512입니다. 넘으면 `RecursionError`가 납니다.
  `System.setRecursionLimit(n)`으로 바꾸고 `System.getRecursionLimit()`으로 읽습니다
  (1 이상 100000 이하).
- 3항 연산자(`? :`)와 예외 없는 `try` 블록은 없습니다.
- `--format`은 주석을 보존하지 않습니다. 정규화된 코드만 필요할 때 씁니다.
- 바이트코드 VM은 아직 없습니다. 성능·메모리 상한이 필요하면 트리 워킹의 한계를
  전제로 해야 합니다.
- `extern`은 프로세스에 이미 있는 C 심볼만 호출할 수 있습니다(임의 라이브러리 로드 없음).
