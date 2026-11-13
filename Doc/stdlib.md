# Aster 표준 라이브러리

Aster의 표준 라이브러리는 전역 내장 함수, 내장 모듈(`System`), 값에 붙는 메서드,
그리고 `Scanner`, `Files` 객체로 구성됩니다. `print`/`println`과 `System.*`는 내장
모듈이라 `import System.stdout.*;`, `import System;`처럼 먼저 가져와야 합니다.
문법 일반은 [language.md](language.md)를 봅니다.

## 1. 콘솔 출력과 전역 내장 함수

| 함수 | 설명 |
| --- | --- |
| `print(값...)` | 값을 공백으로 구분해 표준 출력에 씁니다(줄바꿈 없음). `import System.stdout.*;` 필요. |
| `println(값...)` | `print` 후 줄바꿈합니다. `import System.stdout.*;` 필요. |
| `len(x)` | 문자열(코드포인트 수)/리스트/튜플/딕셔너리/세트의 길이. `__len__`을 가진 객체에도 씁니다. 제너레이터는 지원하지 않습니다. |
| `list(x)` | 반복 가능한 값을 리스트로 복사합니다. 인자가 없으면 빈 리스트. |
| `tuple(x)` | 튜플로 복사합니다. |
| `dict(x)` | 딕셔너리를 복사하거나 `(키, 값)` 쌍의 반복에서 만듭니다. |
| `set(x)` | 세트를 만듭니다. 인자가 없으면 빈 세트. |
| `frozenset(x)` | 불변 세트를 만듭니다. |
| `range(stop)` / `range(start, stop)` / `range(start, stop, step)` | 정수 리스트를 만듭니다. `step`은 0일 수 없습니다. |
| `next(g)` | 제너레이터에서 다음 값을 꺼냅니다. 남은 값이 없으면 `StopIteration`. |
| `memset(ptr, value, size)` | C의 `memset`과 같은 의미로 구조체/값을 초기화합니다. |
| `int(x)`, `float(x)`, `str(x)`, `bool(x)` | 기본 타입 변환. 인자가 없으면 기본값을 만듭니다. |
| `typeName(x)` | 값의 타입 이름을 문자열로 돌려줍니다. |
| `repr(x)` | repr 문자열을 돌려줍니다(`__repr__` 훅 사용). |
| `chr(n)` / `ord(s)` | 코드포인트 ↔ 한 글자 문자열 변환. |
| `abs(x)` | 정수/실수의 절댓값. `INT64_MIN`은 `OverflowError`. |
| `sum(iterable[, start])` | 숫자 원소의 합. `start` 기본값은 0. |
| `min(iterable)` / `min(a, b, ...)` | 가장 작은 값. 빈 시퀀스면 `ValueError`. |
| `max(iterable)` / `max(a, b, ...)` | 가장 큰 값. 빈 시퀀스면 `ValueError`. |
| `all(iterable)` | 모든 원소가 참이면 `true`(빈 시퀀스는 `true`). |
| `any(iterable)` | 참인 원소가 하나라도 있으면 `true`(빈 시퀀스는 `false`). |
| `zip(a, b, ...)` | 같은 위치의 원소를 묶은 `(a[i], b[i], ...)` 튜플 리스트. 가장 짧은 입력에서 멈춥니다. |
| `enumerate(iterable[, start])` | `(인덱스, 값)` 튜플 리스트. `start` 기본값은 0. |
| `sorted(iterable[, key])` | 정렬된 새 리스트. `key` 함수를 주면 key 값으로 비교합니다. 원본은 바꾸지 않습니다. |
| `round(x[, ndigits])` | 가장 가까운 정수로 반올림합니다. `ndigits`를 주면 그 자릿수까지 남깁니다(`ndigits` 없으면 정수). |
| `input([prompt])` | 표준 입력에서 한 줄을 읽습니다(끝 개행 제거). `prompt`는 먼저 출력합니다. 입력이 끝났으면 `EOFError`. |

`print`/`println`은 문자열이 아닌 값도 자동으로 문자열화합니다. 리스트, 딕셔너리,
세트는 각각 `[...]`, `{...}`, `{...}` 형태로 출력됩니다.

```aster
println("x =", 1, "y =", 2);      # x = 1 y = 2
println([1, 2, 3]);               # [1, 2, 3]
println({"a": 1});                # {'a': 1}
println(3.5);                     # 3.5
```

## 2. 문자열

문자열은 내부적으로 UTF-8이지만 `length()`, `charAt()`, 인덱스, 슬라이스,
`substring`, `chars()`, `for-in`은 모두 **유니코드 코드포인트 단위**입니다.
ASCII만 쓰는 경우 바이트 단위와 같습니다.

| 메서드 | 설명 |
| --- | --- |
| `upper()` / `toUpperCase()` | ASCII 소문자를 대문자로. |
| `lower()` / `toLowerCase()` | ASCII 대문자를 소문자로. |
| `strip()` / `trim()` | 양끝 공백 제거. |
| `split([sep])` | 구분자로 나눈 리스트. 구분자를 생략하면 공백 기준. |
| `join(iterable)` | 문자열 원소를 이 문자열로 연결. |
| `replace(old, new[, count])` | 부분 문자열 치환. |
| `startswith(prefix)` / `endswith(suffix)` | 접두/접미 검사. |
| `find(sub)` / `indexOf(sub)` | 첫 위치. 없으면 `-1`. |
| `lastIndexOf(sub)` / `rfind(sub)` | 마지막 위치. 없으면 `-1`. |
| `count(sub)` | 겹치지 않는 등장 횟수. |
| `to_int()` | 정수로 변환. 실패하면 `ValueError`. |
| `to_float()` | 실수로 변환. 실패하면 `ValueError`. |
| `length()` | 코드포인트 수. |
| `charAt(i)` | i번째 코드포인트를 1글자 문자열로. 범위를 벗어나면 `IndexError`. |
| `substring(start[, end])` | `[start, end)` 구간(코드포인트 기준). |
| `isdigit()` / `isalpha()` / `isspace()` | ASCII 기준 검사. 빈 문자열은 `false`. |
| `chars()` | 코드포인트 단위 문자 리스트. |
| `toString()` | 자기 자신. |
| `format(...args)` | `str.format` 스타일 치환. `{}`/`{0}` 위치 필드와 `{1:.2f}` 같은 서식을 지원합니다(이름 필드는 지원하지 않음). |

