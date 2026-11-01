// The no-Wi-Fi starter in the browser: main.c's app_main() runs unchanged as a task (forge/emu_loop.c). No Wi-Fi
// stand-ins (USE_NET := 0): nvs_init() is the emulator's (NVS is the page's localStorage), the board's microphones
// and motion sensor answer "not there" (forge/emu_stubs.c), so screen dimming stays idle.
#include "emu.h"

int main(void)
{
    emu_start_app_main();
    emu_loop();
}
