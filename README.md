# Aster

Aster는 C11로 구현된 정적 타입 프로그래밍 언어입니다. 이 문서는 처음 받은 사람이
**C 컴파일러 설치부터 빌드, 실행, 검증, 설치**까지 순서대로 따라 할 수 있게
안내하고, 마지막에 **새 키워드와 새 내장/표준 라이브러리를 추가하는 방법**을
정리합니다.

- 언어 문법: [Doc/language.md](Doc/language.md)
- 표준 라이브러리: [Doc/stdlib.md](Doc/stdlib.md)
- 패키지 관리자: [Doc/packages.md](Doc/packages.md)
- 확장 안내(상세): [Doc/extending.md](Doc/extending.md)

## 목차

1. [준비물: C 컴파일러](#1-준비물-c-컴파일러)
2. [소스 받기](#2-소스-받기)
3. [빌드](#3-빌드)
4. [첫 실행](#4-첫-실행)
5. [검증](#5-검증)
6. [설치](#6-설치)
7. [언어 확장하기](#7-언어-확장하기)
8. [문제 해결](#8-문제-해결)

## 1. 준비물: C 컴파일러

Aster는 C11로 작성되었으므로 **C 컴파일러**가 반드시 필요합니다. 빌드 스크립트는
`cc`, `gcc`, `clang` 중 하나를 찾습니다. 추가로 `make`가 필요하고, `make check`와
`make regen`을 실행할 때만 Python 3가 필요합니다.

운영체제별로 아래 순서를 따릅니다.

### Windows: MSYS2 (권장)

Windows의 `cmd`/PowerShell에는 `make`와 C 컴파일러가 기본으로 없습니다. MSYS2의
MinGW-w64(UCRT64) 환경을 설치해 그 안에서 빌드합니다.

1. <https://www.msys2.org/>에서 설치 파일을 받아 실행합니다.
2. 시작 메뉴에서 **MSYS2 UCRT64**를 엽니다. (`MSYS2 MSYS`가 아닙니다.)
3. 패키지 데이터베이스를 갱신합니다.

   ```sh
   pacman -Syu
   ```

   창이 닫히면 **MSYS2 UCRT64**를 다시 열고 한 번 더 실행합니다.

   ```sh
   pacman -Syu
   ```

4. 컴파일러, make, Python을 설치합니다.

   ```sh
   pacman -S --needed mingw-w64-ucrt-x86_64-gcc make python
   ```

5. 설치를 확인합니다.

   ```sh
   cc --version
   make --version
   python --version
   ```

`cc`가 `gcc`를 가리키고 버전이 출력되면 준비가 끝났습니다.

> Windows에서 Visual Studio(MSVC)는 지원하지 않습니다. MSYS2를 쓰기 어렵다면
> WSL(Ubuntu)을 사용합니다.

### Windows: WSL (대안)

WSL을 쓰면 Linux 환경에서 그대로 빌드할 수 있습니다. 관리자 PowerShell에서
다음 명령으로 Ubuntu를 설치합니다(재부팅이 필요할 수 있습니다).

```powershell
wsl --install -d Ubuntu
```

Ubuntu를 처음 열어 사용자 계정을 만든 뒤, WSL 안에서 실행합니다.

```sh
sudo apt update
sudo apt install -y build-essential python3
cc --version
make --version
```

### Ubuntu / Debian / Linux Mint

```sh
sudo apt update
sudo apt install -y build-essential python3
cc --version
make --version
```

### Fedora / RHEL 계열

```sh
sudo dnf install -y gcc make python3
```

### Arch Linux / Manjaro

```sh
sudo pacman -S --needed gcc make python
```

### macOS

Xcode 명령 줄 도구를 설치하면 `cc`와 `make`가 함께 설치됩니다.

```sh
xcode-select --install
cc --version
make --version
```

Python 3가 없다면 <https://www.python.org/downloads/>에서 설치하거나
Homebrew로 설치합니다.

```sh
brew install python
```

### 확인

어느 운영체제든 다음 세 명령이 모두 동작해야 합니다.

```sh
cc --version
make --version
python3 --version
```

Windows MSYS2에서는 `python3` 대신 `python --version`을 씁니다.

## 2. 소스 받기

GitHub 저장소 페이지에서 **Code → Download ZIP**으로 받아 압축을 풀거나,
`git`이 있다면 저장소를 복제합니다. 그다음 프로젝트 폴더로 이동합니다.

```sh
cd Aster-Alpha-Version
```

폴더 안에 `Makefile`, `Parser/`, `Python/`, `Lib/`가 있으면 올바른 위치입니다.

## 3. 빌드

프로젝트 루트에서 실행합니다.

```sh
make
```

완료되면 현재 디렉터리에 실행 파일이 생깁니다.

- Linux, WSL, macOS: `./aster`
- Windows(MSYS2/MinGW): `./aster.exe`

빌드 로그에 `cc: command not found`가 나오면 1절로 돌아가 컴파일러를 설치하고,
MSYS2라면 **UCRT64** 셸을 사용하고 있는지 확인합니다.

처음부터 다시 빌드하려면:

```sh
make clean
make
```

## 4. 첫 실행

`hello.ast` 파일을 만듭니다.

```aster
import System.stdout.*;

str name = "Aster";
println("Hello, " + name + "!");
```

실행합니다.

```sh
./aster hello.ast
```

Windows에서는 다음과 같습니다.

```sh
./aster.exe hello.ast
```

출력:

```text
Hello, Aster!
```

표준 입력에서 소스를 읽으려면 파일 이름 대신 `-`를 씁니다.

```sh
./aster - < hello.ast
```

파일 없이 실행하면 대화형 REPL이 열립니다.

```sh
./aster
```

REPL 명령은 `:help`와 `:quit`입니다.

그 밖의 실행 옵션:

| 옵션 | 설명 |
| --- | --- |
| `--check` | 타입 검사만 하고 실행하지 않습니다. |
| `--no-check` | 타입 검사를 건너뜁니다. |
| `--ast` | 파싱한 AST를 출력합니다. |
| `--tokens` | 토큰 목록을 출력합니다. |
| `--format` | 소스를 정규화해 출력합니다(주석은 보존하지 않음). |
| `--lint` | 사용하지 않는 변수 등을 경고합니다. |
| `--debug` | 문장 단위로 멈춰 가며 실행합니다. |

자세한 내용은 [Doc/language.md](Doc/language.md) 1절을 봅니다.

## 5. 검증

```sh
make check
```

프런트엔드(토큰/AST), 실행 테스트, REPL, 도구(포매터/린터/디버거) 테스트를 모두
돌립니다. 검사 항목 구성은 고정되어 있고, 검사 대상 Aster 프로그램의 내용(식, 값,
이름, 구조)은 실행할 때마다 새 시드로 다시 만들어져 파싱, 타입 검사, 실행 결과,
포매터 고정점을 확인합니다. 일부러 거부되어야 하는 프로그램도 함께 생성해
타입 검사기가 잘못된 코드를 통과시키지 않는지 확인합니다.

실패한 케이스는 `build/test-failures/`에 저장됩니다.

무작위 검증만 따로 돌리거나 실패를 재현할 때:

```sh
python3 Tools/check_random.py --count 50
python3 Tools/check_random.py --seed 1234 --keep
```

`--seed`는 모든 검사 스크립트가 받으며, `ASTER_TEST_SEED` 환경 변수로도 고정할
수 있습니다.

```sh
ASTER_TEST_SEED=1234 make check
```

Windows MSYS2에서 Makefile이 `py`를 찾지 못하면 Python 실행 파일을 지정합니다.

```sh
make check PYTHON=python
```

Python은 `make check`와 `make regen`에서만 쓰입니다. 실행 파일을 빌드하는 데는
필요하지 않습니다.

## 6. 설치

```sh
make install
```

기본 설치 경로는 `~/.local/bin/aster`입니다. 다른 곳에 설치하려면 `PREFIX`를
지정합니다.

```sh
make install PREFIX=/usr/local
```

설치 후 `PATH`에 설치 디렉터리를 추가합니다.

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Windows(MSYS2)에서도 같은 명령을 쓰며, 실행 파일 이름은 `aster.exe`가 됩니다.

### 표준 라이브러리 위치

실행 파일은 다음 순서로 표준 라이브러리를 찾습니다.

1. 환경 변수 `ASTER_HOME`이 가리키는 디렉터리의 `Lib`
2. 현재 작업 디렉터리의 `Lib`

저장소 밖으로 실행 파일만 복사했다면 `ASTER_HOME`을 저장소 루트로 지정합니다.

```sh
export ASTER_HOME=/path/to/Aster-Alpha-Version
```

## 7. 문제 해결

### `cc: command not found`

C 컴파일러가 없습니다. 1절의 운영체제별 설치를 따릅니다.

- Debian/Ubuntu/WSL: `sudo apt install build-essential`
- Fedora: `sudo dnf install gcc make`
- Arch: `sudo pacman -S gcc make`
- macOS: `xcode-select --install`
- Windows: MSYS2 UCRT64 셸에서 `pacman -S mingw-w64-ucrt-x86_64-gcc make`

### `make: command not found`

`make`가 없습니다. 컴파일러와 함께 설치합니다.

- Debian/Ubuntu: `sudo apt install make`
- Fedora: `sudo dnf install make`
- macOS: `xcode-select --install`
- Windows: MSYS2 UCRT64 셸에서 `pacman -S make`

### `make check`에서 Python을 찾지 못함

Python 실행 파일 이름을 지정합니다.

```sh
make check PYTHON=python3
```

Windows MSYS2:

```sh
make check PYTHON=python
```

### Windows에서 임시 파일 생성 실패

MSYS2 셸에서 `TMPDIR`를 지정한 뒤 다시 실행합니다.

```sh
export TMPDIR=/tmp
make
```

### 설치했는데 `aster`를 찾지 못함

`PATH`에 설치 디렉터리가 있는지 확인합니다.

```sh
export PATH="$HOME/.local/bin:$PATH"
```

Windows에서는 실행 파일 이름이 `aster.exe`입니다.

### `cannot find module` 또는 표준 라이브러리 오류

실행 파일만 다른 곳으로 복사했다면 `ASTER_HOME`을 저장소 루트로 지정합니다.

```sh
export ASTER_HOME=/path/to/Aster-Alpha-Version
```

### Windows에서 `cmd`/PowerShell로 빌드가 안 됨

`make`, `cc`, 셸 유틸리티가 없기 때문입니다. **MSYS2 UCRT64** 셸 또는 WSL에서
빌드합니다.