`.format()`의 서식부(`: 뒤`)는 채움/정렬(`<`, `>`, `^`, `=`), 부호(`+`, `-`,
공백), `0` 채움, 폭, 정밀도, 타입(`s`, `d`, `x`, `X`, `o`, `b`, `f`, `e`, `E`,
`g`, `G`, `%`)을 받습니다. `{{`와 `}}`는 각각 리터럴 중괄호입니다.

```aster
println("{0} + {1} = {2}".format(2, 3, 5));   # 2 + 3 = 5
println("{:.2f}".format(3.14159));            # 3.14
println("{:>6}".format("hi"));                #     hi
```

## 3. 리스트

| 메서드 | 설명 |
| --- | --- |
| `append(x)` | 끝에 추가. |
| `extend(iterable)` | 반복 가능한 값의 원소를 모두 추가. |
| `insert(i, x)` | i 위치에 삽입. 음수/초과 인덱스는 보정됩니다. |
| `pop([i])` | i 위치(기본 마지막)를 제거하고 돌려줍니다. |
| `remove(x)` | 첫 번째 x를 제거. 없으면 `ValueError`. |
| `clear()` | 모든 원소 제거. |
| `index(x)` | 첫 위치. 없으면 `ValueError`. |
| `count(x)` | 등장 횟수. |
| `copy()` | 얕은 복사. |
| `map(f)` | 각 원소에 `f`를 적용한 새 리스트. |
| `filter(f)` | `f`가 true인 원소만 담은 새 리스트. |
| `reverse()` | 제자리 뒤집기. |
| `sort([key])` | 제자리 오름차순 정렬. `key` 함수를 주면 각 원소를 key 값으로 비교합니다. 비교할 수 없는 값이 있으면 `TypeError`. |

`sort`의 key 함수는 `f(x) -> 비교가능타입` 형태의 함수나 람다입니다.

```aster
var words = ["ccc", "a", "bb"];
words.sort(s -> int => s.length());        # ["a", "bb", "ccc"]
```

`map`/`filter`는 원본을 바꾸지 않고 새 리스트를 돌려줍니다.

## 4. 튜플

| 메서드 | 설명 |
| --- | --- |
| `index(x)` | 첫 위치. 없으면 `ValueError`. |
| `count(x)` | 등장 횟수. |

튜플은 불변이므로 원소를 바꾸는 메서드가 없습니다.

## 5. 딕셔너리

| 메서드 | 설명 |
| --- | --- |
| `get(key[, default])` | 값을 읽습니다. 없으면 `default`(기본 `null`). |
| `keys()` / `values()` / `items()` | 키/값/(키, 값) 리스트. |
| `pop(key[, default])` | 키를 제거하고 값을 돌려줍니다. 없으면 `default`, 그것도 없으면 `KeyError`. |
| `setdefault(key[, default])` | 있으면 값을, 없으면 `default`를 저장하고 돌려줍니다. |
| `update(other)` | 딕셔너리 또는 `(키, 값)` 쌍의 반복으로 갱신. |
| `clear()` / `copy()` | 비우기 / 얕은 복사. |

키는 해시 가능한 값(숫자, 문자열, 튜플 등)이어야 합니다. 리스트, 딕셔너리, 세트를
키로 쓰면 `TypeError`(unhashable)입니다.

## 6. 세트와 frozenset

| 메서드 | 설명 |
| --- | --- |
| `add(x)` | 원소 추가. |
| `remove(x)` | 제거. 없으면 `KeyError`. |
| `discard(x)` | 제거. 없어도 오류 없음. |
| `pop()` | 임의의 원소를 제거하고 돌려줍니다. 비어 있으면 `KeyError`. |
| `clear()` | 비우기. |
| `copy()` | 복사. |
| `union(other)` | 합집합. |
| `intersection(other)` | 교집합. |
| `difference(other)` | 차집합. |
| `issubset(other)` | 부분집합 여부. |
| `issuperset(other)` | 상위집합 여부. |

`frozenset`은 불변이므로 `add`, `remove`, `discard`, `pop`, `clear`가 없습니다.
`remove`/`discard`/`pop`/`clear`는 원본을 바꾸고, `union`/`intersection`/`difference`는
새 값을 돌려줍니다.

## 7. System

`System`은 `import System;`으로 가져오는 내장 모듈입니다. import하지 않으면
`NameError`입니다.

