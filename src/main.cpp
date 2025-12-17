// put wifi.txt on SD (M5Stack core2) or alternatively on SPIFFS (any ESP32)
// first line is SSID, next line is password

#include <ctype.h>
#include <math.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <time.h>
// CORE2 needs its AXP192 chip initialized
#ifdef M5STACK_CORE2
#include <esp_i2c.hpp>        // i2c initialization
#include <m5core2_power.hpp>  // AXP192 power management (core2)
#endif
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_http_server.h"
#include "esp_spiffs.h"
#include "esp_vfs_fat.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "wifi.h"
#include "sdcard.h"
#include "neopixel.h"
#include "httpd_application.h"

#ifdef M5STACK_CORE2
using namespace esp_idf;  // devices
#endif

static char wifi_ssid[65];
static char wifi_pass[129];

#ifdef M5STACK_CORE2
static void power_init() {
    // for AXP192 power management
    static m5core2_power power(esp_i2c<1, 21, 22>::instance);
    // draw a little less power
    power.initialize();
    power.lcd_voltage(3.0);
}
#endif

#ifdef SPI_PORT
static void spi_init() {
    spi_bus_config_t buscfg;
    memset(&buscfg, 0, sizeof(buscfg));
    buscfg.sclk_io_num = SPI_CLK;
    buscfg.mosi_io_num = SPI_MOSI;
    buscfg.miso_io_num = SPI_MISO;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;

    buscfg.max_transfer_sz = 512 + 8;

    // Initialize the SPI bus on VSPI (SPI3)
    ESP_ERROR_CHECK(spi_bus_initialize(SPI_PORT, &buscfg, SPI_DMA_CH_AUTO));
}
#endif

static void spiffs_init() {
    esp_vfs_spiffs_conf_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.base_path = "/spiffs";
    conf.partition_label = NULL;
    conf.max_files = 5;
    conf.format_if_mount_failed = true;
    ESP_ERROR_CHECK(esp_vfs_spiffs_register(&conf));
}

static void loop();
static void loop_task(void* arg) {
    uint32_t ts = pdTICKS_TO_MS(xTaskGetTickCount());
    while (1) {
        loop();
        uint32_t ms = pdTICKS_TO_MS(xTaskGetTickCount());
        if (ms >= ts + 200) {
            ms = pdTICKS_TO_MS(xTaskGetTickCount());
            vTaskDelay(5);
        }
    }
}
static uint32_t start_sram;
extern "C" void app_main() {
    printf("ESP-IDF version: %d.%d.%d\n", ESP_IDF_VERSION_MAJOR,
           ESP_IDF_VERSION_MINOR, ESP_IDF_VERSION_PATCH);
    start_sram = esp_get_free_internal_heap_size();
    printf("Free SRAM: %0.2fKB\n",
           start_sram / 1024.f);
#ifdef M5STACK_CORE2
    power_init();  // do this first
#endif
    neopixel_init();
#ifdef SPI_PORT
    spi_init();  // used by the SD reader
#endif
    bool loaded = false;

    wifi_ssid[0] = 0;

    wifi_pass[0] = 0;

    if (sdcard_init()) {
        puts("SD card found, looking for wifi.txt creds");
        loaded = wifi_load("/sdcard/wifi.txt", wifi_ssid, wifi_pass);
    }
    if (!loaded) {
        spiffs_init();
        puts("Looking for wifi.txt creds on internal flash");
        loaded = wifi_load("/spiffs/wifi.txt", wifi_ssid, wifi_pass);
    }
    if (loaded) {
        printf("Initializing WiFi connection to %s\n", wifi_ssid);
        wifi_init(wifi_ssid, wifi_pass);
        TaskHandle_t loop_handle;
        xTaskCreate(loop_task, "loop_task", 4096, nullptr, 10, &loop_handle);
        printf("Free SRAM: %0.2fKB\n", esp_get_free_internal_heap_size() / 1024.f);
    } else {
        puts("Create wifi.txt with the SSID on the first line, and password on the second line and upload it to SPIFFS");
    }
}
static void loop() {
    static bool is_connected = false;
    if (!is_connected) {  // not connected yet
        if (wifi_status() == WIFI_CONNECTED) {
            is_connected = true;
            puts("Connected");
            // initialize the web server
            puts("Starting httpd");
            httpd_init();
            // set the url text to our website
            static char url_text[256];
            uint32_t ip = wifi_ip_address();
            esp_ip4_addr addy;
            addy.addr = ip;
            snprintf(url_text, sizeof(url_text), "http://" IPSTR,
                     IP2STR(&addy));
            puts(url_text);
            uint32_t free_sram = esp_get_free_internal_heap_size();
            printf("Free SRAM: %0.2fKB\n",
                   free_sram / 1024.f);
            printf("SRAM used for webserver firmware: %0.2fKB\n",
                   (start_sram - free_sram) / 1024.f);
        }
    } else {
        if (wifi_status() == WIFI_CONNECT_FAILED) {
            // we disconnected for some reason
            puts("Disconnected");
            is_connected = false;
            httpd_end();
            wifi_restart();
            
            printf("Free SRAM: %0.2fKB\n",
                   esp_get_free_internal_heap_size() / 1024.f);
        }
    }
}
