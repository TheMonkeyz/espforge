# Screen dimming by presence (include/presence.h). No board code: the microphones, the motion sensor, the touch and
# the brightness come in through presence_hooks_t, so an app with its own board uses it too.
#
# presence_web.c (the settings page's /api/presence, presence_web_routes) only when forge_net is in the build: an app
# with no Wi-Fi leaves forge_net out and keeps the rest. ESP-IDF reads this file twice: first for the requirements
# (BUILD_COMPONENTS still empty, and nothing after idf_component_register runs), so forge_presence never pulls
# forge_net in by itself; then for the build, with the final list. idf_component_optional_requires is ESP-IDF's way to
# depend on a component only when something else put it in the build (REQUIRES that only the second pass would see
# are ignored: the first pass's list is the one that counts).
set(srcs "presence.c" "presence_sm.c" "presence_json.c")
idf_build_get_property(build_components BUILD_COMPONENTS)
if(forge_net IN_LIST build_components)
    list(APPEND srcs "presence_web.c")
endif()
idf_component_register(
    SRCS ${srcs}
    INCLUDE_DIRS "include"
    REQUIRES
    PRIV_REQUIRES forge_core json nvs_flash)
idf_component_optional_requires(PRIVATE forge_net)   # web.h, and esp_http_server through it
