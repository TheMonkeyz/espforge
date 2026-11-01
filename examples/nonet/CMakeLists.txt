cmake_minimum_required(VERSION 3.16)
# espforge's no-Wi-Fi starter (README in main/main.c): the board and the framework's components from this checkout.
# All of components/ is offered, forge_net, forge_ota and dns_server included, and the build is limited to main's
# dependency tree (COMPONENTS): a component that still required forge_net would pull it in, and check_nonet.py would
# see it in build/project_description.json. A new project with no Wi-Fi takes espforge at a tag instead (main's
# idf_component.yml, as esp32-s3-colormesh) and lists forge_core, forge_lvgl, forge_presence, forge_settings, the board.
set(EXTRA_COMPONENT_DIRS ../../boards/ws_amoled175 ../../components)
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(nonet)
