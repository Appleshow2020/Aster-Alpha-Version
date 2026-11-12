# Aster

Aster는 C11로 구현된 정적 타입 프로그래밍 언어입니다.

이 문서는 **설치 방법**만 설명합니다.

## 요구 사항

- C11 컴파일러 (`cc`, `gcc`, `clang` 중 하나)
- `make`
- Python 3 (`make check`, `make regen`을 쓸 때만 필요)

## 빌드

저장소 루트에서 실행합니다.

```sh
make
```

완료되면 현재 디렉터리에 실행 파일이 생깁니다.

- Linux, WSL, macOS: `./aster`
- Windows(MSYS2/MinGW): `./aster.exe`

## 검증

```sh
make check
```

프론트엔드(토큰/AST), 실행 테스트, REPL, 도구(포매터/린터/디버거) 테스트를 모두
돌립니다. Python은 `make check`와 `make regen`에서만 쓰입니다.

## 설치

```sh
make install
```

기본 설치 경로는 `~/.local/bin/aster` 입니다. 다른 곳에 설치하려면 `PREFIX`를 지정합니다.

```sh
make install PREFIX=/usr/local
```

설치 후 `PATH`에 설치 디렉터리를 추가하세요. 예:

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Windows(MSYS2)에서도 같은 명령을 쓰며, 실행 파일 이름은 `aster.exe`가 됩니다.

### 표준 라이브러리 위치

실행 파일은 다음 순서로 표준 라이브러리를 찾습니다.

1. 환경 변수 `ASTER_HOME`이 가리키는 디렉터리의 `Lib`
2. 현재 작업 디렉터리의 `Lib`

저장소 밖으로 실행 파일만 복사했다면 `ASTER_HOME`을 저장소 루트로 지정하세요.

```sh
export ASTER_HOME=/path/to/Aster-Alpha-Version
```

## 실행

```sh
aster hello.ast
```

표준 입력에서 읽으려면 파일 이름 대신 `-`를 씁니다.

```sh
aster - < program.ast
```

파일 없이 실행하면 대화형 REPL이 열립니다(`:help`, `:quit`). 그 밖에 `--check`,
`--ast`, `--tokens`, `--format`, `--lint`, `--debug` 같은 옵션은
[Doc/language.md](Doc/language.md) 1절을 봅니다.

## 문제 해결

- **`cc: command not found`**: C 컴파일러를 설치하세요.
  - Debian/Ubuntu: `sudo apt install build-essential`
  - MSYS2: `pacman -S mingw-w64-x86_64-gcc make`
- **`make check`에서 Python을 찾지 못함**: `PYTHON`을 지정하세요.
  ```sh
  make check PYTHON=python3
  ```
- **Windows에서 임시 파일 생성 실패**: MSYS2 셸에서 `TMPDIR=/tmp`를 export한 뒤 다시 실행하세요.
