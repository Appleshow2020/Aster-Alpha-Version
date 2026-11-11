# Aster 언어 명세

이 문서는 Aster의 문법과 실행 의미를 설명합니다. 설치 방법은 [README](../README.md)를,
내장 함수와 표준 입출력은 [stdlib.md](stdlib.md), 패키지는 [packages.md](packages.md)를 봅니다.

문법의 형식 정의는 [Grammar/aster.gram](../Grammar/aster.gram)에 있습니다.

## 1. 실행

```sh
aster program.ast [인자...]
aster -                    # 표준 입력에서 읽는다
```

| 옵션 | 의미 |
| --- | --- |
| (없음) | 타입 검사 후 실행 |
| `--check` | 타입 검사만 수행 (실행하지 않음) |
| `--no-check` | 타입 검사를 건너뛰고 실행 |
| `--ast` | 파싱한 AST를 출력 |
| `--tokens` | 토큰 목록을 출력 |
| `--version`, `-V` | 버전 출력 |
| `--help`, `-h` | 도움말 |

스크립트는 위에서부터 순서대로 실행됩니다. `main()`은 자동으로 호출되지 않으므로
필요하면 직접 호출합니다. 파일 이름 뒤의 인자는 `System.args`로 들어갑니다.

```aster
println("인자 수: " + str(len(System.args)));
```

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
  throw try catch finally typedef struct and or not is in true false sizeof
  int float bool str null void super exception export yield`
- **소프트 키워드**: `const static enum assert extern constructor getter setter property`.
  지정된 위치에서만 키워드로 취급되므로 변수 이름으로도 쓸 수 있습니다.
  (`yield`는 하드 키워드입니다.)
- **오버로드**: 별도 키워드가 없습니다. 같은 이름의 함수/메서드를 여러 번 선언하면
  오버로드 묶음이 됩니다.

## 3. 타입

### 기본 타입

| 타입 | 설명 |
| --- | --- |
| `int` | 64비트 정수 |
| `float` | 배정밀도 부동소수점 |
| `bool` | `true`, `false` |
| `str` | UTF-8 문자열 |
| `null` | null 값의 타입 |
| `void` | 반환 타입 전용 |

그 밖에 사용자가 선언한 클래스, 인터페이스, 구조체, 예외, 열거형, 제네릭 타입이 있습니다.

### 타입 수식어

```aster
int x;              // 값
int* p;             // 포인터 단계
int** pp;           // 두 단계
str? maybe;         // null 허용 (맨 끝에만)
Point*? node;       // 포인터이면서 null 허용
list<int> xs;       // 제네릭
dict<str, int> m;
```

- nullable 표시 `?`는 타입의 맨 끝에만 붙일 수 있습니다. `int?*`는 오류입니다.
- `void`는 변수, 필드, 매개변수 타입으로 쓸 수 없고 반환 타입에만 쓸 수 있습니다.
- 함수 타입 표기는 없습니다. 람다는 값을 담을 때 `var`나 `any`를 씁니다.

### 타입 추론과 `const`

```aster
var count = 3;          // int로 추론
var name = "aster";     // str로 추론
const int LIMIT = 3;    // 재대입 불가
var xs = [1, 2, 3];     // list<int>
```

`var`와 `any`는 "타입을 검사하지 않음"을 뜻합니다. `const`가 붙은 변수에 다시
대입하면 타입 검사 단계와 실행 단계 모두에서 오류가 납니다.

### 선언과 기본값

```aster
int x;                  // 0
float f;                // 0.0
bool b;                 // false
str s;                  // null
Point* p;               // null
Point p2 = {1, 2};      // 구조체 위치 초기화
Point p3 = { .y = 2, .x = 1 };
```

## 4. 연산자

우선순위는 낮은 것부터:

| 연산자 | 설명 |
| --- | --- |
| `x => ... -> T` | 람다 |
| `a \|> f` | 파이프 ( `f(a)` ) |
| `a ?? b` | null 병합 (왼쪽이 null이면 오른쪽) |
| `or` | 논리 합 (단락 평가) |
| `and` | 논리 곱 (단락 평가) |
| `not` | 논리 부정 |
| `is`, `is not`, `in`, `not in` | 타입 검사, 포함 검사 |
| `== != < <= > >=` | 비교 (연쇄 불가) |
| `+ -` | 덧셈, 뺄셈, 문자열/리스트 연결 |
| `* / // %` | 곱셈, 나눗셈, 몫, 나머지 |
| `**` | 거듭제곱 (오른쪽 결합) |
| `+ - ++ -- & * **` | 단항 |
| `. -> ?. () [] ++ --` | 후위 |

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
int x = (int) 3.9;         // C 스타일 캐스트 (절단)
Point* q = (Point*) raw;
int y = int("42");         // 기본 타입 변환 함수
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
    x += 2;             // += -= *= /= //= %= **=
    x = x + 1;
}
```

대입 좌변은 이름, 속성(`a.b`, `p->b`), 역참조(`*p`, `**pp`), 서브스크립트
(`xs[i]`, `d[k]`), 슬라이스(`xs[1:3]`), 튜플 `(a, b)`만 가능합니다.

```aster
xs[1:3] = [9, 8, 7];    // 슬라이스 대입 (길이는 달라도 됨)
d["k"] = 1;
(a, b) = (b, a);        // 튜플 교환
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

