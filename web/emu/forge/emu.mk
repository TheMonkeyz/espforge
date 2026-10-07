# The framework's build rules for the display in the browser (README.md): LVGL, the framework's components and the
# app's own main/ sources built to WebAssembly with Emscripten, the hardware replaced by forge/emu_*.c. Included by
# the app's web/emu/Makefile, which sets (paths relative to web/emu):
#   APP_SRC    the app's files in main/ (main.c ui.c ...), built unchanged
#   APP_EMU    the app's own emulator files in web/emu (emu_main.c, any stand-in of its own hardware)
#   EMBED      the files main/CMakeLists.txt embeds that the app reads as arrays: name=path (name_start, name_end)
# and may change: FORGE_CORE FORGE_LVGL FORGE_NET FORGE_OTA (the framework's files to build), APP_CFLAGS,
# APP_EXPORTS (more C functions the page calls, e.g. ,_emu_mic), BOARD (the board's include folder), COMP.
#   make            # in WSL, after `source ~/emsdk/emsdk_env.sh`: build/ (serve it over HTTP)
#   make config     # lv_kconfig.h and sdkconfig.h from the firmware's sdkconfig, after changing its options
#   make try F=../../main/ui.c   # compile one file, show the first errors
FORGE_EMU := forge
# LVGL: the firmware's own copy (ESP-IDF's component manager puts it in managed_components); cJSON: ESP-IDF's
LVGL ?= ../../managed_components/lvgl__lvgl
IDF_PATH ?= /mnt/c/Espressif/esp-idf
CJSON ?= $(IDF_PATH)/components/json/cJSON
MAIN ?= ../../main
COMP ?= ../../components
BOARD ?= $(firstword $(wildcard ../../boards/*/board/include))
SDKCONFIG ?= ../../build/v55/sdkconfig
APP ?= $(shell python3 -c "import json; print(json.load(open('../../forge.json'))['app'])")
VERSION ?= $(shell git describe --tags --always 2>/dev/null || echo dev)
OUT := build

ifeq ($(filter clean config,$(MAKECMDGOALS)),)
ifeq ($(wildcard $(CJSON)/cJSON.c),)
$(error no cJSON at $(CJSON): set IDF_PATH (ESP-IDF's folder) or CJSON=<folder with cJSON.c>)
endif
ifeq ($(wildcard $(LVGL)/lvgl.h),)
$(error no LVGL at $(LVGL): build the firmware once (idf.py fetches it into managed_components) or LVGL=<lvgl checkout>)
endif
ifeq ($(wildcard lv_kconfig.h),)
$(error no lv_kconfig.h: make config (from the firmware's sdkconfig, $(SDKCONFIG)))
endif
endif

# The framework's files the starter app needs; an app that uses more (png_rows.c, http_once.c...) adds them
FORGE_CORE ?= i18n.c i18n_nvs.c textfit.c testcon_registry.c
FORGE_LVGL ?= forge_lvgl.c pager.c slide.c screens.c
FORGE_NET ?= svc.c
FORGE_OTA ?= ota_web.c
EMU := emu_loop.c emu_display.c emu_touch.c emu_http.c emu_nvs.c emu_stubs.c emu_tasks.c emu_web.c emu_time.c
LV_SRC := $(shell find $(LVGL)/src -name '*.c' 2>/dev/null)

CFLAGS := -O2 -DEMU_BUILD '-DEMU_VERSION="$(VERSION)"' '-DEMU_APP="$(APP)"' -I. -I$(FORGE_EMU) -I$(FORGE_EMU)/shim \
          -I$(MAIN) -I$(COMP)/forge_core/include -I$(COMP)/forge_lvgl/include -I$(COMP)/forge_net/include \
          -I$(COMP)/forge_ota/include -I$(BOARD) -I$(LVGL) -I$(LVGL)/src -I$(CJSON) \
          -include string.h -include stdint.h '-DLV_CONF_KCONFIG_EXTERNAL_INCLUDE="lv_kconfig.h"' \
          -DLV_LVGL_H_INCLUDE_SIMPLE -Wno-unused-parameter $(APP_CFLAGS)
# Header dependencies of our own files (LVGL and cJSON are fixed versions: tracking their 400 files costs minutes
# over /mnt/c); after a change in LVGL's options, make clean
DEP := -MMD -MP
# The app's local time is its TZ (the starter's main.c sets it); Emscripten's localtime_r ignores TZ (the browser's
# zone): emu_time.c follows TZ instead, so a visitor anywhere sees the display's time
FWFLAGS := -Dlocaltime_r=emu_localtime_r
LDFLAGS := -sASYNCIFY -sASYNCIFY_STACK_SIZE=65536 -sALLOW_MEMORY_GROWTH -sSTACK_SIZE=262144 -sENVIRONMENT=web \
           -sEXPORTED_FUNCTIONS=_main,_emu_fb,_emu_fb_dirty,_emu_touch,_emu_brightness,_malloc,_free$(APP_EXPORTS) \
           -sEXPORTED_RUNTIME_METHODS=HEAPU8,HEAPU16 -sMODULARIZE -sEXPORT_NAME=ForgeEmu

OBJ := $(patsubst %.c,$(OUT)/fw/%.o,$(APP_SRC)) $(patsubst %.c,$(OUT)/app/%.o,$(APP_EMU)) \
       $(patsubst %.c,$(OUT)/forge_core/%.o,$(FORGE_CORE)) $(patsubst %.c,$(OUT)/forge_lvgl/%.o,$(FORGE_LVGL)) \
       $(patsubst %.c,$(OUT)/forge_net/%.o,$(FORGE_NET)) $(patsubst %.c,$(OUT)/forge_ota/%.o,$(FORGE_OTA)) \
       $(patsubst %.c,$(OUT)/emu/%.o,$(EMU)) $(patsubst $(LVGL)/%.c,$(OUT)/lvgl/%.o,$(LV_SRC)) \
       $(OUT)/cjson/cJSON.o $(if $(EMBED),$(OUT)/emu/embed.o)
# The page uses the flasher site's fonts (../fonts/, from main/); served alone, build/ is the site's root: a copy there
PAGE_FONTS := $(foreach e,$(filter %.ttf,$(EMBED)),$(OUT)/fonts/$(notdir $(word 2,$(subst =, ,$(e)))))
PAGE := $(OUT)/index.html $(OUT)/settings.html $(OUT)/emu-page.js $(OUT)/emu-settings.js $(PAGE_FONTS)

all: $(OUT)/emu.js $(PAGE)

$(OUT)/emu.js: $(OBJ)
	emcc $(CFLAGS) $(LDFLAGS) $(OBJ) -o $@
	@ls -l $(OUT)/emu.js $(OUT)/emu.wasm

$(OUT)/index.html: index.html
	@mkdir -p $(OUT)
	cp $< $@
$(OUT)/fonts/%.ttf: $(MAIN)/%.ttf
	@mkdir -p $(dir $@)
	cp $< $@
$(OUT)/emu-page.js: $(FORGE_EMU)/emu-page.js
	@mkdir -p $(OUT)
	cp $< $@
$(OUT)/emu-settings.js: $(FORGE_EMU)/emu-settings.js
	@mkdir -p $(OUT)
	cp $< $@
# The display's own settings page beside the emulator: unchanged, with emu-settings.js first in its <head> (its
# requests to /api/ go to the emulator, emu_web.c)
$(OUT)/settings.html: $(MAIN)/web/index.html
	@mkdir -p $(OUT)
	sed 's|<head>|<head><script src="emu-settings.js"></script>|' $< > $@   # (one <head>; macOS's sed has no 0,/re/)
	@grep -q 'emu-settings.js' $@ || (echo "settings.html: no <head> to put emu-settings.js in" && rm -f $@ && false)

$(OUT)/fw/%.o: $(MAIN)/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) $(DEP) $(FWFLAGS) -c $< -o $@
$(OUT)/app/%.o: %.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) $(DEP) $(FWFLAGS) -c $< -o $@
$(OUT)/forge_%.o: $(COMP)/forge_%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) $(DEP) -c $< -o $@
$(OUT)/emu/%.o: $(FORGE_EMU)/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) $(DEP) -c $< -o $@
$(OUT)/lvgl/%.o: $(LVGL)/%.c lv_kconfig.h
	@mkdir -p $(dir $@)
	@emcc $(CFLAGS) -c $< -o $@
$(OUT)/cjson/cJSON.o: $(CJSON)/cJSON.c
	@mkdir -p $(dir $@)
	emcc $(CFLAGS) -c $< -o $@

# The files the firmware embeds (EMBED_FILES), as arrays: name_start[] and name_end (the app's one #ifdef EMU_BUILD)
$(OUT)/emu/embed.c: $(foreach e,$(EMBED),$(word 2,$(subst =, ,$(e)))) $(FORGE_EMU)/embed.py
	@mkdir -p $(dir $@)
	python3 $(FORGE_EMU)/embed.py $@ $(EMBED)
$(OUT)/emu/embed.o: $(OUT)/emu/embed.c
	emcc $(CFLAGS) -c $< -o $@

# The firmware's settings for the browser build, from its sdkconfig (an idf.py build makes it): commit both
config:
	python3 $(FORGE_EMU)/gen_config.py $(SDKCONFIG) .

try:
	emcc $(CFLAGS) $(FWFLAGS) -O0 -c $(F) -o /tmp/emu-try.o 2>&1 | grep -E "error|fatal" | head -$(or $(N),12)

clean:
	rm -rf $(OUT)
.PHONY: all clean config try

-include $(patsubst %.o,%.d,$(filter-out $(OUT)/lvgl/% $(OUT)/cjson/%,$(OBJ)))
