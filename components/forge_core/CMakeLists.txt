# No esp_http_client here: include/http_once.h is inline, and each component that includes it requires esp_http_client
# itself (forge_net, forge_ota, the apps' main). A public requirement here pulled the HTTP client, and with it TLS and
# lwIP, into an app with no Wi-Fi.
idf_component_register(
    SRCS "version.c" "png_rows.c" "diag.c" "testcon.c" "testcon_registry.c" "i18n.c" "i18n_nvs.c" "nvs_util.c" "textfit.c" "cfgcon.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash esp_timer esp_partition esp_app_format app_update espcoredump spi_flash esp_psram
    PRIV_REQUIRES esp_driver_usb_serial_jtag esp_rom)
