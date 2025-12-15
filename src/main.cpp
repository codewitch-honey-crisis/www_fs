// put wifi.txt on SD (M5Stack core2) or alternatively on SPIFFS (any ESP32)
// first line is SSID, next line is password

// for testing, won't actually store the uploaded file on the FS
//#define NO_STORE_UPLOAD

// This is the size of our working buffer. While larger is faster, 
// 8192 seems to yield the most performance. After that it levels off
#define UPLOAD_BUFFER_SIZE 8192
#define UPLOAD_WORKING_SIZE 8192

// SD support on the Core2
#ifdef M5STACK_CORE2
#define SPI_PORT SPI3_HOST
#define SPI_CLK 18
#define SPI_MISO 38
#define SPI_MOSI 23

#define SD_PORT SPI_PORT
#define SD_CS 4
#endif
// SD support on the Freenove S3 Devkit
#ifdef FREENOVE_S3_DEVKIT
#define SDMMC_D0 40
#define SDMMC_CLK 39
#define SDMMC_CMD 38
#endif

// Example of adding SD support to the C6 kit:
// #ifdef C6DEVKITC1
// #define SPI_PORT SPI2_HOST
// #define SPI_CLK 6
// #define SPI_MISO 19
// #define SPI_MOSI 20

// #define SD_PORT SPI_PORT
// #define SD_CS 19
// #endif

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
#include "sdmmc_cmd.h"
#include "driver/sdmmc_host.h"
#include "esp_wifi.h"
#include "mpm_parser.h"
#include "nvs_flash.h"

// not all compiler vendors implement stricmp
static int my_stricmp(const char* lhs, const char* rhs) {
    int result = 0;
    while (!result && *lhs && *rhs) {
        result = tolower(*lhs++) - tolower(*rhs++);
    }
    if (!result) {
        if (*lhs) {
            return 1;
        } else if (*rhs) {
            return -1;
        }
        return 0;
    }
    return result;
}

// used by the page handlers
typedef struct {
    char path_and_query[513];
    int method;
    void* handle;
    int fd;
} httpd_context_t;

typedef struct {
    const char* ext;
    const char* ctype;
} httpd_content_entry;

static httpd_content_entry httpd_content_types[] = {
    {".aac", "audio/aac"},
    {".avif", "image/avif"},
    {".bin", "application/octet-stream"},
    {".bmp", "image/bmp"},
    {".css", "text/css"},
    {".csv", "text/csv"},
    {".doc", "application/msword"},
    {".docx", "application/vnd.openxmlformats-officedocument.wordprocessingml.document"},
    {".epub", "application/epub+zip"},
    {".gz", "application/gzip"},
    {".gif", "image/gif"},
    {".ico", "image/x-icon"},
    {".jar", "application/java-archive"},
    {".js", "text/javascript"},
    {".mjs", "text/javascript"},
    {".json", "application/json"},
    {".mid", "audio/midi"},
    {".midi", "audio/midi"},
    {".mp3", "audio/mpeg"},
    {".mp4", "video/mp4"},
    {".mpeg", "video/mpeg"},
    {".ogg", "audio/ogg"},
    {".otf", "font/otf"},
    {".pdf", "application/pdf"},
    {".ppt", "application/vnd.ms-powerpoint"},
    {".pptx", "application/vnd.openxmlformats-officedocument.presentationml.presentation"},
    {".rar", "application/vnd.rar"},
    {".rtf", "application/rtf"},
    {".jpg", "image/jpeg"},
    {".jpeg", "image/jpeg"},
    {".png", "image/png"},
    {".apng", "image/apng"},
    {".htm", "text/html"},
    {".html", "text/html"},
    {".svg", "image/svg+xml"},
    {".tar", "application/x-tar"},
    {".tif", "image/tiff"},
    {".tiff", "image/tiff"},
    {".ttf", "font/ttf"},
    {".txt", "text/plain"},
    {".vsd", "application/vnd.visio"},
    {".wav", "audio/wav"},
    {".weba", "audio/webm"},
    {".webm", "video/webm"},
    {".webp", "image/webp"},
    {".woff", "font/woff"},
    {".woff2", "font/woff2"},
    {".xhtm", "application/xhtml+xml"},
    {".xhtml", "application/xhtml+xml"},
    {".xls", "application/vnd.ms-excel"},
    {".xlsx", "application/vnd.openxmlformats-officedocument.spreadsheetml.sheet"},
    {".xml", "application/xml"},
    {".zip", "application/zip"},
    {".7z", "application/x-7z-compressed"}
};
static const size_t httpd_content_types_size = 54;
static const char* httpd_content_type(const char* path) {
    const char* ext = strrchr(path,'.');
    if(ext!=NULL) {
        // could optimize with a binary search or DFA
        for(size_t i = 0;i<httpd_content_types_size;++i) {
            if(0==my_stricmp(httpd_content_types[i].ext,ext)) {
                return httpd_content_types[i].ctype;
            }
        }
    }
    return "application/octet-stream";
}
// here we put globals we use in the page
// url encoding tables
char enc_rfc3986[256] = {0};
char enc_html5[256] = {0};