| 멤버 | 설명 |
| --- | --- |
| `System.out` | 표준 출력. `print`, `println`. |
| `System.stdout` | `System.out`과 같은 객체. |
| `System.err` | 표준 에러. `print`, `println`. |
| `System.in` | 표준 입력. `Scanner`에 넘깁니다. |
| `System.args` | 스크립트 이름 뒤의 명령줄 인자 리스트. |
| `System.exit([code])` | 프로세스를 종료합니다. 기본 코드는 0. |
| `System.setRecursionLimit(n)` | 최대 호출 깊이를 바꿉니다(1 이상 100000 이하). 기본값 512. |
| `System.getRecursionLimit()` | 현재 최대 호출 깊이를 돌려줍니다. |

```aster
import System;

System.out.println("Hello");
System.err.println("경고");
System.setRecursionLimit(2000);
System.exit(2);
```

`import System.stdout.*;`는 표준 출력의 `print`/`println`을, `import
System.stderr.*;`는 표준 에러의 `print`/`println`을 현재 범위에 바로 정의합니다.

## 8. Scanner

Java의 `Scanner`와 같은 사용법입니다. 표준 입력에서 토큰을 읽습니다.

```aster
Scanner sc = new Scanner(System.in);
while (sc.hasNextLine()) {
    str line = sc.nextLine();
    println(line);
}
sc.close();
```

| 메서드 | 설명 |
| --- | --- |
| `nextLine()` | 다음 줄 전체. 입력이 끝나면 `EOFError`. |
| `next()` | 다음 공백 구분 토큰. 입력이 끝나면 `EOFError`. |
| `nextInt()` | 다음 토큰을 정수로. 실패하면 `NumberFormatError`. |
| `nextFloat()` | 다음 토큰을 실수로. 실패하면 `NumberFormatError`. |
| `hasNextLine()` | 읽을 줄이 남아 있는지. |
| `hasNext()` | 다음 줄이 남아 있는지 (`hasNextLine()`과 같은 구현). |
| `close()` | 닫습니다(현재는 아무 동작도 하지 않음). |

생성자 인자(`System.in`)는 현재 무시됩니다. 입력은 항상 표준 입력에서 읽습니다.

## 9. Files

정적 메서드처럼 호출합니다. 경로는 문자열입니다.

```aster
Files.writeString("out.txt", "hello\n");
str text = Files.readString("out.txt");
var lines = Files.readLines("out.txt");
if (Files.exists("out.txt")) {
    Files.delete("out.txt");
}
```

