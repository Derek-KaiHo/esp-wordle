#include "link.h"

#include <string.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "lwip/sockets.h"
#include "sdkconfig.h"

#define LINK_SSID "ESP-WORDLE"
#define LINK_PASS "wordle123"
#define LINK_PORT 3333
#define HOST_IP   "192.168.4.1"  // default softAP address

static const char *TAG = "link";

static link_rx_cb_t s_rx_cb;
static volatile int s_sock = -1;
static volatile bool s_wifi_ready;

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        s_wifi_ready = false;
        ESP_LOGW(TAG, "WiFi disconnected, retrying...");
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_wifi_connect();
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "joined host WiFi");
        s_wifi_ready = true;
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_AP_STACONNECTED) {
        ESP_LOGI(TAG, "a board joined our WiFi");
    }
}

static void wifi_init(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

#if CONFIG_WORDLE_ROLE_HOST
    esp_netif_create_default_wifi_ap();
    wifi_config_t ap_cfg = {
        .ap = {
            .ssid = LINK_SSID,
            .password = LINK_PASS,
            .authmode = WIFI_AUTH_WPA2_PSK,
            .max_connection = 1,
            .channel = 1,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap_cfg));
#else
    esp_netif_create_default_wifi_sta();
    wifi_config_t sta_cfg = {
        .sta = {
            .ssid = LINK_SSID,
            .password = LINK_PASS,
        },
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta_cfg));
#endif
    ESP_ERROR_CHECK(esp_wifi_start());
}

// Reads from the socket until it closes, invoking the callback per line.
static void rx_loop(int sock)
{
    char buf[128];
    char line[128];
    size_t len = 0;
    int n;
    while ((n = recv(sock, buf, sizeof(buf), 0)) > 0) {
        for (int i = 0; i < n; i++) {
            char c = buf[i];
            if (c == '\n') {
                line[len] = '\0';
                if (len > 0 && s_rx_cb) {
                    s_rx_cb(line);
                }
                len = 0;
            } else if (c != '\r' && len < sizeof(line) - 1) {
                line[len++] = c;
            }
        }
    }
}

#if CONFIG_WORDLE_ROLE_HOST
static void link_task(void *arg)
{
    int listen_sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_addr.s_addr = htonl(INADDR_ANY),
        .sin_port = htons(LINK_PORT),
    };
    int opt = 1;
    setsockopt(listen_sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(listen_sock, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        listen(listen_sock, 1) != 0) {
        ESP_LOGE(TAG, "failed to bind/listen on port %d", LINK_PORT);
        vTaskDelete(NULL);
    }
    ESP_LOGI(TAG, "waiting for the other board on port %d", LINK_PORT);

    while (true) {
        int sock = accept(listen_sock, NULL, NULL);
        if (sock < 0) {
            vTaskDelay(pdMS_TO_TICKS(500));
            continue;
        }
        ESP_LOGI(TAG, "peer connected");
        s_sock = sock;
        rx_loop(sock);
        s_sock = -1;
        close(sock);
        ESP_LOGW(TAG, "peer disconnected, waiting for reconnect");
    }
}
#else
static void link_task(void *arg)
{
    while (true) {
        if (!s_wifi_ready) {
            vTaskDelay(pdMS_TO_TICKS(200));
            continue;
        }
        int sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        struct sockaddr_in addr = {
            .sin_family = AF_INET,
            .sin_port = htons(LINK_PORT),
        };
        inet_pton(AF_INET, HOST_IP, &addr.sin_addr);
        if (connect(sock, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
            close(sock);
            vTaskDelay(pdMS_TO_TICKS(1000));
            continue;
        }
        ESP_LOGI(TAG, "connected to host");
        s_sock = sock;
        rx_loop(sock);
        s_sock = -1;
        close(sock);
        ESP_LOGW(TAG, "lost connection to host, retrying");
    }
}
#endif

void link_start(link_rx_cb_t rx_cb)
{
    s_rx_cb = rx_cb;
    wifi_init();
    xTaskCreate(link_task, "link", 4096, NULL, 5, NULL);
}

bool link_connected(void)
{
    return s_sock >= 0;
}

bool link_send_line(const char *line)
{
    int sock = s_sock;
    if (sock < 0) {
        return false;
    }
    char buf[130];
    int n = snprintf(buf, sizeof(buf), "%s\n", line);
    return send(sock, buf, n, 0) == n;
}