괄호는 필수입니다. 3항 연산자(`? :`)와 `switch`는 없습니다.

### 반복문

```aster
for (int i = 0; i < 10; i++) { println(i); }       // C 스타일
for (int i in xs) { println(i); }                  // for-in
for (int i; i = 1..6..2) { println(i); }           // 범위 (1,3,5)
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
int x = id(3);              // T는 호출 시 추론
```

타입 인자를 명시하는 호출 문법(`id<int>(3)`)은 없습니다.

### 람다

```aster
var double = x => x * 2 -> int;
var add = (int a, int b) => a + b -> int;
var make = () => 42 -> int;
```

- 반환 타입은 필수입니다.
- 매개변수가 하나면 타입을 생략할 수 있습니다. 둘 이상이면 타입이 필요합니다.
- 본문 최상위의 `->`는 반환 타입 화살표로 읽힙니다. 포인터 멤버 접근은 괄호로
  감쌉니다: `x => (p->v) -> int`.

### extern

```aster
extern fn puts(str text) -> int;
```

본문 없이 선언만 합니다. 실행 파일에는 외부 함수 연결이 없으므로 호출하면
`RuntimeError`가 납니다. 타입 검사용 선언으로 취급합니다.

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
println(next(g));       // 1
println(len(g));        // 남은 개수
println(list(g));       // 남은 값을 리스트로
```

본문에 `yield`가 하나라도 있으면 그 함수는 제너레이터 함수가 됩니다. Aster의
제너레이터는 **열성 평가**입니다. 함수를 호출하는 순간 본문이 끝까지 실행되며
`yield`된 값이 모두 모입니다. 그 뒤 `next()`/`for-in`은 모인 값을 순서대로 돌려줍니다.
남은 값이 없으면 `next()`가 `StopIteration`을 던집니다. `return`은 수집을 끝내고,
`break`/`continue`가 반복문 밖에서 함수를 빠져나가면 `RuntimeError`입니다.
최상위 문장의 `yield`는 타입 검사에서 거부됩니다.

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
  생략하면 `private`입니다.
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

| 형태 | 결과 |
| --- | --- |
| `throw "메시지";` | `Exception("메시지")` |
| `throw "메시지" as ValueError;` | `ValueError("메시지")` |
| `throw ValueError;` | 메시지 없는 `ValueError` |
| `throw 예외값;` | 그 예외 값 |

`catch`는 예외 클래스 계층을 따라 매칭합니다. 잡힌 값에는 `.message`와 `.name`이
있습니다. `finally`는 정상 종료와 예외 모두에서 실행됩니다. 잡히지 않은 예외는
파일:줄:열과 함께 표준 에러로 출력되고 종료 코드는 1이 됩니다.

사용자 예외는 `exception 이름;` 또는 `exception 이름 extends 부모;`로 선언합니다.
부모를 생략하면 `Exception`을 상속합니다.

내장 예외 목록은 [stdlib.md](stdlib.md)를 봅니다.

## 9. 열거형

```aster
enum Color { RED, GREEN, BLUE }
enum Code { OK = 0, WARN = 10, ERROR = 20 }

println(Color.GREEN.name);     // GREEN
println(Color.BLUE.ordinal);   // 2
println(Code.WARN.value);      // 10
```

값을 생략하면 0부터의 순서가 됩니다. 열거형 멤버는 `타입.이름`으로 접근하고
`.name`, `.ordinal`, `.value`를 읽을 수 있습니다.

## 10. 컬렉션

### 리터럴과 생성

```aster
var xs = [1, 2, 3];
var empty = [];
var t = (1, 2);          // 튜플
var one = (1,);          // 원소 1개 튜플
var d = {"a": 1, "b": 2};
var s = {1, 2, 3};       // 세트
var e = {};              // 빈 딕셔너리
var fs = frozenset(xs);  // 불변 세트 (리터럴 없음)
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

### 인덱스와 슬라이스

