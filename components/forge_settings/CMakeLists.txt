# The Settings screen a long press opens (include/forge_settings.h): rows in sections, ready-made ones for
# forge_presence, the language, the phone's settings page, Wi-Fi, forge_ota and About. Needs the board's panel hooks
# for its slides (forge_lvgl's slide.c; without them LVGL's own animation).
#
# forge_net and forge_ota only when something else put them in the build (forge_presence's CMakeLists.txt says why
# this way): without forge_net, no phone page (its QR code of web_url(), so no LV_USE_QRCODE either) and no network /
# IP rows; without forge_ota, no updates row, and Restart restarts at once. FORGE_HAVE_NET / FORGE_HAVE_OTA tell the
# sources (the browser emulator sets them from emu.mk's USE_NET).
idf_build_get_property(build_components BUILD_COMPONENTS)
idf_component_register(
    SRCS "settings.c" "settings_std.c"
    INCLUDE_DIRS "include"
    REQUIRES lvgl
    PRIV_REQUIRES forge_core forge_lvgl forge_presence esp_app_format esp_timer)
idf_component_optional_requires(PRIVATE forge_net forge_ota)   # esp_wifi comes through forge_net
if(forge_net IN_LIST build_components)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FORGE_HAVE_NET=1)
endif()
if(forge_ota IN_LIST build_components)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FORGE_HAVE_OTA=1)
endif()
