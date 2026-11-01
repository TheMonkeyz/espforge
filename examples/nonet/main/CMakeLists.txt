# No forge_net, forge_ota, dns_server, esp_wifi: check_nonet.py fails the build if any of them is in it
idf_component_register(
    SRCS "main.c" "ui.c" "app_text.c"
    INCLUDE_DIRS "."
    REQUIRES board forge_core forge_lvgl forge_presence forge_settings lvgl esp_timer
    # The Wi-Fi starter's font (a new project copies it into its main/)
    EMBED_FILES "../../../main/montserrat.ttf")