#define WWW_CONTENT_IMPLEMENTATION
#include "www_content.h"

static stat_t fs_stat(const char* path) {
    stat_t s;
    stat(path, &s);
    return s;
}

#ifdef M5STACK_CORE2
using namespace esp_idf;  // devices
#endif

enum WIFI_STATUS { WIFI_WAITING, WIFI_CONNECTED, WIFI_CONNECT_FAILED };
static constexpr const EventBits_t wifi_connected_bit = BIT0;
static constexpr const EventBits_t wifi_fail_bit = BIT1;
static EventGroupHandle_t wifi_event_group = NULL;
static char wifi_ssid[65];
static char wifi_pass[129];
static esp_ip4_addr_t wifi_ip;
static size_t wifi_retry_count = 0;

static void wifi_event_handler(void* arg, esp_event_base_t event_base,
                               int32_t event_id, void* event_data) {
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    } else if (event_base == WIFI_EVENT &&
               event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (wifi_retry_count < 10) {
            esp_wifi_connect();
            ++wifi_retry_count;
        } else {
            puts("wifi connection failed");
            xEventGroupSetBits(wifi_event_group, wifi_fail_bit);
        }
    } else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        puts("got IP address");
        wifi_retry_count = 0;
        ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
        memcpy(&wifi_ip, &event->ip_info.ip, sizeof(wifi_ip));
        xEventGroupSetBits(wifi_event_group, wifi_connected_bit);
    }
}

static bool wifi_load(const char* path, char* ssid, char* pass) {
    FILE* file = fopen(path, "r");
    if (file != nullptr) {
        // parse the file
        fgets(ssid, 64, file);
        char* sv = strchr(ssid, '\n');
        if (sv != nullptr) *sv = '\0';
        sv = strchr(ssid, '\r');
        if (sv != nullptr) *sv = '\0';
        fgets(pass, 128, file);
        fclose(file);
        sv = strchr(pass, '\n');
        if (sv != nullptr) *sv = '\0';
        sv = strchr(pass, '\r');
        if (sv != nullptr) *sv = '\0';
        return true;
    }
    return false;
}

static void wifi_init(const char* ssid, const char* password) {
    nvs_flash_init();
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());

    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    esp_event_handler_instance_t instance_any_id;
    esp_event_handler_instance_t instance_got_ip;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        WIFI_EVENT, ESP_EVENT_ANY_ID, &wifi_event_handler, NULL,
        &instance_any_id));
    ESP_ERROR_CHECK(esp_event_handler_instance_register(
        IP_EVENT, IP_EVENT_STA_GOT_IP, &wifi_event_handler, NULL,
        &instance_got_ip));

    wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    memcpy(wifi_config.sta.ssid, ssid, strlen(ssid) + 1);
    memcpy(wifi_config.sta.password, password, strlen(password) + 1);
    wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA_WPA2_PSK;
    wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;
    // wifi_config.sta.sae_h2e_identifier[0]=0;
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());
}

static WIFI_STATUS wifi_status() {
    if (wifi_event_group == nullptr) {
        return WIFI_WAITING;
    }
    EventBits_t bits = xEventGroupGetBits(wifi_event_group) &
                       (wifi_connected_bit | wifi_fail_bit);
    if (bits == wifi_connected_bit) {
        return WIFI_CONNECTED;
    } else if (bits == wifi_fail_bit) {
        return WIFI_CONNECT_FAILED;
    }
    return WIFI_WAITING;
}

static httpd_handle_t httpd_handle = nullptr;
static SemaphoreHandle_t httpd_ui_sync = nullptr;

