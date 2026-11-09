# macOS only. Windows continues to use build.cmd.
ARCH ?= $(shell uname -m)
MIN_MACOS := 13.0
OUT := build/macos/$(ARCH)
APP := $(OUT)/Jotila.app
CC := xcrun clang
COMMON := -arch $(ARCH) -mmacosx-version-min=$(MIN_MACOS) -O2 -Wall -Wextra -Werror -Isrc
CFLAGS := $(COMMON) -std=c17
OBJCFLAGS := $(COMMON) -fobjc-arc -fblocks
FRAMEWORKS := -framework AppKit -framework Carbon -framework ServiceManagement
CORE := src/date.c src/colors.c
MAC := $(filter-out src/macos/main.m,$(wildcard src/macos/*.m))
OBJECTS := $(CORE:%.c=$(OUT)/%.o) $(MAC:%.m=$(OUT)/%.o)
SIGN_IDENTITY ?= -

.PHONY: all run test smoke-test analyze universal package
all: $(APP)/Contents/MacOS/Jotila

$(OUT)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MMD -MP -c $< -o $@

$(OUT)/%.o: %.m
	@mkdir -p $(dir $@)
	$(CC) $(OBJCFLAGS) -MMD -MP -c $< -o $@

$(APP)/Contents/MacOS/Jotila: $(OBJECTS) $(OUT)/src/macos/main.o src/macos/Info.plist assets/jotila.png tools/macos/icon.sh Makefile
	@mkdir -p $(APP)/Contents/MacOS $(APP)/Contents/Resources
	$(CC) $(COMMON) $(OBJECTS) $(OUT)/src/macos/main.o $(FRAMEWORKS) -o $@
	cp src/macos/Info.plist $(APP)/Contents/Info.plist
	sh tools/macos/icon.sh assets/jotila.png $(OUT)
	cp $(OUT)/Jotila.icns $(APP)/Contents/Resources/Jotila.icns
	codesign --force --sign "$(SIGN_IDENTITY)" --options runtime $(APP)
	@touch $@

run: all
	open $(APP)

$(OUT)/jotila-tests: $(OBJECTS) $(OUT)/tests/macos/tests.o
	$(CC) $(COMMON) $^ $(FRAMEWORKS) -o $@

test: $(OUT)/jotila-tests
	$(OUT)/jotila-tests

smoke-test: all
	@archive=$$(mktemp -d "$(CURDIR)/$(OUT)/smoke.XXXXXX"); \
	  $(APP)/Contents/MacOS/Jotila --portable --archive "$$archive" --smoke-test

analyze:
	@mkdir -p $(OUT)/analysis
	@for source in $(MAC) src/macos/main.m; do \
	  $(CC) $(OBJCFLAGS) --analyze $$source -o $(OUT)/analysis/$$(basename $$source).plist || exit; \
	done

universal:
	$(MAKE) ARCH=arm64 all
	$(MAKE) ARCH=x86_64 all

package: universal
	SIGN_IDENTITY="$(SIGN_IDENTITY)" sh tools/macos/package.sh

-include $(OBJECTS:.o=.d) $(OUT)/src/macos/main.d $(OUT)/tests/macos/tests.d
