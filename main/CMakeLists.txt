idf_component_register(SRCS "main.c" "link.c" "game.c" "wordle.c" "wordlist.c" "answerlist.c"
                       INCLUDE_DIRS "."
                       PRIV_REQUIRES nvs_flash esp_wifi esp_netif esp_event esp_driver_uart lwip vfs)
