# Makefile -- wrapper around the CMake/pico-sdk build of the Minty Intellivision
# cartridge firmware (RP2040/RP2350).  'make help' lists every target.
#
# Usage:
#   make                            Release for every board found in boards/
#   make fujicard                   Release for one board ('make list')
#   make fujicard-debug             Debug; -release and -ecs-iv also exist
#   make build-all                  both configs, all boards (= scripts/build-all.sh)
#   make BOARD=pirto CONFIG=debug   knob form of the per-board targets
#   make clean | make distclean
#
# Why: the board list, the build/<board>/<config> layout and the CMake flags were
# copy-pasted across scripts/*.sh and the CI matrix, and had already drifted --
# those lists omit fujicard.  Here the list is discovered from boards/*.cmake.
#
# Boards are never built concurrently: CMake's configure step writes into the
# source tree (FetchContent clones under lib/, file(COPY) of src/sd_card_spi.c,
# include/version.h), so two configures racing clobber each other.  Hence
# .NOTPARALLEL below, with -j handed to each individual board build instead.

ROOT  := $(patsubst %/,%,$(dir $(abspath $(lastword $(MAKEFILE_LIST)))))
BUILD := $(ROOT)/build

# boards/*.cmake is the single source of truth for the board list
BOARDS := $(sort $(notdir $(basename $(wildcard $(ROOT)/boards/*.cmake))))

# ECS audio and Intellivoice need the PWM audio pin (include/board.h maps
# AUDIO_PIN from MI_AUDIO_PIN), so the variant exists only where one is declared
ECS_BOARDS := $(filter $(BOARDS),$(notdir $(basename \
   $(shell grep -l MI_AUDIO_PIN $(ROOT)/boards/*.h 2>/dev/null))))

CMAKE      ?= cmake
SIZE       ?= arm-none-eabi-size
PICOTOOL   ?= picotool
HOSTCC     ?= $(CC)
JOBS       ?= $(shell nproc 2>/dev/null || echo 4)
GENERATOR  ?= $(if $(shell command -v ninja 2>/dev/null),Ninja,Unix Makefiles)
CMAKE_ARGS ?=
BOARD      ?=
CONFIG     ?= release
ROMS       ?=
ROM_WORDS  ?=

# CMAKE_BUILD_TYPE is read by boards/*.cmake and selects compiled features
# (CONFIG_USB_DEVICE, MAX_ROM_SIZE), not just optimisation, so always pass it.
# TYPE_/DEFS_/NICK_ are keyed by build-dir name, DIR_ is the inverse map.
TYPE_debug          := Debug
TYPE_release        := Release
TYPE_release-ecs-iv := Release
DEFS_release-ecs-iv := -DCONFIG_ECS_AUDIO=ON -DCONFIG_INTELLIVOICE=ON -DOUTPUT_SUFFIX=_ecs_iv
NICK_debug          := debug
NICK_release        := release
NICK_release-ecs-iv := ecs-iv
DIR_debug           := debug
DIR_release         := release
DIR_ecs-iv          := release-ecs-iv
NICKS               := debug release ecs-iv

ifneq ($(filter-out $(BOARDS),$(BOARD)),)
$(error BOARD='$(BOARD)' unknown -- valid: $(BOARDS))
endif
ifneq ($(filter-out $(NICKS),$(CONFIG)),)
$(error CONFIG='$(CONFIG)' unknown -- valid: $(NICKS))
endif

# pico_sdk_import.cmake does not exist in this tree, so PICO_SDK_PATH is
# mandatory; checked in the configure recipe only, so help/list/clean still work
SDK_GUARD = test -f "$(PICO_SDK_PATH)/pico_sdk_init.cmake" || { \
	   echo "E: PICO_SDK_PATH must point at a pico-sdk checkout (got '$(PICO_SDK_PATH)')" >&2; \
	   echo "   export PICO_SDK_PATH=/path/to/pico-sdk   (must contain pico_sdk_init.cmake)" >&2; \
	   exit 1; }

# OUTPUT_SUFFIX is settable only from the command line, so glob for the artifact
# rather than reconstructing Minty_<board><suffix>.uf2
define report
u=$$(ls $(1)/*.uf2 2>/dev/null | head -1); echo "==> $$u"; \
	   [ -n "$$u" ] && command -v $(SIZE) >/dev/null && $(SIZE) "$${u%.uf2}.elf" || true
endef

NEED_BOARD = test -n "$(BOARD)" || { echo "E: needs BOARD=<board> ('make list')" >&2; exit 1; }
OUT        = $(BUILD)/$(BOARD)/$(DIR_$(CONFIG))

.NOTPARALLEL:
# a failed configure still leaves CMakeCache.txt, which would look configured
.DELETE_ON_ERROR:
.DEFAULT_GOAL := all

# $(1) = board, $(2) = build-dir name.  The cache target has no prerequisites:
# configure runs once per build dir, and CMake re-runs itself from the build step
# when CMakeLists.txt changes.  -G therefore only ever reaches a fresh build dir,
# which matters because -G against an existing cache is fatal.
define board_rules
$(BUILD)/$(1)/$(2)/CMakeCache.txt:
	@$(SDK_GUARD)
	$(CMAKE) -S $(ROOT) -B $(BUILD)/$(1)/$(2) -G "$(GENERATOR)" -DPICO_BOARD=$(1) \
	   -DCMAKE_BUILD_TYPE=$(TYPE_$(2)) $(DEFS_$(2)) $(CMAKE_ARGS)

$(1)-$(NICK_$(2)): $(BUILD)/$(1)/$(2)/CMakeCache.txt
	@echo "==> $(1) $(2)"
	MAKEFLAGS= $(CMAKE) --build $(BUILD)/$(1)/$(2) -j $(JOBS)
	@$$(call report,$(BUILD)/$(1)/$(2))
.PHONY: $(1)-$(NICK_$(2))
endef

define board_aliases
$(1): $(1)-release
clean-$(1):
	@rm -rf $(BUILD)/$(1)
.PHONY: $(1) clean-$(1)
endef

# ecs-iv rules simply do not exist for boards without an audio pin, where
# src/audio.c would fail to compile
configs_for = debug release $(if $(filter $(1),$(ECS_BOARDS)),release-ecs-iv)
$(foreach b,$(BOARDS),$(eval $(call board_aliases,$(b))) \
   $(foreach c,$(call configs_for,$(b)),$(eval $(call board_rules,$(b),$(c)))))

# Aggregates keep going and summarise instead of aborting on the first board:
# boards whose fragment sets CONFIG_USB_DEVICE=0 for a config cannot satisfy
# CONFIG_FUJINET and fail that configure by design, and one of those must not
# hide the other five.  Single-board targets stay fail-fast, which is what CI wants.
define run_matrix
@fail=; for t in $(1); do \
	printf '\n=== %s ===\n' "$$t"; \
	$(MAKE) --no-print-directory $$t || fail="$$fail $$t"; \
 done; \
 printf '\n--- summary ---\n'; \
 for t in $(1); do case " $$fail " in *" $$t "*) s=FAIL;; *) s=ok;; esac; \
	printf '  %-26s %s\n' "$$t" "$$s"; done; \
 [ -z "$$fail" ] || { echo "E: failed:$$fail" >&2; exit 1; }
endef

MATRIX_all     = $(foreach b,$(or $(BOARD),$(BOARDS)),$(b)-$(CONFIG))
MATRIX_debug   = $(addsuffix -debug,$(BOARDS))
MATRIX_release = $(addsuffix -release,$(BOARDS))
MATRIX_ecs     = $(addsuffix -ecs-iv,$(ECS_BOARDS))

all:
	$(call run_matrix,$(MATRIX_all))
all-debug:
	$(call run_matrix,$(MATRIX_debug))
all-release:
	$(call run_matrix,$(MATRIX_release))
all-ecs-iv:
	$(call run_matrix,$(MATRIX_ecs))
build-all:
	$(call run_matrix,$(foreach b,$(BOARDS),$(b)-debug $(b)-release))
build-release:
	$(call run_matrix,$(MATRIX_release) $(MATRIX_ecs))

size:
	@$(NEED_BOARD)
	$(SIZE) $(OUT)/*.elf

flash: $(if $(BOARD),$(BOARD)-$(CONFIG))
	@$(NEED_BOARD)
	u=$$(ls $(OUT)/*.uf2 | head -1) && $(PICOTOOL) load -f "$$u" && $(PICOTOOL) reboot

# host_test/ drivers build with the host compiler; binaries land under build/ so
# clean covers them and the source tree stays clean
HOST_TEST   := $(BUILD)/host_test
HOST_CFLAGS ?= -Wall -Wextra -O1 -g -I$(ROOT) -I$(ROOT)/include

test:
	@mkdir -p $(HOST_TEST)
	$(HOSTCC) $(HOST_CFLAGS) -o $(HOST_TEST)/test_fujibus \
	   $(ROOT)/host_test/test_fujibus.c $(ROOT)/src/fujibus.c
	$(HOSTCC) $(HOST_CFLAGS) -o $(HOST_TEST)/test_bootmap \
	   $(ROOT)/host_test/test_bootmap.c $(ROOT)/src/bootmap.c $(ROOT)/src/memory.c \
	   $(ROOT)/src/utils.c
	@echo "--- test_fujibus"
	@$(HOST_TEST)/test_fujibus
# test_bootmap needs cartridge files; bare, it exits 2 with a usage line
	@if [ -n "$(ROMS)" ]; then echo "--- test_bootmap"; \
	   $(HOST_TEST)/test_bootmap $(if $(ROM_WORDS),--rom-words $(ROM_WORDS)) $(ROMS); \
	 else echo "i: test_bootmap built; run it with ROMS='path/*.bin'"; fi

# build/ only: the lib/ clones stay, so the next configure does not re-download
clean:
	@rm -rf $(BUILD)

# implies clean: include/version.h is written at configure time, so a surviving
# build dir would compile against a header that is no longer there
distclean: clean
	@echo "W: removing lib/ clones -- the next configure re-clones over the network"
	@rm -rf $(ROOT)/lib/no-OS-FatFS-SD-SDIO-SPI-RPi-Pico $(ROOT)/lib/littlefs
	@rm -f $(ROOT)/include/version.h $(ROOT)/littlefs.bin

list:
	@echo "boards: $(BOARDS)"
	@echo "ecs-iv: $(ECS_BOARDS)"

help:
	@echo "$$HELP"

.PHONY: all all-debug all-release all-ecs-iv build-all build-release \
        size flash test clean distclean list help

define HELP
Minty firmware build wrapper (CMake + pico-sdk).  Usable with -f from any directory.

Targets
  all (default)     Release for every board, or only BOARD= when set
  <board>           Release for one board
  <board>-debug     Debug build
  <board>-release   Release build
  <board>-ecs-iv    Release plus ECS audio and Intellivoice (capable boards only)
  all-debug         Debug for every board
  all-release       Release for every board
  all-ecs-iv        every ecs-iv variant
  build-all         all-debug then all-release  (same set as scripts/build-all.sh)
  build-release     all-release then all-ecs-iv (same set as scripts/build-release.sh)
  size              arm-none-eabi-size on one build, needs BOARD=
  flash             build, then picotool load and reboot, needs BOARD=
  test              build and run the host_test drivers, add ROMS= for test_bootmap
  clean             remove build/ ; keeps the lib/ clones, so no re-download
  clean-<board>     remove build/<board>
  distclean         clean plus the lib/ clones, include/version.h, littlefs.bin
  list              print the discovered boards

Knobs (command line or environment)
  BOARD=            restrict the default goal, and the target of size and flash
  CONFIG=           debug, release or ecs-iv                  (now: $(CONFIG))
  JOBS=             compile jobs within one board build       (now: $(JOBS))
  GENERATOR=        CMake generator, fresh configures only    (now: $(GENERATOR))
  CMAKE_ARGS=       extra -D arguments, fresh configures only
  ROMS=             cartridge files for the test_bootmap host test
  PICO_SDK_PATH=    required for a fresh configure            (now: $(PICO_SDK_PATH))

Boards (from boards/*.cmake): $(BOARDS)
ECS and Intellivoice capable: $(ECS_BOARDS)
Build tree: build/<board>/{debug,release,release-ecs-iv}

Aggregate targets keep going and end with a pass/fail summary; single-board
targets stop at the first error, which is what CI wants.  CONFIG_FUJINET needs
the USB device stack, which some boards switch off in Release -- those fail that
configure with an explicit CMake error and are Debug-only until their board
fragment changes.

build-all does not wipe build/ the way scripts/build-all.sh does; use
make clean build-all for that.
endef
export HELP
