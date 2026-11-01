# The no-Wi-Fi starter in the browser (web/emu/README.md at the top of this repository): an app built without
# forge_net and forge_ota (USE_NET := 0), so no settings page beside the screen. The rules are the framework's.
#   make -j8        # in WSL, after `source ~/emsdk/emsdk_env.sh`, after one firmware build of examples/nonet
#   make config     # lv_kconfig.h and sdkconfig.h from examples/nonet/build/sdkconfig
# then: node ../../../../web/emu/forge/smoke.js build   (from here; it skips the settings page's checks)

# main/'s sources, built unchanged (main/CMakeLists.txt SRCS)
APP_SRC := main.c ui.c app_text.c
APP_EMU := emu_main.c
# The font main/CMakeLists.txt embeds (the Wi-Fi starter's), as ttf_start / ttf_end (ui.c)
EMBED := ttf=../../../../main/montserrat.ttf
USE_SETTINGS := 1
USE_NET := 0

# This checkout's framework, LVGL from this example's own firmware build, its sdkconfig; no forge.json here
FORGE_EMU := ../../../../web/emu/forge
COMP := ../../../../components
BOARD := ../../../../boards/ws_amoled175/board/include
LVGL ?= ../../managed_components/lvgl__lvgl
SDKCONFIG ?= ../../build/sdkconfig
APP := nonet

include $(FORGE_EMU)/emu.mk
