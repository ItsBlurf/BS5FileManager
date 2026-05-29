# BS5FileManager - minimal PS5 browser file manager payload.

PS5_HOST ?= ps5
PS5_PORT ?= 9021

ifdef PS5_PAYLOAD_SDK
    include $(PS5_PAYLOAD_SDK)/toolchain/prospero.mk
else
    $(error PS5_PAYLOAD_SDK is undefined)
endif

VERSION_TAG := bs5fm-v1.0.0
BUILD_VERSION ?= $(shell git describe --tags --always --dirty 2>/dev/null || echo dev)
PYTHON ?= python3
HOST_LLVM_BINDIR ?= $(shell dirname "$$(command -v llvm-strip 2>/dev/null || command -v llvm-strip.exe 2>/dev/null || echo llvm-strip)" 2>/dev/null)
LLVM_STRIP ?= llvm-strip
UNIX_CURDIR := $(shell cygpath -u '$(CURDIR)' 2>/dev/null || pwd)

export LLVM_BINDIR ?= $(HOST_LLVM_BINDIR)
export LLVM_CONFIG ?= $(UNIX_CURDIR)/build-tools/llvm-config
BUILD_PATH := $(UNIX_CURDIR)/build-tools:$(HOST_LLVM_BINDIR):/mingw64/bin:/usr/local/bin:/usr/bin:/bin:/c/Users/Blurf/scoop/shims
BUILD_ENV := cd "$(UNIX_CURDIR)"; export LLVM_CONFIG="$(LLVM_CONFIG)"; export LLVM_BINDIR="$(LLVM_BINDIR)"; export PATH="$(BUILD_PATH):$$PATH"

BIN := bs5filemanager.elf

SRCS := src/lite_main.c
SRCS += src/websrv_lite.c
SRCS += src/asset.c
SRCS += src/fs.c
SRCS += src/mime.c
SRCS += src/notify.c
SRCS += src/transfer.c
SRCS += src/app_installer.c

CFLAGS := -Os -Wall -Werror -Isrc
CFLAGS += -ffunction-sections -fdata-sections
CFLAGS += -flto
CFLAGS += -DVERSION_TAG=\"$(VERSION_TAG)\"
CFLAGS += -DBUILD_VERSION=\"$(BUILD_VERSION)\"

LDFLAGS := -Wl,--gc-sections -flto
LDFLAGS += -B$(PS5_PAYLOAD_SDK)/win

LDADD := -lkernel_sys -lSceNotification
LDADD += -lSceIpmi -lSceAppInstUtil -lSceUserService -lSceSystemService

ASSETS := $(wildcard assets/*)
GEN_SRCS := $(patsubst assets/%,gen/assets/%,$(ASSETS:=.c))
APP_ASSETS := assets-app/param.json assets-app/icon0.png

all: $(BIN)

gen/assets:
	mkdir -p gen/assets

gen/assets/%.c: assets/% | gen/assets
	$(PYTHON) gen-asset-module.py --path $* $< > $@

$(BIN): $(SRCS) $(GEN_SRCS) $(APP_ASSETS)
	bash -lc '$(BUILD_ENV); $(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(SRCS) $(GEN_SRCS) $(LDADD)'
	bash -lc 'cd "$(UNIX_CURDIR)"; test -f $@'
	bash -lc '$(BUILD_ENV); $(LLVM_STRIP) --strip-all $@'

deploy: $(BIN)
	$(PS5_DEPLOY) -h $(PS5_HOST) -p $(PS5_PORT) $<

clean:
	rm -rf $(BIN) gen

.PHONY: all clean deploy
