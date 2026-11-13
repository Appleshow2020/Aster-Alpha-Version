CC ?= cc
CFLAGS ?= -std=c11 -O2 -g -Wall -Wextra -IInclude
LDLIBS ?= -lm

# MSYS make가 자식 프로세스에 TEMP를 넘기지 않아 gcc가 C:\WINDOWS에 임시 파일을 만들려다 실패한다.
TMPDIR ?= /tmp
export TMPDIR
export TEMP = $(TMPDIR)
export TMP = $(TMPDIR)

# OS 환경 변수는 셸에 따라 비어 있을 수 있으므로 uname으로 플랫폼을 판별한다.
UNAME_S := $(shell uname -s 2>/dev/null)
ifneq (,$(filter MINGW% MSYS% CYGWIN%,$(UNAME_S)))
EXE = .exe
# Windows Store의 python.exe 별칭은 make가 실행한 뒤 종료되지 않는다. py 런처를 기본으로 쓴다.
PYTHON ?= py
else
EXE =
PYTHON ?= python3
endif
BIN = aster$(EXE)
PREFIX ?= $(HOME)/.local
BINDIR ?= $(PREFIX)/bin

SRCS = Programs/aster.c \
       Programs/pkg.c \
       Parser/token.c \
       Parser/lexer/lexer.c \
       Parser/parser.c \
       Python/arena.c \
       Python/Aster-ast.c \
       Python/runtime.c \
       Python/regex.c \
       Python/interp.c \
       Python/typecheck.c \
       Python/formatter.c \
       Python/linter.c \
       Objects/object.c

HEADERS = Include/internal/token.h \
          Include/internal/lexer.h \
          Include/internal/parser.h \
          Include/internal/ast.h \
          Include/internal/asdl.h \
          Include/internal/arena.h \
          Include/internal/value.h \
          Include/internal/object.h \
          Include/internal/runtime.h \
          Include/internal/regex.h \
          Include/internal/interp.h \
          Include/internal/typecheck.h \
          Include/internal/formatter.h \
          Include/internal/linter.h \
          Include/internal/regex.h \
          Include/internal/unicode_ranges.h \
          Include/internal/pkg.h

$(BIN): $(SRCS) $(HEADERS)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDLIBS)

# make는 -v를 자체 --version 플래그로 먼저 처리하므로, 검사 스크립트의 -v는
# make check VERBOSE=1 또는 make check-v로 전달한다.
VERBOSE ?= $(V)
CHECK_FLAGS = $(if $(filter 1 yes true on,$(VERBOSE)),-v,)

# --seed를 지원하는 검사 스크립트에만 시드를 전달한다. 무작위 검증이 없는
# check_lib.py는 제외한다. ASTER_TEST_SEED도 함께 내보내므로 random_suite를
# 쓰는 스크립트는 플래그 없이도 같은 시드를 자동으로 사용한다.
SEED ?=
ifneq ($(strip $(SEED)),)
SEED_ARG = --seed $(SEED)
export ASTER_TEST_SEED = $(SEED)
endif

check: $(BIN)
	$(PYTHON) Tools/check_frontend.py $(SEED_ARG) $(CHECK_FLAGS)
	$(PYTHON) Tools/check_runtime.py $(SEED_ARG) $(CHECK_FLAGS)
	$(PYTHON) Tools/check_lib.py $(CHECK_FLAGS)
	$(PYTHON) Tools/check_repl.py $(SEED_ARG) $(CHECK_FLAGS)
	$(PYTHON) Tools/check_tools.py $(SEED_ARG) $(CHECK_FLAGS)
	$(PYTHON) Tools/check_random.py $(SEED_ARG) $(CHECK_FLAGS)
	$(PYTHON) Tools/check_java.py $(SEED_ARG) $(CHECK_FLAGS)

check-v:
	$(MAKE) check VERBOSE=1

check-java: $(BIN)
	$(PYTHON) Tools/check_java.py --require-jdk $(SEED_ARG) $(CHECK_FLAGS)

regen:
	$(PYTHON) Tools/build/generate_token.py Grammar/Tokens Include/internal/token.h Parser/token.c
	$(PYTHON) Parser/asdl_c.py Parser/Aster.asdl -H Include/internal/ast.h -C Python/Aster-ast.c

install: $(BIN)
	mkdir -p "$(DESTDIR)$(BINDIR)"
	cp $(BIN) "$(DESTDIR)$(BINDIR)/aster$(EXE)"
	@echo "Install Complete: $(DESTDIR)$(BINDIR)/aster$(EXE)  (Add $(BINDIR) in PATH.)"

clean:
	rm -f aster aster.exe

.PHONY: check check-v check-java regen clean install
