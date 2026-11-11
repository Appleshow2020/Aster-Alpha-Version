# Aster 패키지 관리자

Aster의 패키지 관리자는 프로젝트 단위 의존성을 `aster.pkg` 파일에 기록하고,
실행할 때 그 경로를 모듈 탐색 경로에 추가합니다. 별도 다운로드나 버전 해석은
하지 않습니다. 로컬 디렉터리 기반의 최소 관리자입니다.

## 1. 빠른 시작

```sh
mkdir myapp
cd myapp
aster pkg init myapp
```

`aster.pkg`가 만들어집니다.

```ini
# Aster package manifest
name = myapp
version = 0.1.0

[deps]
```

의존성을 추가합니다.

```sh
aster pkg add util ../shared
```

`../shared`에서 `util.ast` 또는 `util/__init__.ast`를 찾을 수 있어야 합니다.
패키지 디렉터리 자체(`../shared/util`)를 넘겨도 됩니다.

```sh
aster pkg list
aster pkg check
```

이제 소스에서 바로 import할 수 있습니다.

```aster
import util;
println(util.hello());
```

## 2. 명령

| 명령 | 설명 |
| --- | --- |
| `aster pkg init [name]` | 현재 디렉터리에 `aster.pkg`를 만듭니다. 이름 기본값은 `app`. |
| `aster pkg add NAME PATH` | 의존성을 추가합니다. `PATH`는 모듈 루트 또는 패키지 디렉터리. |
| `aster pkg remove NAME` | 의존성을 제거합니다. |
| `aster pkg list` | 이름, 버전, 의존성을 출력합니다. |
| `aster pkg check` | 각 의존성이 실제로 import 가능한지 검사합니다. |
| `aster pkg -h` | 도움말. |

`aster pkg`는 소스를 실행하지 않습니다.

## 3. `aster.pkg` 형식

`key = value` 줄과 `[deps]` 섹션으로 구성됩니다. `#`부터 줄 끝까지는 주석입니다.
값의 양끝에 따옴표를 써도 됩니다.

```ini
# Aster package manifest
name = myapp
version = 1.2.0

[deps]
util = ../shared
network = libs/network
```

- `name`, `version`은 선택입니다.
- `[deps]` 아래의 각 줄은 `별칭 = 경로`입니다. 별칭은 import에 쓰는 모듈 이름과
  같아야 합니다.
- 상대 경로는 **`aster.pkg`가 있는 디렉터리**를 기준으로 해석됩니다.
- `aster pkg add`/`remove`는 파일을 다시 쓸 때 주석과 알 수 없는 키를 보존하지
  않습니다.

## 4. 탐색 규칙

실행 파일은 주 스크립트가 있는 디렉터리에서 위로 올라가며 가장 가까운
`aster.pkg`를 찾습니다(최대 32단계). 그 파일의 각 의존성 경로를 모듈 탐색
경로에 추가합니다.

모듈 탐색 순서는 다음과 같습니다.

1. 주 스크립트의 디렉터리
2. 표준 라이브러리 (`ASTER_HOME/Lib`, 없으면 현재 디렉터리의 `Lib`)
3. `aster.pkg`의 의존성 경로들
4. `ASTER_PATH` 환경 변수의 경로들

`ASTER_PATH`는 Windows에서는 `;`, Unix에서는 `;`와 `:`로 구분합니다.

```sh
export ASTER_PATH=/opt/aster-libs:$HOME/aster-libs
```

`import util;`은 각 경로에서 `util.ast`를 먼저, 그다음 `util/__init__.ast`를
찾습니다. `import a.b;`는 `a/b.ast` 또는 `a/b/__init__.ast`를 찾습니다.

## 5. 패키지 만들기

패키지는 디렉터리와 `__init__.ast`로 구성할 수 있습니다.

```text
shared/
  __init__.ast        # export fn hello() -> str { ... }
  text.ast
```

```aster
// shared/text.ast
export fn shout(str s) -> str { return s.upper() + "!"; }

// shared/__init__.ast
import text;
export fn hello() -> str { return text.shout("hello"); }
```

사용하는 프로젝트에서:

```sh
aster pkg add shared ../shared
```

`shared` 디렉터리의 부모를 저장하므로 `import shared;`가 동작합니다.

## 6. 한계

- 버전 해석, 잠금 파일, 원격 다운로드는 없습니다.
- 의존성의 의존성(전이 의존성)은 자동으로 읽지 않습니다. 필요한 경로를 직접
  추가하거나 `ASTER_PATH`를 씁니다.
- `aster pkg check`는 각 별칭이 최상위 경로에서 import 가능한지만 확인합니다.
- 경로는 실행 시점에 해석되므로 프로젝트를 옮기면 상대 경로를 확인해야 합니다.
