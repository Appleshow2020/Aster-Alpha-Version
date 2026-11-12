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

`print`/`println`은 문자열이 아닌 값도 자동으로 문자열화합니다. 리스트, 딕셔너리,
세트는 각각 `[...]`, `{...}`, `{...}` 형태로 출력됩니다.

```aster
println("x =", 1, "y =", 2);      // x = 1 y = 2
println([1, 2, 3]);               // [1, 2, 3]
println({"a": 1});                // {'a': 1}
println(3.5);                     // 3.5
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
| `count(sub)` | 겹치지 않는 등장 횟수. |
| `to_int()` | 정수로 변환. 실패하면 `ValueError`. |
| `to_float()` | 실수로 변환. 실패하면 `ValueError`. |
| `length()` | 코드포인트 수. |
| `charAt(i)` | i번째 코드포인트를 1글자 문자열로. 범위를 벗어나면 `IndexError`. |
| `substring(start[, end])` | `[start, end)` 구간(코드포인트 기준). |
| `isdigit()` / `isalpha()` / `isspace()` | ASCII 기준 검사. 빈 문자열은 `false`. |
| `chars()` | 코드포인트 단위 문자 리스트. |
| `toString()` | 자기 자신. |

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
| `sort()` | 제자리 오름차순 정렬. 비교할 수 없는 값이 있으면 `TypeError`. |

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
| `exists(path)` | 파일이 열리는지 검사합니다. |
| `delete(path)` | 파일을 지웁니다. 실패하면 `OSError`. |

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
└── StopIteration
```

사용자 예외는 `exception 이름 [extends 부모];`로 선언합니다. 부모를 생략하면
`Exception`을 상속합니다.

## 11. 표준 라이브러리 모듈

`Lib/aster/` 아래에 Aster로 작성된 표준 모듈이 있습니다. 탐색 경로와 export
규칙은 [language.md](language.md) 12절을 봅니다.

```aster
import aster.math;
println(math.sqrt(16.0));      // 4.0

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
| `aster.time` | `millis`, `seconds`, `cpuMillis`, `sleepMillis`, `sleep` |
| `aster.json` | `parse`, `stringify`, `prettify` |

- `aster.math`의 수학 함수는 [extern](#extern)으로 libm 심볼을 직접 호출합니다.
- `aster.json`의 `parse`/`stringify`는 `var` 값을 다룹니다. 객체는 딕셔너리,
  배열은 리스트, `null`은 null로 변환됩니다. 문법 오류는 모듈 내부의 `JsonError`
  예외로 던져지므로 `catch (Exception e)`로 잡을 수 있습니다.
- `aster.os.run`은 명령을 실행하고 종료 코드를 돌려줍니다.