static char* httpd_url_encode(char* enc, size_t size, const char* s,
                              const char* table) {
    char* result = enc;
    if (table == NULL) table = enc_rfc3986;
    for (; *s; s++) {
        if (table[(int)*s]) {
            *enc++ = table[(int)*s];
            --size;
        } else {
            snprintf(enc, size, "%%%02X", *s);
            while (*++enc) {
                --size;
            }
        }
    }
    if (size) {
        *enc = '\0';
    }
    return result;
}

static char* httpd_url_decode(char* dst, size_t dstlen, const char* src) {
    char a, b;
    while (*src && dstlen) {
        if ((*src == '%') && ((a = src[1]) && (b = src[2])) &&
            (isxdigit(a) && isxdigit(b))) {
            if (a >= 'a') a -= 'a' - 'A';
            if (a >= 'A')
                a -= ('A' - 10);
            else
                a -= '0';
            if (b >= 'a') b -= 'a' - 'A';
            if (b >= 'A')
                b -= ('A' - 10);
            else
                b -= '0';
            *dst++ = 16 * a + b;
            dstlen--;
            src += 3;
        } else if (*src == '+') {
            *dst++ = ' ';
            dstlen--;
            src++;
        } else {
            *dst++ = *src++;
            dstlen--;
        }
    }
    if (dstlen) {
        *dst++ = '\0';
    }
    return dst;
}

static const char* httpd_crack_query(const char* next_query_part,
                                     char* out_name, size_t name_size,
                                     char* out_value, size_t value_size) {
    if (!*next_query_part) return NULL;

    const char start = *next_query_part;
    if (start == '&' || start == '?') {
        ++next_query_part;
    }
    size_t i = 0;
    char* name_cur = out_name;
    while (*next_query_part && *next_query_part != '=' &&
           *next_query_part != '&' && *next_query_part != ';') {
        if (i < name_size) {
            *name_cur++ = *next_query_part;
        }
        ++next_query_part;
        ++i;
    }
    if (name_size) {
        *name_cur = '\0';
    }
    if (!*next_query_part || *next_query_part == '&' ||
        *next_query_part == ';') {
        if (value_size) {
            *out_value = '\0';
        }
        return next_query_part;
    }
    ++next_query_part;
    i = 0;
    char* value_cur = out_value;
    while (*next_query_part && *next_query_part != '&' &&
           *next_query_part != ';') {
        if (i < value_size) {
            *value_cur++ = *next_query_part;
        }
        ++next_query_part;
        ++i;
    }
    if (value_size) {
        *value_cur = '\0';
    }
    return next_query_part;
}

static void httpd_send_block(const char* data, size_t len, void* arg) {
    if (!data || !len) {
        return;
    }
    httpd_context_t* resp_arg = (httpd_context_t*)arg;
    int fd = resp_arg->fd;
    if (fd > -1) {
        httpd_handle_t hd = (httpd_handle_t)resp_arg->handle;
        httpd_socket_send(hd, fd, data, len, 0);
    } else {
        httpd_req_t* r = (httpd_req_t*)resp_arg->handle;
        httpd_send(r, data, len);
    }
}

static void httpd_send_chunked(const char* buffer, size_t buffer_len,
                               void* arg) {
    char buf[64];
    if (buffer && buffer_len) {
        itoa(buffer_len, buf, 16);
        strcat(buf, "\r\n");
        httpd_send_block(buf,strlen(buf),arg);
        httpd_send_block(buffer,buffer_len,arg);
        httpd_send_block("\r\n",2,arg);
        return;
    }
    httpd_send_block("0\r\n\r\n", 5, arg);
}

static void httpd_send_expr(float expr, void* arg) {
    if (isnan(expr)) {
        return;
    }
    char buf[64] = {0};
    sprintf(buf, "%0.2f", expr);
    for (size_t i = sizeof(buf) - 1; i > 0; --i) {
        char ch = buf[i];
        if (ch == '0' || ch == '.') {
            buf[i] = '\0';
            if (ch == '.') {
                break;
            }
        } else if (ch != '\0') {
            break;
        }
    }
    httpd_send_chunked(buf, strlen(buf), arg);
}

static void httpd_send_expr(time_t time, void* arg) {
    httpd_send_expr(ctime(&time), arg);
}

static void httpd_send_expr(const char* expr, void* arg) {
    if (!expr || !*expr) {
        return;
    }
    httpd_send_chunked(expr, strlen(expr), arg);
}

