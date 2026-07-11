CXX      ?= clang++
CXXFLAGS := -std=c++23 -O2 -Wall -Wextra -Wimplicit-fallthrough
LDLIBS   := -lreadline

SRCS     := $(wildcard src/*.cpp)

UNAME_S  := $(shell uname -s)
ifeq ($(UNAME_S),Linux)
    OS := linux
else ifeq ($(UNAME_S),Darwin)
    OS := mac
    CXXFLAGS += -I/opt/homebrew/opt/readline/include
    LDFLAGS  += -L/opt/homebrew/opt/readline/lib
    export MACOSX_DEPLOYMENT_TARGET := $(shell sw_vers -productVersion)
else ifneq (,$(findstring MINGW,$(UNAME_S)))
    OS := windows
else ifneq (,$(findstring MSYS,$(UNAME_S)))
    OS := windows
else
    OS := unknown
endif

OUT_DIR  := build/$(OS)
BIN      := $(OUT_DIR)/anvil
PREFIX   ?= /usr/local
BINDIR   ?= $(HOME)/.local/bin

ASAN_BIN := $(OUT_DIR)/anvil-asan

ifeq ($(OS),mac)
    ASAN_CXX     ?= /usr/bin/clang++
    ASAN_LDFLAGS := -L/opt/homebrew/opt/readline/lib
else
    ASAN_CXX     ?= $(CXX)
    ASAN_LDFLAGS := $(LDFLAGS)
endif

.PHONY: all clean re install uninstall link unlink asan test test-update

all: $(BIN) link

$(BIN): $(SRCS)
	@mkdir -p $(OUT_DIR)
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(SRCS) $(LDLIBS) -o $@

asan: $(ASAN_BIN)

$(ASAN_BIN): $(SRCS)
	@mkdir -p $(OUT_DIR)
	$(ASAN_CXX) $(CXXFLAGS) -g -O1 -fno-omit-frame-pointer -fsanitize=address,undefined \
		$(ASAN_LDFLAGS) $(SRCS) $(LDLIBS) -o $@

test: $(BIN)
	@tests/run.sh

test-update: $(BIN)
	@UPDATE=1 tests/run.sh

link: $(BIN)
	@mkdir -p $(BINDIR)
	@ln -sf $(abspath $(BIN)) $(BINDIR)/anvil
	@echo "linked $(BINDIR)/anvil -> $(abspath $(BIN))"
	@case ":$$PATH:" in *:"$(BINDIR)":*) ;; *) echo ">> note: $(BINDIR) is not on PATH -- add it to run 'anvil' from anywhere";; esac

unlink:
	rm -f $(BINDIR)/anvil

clean:
	rm -rf build

re: clean all

install: $(BIN)
	install -d $(DESTDIR)$(PREFIX)/bin
	install -m 755 $(BIN) $(DESTDIR)$(PREFIX)/bin/anvil
	@echo "installed $(DESTDIR)$(PREFIX)/bin/anvil ($(OS))"

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/anvil
