# Retro-Go SD — OpenLara GWHB homebrew (fixed-point engine + MODE4 soft raster).
#
#   make                              — build + pack OpenLara.bin (homebrew)
#   make PROJECT_KIND=homebrew
#   make docker PROJECT_KIND=homebrew
#   make host                         — macOS/Linux SDL build (no FMV)
#   ./OpenLara_host [data_dir]        — PKD folder (see OPENLARA_DATA)
#
# Requires third_party/OpenLara (see scripts/fetch_openlara.sh + README).

#######################################
# Project identity
#######################################
PROJECT_KIND ?= homebrew

CORE_NAME  := openlara
CORE_ENTRY := app_main

# Skeleton src/main.c is unused; OpenLara entry is src/platform/gnw/main.cpp
VID := src/platform/gnw/video

CORE_C_SOURCES := \
$(VID)/avi.c \
$(VID)/video_play.c \
$(VID)/video_decode.c \
$(VID)/video_audio.c \
$(VID)/video_scratch.c \
$(VID)/minimp3.c \
$(VID)/hw_jpeg_decoder.c \
$(VID)/hal/stm32h7xx_hal_jpeg.c \
$(VID)/hal/stm32h7xx_hal_dma2d.c

CORE_C_INCLUDES := -I$(VID)

OPENLARA := third_party/OpenLara

CORE_CXX_SOURCES := \
src/platform/gnw/main.cpp \
src/platform/gnw/os.cpp \
src/platform/gnw/present.cpp \
src/platform/gnw/input.cpp \
src/platform/gnw/sound.cpp \
src/platform/gnw/fmv.cpp \
$(OPENLARA)/src/fixed/common.cpp \
$(OPENLARA)/src/platform/gba/render.iwram.cpp

GNW_CORE_SDK ?= sdk
BUILD_DIR ?= build/$(PROJECT_KIND)

#######################################
# Kind-specific compile defs + packing
#######################################
ifeq ($(PROJECT_KIND),core)
$(error OpenLara is a homebrew — use PROJECT_KIND=homebrew)
else ifeq ($(PROJECT_KIND),homebrew)
CORE_C_DEFS := \
-DPROJECT_KIND_HOMEBREW=1 \
-D__GNW__=1

PACKED_BIN := OpenLara.bin
HB_NAME    := OpenLara
COVER_JPG  := $(BUILD_DIR)/cover.jpg
COVER_WIDTH  ?= 128
COVER_HEIGHT ?= 96

else
$(error PROJECT_KIND must be 'homebrew' (got '$(PROJECT_KIND)'))
endif

include $(GNW_CORE_SDK)/Makefile

# Append (do not prepend): porting/common.h must stay ahead of fixed/ for
# gw_firmware_abi.h. OpenLara headers in fixed/ still resolve "common.h" via
# the including file's directory; fmt/*.h needs -I fixed for "stream.h".
OL_INC := \
-Isrc/platform/gnw \
-I$(OPENLARA)/src/fixed \
-I$(OPENLARA)/src/platform/gba

OL_OBJS := \
$(BUILD_DIR)/main.o \
$(BUILD_DIR)/os.o \
$(BUILD_DIR)/present.o \
$(BUILD_DIR)/input.o \
$(BUILD_DIR)/sound.o \
$(BUILD_DIR)/fmv.o \
$(BUILD_DIR)/common.o \
$(BUILD_DIR)/render.iwram.o \
$(BUILD_DIR)/gw_core_cxx_support.o

$(OL_OBJS): CXXFLAGS += $(OL_INC)

# Host SDL (no device video stack / FMV).
HOST_BIN := OpenLara_host
HOST_C_SOURCES := host/fmv_stub.c
HOST_CXX_SOURCES := \
src/platform/gnw/main.cpp \
src/platform/gnw/os.cpp \
src/platform/gnw/present.cpp \
src/platform/gnw/input.cpp \
src/platform/gnw/sound.cpp \
$(OPENLARA)/src/fixed/common.cpp \
$(OPENLARA)/src/platform/gba/render.iwram.cpp
HOST_CXX_EXTRA_INCLUDES := $(OL_INC) -Isrc/platform/gnw

PACK_HOMEBREW := $(GNW_CORE_SDK)/tools/pack_homebrew.py
GEN_COVER     := scripts/gen_homebrew_cover.py

#######################################
# Packed header version
#######################################
# gnw_core_meta_t / gwhb_meta_t only store major.minor.patch (0..255).
# CORE_VERSION is the full git describe string passed to the packers; they
# extract the leading vX.Y.Z (NOTAG / missing tags → 0.0.0).
# Override: make CORE_VERSION=v1.2.3
CORE_VERSION ?= $(shell git describe --tags --dirty 2>/dev/null || echo NOTAG)

