#include <stdio.h>

#include "nvs_flash.h"
#include "driver/uart.h"
#include "driver/uart_vfs.h"
#include "sdkconfig.h"

#include "link.h"
#include "game.h"

#if CONFIG_WORDLE_ROLE_HOST
#define ROLE_NAME "host"
#else
#define ROLE_NAME "joiner"
#endif

// Route stdin through the UART driver so fgetc() blocks instead of busy-polling.
static void console_init(void)
{
    ESP_ERROR_CHECK(uart_driver_install(CONFIG_ESP_CONSOLE_UART_NUM, 256, 0, 0, NULL, 0));
    uart_vfs_dev_use_driver(CONFIG_ESP_CONSOLE_UART_NUM);
    uart_vfs_dev_port_set_rx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CR);
    uart_vfs_dev_port_set_tx_line_endings(CONFIG_ESP_CONSOLE_UART_NUM, ESP_LINE_ENDINGS_CRLF);
}

void app_main(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }

    console_init();
    printf("\n=== ESP-Wordle (%s) ===\n", ROLE_NAME);
    link_start(game_on_peer_line);
    game_run();
}
