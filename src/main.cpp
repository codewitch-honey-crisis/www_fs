// put wifi.txt on SD (M5Stack core2) or alternatively on SPIFFS (any ESP32)
// first line is SSID, next line is password

// for testing, won't actually store the uploaded file on the FS
// #define NO_STORE_UPLOAD

// 8192 seems to yield the most performance. After that it levels off
#define UPLOAD_BUFFER_SIZE 8192
#define DOWNLOAD_BUFFER_SIZE 8192
#define UPLOAD_WORKING_SIZE 8192
#define HTTPD_STACK_SIZE (32 * 1024)
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
#include "sdcard.h"
#include "wifi.h"
#include "neopixel.h"
#include "mpm_parser.h"
#define WWW_CONTENT_IMPLEMENTATION
#include "www_content.h"

#ifdef M5STACK_CORE2
using namespace esp_idf;  // devices
#endif

static char wifi_ssid[65];
static char wifi_pass[129];

static httpd_handle_t httpd_handle = nullptr;
static SemaphoreHandle_t httpd_ui_sync = nullptr;

// for reading HTTP request content bodies.
typedef struct {
    httpd_req_t* req;
    size_t remaining;
    size_t length;
    uint32_t start;
    int previous_percent;
    char buffer[UPLOAD_BUFFER_SIZE];
    char working[UPLOAD_WORKING_SIZE + 1];
    size_t buffer_size;
    size_t buffer_pos;
} httpd_recv_buffer_t;