// static void httpd_send_expr(int expr, void* arg) {
//     char buf[64];
//     itoa(expr, buf, 10);
//     httpd_send_chunked(buf, strlen(buf), arg);
// }

// static void httpd_send_expr(unsigned char expr, void* arg) {
//     char buf[64];
//     sprintf(buf, "%02d", (int)expr);
//     httpd_send_chunked(buf, strlen(buf), arg);
// }


// for reading HTTP request content bodies.
typedef struct {
    httpd_req_t* req;
    size_t remaining;
    size_t length;
    uint32_t start;
    int last_out;
    char buffer[UPLOAD_BUFFER_SIZE];
    char working[UPLOAD_WORKING_SIZE+1];
    size_t buffer_size;
    size_t buffer_pos;
} httpd_recv_buffer_t;

static int httpd_buffered_read(void* state) {
    if (state == nullptr) {
        return -1;
    }
    httpd_recv_buffer_t* rfb = (httpd_recv_buffer_t*)state;
    char buf[32];
    int num;
    char result;
    if (rfb->buffer_pos >= rfb->buffer_size) {
        if (!rfb->remaining) {
            goto done;
        }
        // reset the WDT
        vTaskDelay(5);
        int r = httpd_req_recv(rfb->req, rfb->buffer,
                               rfb->remaining <= sizeof(rfb->buffer)
                                   ? rfb->remaining
                                   : sizeof(rfb->buffer));
        num = ((float)(rfb->length-rfb->remaining)/(float)rfb->length)*100;
        if(num!=rfb->last_out) {
            itoa(num,buf,10);
            fputs("Uploading ",stdout);
            fputs(buf,stdout);
            puts("% complete");
            rfb->last_out = num;
        }
        if (r < 1) {
            rfb->buffer_size = 0;
            rfb->buffer_pos = 0;
            if(r==0) {
                goto done;
            }
            rfb->remaining = 0;
            return -1;
        }

        rfb->buffer_size = r;
        rfb->remaining -= r;
        rfb->buffer_pos = 0;
        if (rfb->buffer_size == 0) {
            goto done;
        }
    }
    result = rfb->buffer[rfb->buffer_pos];
    ++rfb->buffer_pos;
    return result;
done:
    return -1;
}
static esp_err_t httpd_request_handler(httpd_req_t* req) {
    // match the handler
    int handler_index = www_response_handler_match(req->uri);
    // printf("url: %s, index: %d\n",req->uri,handler_index);
    // we keep our response context on the stack if we can
    httpd_context_t resp_arg;
    // but for async responses it must be on the heap
    httpd_context_t* resp_arg_async;
    if (req->method == HTTP_GET || req->method == HTTP_POST) {  // async is only for GET and POST
        if (handler_index == WWW_RESPONSE_HANDLER_COUNT-1) { // this is our FS handler
            bool is_upload = req->method != HTTP_GET;
            // get the filepath from the path and query string
            char path[513];
            const char* sze = strchr(req->uri, '?');
            size_t path_len =
                sze == nullptr ? strlen(req->uri) : sze - req->uri;
            httpd_url_decode(path, path_len, req->uri);
            path[path_len] = '\0';
            if (is_upload) {
                char ctype[256];
                FILE* fcur = NULL;
                httpd_req_get_hdr_value_str(req, "Content-Type", ctype,
                                            sizeof(ctype));
                char* be = strstr(ctype, "boundary=");
                // this is multipart MIME upload
                if (be != nullptr) {
                    be += 9;
                    while (*be == ' ') {
                        ++be;
                    }
                    // not enough stack for this:
                    httpd_recv_buffer_t* recb = (httpd_recv_buffer_t*)malloc(
                        sizeof(httpd_recv_buffer_t));
                    if (recb == nullptr) {
                        goto error;  // out of memory;
                    }
                    memset(recb, 0, sizeof(httpd_recv_buffer_t));
                    recb->req = req;
                    recb->remaining = recb->length = req->content_len;
                    recb->start = pdTICKS_TO_MS(xTaskGetTickCount());
                    if (recb->remaining > 0) {
                        // initialize our multipart mime parser
                        mpm_context_t ctx;
                        mpm_init(be, 0, httpd_buffered_read, recb, &ctx);
                        size_t size = UPLOAD_WORKING_SIZE;
                        mpm_node_t node;
                        
                        bool disposition = false;
                        // parse the MIME data
                        while ((node = mpm_parse(&ctx, recb->working, &size)) >
                               0) {
                            switch (node) {
                                case MPM_HEADER_NAME_PART:
                                    recb->working[size] = '\0';
                                    disposition =
                                        0 == my_stricmp(recb->working,
                                                        "Content-Disposition");
                                    break;
                                case MPM_HEADER_VALUE_PART:
                                    if (disposition) {
                                        const char* sz = recb->working;
                                        recb->working[size] = 0;
                                        const char* szeq = strchr(sz, '=');
                                        if (szeq != nullptr) {
                                            recb->working[szeq - sz] = '\0';
                                            ++szeq;
                                            if (0 ==
                                                my_stricmp(sz, "filename")) {
                                                if (*szeq == '\"') {
                                                    ++szeq;
                                                    if (*szeq) {
                                                        recb->working
                                                            [(szeq -
                                                              recb->working) +
                                                             strlen(szeq) - 1] =
                                                            '\0';
                                                    }
                                                } else {
                                                    recb->working
                                                        [(szeq -
                                                          recb->working) +
                                                         strlen(szeq)] = '\0';
                                                }
                                                if(*szeq!='\0') { 
                                                    strcat(path, szeq);
#ifndef NO_STORE_UPLOAD
                                                    remove(path);  // delete if it
                                                                // exists;
                                                    fcur = fopen(path, "wb");
#else
                                                    fcur = nullptr;
#endif
                                                } else {
                                                    fcur = nullptr;
                                                }
                                            }
                                        }
                                    }
                                    break;

                                case MPM_CONTENT_PART:
                                    if (fcur != nullptr) {
                                        fwrite(recb->working, 1, size, fcur);
                                    }
                                    break;
                                case MPM_CONTENT_END:
                                    if (fcur != NULL) {
                                        fclose(fcur);
                                        fcur = NULL;
                                    }
                                    path[path_len] = '\0';
                                    break;
                                default: // don't care
                                    break;
                            }
                            size = UPLOAD_WORKING_SIZE;
                        }
                    }
                    if (recb != nullptr) {
                        char buf[32];
                        fputs("Uploading complete in ",stdout);
                        int num = (((float)pdTICKS_TO_MS(xTaskGetTickCount())-(float)recb->start)/1000.f+.5);
                        itoa(num,buf,10);
                        fputs(buf,stdout);
                        puts(" seconds");
                        free(recb);
                        recb = nullptr;
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
                        fputs("Deleting ",stdout);
                        puts(path);
                        remove(path);
                        path[path_len] = '\0';
                    }
                    free(data);
                }
            }
            stat_t st;
            if (0 == stat(path, &st) && ((st.st_mode & S_IFMT) != S_IFDIR)) {
                const char* pq = strrchr(req->uri,'?');
                char cname[64],cval[64];
                bool downloading = false;
                if(pq!=NULL) {
                    while (NULL!=(pq=httpd_crack_query(pq,cname,sizeof(cname),cval,sizeof(cval)))) {
                        if(0==my_stricmp(cname,"download")) {
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
                
                if(downloading) {
                    fputs("Downloading ",stdout);
                    puts(path);
                    static const char* headerd =
                        "HTTP/1.1 200 OK\r\nContent-Disposition: attachment; "
                        "filename=\"";
                    httpd_send(req, headerd, strlen(headerd));
                    httpd_send(req, szfn, strlen(szfn));
                    static const char* headerd2 = "\"\r\nContent-Length: ";
                    httpd_send(req, headerd2, strlen(headerd2));    
                
                } else {
                    static const char* headerv =
                        "HTTP/1.1 200 OK\r\nContent-Type: ";
                    httpd_send(req, headerv, strlen(headerv));
                    const char* ct = httpd_content_type(path);
                    httpd_send(req,ct,strlen(ct));
                    static const char* header2 = "\r\nContent-Length: ";
                    httpd_send(req, header2, strlen(header2));    
                }
                char buf[1024];
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
    for (int i = 0; i < 256; i++) {
        enc_rfc3986[i] =
            isalnum(i) || i == '~' || i == '-' || i == '.' || i == '_' ? i : 0;
        enc_html5[i] =
            isalnum(i) || i == '*' || i == '-' || i == '.' || i == '_' ? i
            : (i == ' ')                                               ? '+'
                                                                       : 0;
    }
    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.max_uri_handlers = 2;
    config.server_port = 80;
    config.max_open_sockets = (CONFIG_LWIP_MAX_SOCKETS - 3);
    config.uri_match_fn = httpd_match;
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
#if defined(SD_CS) || defined(SDMMC_D0)
sdmmc_card_t* sd_card = nullptr;
static bool sd_init() {
    static const char mount_point[] = "/sdcard";
    esp_vfs_fat_sdmmc_mount_config_t mount_config;
    memset(&mount_config, 0, sizeof(mount_config));
    mount_config.format_if_mount_failed = false;
    mount_config.max_files = 5;
    mount_config.allocation_unit_size = 0;
#if defined(SD_CS) 
    sdmmc_host_t host = SDSPI_HOST_DEFAULT();
    host.slot = SD_PORT;
    // // This initializes the slot without card detect (CD) and write
    // protect (WP)
    // // signals.
    sdspi_device_config_t slot_config;
    memset(&slot_config, 0, sizeof(slot_config));
    slot_config.host_id = (spi_host_device_t)SD_PORT;
    slot_config.gpio_cs = (gpio_num_t)SD_CS;
    slot_config.gpio_cd = SDSPI_SLOT_NO_CD;
    slot_config.gpio_wp = SDSPI_SLOT_NO_WP;
    slot_config.gpio_int = GPIO_NUM_NC;
    if (ESP_OK != esp_vfs_fat_sdspi_mount(mount_point, &host, &slot_config,
                                          &mount_config, &sd_card)) {
        return false;
    }
    return true;
#else
    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    host.flags = SDMMC_HOST_FLAG_1BIT; //use 1-line SD mode
    host.max_freq_khz = 20*1000;
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.clk = (gpio_num_t)SDMMC_CLK;
    slot_config.cmd = (gpio_num_t)SDMMC_CMD;
    slot_config.d0 = (gpio_num_t)SDMMC_D0;
    slot_config.width = 1;
    // assuming the board is built correctly, we don't need this:
    // slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;
    esp_err_t ret = esp_vfs_fat_sdmmc_mount(mount_point, &host, &slot_config, &mount_config, &sd_card);
    if(ret!=ESP_OK) {
        return 0;
    }
    return true;
#endif
}
#endif


static void spiffs_init() {
    esp_vfs_spiffs_conf_t conf;
    memset(&conf, 0, sizeof(conf));
    conf.base_path = "/spiffs";
    conf.partition_label = NULL;
    conf.max_files = 5;
    conf.format_if_mount_failed = true;
    if (ESP_OK != esp_vfs_spiffs_register(&conf)) {
        puts("Unable to initialize SPIFFS");
        while (1) vTaskDelay(5);
    }
}

static void loop();
static void loop_task(void* arg) {
    uint32_t ts = pdTICKS_TO_MS(xTaskGetTickCount());
    while (1) {
        loop();
        uint32_t ms = pdTICKS_TO_MS(xTaskGetTickCount());
        if (ms > ts + 200) {
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
#ifdef SPI_PORT
    spi_init();  // used by the SD reader
#endif
    bool loaded = false;

    wifi_ssid[0] = 0;

    wifi_pass[0] = 0;

#if defined(SD_CS) || defined(SDMMC_D0)
    if (sd_init()) {
        puts("SD card found, looking for wifi.txt creds");
        loaded = wifi_load("/sdcard/wifi.txt", wifi_ssid, wifi_pass);
    }
#endif
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
            snprintf(url_text, sizeof(url_text), "http://" IPSTR,
                     IP2STR(&wifi_ip));
            puts(url_text);
            uint32_t free_sram = esp_get_free_internal_heap_size() ;
            printf("Free SRAM: %0.2fKB\n",
                   free_sram / 1024.f);
            printf("SRAM used for webserver firmware: %0.2fKB\n",
                   (start_sram-free_sram) / 1024.f);
            
            
        }
    } else {
        if (wifi_status() == WIFI_CONNECT_FAILED) {
            // we disconnected for some reason
            puts("Disconnected");
            is_connected = false;
            httpd_end();
            wifi_retry_count = 0;
            esp_wifi_start();
            printf("Free SRAM: %0.2fKB\n",
                   esp_get_free_internal_heap_size() / 1024.f);
        }
    }
}
