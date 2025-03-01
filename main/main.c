#include "esp_log.h"
#include "esp_system.h"
#include "time.h"
#include "sys/time.h"
#include "esp_sntp.h"
#include "esp_wifi.h"
#include "esp_eap_client.h"
#include "esp_event.h"
#include "esp_sleep.h"
#include "nvs_flash.h"
#include "tm1637.h"
#include "sdkconfig.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "esp_http_server.h"
#include "webInterface.h"

#define WIFI_SSID     CONFIG_WIFI_SSID
#define WIFI_ID       CONFIG_WIFI_ID
#define WIFI_USER     CONFIG_WIFI_USER
#define WIFI_PASS     CONFIG_WIFI_PASS
#define SNTP_RETRYS   CONFIG_SNTP_RETRYS
#define WIFI_RETRYS   CONFIG_WIFI_RETRYS

static EventGroupHandle_t wifi_event_group;
static esp_netif_t* sta_netif = NULL;
const int CONNECTED_BIT = BIT0;

#define DEBUG 0

static const char* TAG = "CLOCK";

void sntp_start(void) {
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_init();

    for (int i = 0; i < SNTP_RETRYS; i++) {
        time_t now = 0;
        struct tm timeinfo = { 0 };
        time(&now);
        localtime_r(&now, &timeinfo);

        if (timeinfo.tm_hour > 0) {
            ESP_LOGI(TAG, "SNTP set to %s", asctime(&timeinfo));
            return;
        }

        ESP_LOGI(TAG, "Waiting for SNTP sync...");
        vTaskDelay(pdMS_TO_TICKS(2000));
    }
    ESP_LOGW(TAG, "SNTP sync failed");

    #if !DEBUG
    esp_sleep_enable_timer_wakeup(6000000); // 10 minute nap
    esp_deep_sleep_start();
    #endif
}

void wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
    static uint_least8_t retryCount = 0;

    // Check heap integrity before handling the event
    heap_caps_check_integrity_all(true);

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ESP_LOGI(TAG, "Connected to wifi");
        xEventGroupSetBits(wifi_event_group, CONNECTED_BIT);
    } else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (retryCount++ < WIFI_RETRYS) {
            ESP_LOGI(TAG, "Disconnected from wifi. Retrying in 5 seconds...");
            vTaskDelay(pdMS_TO_TICKS(5000));
            esp_wifi_connect();
        } else {
            ESP_LOGI(TAG, "Failed to connect to wifi after %d attempts", WIFI_RETRYS);
        }
    }

    // Check heap integrity after handling the event
    heap_caps_check_integrity_all(true);
}

static void wifi_start(void) {
    ESP_ERROR_CHECK(esp_netif_init());
    wifi_event_group = xEventGroupCreate();
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    sta_netif = esp_netif_create_default_wifi_sta();
    assert(sta_netif);
    // ESP_ERROR_CHECK(nvs_flash_init());

    wifi_init_config_t wifi_init_config = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&wifi_init_config));
    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_wifi_set_storage(WIFI_STORAGE_RAM));
    wifi_config_t wifi_config = {
        .sta = {
            .ssid = WIFI_SSID,
        }
    };
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

    if (WIFI_PASS[0] != '\0') // ensure cred is not empty string, will break following function if strlen == 0
        ESP_ERROR_CHECK(esp_eap_client_set_password((uint8_t*)WIFI_PASS, strlen(WIFI_PASS)));
    
    #if defined(CONFIG_WIFI_EAP)
    if (WIFI_ID[0] != '\0') // ensure cred is not empty string, will break following function if strlen == 0
        ESP_ERROR_CHECK(esp_eap_client_set_identity((uint8_t*)WIFI_ID, strlen(WIFI_ID)));
    if (WIFI_USER[0] != '\0') // ensure cred is not empty string, will break following function if strlen == 0
        ESP_ERROR_CHECK(esp_eap_client_set_username((uint8_t*)WIFI_USER, strlen(WIFI_USER)));
    ESP_ERROR_CHECK(esp_wifi_sta_enterprise_enable());
    #endif
    ESP_ERROR_CHECK(esp_wifi_start());
    ESP_LOGI(TAG, "Wifi started");
}

static void web_interface_start(void) {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  httpd_handle_t server = NULL; // initialize in httpd_start()
  ESP_ERROR_CHECK(httpd_start(&server, &config));
  httpd_register_uri_handler(server, &getHome);
  httpd_register_uri_handler(server, &postHome);
}

static void driveTM1637(void* pvParameters) {
    // CLK: 22
    // DIO: 21
    tm1637_led_t* tm1637 = tm1637_init(CONFIG_TM1637_CLK_PIN, CONFIG_TM1637_DIO_PIN);
    if (tm1637 == NULL) {
        ESP_LOGE(TAG, "Failed to init TM1637");
        return;
    }
    ESP_LOGI(TAG, "Successfully inited TM1637");

    while (true) {
        time_t now;
        struct tm timeinfo;
        time(&now);
        localtime_r(&now, &timeinfo);

        tm1637_set_number(tm1637, timeinfo.tm_hour * 100 + timeinfo.tm_min, false, 0xFF);
        ESP_LOGI(TAG, "Displayed time: %d:%d", timeinfo.tm_hour, timeinfo.tm_min);

        // Update the display every minute
        // however long the timeinfo tells us that is
        // This correction helps to deal with clock drift
        ESP_LOGI(TAG, "Updating clock in %d seconds", 60 - timeinfo.tm_sec);
        vTaskDelay(pdMS_TO_TICKS(1000 * (60 - timeinfo.tm_sec)));
    }
}

void app_main(void) {
    ESP_LOGI(TAG, "Connecting to wifi");
    ESP_ERROR_CHECK(nvs_flash_init());
    wifi_start();
    while (!(CONNECTED_BIT & xEventGroupGetBits(wifi_event_group)))
        vTaskDelay(pdMS_TO_TICKS(1000));

    ESP_LOGI(TAG, "Starting SNTP");
    sntp_start();

    ESP_LOGI(TAG, "Starting Web Interface");
    web_interface_start();

    setenv("TZ", "EST5EDT,M3.2.0/2:00:00,M11.1.0/2:00:00", 1);
    tzset();
    ESP_LOGI(TAG, "Setup done");

    xTaskCreate(driveTM1637, "driveTM1637", 1 << 11, NULL, 5, NULL);
}