static int httpd_buffered_read(void* state) {
    if (state == nullptr) {
        return -1;
    }
    httpd_recv_buffer_t* recv_buf = (httpd_recv_buffer_t*)state;
    int percent;
    char result;
    if (recv_buf->buffer_pos >= recv_buf->buffer_size) {
        if (!recv_buf->remaining) {
            goto done;
        }
        // reset the WDT
        vTaskDelay(5);
        int r = httpd_req_recv(recv_buf->req, recv_buf->buffer,
                               recv_buf->remaining <= sizeof(recv_buf->buffer)
                                   ? recv_buf->remaining
                                   : sizeof(recv_buf->buffer));
        percent = ((float)(recv_buf->length - recv_buf->remaining) / (float)recv_buf->length) * 100;
        if (percent != recv_buf->previous_percent) {
            char num_buf[32];
            itoa(percent, num_buf, 10);
            fputs("Uploading ", stdout);
            fputs(num_buf, stdout);
            puts("% complete");
            recv_buf->previous_percent = percent;
        }
        if (r < 1) {
            recv_buf->buffer_size = 0;
            recv_buf->buffer_pos = 0;
            if (r == 0) {
                goto done;
            }
            recv_buf->remaining = 0;
            return -1;
        }

        recv_buf->buffer_size = r;
        recv_buf->remaining -= r;
        recv_buf->buffer_pos = 0;
        if (recv_buf->buffer_size == 0) {
            goto done;
        }
    }
    result = recv_buf->buffer[recv_buf->buffer_pos];
    ++recv_buf->buffer_pos;
    return result;
done:
    return -1;
}
static esp_err_t httpd_request_handler(httpd_req_t* req) {
    neopixel_color(0,0,255);
    // match the handler
    int handler_index = www_response_handler_match(req->uri);
    // we keep our response context on the stack if we can
    httpd_context_t resp_arg;
    // but for async responses it must be on the heap
    httpd_context_t* resp_arg_async;
    if (req->method == HTTP_GET || req->method == HTTP_POST) {  // async is only for GET and POST
        if (handler_index == WWW_RESPONSE_HANDLER_COUNT - 1) {  // this is our FS handler
            bool is_upload = req->method != HTTP_GET;
            // get the filepath from the path and query string
            char path[513];
            const char* sze = strchr(req->uri, '?');
            size_t path_len =
                sze == nullptr ? strlen(req->uri) : sze - req->uri;
            httpd_url_decode(path, path_len, req->uri);
            path[path_len] = '\0';
            if (is_upload) {
                neopixel_color(255,0,0);
                char content_type[256];
                FILE* file_cursor = NULL;
                httpd_req_get_hdr_value_str(req, "Content-Type", content_type,
                                            sizeof(content_type));
                char* mime_boundary = strstr(content_type, "boundary=");
                // this is multipart MIME upload
                if (mime_boundary != nullptr) {
                    mime_boundary += 9;
                    while (*mime_boundary == ' ') {
                        ++mime_boundary;
                    }
                    // not enough stack for this:
                    httpd_recv_buffer_t* recv_buf = (httpd_recv_buffer_t*)malloc(
                        sizeof(httpd_recv_buffer_t));
                    if (recv_buf == nullptr) {
                        goto error;  // out of memory;
                    }
                    memset(recv_buf, 0, sizeof(httpd_recv_buffer_t));
                    recv_buf->req = req;
                    recv_buf->remaining = recv_buf->length = req->content_len;
                    recv_buf->start = pdTICKS_TO_MS(xTaskGetTickCount());
                    if (recv_buf->remaining > 0) {
                        // initialize our multipart mime parser
                        mpm_context_t ctx;
                        mpm_init(mime_boundary, 0, httpd_buffered_read, recv_buf, &ctx);
                        size_t size = UPLOAD_WORKING_SIZE;
                        mpm_node_t node;

                        bool disposition = false;
                        // parse the MIME data
                        while ((node = mpm_parse(&ctx, recv_buf->working, &size)) >
                               0) {
                            switch (node) {
                                case MPM_HEADER_NAME_PART:
                                    recv_buf->working[size] = '\0';
                                    disposition =
                                        0 == my_stricmp(recv_buf->working,
                                                        "Content-Disposition");
                                    break;
                                case MPM_HEADER_VALUE_PART:
                                    if (disposition) {
                                        const char* sz = recv_buf->working;
                                        recv_buf->working[size] = 0;
                                        const char* szeq = strchr(sz, '=');
                                        if (szeq != nullptr) {
                                            recv_buf->working[szeq - sz] = '\0';
                                            ++szeq;
                                            if (0 ==
                                                my_stricmp(sz, "filename")) {
                                                if (*szeq == '\"') {
                                                    ++szeq;
                                                    if (*szeq) {
                                                        recv_buf->working
                                                            [(szeq -
                                                              recv_buf->working) +
                                                             strlen(szeq) - 1] =
                                                            '\0';
                                                    }
                                                } else {
                                                    recv_buf->working
                                                        [(szeq -
                                                          recv_buf->working) +
                                                         strlen(szeq)] = '\0';
                                                }
                                                if (*szeq != '\0') {
                                                    strcat(path, szeq);
#ifndef NO_STORE_UPLOAD
                                                    remove(path);  // delete if it
                                                                   // exists;
                                                    fputs("Opened ", stdout);
                                                    puts(path);
                                                    file_cursor = fopen(path, "wb");
#else
                                                    file_cursor = nullptr;
#endif
                                                } else {
                                                    file_cursor = nullptr;
                                                }
                                            }
                                        }
                                    }
                                    break;

                                case MPM_CONTENT_PART:
                                    disposition = false;
                                    if (file_cursor != nullptr) {
                                        fwrite(recv_buf->working, 1, size, file_cursor);
                                    }
                                    break;
                                case MPM_CONTENT_END:
                                    disposition = false;
                                    if (file_cursor != NULL) {
                                        fclose(file_cursor);
                                        file_cursor = NULL;
                                    }
                                    path[path_len] = '\0';
                                    break;
                                default:  // don't care
                                    break;
                            }
                            size = UPLOAD_WORKING_SIZE;
                        }
                    }
                    if (recv_buf != nullptr) {
                        char buf[32];
                        fputs("Uploading complete in ", stdout);
                        int num = (((float)pdTICKS_TO_MS(xTaskGetTickCount()) - (float)recv_buf->start) / 1000.f + .5);
                        itoa(num, buf, 10);
                        fputs(buf, stdout);
                        puts(" seconds");
                        free(recv_buf);
                        recv_buf = nullptr;
                    }
                } else {  // standard form post, probably delete
                    char* data = (char*)malloc(req->content_len + 1);
                    if (data == nullptr) {
                        goto error;
                    }
                    httpd_req_recv(req, data, req->content_len);
                    data[req->content_len] = '\0';
                    char* sz = strstr(data, "delete=");
                    if (sz != nullptr) {
                        sz += 7;
                        char* sze = strpbrk(sz, "&;");
                        if (sze == nullptr) {
                            sze = sz + strlen(sz);
                        } else {
                            *sze = '\0';
                        }
                        strcat(path, sz);
                        neopixel_color(0,0,0);
                        fputs("Deleting ", stdout);
                        puts(path);
                        remove(path);
                        path[path_len] = '\0';
                    }
                    free(data);
                }
            }
            stat_t st;
            if (0 == stat(path, &st) && ((st.st_mode & S_IFMT) != S_IFDIR)) {
                const char* pq = strrchr(req->uri, '?');
                char cname[64], cval[64];
                bool downloading = false;
                if (pq != NULL) {
                    while (NULL != (pq = httpd_crack_query(pq, cname, sizeof(cname), cval, sizeof(cval)))) {
                        if (0 == my_stricmp(cname, "download")) {
                            downloading = true;
                            break;
                        }
                    };
                }
                // is file
                const char* szpfn = path + 8;
                const char* szfn = strrchr(szpfn, '/');
                if (szfn != nullptr)
                    ++szfn;
                else
                    szfn = szpfn;

                if (downloading) {
                    fputs("Downloading ", stdout);
                    puts(path);
                    static const char* headerd =
                        "HTTP/1.1 200 OK\r\nContent-Disposition: attachment; "
                        "filename=\"";
                    httpd_send(req, headerd, strlen(headerd));
                    httpd_send(req, szfn, strlen(szfn));
                    static const char* header2 = "\"\r\nContent-Length: ";
                    httpd_send(req, header2, strlen(header2));
                } else {
                    static const char* headerv =
                        "HTTP/1.1 200 OK\r\nContent-Type: ";
                    httpd_send(req, headerv, strlen(headerv));
                    const char* ct = httpd_content_type(path);
                    httpd_send(req, ct, strlen(ct));
                    static const char* header2 = "\r\nContent-Length: ";
                    httpd_send(req, header2, strlen(header2));
                }
                char buf[DOWNLOAD_BUFFER_SIZE];
                size_t l = (size_t)st.st_size;
                itoa((int)l, buf, 10);
                httpd_send(req, buf, strlen(buf));
                static const char* header3 = "\r\n\r\n";
                httpd_send(req, header3, strlen(header3));
                FILE* file = fopen(path, "rb");
                if (!file) {
                    return ESP_FAIL;
                }
                l = fread(buf, 1, sizeof(buf), file);
                while (l > 0) {
                    httpd_send(req, buf, l);
                    l = fread(buf, 1, sizeof(buf), file);
                }
                fclose(file);
                neopixel_color(0,0,0);
                return ESP_OK;
            } else {
                DIR* d = opendir(path);
                if (d != nullptr) {
                    closedir(d);
                    if (strrchr(path, '/') != path + path_len - 1) {
                        handler_index = -1;
                    }
                } else {
                    handler_index = -1;
                }
            }
        }
        resp_arg_async = (httpd_context_t*)malloc(sizeof(httpd_context_t));
        if (resp_arg_async == nullptr) {  // no memory
            // we can still do it synchronously
            goto synchronous;
        }
        strncpy(resp_arg_async->path_and_query, req->uri, sizeof(req->uri));
        resp_arg_async->handle = req->handle;
        resp_arg_async->method = req->method;
        resp_arg_async->fd = httpd_req_to_sockfd(req);
        if (resp_arg_async->fd < 0) {  // error getting socket
            free(resp_arg_async);
            goto error;
        }
        httpd_work_fn_t handler_fn;
        if (handler_index == -1) {
            // no match, send a 404
            handler_fn = www_content_404_clasp;
        } else if (handler_index == -2) {
            handler_fn = www_content_500_clasp;
        } else {
            // choose the handler
            handler_fn =
                (httpd_work_fn_t)www_response_handlers[handler_index].handler;
        }
        // and off we go.
        httpd_queue_work(req->handle, handler_fn, resp_arg_async);
        return ESP_OK;
    }
synchronous:
    // must serve synchronously
    resp_arg.fd = -1;
    resp_arg.handle = req;
    resp_arg.method = req->method;
    strncpy(resp_arg.path_and_query, req->uri, sizeof(req->uri));
    resp_arg_async = &resp_arg;
    if (handler_index == -1) {
        www_content_404_clasp(resp_arg_async);
    } else if (handler_index == -2) {
        www_content_500_clasp(resp_arg_async);
    } else {
        www_response_handlers[handler_index].handler(resp_arg_async);
    }
    return ESP_OK;
error:
    // allocate a resp arg on the stack, fill it with our info
    // and send a 500
    resp_arg.fd = -1;
    resp_arg.handle = req;
    resp_arg.method = req->method;
    strncpy(resp_arg.path_and_query, req->uri, sizeof(req->uri));
    resp_arg_async = &resp_arg;
    www_content_500_clasp(resp_arg_async);
    return ESP_OK;
}
static bool httpd_match(const char* cmp, const char* uri, size_t len) {
    return true;  // match anything.
}
static void httpd_init() {
    httpd_ui_sync = xSemaphoreCreateMutex();
    if (httpd_ui_sync == nullptr) {
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }
    httpd_init_encoding();
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 2;
    config.server_port = 80;
    config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS - 3);
    config.uri_match_fn = httpd_match;
    config.stack_size = HTTPD_STACK_SIZE;
    ESP_ERROR_CHECK(httpd_start(&httpd_handle, &config));
    httpd_uri_t handler = {.uri = "/",
                           .method = HTTP_GET,
                           .handler = httpd_request_handler,
                           .user_ctx = nullptr};
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &handler));
    handler.method = HTTP_POST;
    ESP_ERROR_CHECK(httpd_register_uri_handler(httpd_handle, &handler));
}

static void httpd_end() {
    if (httpd_handle == nullptr) {
        return;
    }
    ESP_ERROR_CHECK(httpd_stop(httpd_handle));
    httpd_handle = nullptr;
    vSemaphoreDelete(httpd_ui_sync);
    httpd_ui_sync = nullptr;
}

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
