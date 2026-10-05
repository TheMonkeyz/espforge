# Host unit tests: firmware C files built with gcc against small shims of the ESP-IDF headers (shim/) and a scripted
# HTTP client (fake.c), with AddressSanitizer. Linux or WSL:   make -C tests/host
# IDF_PATH: an ESP-IDF checkout (cJSON, for tests of JSON parsers); on the Windows PC through WSL it is
# /mnt/c/Espressif/esp-idf. The tests below don't need it.
IDF_PATH ?= /mnt/c/Espressif/esp-idf
CJSON := $(IDF_PATH)/components/json/cJSON
FORGE := ../../components/forge_core
LVGLC := ../../components/forge_lvgl
MAIN := ../../main
OUT := build
CFLAGS := -std=gnu11 -D_DEFAULT_SOURCE -g -O1 -Wall -Wno-unused-function -fsanitize=address,undefined \
          -fno-sanitize-recover=undefined -Ishim -I$(FORGE)/include -I$(MAIN) -include shim/host.h
# test_testcon only once the firmware has the pure command table (testcon_registry.c)
TESTS := utf8 version png i18n textfit $(if $(wildcard $(FORGE)/testcon_registry.c),testcon)

all: $(TESTS:%=$(OUT)/test_%)
	@set -e; for t in $(TESTS); do ./$(OUT)/test_$$t; done

$(OUT)/test_utf8: test_utf8.c $(FORGE)/include/utf8.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_utf8.c
$(OUT)/test_version: test_version.c $(FORGE)/version.c $(FORGE)/include/version.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_version.c $(FORGE)/version.c
# i18n.c is pure C (its NVS half, i18n_nvs.c, is not compiled here); the texts are the app's
$(OUT)/test_i18n: test_i18n.c $(FORGE)/i18n.c $(FORGE)/include/forge_i18n.h $(MAIN)/i18n_strings.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_i18n.c $(FORGE)/i18n.c
$(OUT)/test_textfit: test_textfit.c $(FORGE)/textfit.c $(FORGE)/include/textfit.h $(MAIN)/montserrat.ttf | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_textfit.c $(FORGE)/textfit.c
$(OUT)/test_testcon: test_testcon.c $(FORGE)/testcon_registry.c $(FORGE)/include/testcon.h | $(OUT)
	$(CC) $(CFLAGS) -o $@ test_testcon.c $(FORGE)/testcon_registry.c

# miniz 3.0.2 (MIT) for test_png: its inflate is the ROM's tinfl, its deflate builds the test images. Downloaded once.
MINIZ := $(OUT)/miniz
$(MINIZ)/miniz.c: | $(OUT)
	mkdir -p $(MINIZ)
	curl -fsSL -o $(OUT)/miniz.zip https://github.com/richgel999/miniz/releases/download/3.0.2/miniz-3.0.2.zip
	python3 -c "import zipfile; zipfile.ZipFile('$(OUT)/miniz.zip').extractall('$(MINIZ)', ['miniz.c', 'miniz.h'])"
$(OUT)/test_png: test_png.c $(FORGE)/png_rows.c $(FORGE)/include/png_rows.h $(MINIZ)/miniz.c | $(OUT)
	$(CC) $(CFLAGS) -I$(MINIZ) -o $@ test_png.c $(FORGE)/png_rows.c $(MINIZ)/miniz.c

$(OUT):
	mkdir -p $(OUT)
clean:
	rm -rf $(OUT)
.PHONY: all clean