```aster
println(xs[0]);
println(xs[-1]);         // 뒤에서 첫 번째
println(d["a"]);
var part = xs[1:3];      // [xs[1], xs[2]]
var step = xs[0:5:2];
xs[0] = 9;
xs[1:3] = [7, 7, 7];
```

범위를 벗어난 인덱스는 `IndexError`, 없는 키는 `KeyError`입니다. 세트와
frozenset은 인덱스로 읽을 수 없습니다. 슬라이스 경계는 생략할 수 있습니다.

### 순회

```aster
for (int x in xs) { ... }
for (str k in d) { ... }         // 키
for (int v in s) { ... }
for (str ch in "abc") { ... }    // 바이트 단위 문자
```

### 내장 함수와 메서드

`len`, `list`, `tuple`, `dict`, `set`, `frozenset`, `range`, `next`와 각 컬렉션의
메서드는 [stdlib.md](stdlib.md)에 정리되어 있습니다.

## 11. 문자열

```aster
str s = "Aster";
println(s.upper());              // ASTER
println(s.length());
println(s.substring(1, 3));
println(s.charAt(0));
println("  x  ".trim());
println("-".join(["a", "b"]));   // a-b
println("42".to_int());

str msg = f"{name}은 {count}살, 평균 {avg:.2f}";
```

f-string은 `f"..."`로 쓰고 `{식}`을 보간합니다. `{{`/`}}`는 중괄호 문자입니다.
`!r`은 repr, `!s`는 문자열 변환입니다. 포맷 지정자는 소수(`:.2f`), 지수(`:e`),
정수(`:d`), 16진수(`:x`)를 지원합니다.

## 12. 모듈

```aster
// helper.ast
export fn greet(str name) -> str { return "Hello, " + name; }
export int answer = 42;

// main.ast
import helper;
println(helper.greet("aster"));

import helper as h;
println(h.answer);

from helper import greet, answer;
from helper import *;
```

- 모듈 경로의 `.`은 디렉터리 구분자입니다. `import a.b`는 `a/b.ast` 또는
  `a/b/__init__.ast`를 찾습니다.
- 탐색 순서: 실행 파일이 있는 디렉터리 → 표준 라이브러리(`ASTER_HOME/Lib`
  또는 현재 디렉터리의 `Lib`) → 패키지 의존성 경로 → `ASTER_PATH`.
- `export`가 하나라도 있으면 **export한 이름만** import할 수 있습니다.
  `export`가 없으면 밑줄로 시작하지 않는 모든 이름을 공개합니다.
- 모듈은 이름별로 한 번만 로드되고 캐시됩니다.
- `import *`는 공개 이름을 현재 범위로 가져옵니다.
- `package 이름;` 문장은 파싱되지만 현재 로더 동작에는 영향을 주지 않습니다.

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

`System.out`/`System.err`에는 `print`와 `println`이 있습니다. `Scanner`는
`nextLine`, `next`, `nextInt`, `nextFloat`, `hasNextLine`, `hasNext`, `close`를
제공합니다. `Files`는 `readString`, `writeString`, `readLines`, `exists`, `delete`를
제공합니다. 자세한 내용은 [stdlib.md](stdlib.md)를 봅니다.

## 14. 타입 검사기

Aster는 실행 전에 정적 타입 검사를 합니다.

- 기본값: 검사 후 실행. 오류가 있으면 실행하지 않습니다.
- `--check`: 검사만 하고 종료합니다.
- `--no-check`: 검사를 건너뜁니다.

검사기는 **보수적**입니다. 확실한 오류만 보고하고, 추론할 수 없는 값은 `any`로
처리합니다. 현재 **주 모듈만** 검사하며, import한 모듈은 검사하지 않습니다.
또한 다음은 검사하지 않습니다.

- 인터페이스 구현 여부
- `extern` 함수의 실제 연결
- 객체의 동적 속성 존재 여부
- 배열/딕셔너리 인덱스의 범위

## 15. 실행 모델과 제한

- 트리 워킹 인터프리터이며, 값은 태그가 붙은 `RValue`로 표현됩니다.
- 메모리는 아레나에 할당되고 프로그램 종료 시 한꺼번에 해제됩니다.
- 클래스 인스턴스는 참조 의미, 구조체는 값 의미입니다.
- 제너레이터는 열성 평가입니다(6.5절).
- `switch`, 3항 연산자, 함수 타입, 연산자 오버로딩, 예외 없는 `try` 블록은 없습니다.
- 인터페이스는 컴파일 타임 계약이 아니라 선언입니다.
- `extern`은 선언만 지원하고 실제 C 함수와 연결하지 않습니다.