#######################################
# Pack
#######################################
.PHONY: pack cover prepare

prepare:
	@test -f $(OPENLARA)/src/fixed/common.h || { \
		echo "Missing $(OPENLARA) — run: ./scripts/fetch_openlara.sh"; exit 1; }
	@if ! grep -q '__GNW__' $(OPENLARA)/src/fixed/common.h; then \
		echo "[ PATCH ] patches/0001-openlara-gnw.patch"; \
		patch -d $(OPENLARA) -p1 < patches/0001-openlara-gnw.patch; \
	fi
	@if ! grep -q 'HOST_BUILD' $(OPENLARA)/src/fixed/fmt/pkd.h 2>/dev/null; then \
		echo "[ HOST ] patches/host-overlay + 0002-openlara-host.patch"; \
		cp patches/host-overlay/src/fixed/fmt/pkd.h $(OPENLARA)/src/fixed/fmt/pkd.h; \
		cp patches/host-overlay/src/platform/gba/rasterizer.h $(OPENLARA)/src/platform/gba/rasterizer.h; \
		cp patches/host-overlay/src/platform/gba/render.iwram.cpp $(OPENLARA)/src/platform/gba/render.iwram.cpp; \
		patch -d $(OPENLARA) -p1 < patches/0002-openlara-host.patch; \
	fi
	@test -L src/platform/gnw/ol || ln -sfn ../../../third_party/OpenLara/src/fixed src/platform/gnw/ol

cover: $(COVER_JPG)

$(COVER_JPG): $(GEN_COVER)
	$(V)$(ECHO) [ COVER ] $(COVER_JPG) $(COVER_WIDTH)x$(COVER_HEIGHT)
	$(V)python3 $(GEN_COVER) \
		--out $(COVER_JPG) \
		--title "$(HB_NAME)" \
		--width $(COVER_WIDTH) \
		--height $(COVER_HEIGHT)

pack: prepare $(TARGET_BIN) $(COVER_JPG)
	$(V)$(ECHO) [ PACK GWHB ] $(PACKED_BIN) version=$(CORE_VERSION)
	$(V)python3 $(PACK_HOMEBREW) \
		--elf $(TARGET_ELF) --bin $(TARGET_BIN) \
		--name "$(HB_NAME)" --version "$(CORE_VERSION)" \
		--cover $(COVER_JPG) \
		--out $(PACKED_BIN)

all: pack

.PHONY: print-PROJECT_KIND print-PACKED_BIN print-CORE_NAME print-DOCKER_IMAGE \
	print-TARGET_ELF print-TARGET_MAP print-CORE_VERSION
print-PROJECT_KIND:
	@echo $(PROJECT_KIND)
print-PACKED_BIN:
	@echo $(PACKED_BIN)
print-CORE_NAME:
	@echo $(CORE_NAME)
print-DOCKER_IMAGE:
	@echo $(DOCKER_IMAGE)
print-TARGET_ELF:
	@echo $(TARGET_ELF)
print-TARGET_MAP:
	@echo $(BUILD_DIR)/$(CORE_NAME)_core.map
print-CORE_VERSION:
	@echo $(CORE_VERSION)

clean::
	$(V)rm -f $(PACKED_BIN)
	$(V)rm -f $(COVER_JPG)

#######################################
# Docker
#######################################
.PHONY: docker docker_pull docker_shell

RELEASE_VERSION ?= v1.5
DOCKER_REPOSITORY ?= sylverb/retro-go-sd-builder
DOCKER_IMAGE ?= $(DOCKER_REPOSITORY):$(RELEASE_VERSION)

DOCKER_TTY_FLAG := $(shell if [ -t 0 ]; then echo -it; else echo; fi)
DOCKER_USER := $(shell id -u):$(shell id -g)
DOCKER_RUN := docker run --rm $(DOCKER_TTY_FLAG) \
	--user $(DOCKER_USER) \
	-v "$(CURDIR):/opt/workdir" \
	-w /opt/workdir \
	$(DOCKER_IMAGE)

docker: prepare
	$(V)$(ECHO) "[ DOCKER ]" $(DOCKER_IMAGE) "PROJECT_KIND=$(PROJECT_KIND)"
	$(V)$(DOCKER_RUN) make --no-print-directory -j$$(nproc) PROJECT_KIND=$(PROJECT_KIND)

docker_pull:
	$(V)$(ECHO) "[ PULL ]" $(DOCKER_IMAGE)
	$(V)docker pull $(DOCKER_IMAGE)

docker_shell:
	$(DOCKER_RUN) bash

#######################################
# Host SDL (Linux / macOS)
#######################################
include host/Makefile.host

host: prepare