| 메서드 | 설명 |
| --- | --- |
| `readString(path)` | 파일 전체를 문자열로. 열 수 없으면 `OSError`. |
| `writeString(path, text)` | 파일을 덮어씁니다. 쓸 수 없으면 `OSError`. |
| `readLines(path)` | 줄 단위 리스트. `\r\n`도 처리합니다. |
| `exists(path)` | 경로(파일 또는 디렉터리)가 존재하는지 검사합니다. |
| `isFile(path)` / `isDirectory(path)` | 일반 파일/디렉터리 여부. |
| `length(path)` | 파일 크기(바이트). 없으면 0. |
| `list(path)` | 디렉터리의 이름 목록. 실패하면 `OSError`. |
| `mkdir(path)` | 디렉터리 하나를 만듭니다. 성공 여부를 돌려줍니다. |
| `mkdirs(path)` | 필요한 상위 디렉터리를 모두 만듭니다. |
| `remove(path)` | 파일 또는 빈 디렉터리를 지우고 성공 여부를 돌려줍니다. |
| `rename(from, to)` | 이름을 바꾸고 성공 여부를 돌려줍니다. |
| `absolutePath(path)` | 현재 작업 디렉터리 기준 절대 경로. |
| `separator()` | 플랫폼 경로 구분자(`/` 또는 `\`). |
| `delete(path)` | 파일을 지웁니다. 실패하면 `OSError`. |

경로·디렉터리 작업은 Java의 `File`과 같은 클래스인 `aster.io.File`로 쓰는 것이
권장됩니다(아래 11절).

## 10. 예외

모든 내장 예외는 `Exception`의 하위입니다. `catch`는 부모 방향으로 매칭합니다.

```text
Exception
├── ArithmeticError
│   ├── ZeroDivisionError
│   └── OverflowError
├── RuntimeError
│   ├── NullPointerError
│   └── RecursionError
├── LookupError
│   ├── IndexError
│   └── KeyError
├── ImportError
│   └── ModuleNotFoundError
├── AssertionError
├── ValueError
├── TypeError
├── NameError
├── AttributeError
├── OSError
├── EOFError
├── IOException
├── NumberFormatError
├── NotImplementedError
├── NoSuchElementError
├── RegexError
└── StopIteration
```

사용자 예외는 `exception 이름 [extends 부모];`로 선언합니다. 부모를 생략하면
`Exception`을 상속합니다.

## 11. 표준 라이브러리 모듈

`Lib/aster/` 아래에 Aster로 작성된 표준 모듈이 있습니다. 탐색 경로와 export
규칙은 [language.md](language.md) 12절을 봅니다.

```aster
import aster.math;
println(math.sqrt(16.0));      # 4.0

from aster.time import seconds;
println(seconds() > 0);

import aster.json as jsonmod;
str text = jsonmod.stringify({"a": 1});
```

| 모듈 | 공개 이름 |
| --- | --- |
| `aster.math` | `PI`, `E`, `TAU`, libm 함수(`sqrt`, `pow`, `sin`, `cos`, `floor`, `round`, …), `abs`, `degrees`, `radians`, `isclose`, `sign`, `clamp`, `isFinite`, `factorial`, `gcd`, `lcm`, `isPrime` |
| `aster.os` | `env`, `setEnv`, `cwd`, `pid`, `platform`, `run`, `exit` |
| `aster.io` | `readFile`, `writeFile`, `readLines`, `exists`, `deleteFile`, `print`, `println`, `eprintln` |
| `aster.io.File` | `File` 클래스 |
| `aster.lang.String` | `StringBuilder` 클래스와 문자열 헬퍼 함수 |
| `aster.util.Deque` | `Deque<T>` 클래스 |
| `aster.util.regex.Pattern` | `Pattern` 클래스 (Java 스타일 정규식) |
| `aster.util.regex.Matcher` | `Matcher` 클래스 |
| `aster.util.regex.Native` | 파이썬 `re`와 동일한 저수준 함수 (`compile`, `exec`, `findall`, `split`, `sub`, `escape`, …) |
| `aster.time` | `millis`, `seconds`, `cpuMillis`, `sleepMillis`, `sleep`, `formatTime`, `formatMillis`, `formatNow`, `isoDate`, `isoDateTime` |
| `aster.json` | `parse`, `stringify`, `prettify` |

`aster.lang`, `aster.util`(컬렉션·유틸·스트림·함수 인터페이스), `aster.io`
스트림, `aster.math`(BigInteger/BigDecimal), `aster.text`, `aster.time`
날짜·시각 타입은 아래 12절에서 정리합니다.

`import a.b.C;`는 모듈 전체가 아니라 모듈이 공개한 이름 `C`를 직접 가져옵니다
(Java의 `import a.b.C;`와 같습니다). `import a.b;`처럼 마지막 이름이 공개
선언과 같지 않으면 모듈 객체를 가져옵니다.

### aster.io.File

Java `java.io.File`과 같은 경로·디렉터리 API입니다.

```aster
import aster.io.File;

File f = new File("out/notes.txt");
println(f.getName());
println(f.getParent());
println(f.getAbsolutePath());
if (f.exists()) {
    println(f.length());
}
f.delete();
```

| 메서드 | 설명 |
| --- | --- |
| `getPath()` | 생성자에 넘긴 경로 문자열. |
| `getAbsolutePath()` | 절대 경로. |
| `getName()` / `getParent()` | 마지막 이름/상위 경로. `getParent()`는 없으면 `null`. |
| `exists()` / `isFile()` / `isDirectory()` | 존재 여부와 종류. |
| `length()` | 파일 크기(바이트). |
| `list()` | 디렉터리 이름 목록. 디렉터리가 아니면 빈 리스트. |
| `mkdir()` / `mkdirs()` | 디렉터리 생성. |
| `delete()` / `renameTo(dest)` | 삭제/이름 변경. 성공 여부를 돌려줍니다. |
| `File.separator()` | 플랫폼 경로 구분자. |

### aster.util.Deque

Java `ArrayDeque`와 같은 양방향 큐입니다. 앞/뒤 연산은 분할 상환 O(1)입니다.

```aster
import aster.util.Deque;

Deque<str>* d = new Deque<str>();
d.addLast("b");
d.addFirst("a");
println(d);              # ['a', 'b']
println(d.removeFirst());
println(d.peekLast());
```

| 메서드 | 설명 |
| --- | --- |
| `addFirst(x)` / `push(x)` / `addLast(x)` / `add(x)` | 앞/뒤에 추가. |
| `offerFirst(x)` / `offerLast(x)` | 추가하고 `true`. |
| `removeFirst()` / `removeLast()` | 꺼내고 제거. 비어 있으면 `NoSuchElementError`. |
| `pop()` | `removeFirst()`와 같습니다. |
| `pollFirst()` / `pollLast()` | 꺼내고 제거. 비어 있으면 `null`. |
| `peekFirst()` / `peekLast()` | 제거하지 않고 읽음. 비어 있으면 `null`. |
| `get(i)` / `set(i, x)` | 인덱스 접근. 범위를 벗어나면 `IndexError`. |
| `contains(x)` | 포함 여부. |
| `size()` / `isEmpty()` / `clear()` | 크기/비었는지/비우기. |
| `toList()` | 앞에서 뒤 순서의 리스트. |

`len(d)`와 `println(d)`도 동작합니다(`__len__`, `__str__`).

### aster.lang.String

`StringBuilder`는 불변 문자열을 조각으로 모았다가 마지막에 한 번만 연결합니다.
`append`는 분할 상환 O(1), `toString`은 O(n)입니다. 헬퍼 함수도 함께 공개합니다.

```aster
from aster.lang.String import StringBuilder;
from aster.lang.String import padStart;

StringBuilder sb = new StringBuilder();
sb.append("hello").append(" ").appendLine("world");
println(sb.toString());              # hello world\n
println(padStart("7", 3, "0"));      # 007
```

| 멤버 | 설명 |
| --- | --- |
| `append(s)` | 문자열을 덧붙이고 `self`를 돌려줍니다(체이닝). |
| `appendChar(ch)` / `appendLine(s)` | 한 글자/문자열 + 개행을 덧붙입니다. |
| `newLine()` | 개행을 덧붙입니다. |
| `length()` / `isEmpty()` | 현재 문자열 길이/비었는지. |
| `clear()` | 비웁니다. |
| `toString()` | 지금까지 모은 문자열. `__str__`도 같습니다. |
| `reverse()` | 제자리에서 뒤집고 `self`를 돌려줍니다. |
| `__len__()` | `len(sb)` 지원. |

공개 헬퍼 함수:

| 함수 | 설명 |
| --- | --- |
| `repeat(s, count)` | 문자열을 `count`번 반복합니다. |
| `padStart(s, width, fill)` / `padEnd(s, width, fill)` | 폭에 맞춰 앞/뒤를 `fill`로 채웁니다. |
| `capitalize(s)` | 첫 글자만 대문자로 바꿉니다. |
| `isBlank(s)` | 공백만으로 이루어졌는지 검사합니다. |
| `lines(s)` | `\n` 기준으로 나눈 리스트. |
| `collapseSpaces(s)` | 연속 공백을 하나로 접고 앞뒤를 정리합니다. |

### aster.time

시계와 지연, 그리고 `strftime` 패턴 포매팅을 제공합니다.

```aster
from aster.time import formatNow;
from aster.time import isoDate;

println(formatNow("%Y-%m-%d %H:%M:%S"));
println(isoDate(0));        # 1970-01-01 (로컬 시간대)
```

| 함수 | 설명 |
| --- | --- |
| `millis()` | 에포크 기준 밀리초. |
| `seconds()` | 에포크 기준 초. |
| `cpuMillis()` | 프로세스가 사용한 CPU 시간(ms). |
| `sleepMillis(ms)` / `sleep(sec)` | 지정 시간만큼 지연합니다. |
| `formatTime(epochSeconds, pattern)` | 로컬 시각을 `strftime` 패턴으로 포맷합니다. |
| `formatMillis(epochMillis, pattern)` | 밀리초 기준으로 포맷합니다. |
| `formatNow(pattern)` | 현재 시각을 포맷합니다. |
| `isoDate(epochSeconds)` | `%Y-%m-%d` 형식. |
| `isoDateTime(epochSeconds)` | `%Y-%m-%dT%H:%M:%S` 형식. |

### aster.util.regex

정규식 엔진은 **파이썬 `re`와 같은 문법과 의미**를 따릅니다. lookaround,
원자 그룹(`(?>…)`), 소유격 수량자(`a++`), 조건부 그룹(`(?(1)…)`), 이름 있는
그룹(`(?P<name>…)`), 인라인 플래그(`(?i)`), `\A`/`\z`/`\b`, 유니코드
`\w`/`\d`/`\s`까지 Python 3와 동일하게 동작합니다.

```aster
import aster.util.regex.Pattern;

Pattern* p = Pattern.compile("(?P<word>\\w+)");
Matcher* m = p->matcher("hello world");
while (m->find()) {
    println(m->group("word"));
}
```

`Pattern`은 Java `java.util.regex.Pattern`과 같은 모양입니다.

| 멤버 | 설명 |
| --- | --- |
| `Pattern.compile(regex)` / `Pattern.compile(regex, flags)` | 컴파일. 잘못된 패턴은 `RegexError`. |
| `CASE_INSENSITIVE`(`IGNORECASE`), `MULTILINE`, `DOTALL`, `UNICODE`, `VERBOSE`, `ASCII` | 플래그 상수 (파이썬 `re`와 같은 값). |
| `matcher(input)` | `Matcher` 생성. |
| `matches(input)` | 입력 전체 일치(Python `fullmatch`). |
| `split(input)` / `split(input, limit)` | 분리. 캡처 그룹이 있으면 Python처럼 그룹도 결과에 섞입니다. |
| `findAll(input)` | Python `re.findall`과 같은 결과. |
| `replaceAll(input, repl)` / `replaceFirst(input, repl)` | 치환. 여기서는 Java식 `$1`, `${name}`을 씁니다. |
| `Pattern.quote(text)` | 메타문자를 이스케이프한 리터럴 문자열. |
| `pattern()` / `flags()` / `groupCount()` / `groupIndex()` | 메타 정보. |

`Matcher`는 `find()`/`find(start)`/`matches()`/`lookingAt()`,
`group()`/`group(i)`/`group(name)`, `start`/`end`(번호·이름), `groupCount()`,
`reset()`/`reset(input)`, `replaceAll`/`replaceFirst`, `pattern()`, `inputText()`를
제공합니다.

`aster.util.regex.Native`는 래퍼 없이 엔진을 직접 씁니다. 함수는 모두 패턴 id
(`compile` 반환값)나 매치 id(`exec` 반환값)를 받습니다.

```aster
import aster.util.regex.Native;

int re = Native.compile("(\\d+)-(\\d+)", 0);
int mid = Native.exec(re, "10-20", 0, 0);
println(Native.group(mid, 1));       # 10
println(Native.findall(re, "1-2 3-4"));
```

`Native`의 `sub` 치환 문자열은 Python `re.sub`와 같은 문법(`\1`, `\g<name>`)을
쓰고, `$`는 리터럴입니다. `Pattern`/`Matcher`의 `replaceAll`/`replaceFirst`만
Java식 `$1`, `${name}`을 Python 문법으로 번역합니다.

알려진 차이:
- `\N{CHARACTER NAME}`은 지원하지 않습니다. 형식이 틀린 `\N`은 Python과 같은
  오류 메시지를 냅니다.
- 대소문자 무시는 단순 case folding입니다(다중 문자 fold 미지원).
- `Pattern.split`에서 참여하지 않은 캡처 그룹은 Python의 `None` 대신 `""`로
  나옵니다.

- `aster.math`의 수학 함수는 [extern](#extern)으로 libm 심볼을 직접 호출합니다.
- `aster.json`의 `parse`/`stringify`는 `var` 값을 다룹니다. 객체는 딕셔너리,
  배열은 리스트, `null`은 null로 변환됩니다. 문법 오류는 모듈 내부의 `JsonError`
  예외로 던져지므로 `catch (Exception e)`로 잡을 수 있습니다.
- `aster.os.run`은 명령을 실행하고 종료 코드를 돌려줍니다.

## 12. java.base 스타일 패키지

Java SE 11의 `java.base` 모듈 중 Aster로 옮길 수 있는 영역을 `aster.*`
패키지로 제공합니다. 클래스·메서드 이름은 Java API와 같게 유지하고,
패키지 이름만 `java`를 `aster`로 바꿨습니다.

```aster
import aster.util.ArrayList;
import aster.util.HashMap;
import aster.util.stream.Stream;

ArrayList<int> xs = new ArrayList<int>();
xs.add(1);
xs.add(2);
println(xs.size());                  # 2

HashMap<str, int> m = new HashMap<str, int>();
m.put("a", 1);
println(m.get("a"));                 # 1

Stream<int> s = Stream.of([1, 2, 3, 4]);
println(s.filter((int x) -> bool => x % 2 == 0).toList());  # [2, 4]
```

Aster 쪽 사정에 맞춰 다음과 같이 적응했습니다.

- **계약 인터페이스는 `any` 기반으로 선언했습니다.** 언어는 제네릭 인터페이스와
  람다 대입을 지원하지만(언어 문서 §6, §7), `Collection`, `Iterable`,
  `Comparator`, `Function` 같은 java.base 계약은 여러 구현 클래스가 공유해야
  해서 타입 인자 없이 두었습니다. 사용자 코드에서는
  `interface Mapper<T> { ... }`처럼 제네릭 인터페이스를 직접 만들 수 있습니다.
- **람다는 메서드 서명이 맞으면 인터페이스 타입 값으로 쓸 수 있습니다.**
  스트림과 유틸리티 API는 Java와 같은 함수 인터페이스(`Function`,
  `Predicate` 등)와 `fn(T) -> R` 오버로드를 모두 제공합니다.
- **`char`/`byte` 타입이 없습니다.** 문자열은 유니코드 코드포인트이고,
  바이트는 `list<int>`(0..255)로 다룹니다. `Character`는 한 글자 문자열을
  받습니다.
- **비트 연산자가 없습니다.** 정수 비트 유틸리티와 `BitSet`은 산술
  연산으로 구현했으며, `BitSet.and/or/xor/andNot`은 키워드와 겹치지 않도록
  `andWith/orWith/xorWith/andNotWith`로 이름을 바꿨습니다.
- **`enum`은 메서드를 가질 수 있습니다.** `DayOfWeek`과 `Month`는
  `getValue`, `plus`, `minus`, `fullName`, `length`, `firstDayOfYear` 같은
  Java 인스턴스 메서드를 enum 메서드로 제공합니다. 정적 생성
  (`DayOfWeek.of`)에 해당하는 `dayOfWeekOf`/`monthOf`만 컴패니언 함수로
  둡니다. `RoundingMode`는 Java에도 인스턴스 메서드가 없어 이름 조회만
  컴패니언 함수로 제공합니다.
- **생성자는 여러 개를 오버로드할 수 있습니다.** java.base 포팅 클래스는
  Java와 같은 생성자를 제공하고, 추가 입력 형태는 `Random.withSeed(seed)`
  같은 정적 팩토리로도 제공합니다.
- **JVM 전용 영역은 제외했습니다.** 리플렉션(`java.lang.reflect`,
  `MethodHandle`), 모듈 시스템, 클래스 로더, 스레드/동시성
  (`java.util.concurrent`), `java.nio` 채널, 네트워크, 보안·암호,
  `java.util.zip`, `java.util.jar` 등은 Aster 런타임에 대응하는 기반이
  없어 구현하지 않았습니다. `java.lang.System`은 기존 `System` 모듈을
  사용합니다.

### aster.lang

| 모듈 | 공개 이름 |
| --- | --- |
| `aster.lang.Object` | `Object` 클래스 |
| `aster.lang.Number` | `Number` 추상 클래스 |
| `aster.lang.Integer` | `Integer` (`MAX_VALUE`, `MIN_VALUE`, `SIZE`, `BYTES`, `parseInt`, `valueOf`, `toString`, `toHexString`, `toBinaryString`, `bitCount`, `compare`, `signum`, `sum`, `max`, `min`, …) |
| `aster.lang.Long` | `Long` (같은 계열의 64비트 API) |
| `aster.lang.Double` / `Float` | `Double`/`Float` (`NaN`, `POSITIVE_INFINITY`, `parseDouble`, `isNaN`, `isInfinite`, `compare`, …) |
| `aster.lang.Boolean` | `Boolean` (`TRUE`/`FALSE`, `parseBoolean`, `logicalAnd/Or/Xor/Not`, `valueOf`) |
| `aster.lang.Character` | `Character` (`MIN_VALUE`, `MAX_VALUE`, `isDigit`, `isLetter`, `isWhitespace`, `toUpperCase`, `toLowerCase`, `getNumericValue`, …) |
| `aster.lang.Math` | `Math` (`PI`, `E`, 삼각·지수·로그, `abs`, `max`, `min`, `floor`, `ceil`, `round`, `rint`, `hypot`, `signum`, `addExact`, `floorDiv`, `floorMod`, `copySign`, `random`) |
| `aster.lang.String` | `StringBuilder`와 문자열 헬퍼 |
| `aster.lang.StringBuffer` | `StringBuffer` (동기화 없는 `StringBuilder` 별칭) |
| `aster.lang.CharSequence` | `CharSequence` 인터페이스 |
| `aster.lang.Comparable` | `Comparable` 인터페이스 |
| `aster.lang.Iterable` | `Iterable` 인터페이스 |
| `aster.lang.Runnable` | `Runnable` 인터페이스 |
| `aster.lang.AutoCloseable` | `AutoCloseable` 인터페이스 |
| `aster.lang.Cloneable` | `Cloneable` 인터페이스 |

### aster.util

| 모듈 | 공개 이름 |
| --- | --- |
| `aster.util.Collection` / `List` / `Set` / `Map` / `Queue` / `Deque` | 컬렉션 계약 인터페이스 |
| `aster.util.Iterator` / `ListIterator` / `ListIteratorImpl` | 반복자 |
| `aster.util.Comparator` | `Comparator` 인터페이스 |
| `aster.util.ArrayList` | 배열 기반 `List` (`add`, `get`, `set`, `remove`, `indexOf`, `subList`, `sort`, `iterator`, …) |
| `aster.util.LinkedList` | 연결 리스트 `List`/`Queue` |
| `aster.util.HashMap` / `LinkedHashMap` / `TreeMap` | `Map` 구현 (`put`, `get`, `remove`, `keySet`, `values`, `entrySet`, `getOrDefault`, `putIfAbsent`) |
| `aster.util.HashSet` / `LinkedHashSet` / `TreeSet` | `Set` 구현 |
| `aster.util.Stack` | `Stack` (`push`, `pop`, `peek`, `search`) |
| `aster.util.MapEntry` | `MapEntry<K, V>` (`getKey`, `getValue`, `setValue`) |
| `aster.util.CollectionList` | 컬렉션을 리스트처럼 다루는 어댑터 |
| `aster.util.Arrays` | `toString`, `deepToString`, `sort`, `binarySearch`, `copyOf`, `copyOfRange`, `fill`, `asList`, `equals`, `hashCode` |
| `aster.util.Collections` | `emptyList`, `singletonList`, `nCopies`, `sort`, `reverse`, `shuffle`, `swap`, `min`, `max`, `frequency`, `binarySearch`, `unmodifiableList` |
| `aster.util.Objects` | `isNull`, `nonNull`, `equals`, `deepEquals`, `requireNonNull`, `toString`, `hashCode`, `hash`, `compare` |
| `aster.util.Optional` / `OptionalInt` / `OptionalLong` / `OptionalDouble` | `empty`, `of`, `ofNullable`, `isPresent`, `get`, `orElse`, `orElseGet`, `orElseThrow`, `map`, `filter`, `ifPresent` |
| `aster.util.Random` | `Random`, `Random.withSeed(seed)`, `nextInt`, `nextLong`, `nextDouble`, `nextBoolean`, `nextGaussian`, `setSeed` |
| `aster.util.UUID` | `randomUUID`, `fromString`, `toString`, `getMostSignificantBits`, `getLeastSignificantBits` |
| `aster.util.StringTokenizer` | `StringTokenizer` (`hasMoreTokens`, `nextToken`, `countTokens`) |
| `aster.util.BitSet` | `BitSet` (`set`, `clear`, `get`, `flip`, `cardinality`, `nextSetBit`, `andWith`, `orWith`, `xorWith`, `andNotWith`) |
| `aster.util.Properties` | `Properties` (`setProperty`, `getProperty`, `propertyNames`, `load`, `store`) |
| `aster.util.Locale` | `Locale` (`getDefault`, `getLanguage`, `getCountry`, `forLanguageTag`, `toString`) |
| `aster.util.Formatter` | Java 스타일 `%` 포매터 |
| `aster.util.Date` | `Date` (`Date.fromMillis`, `getTime`, `before`, `after`, `compareTo`) |
| `aster.util.function.*` | `Function`, `BiFunction`, `Consumer`, `BiConsumer`, `Supplier`, `Predicate`, `BiPredicate`, `UnaryOperator`, `BinaryOperator`, `IntFunction`, `IntPredicate`, `IntConsumer`, `IntSupplier`, `IntUnaryOperator`, `IntBinaryOperator`, `BooleanSupplier`, `DoubleSupplier`, `DoublePredicate`, `DoubleUnaryOperator`, `LongSupplier`, `LongPredicate`, `LongUnaryOperator`, `ToIntFunction`, `ToDoubleFunction`, `ToLongFunction` |
| `aster.util.stream.*` | `Stream`, `IntStream`, `LongStream`, `DoubleStream`, `Collector`, `Collectors` |

`Stream`은 `of`, `empty`, `concat`, `iterate`, `generate`, `filter`, `map`,
`flatMap`, `distinct`, `sorted`, `peek`, `limit`, `skip`, `forEach`, `count`,
`anyMatch`, `allMatch`, `noneMatch`, `findFirst`, `findAny`, `reduce`,
`toList`, `toSet`, `collect`을 제공합니다. `Collectors`에는 `toList`, `toSet`,
`joining`, `counting`, `mapping`, `summingInt`, `averagingDouble`,
`groupingBy`가 있습니다.

```aster
import aster.util.stream.Collectors;

list<int> doubled = Stream.of([1, 2, 3])
    .map((int x) -> int => x * 2)
    .collect(Collectors.toList());
println(doubled);                    # [2, 4, 6]
```

### aster.io

`Reader`/`Writer` 계층과 바이트 배열 스트림, 데이터 스트림을 제공합니다.
바이트는 `list<int>`로 주고받습니다.

| 모듈 | 공개 이름 |
| --- | --- |
| `aster.io.Closeable` / `Flushable` / `Serializable` / `Readable` | 기본 인터페이스 |
| `aster.io.Reader` / `Writer` | 문자 스트림 추상 클래스 |
| `aster.io.StringReader` / `StringWriter` | 메모리 문자열 스트림 |
| `aster.io.BufferedReader` / `BufferedWriter` | 버퍼링 래퍼 |
| `aster.io.PrintWriter` | `print`/`println`/`printf`/`format`/`flush` |
| `aster.io.ByteArrayInputStream` / `ByteArrayOutputStream` | 바이트 배열 스트림 |
| `aster.io.DataInputStream` / `DataOutputStream` | 빅엔디언 정수·실수 직렬화 (`readInt`, `writeInt`, `readDouble`, …) |
| `aster.io.Codec` | UTF-8 인코딩·디코딩 헬퍼 |

기존 `aster.io.readFile`/`writeFile`/`print`/`println`은 그대로 사용할 수
있습니다.

### aster.math

| 모듈 | 공개 이름 |
| --- | --- |
| `aster.math.BigInteger` | 임의 정밀도 정수 (`valueOf`, `fromRadix`, `add`, `subtract`, `multiply`, `divide`, `remainder`, `divideAndRemainder`, `mod`, `pow`, `gcd`, `abs`, `negate`, `compareTo`, `bitLength`, `shiftLeft`, `shiftRight`, `testBit`, `setBit`, `isProbablePrime`) |
| `aster.math.BigDecimal` | 임의 정밀도 소수 (`valueOf`, `add`, `subtract`, `multiply`, `divide`, `setScale`, `pow`, `stripTrailingZeros`, `movePointLeft`, `movePointRight`, `compareTo`, `scale`, `precision`) |
| `aster.math.MathContext` | 유효 자릿수와 반올림 모드 |
| `aster.math.RoundingMode` | `CEILING`, `DOWN`, `FLOOR`, `HALF_DOWN`, `HALF_EVEN`, `HALF_UP`, `UNNECESSARY`, `UP` |

```aster
import aster.math.BigInteger;

BigInteger a = BigInteger.valueOf(123456789);
BigInteger b = BigInteger.valueOf(987654321);
println(a.multiply(b));              # 121932631112635269
```

### aster.text

| 모듈 | 공개 이름 |
| --- | --- |
| `aster.text.ParseException` | 파싱 실패 예외 |
| `aster.text.DateFormat` | 날짜·시각 포맷 계약 |
| `aster.text.SimpleDateFormat` | `yyyy`, `MM`, `dd`, `HH`, `mm`, `ss`, `SSS` 패턴 |
| `aster.text.NumberFormat` | 숫자 포맷 계약 (`getInstance`, `format`, `parse`) |
| `aster.text.DecimalFormat` | 패턴 기반 10진 포맷 (`#,##0.00` 등) |
| `aster.text.MessageFormat` | `{0}`, `{1}` 자리표시자 치환 |

### aster.time

`java.time` 계열의 불변 날짜·시각 타입입니다. 모든 값 타입은 `equals`,
`hashCode`, `toString`, `compareTo`를 제공하며 `__str__`로 바로 출력할 수
있습니다.

| 모듈 | 공개 이름 |
| --- | --- |
| `aster.time.Instant` | `now`, `ofEpochMilli`, `ofEpochSecond`, `toEpochMilli`, `plusMillis`, `plusSeconds`, `isBefore`, `isAfter` |
| `aster.time.Duration` | `ofSeconds`, `ofMillis`, `ofMinutes`, `ofHours`, `ofDays`, `plus`, `minus`, `toMillis`, `toSeconds`, `getSeconds` |
| `aster.time.Period` | `ofDays`, `ofMonths`, `ofYears`, `of`, `plus`, `minus`, `getDays`, `getMonths`, `getYears` |
| `aster.time.LocalDate` | `now`, `of`, `ofEpochDay`, `parse`, `plusDays/Weeks/Months/Years`, `minus*`, `withYear/Month/DayOfMonth`, `getYear`, `getMonthValue`, `getDayOfMonth`, `getDayOfWeek`, `getDayOfYear`, `lengthOfMonth`, `isLeapYear`, `toEpochDay`, `isBefore`, `isAfter`, `atStartOfDay`, `atTime` |
| `aster.time.LocalTime` | `now`, `of`, `parse`, `plusHours/Minutes/Seconds/Nanos`, `getHour`, `getMinute`, `getSecond`, `getNano`, `isBefore`, `isAfter` |
| `aster.time.LocalDateTime` | `now`, `of`, `parse`, `toLocalDate`, `toLocalTime`, `plusDays/Hours/Minutes`, `isBefore`, `isAfter` |
| `aster.time.Year` / `YearMonth` / `MonthDay` | 연/연월/월일 타입 |
| `aster.time.DayOfWeek` / `Month` | 요일·월 상수와 `dayOfWeekValue`/`dayOfWeekOf`/`dayOfWeekPlus`/`monthValue`/`monthOf`/`monthLength`/`monthPlus` 헬퍼 |
| `aster.time.ZoneId` / `ZoneOffset` | 시간대 식별자와 오프셋 |
| `aster.time.DateTimeParseException` | 파싱 실패 예외 |
| `aster.time.format.DateTimeFormatter` | `ofPattern`, `format`, `parseDate`, `parseTime`, `parseDateTime` |

```aster
import aster.time.LocalDate;
import aster.time.format.DateTimeFormatter;

LocalDate d = LocalDate.of(2026, 11, 13);
println(d.plusDays(30));             # 2026-12-13
println(d.getDayOfWeek());
println(DateTimeFormatter.ofPattern("yyyy/MM/dd").format(d));  # 2026/11/13
```

`aster.time`의 기존 함수(`millis`, `sleep`, `formatNow`, `isoDate` 등)는
위 11절 표에 정리되어 있으며 그대로 유지됩니다.
