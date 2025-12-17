
// for testing, won't actually store the uploaded file on the FS
// #define HTTPD_NO_STORE_UPLOAD

// 8192 seems to yield the most performance. After that it levels off
#ifndef HTTPD_UPLOAD_BUFFER_SIZE
#define HTTPD_UPLOAD_BUFFER_SIZE 8192
#endif
#ifndef HTTPD_DOWNLOAD_BUFFER_SIZE
#define HTTPD_DOWNLOAD_BUFFER_SIZE 8192
#endif
#ifndef HTTPD_UPLOAD_WORKING_SIZE
#define HTTPD_UPLOAD_WORKING_SIZE 8192
#endif
#ifndef HTTPD_STACK_SIZE
#define HTTPD_STACK_SIZE (32 * 1024)
#endif
#include <sys/stat.h>
#include <sys/unistd.h>
#include <string.h>
#include "esp_http_server.h"
#include "httpd_application.h"
#include "mpm_parser.h"
#define HTTPD_CONTENT_IMPLEMENTATION
#include "httpd_content.h"

char enc_rfc3986[256] = {0};
char enc_html5[256] = {0};

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
const char* httpd_content_type(const char* path) {
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
char* httpd_url_encode(char* enc, size_t size, const char* s,
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

char* httpd_url_decode(char* dst, size_t dstlen, const char* src) {
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

const char* httpd_crack_query(const char* next_query_part,
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

void httpd_send_block(const char* data, size_t len, void* arg) {
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
void httpd_init_encoding(void) {
    for (int i = 0; i < 256; i++) {
        enc_rfc3986[i] =
            isalnum(i) || i == '~' || i == '-' || i == '.' || i == '_' ? i : 0;
        enc_html5[i] =
            isalnum(i) || i == '*' || i == '-' || i == '.' || i == '_' ? i
            : (i == ' ')                                               ? '+'
                                                                       : 0;
    }
}
void httpd_send_chunked(const char* buffer, size_t buffer_len,
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

void httpd_send_expr(float expr, void* arg) {
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

void httpd_send_expr(time_t time, void* arg) {
    httpd_send_expr(ctime(&time), arg);
}

void httpd_send_expr(const char* expr, void* arg) {
    if (!expr || !*expr) {
        return;
    }
    httpd_send_chunked(expr, strlen(expr), arg);
}

// void httpd_send_expr(int expr, void* arg) {
//     char buf[64];
//     itoa(expr, buf, 10);
//     httpd_send_chunked(buf, strlen(buf), arg);
// }

// void httpd_send_expr(unsigned char expr, void* arg) {
//     char buf[64];
//     sprintf(buf, "%02d", (int)expr);
//     httpd_send_chunked(buf, strlen(buf), arg);
// }


static httpd_handle_t httpd_handle = nullptr;
static SemaphoreHandle_t httpd_ui_sync = nullptr;

// for reading HTTP request content bodies.
typedef struct {
    httpd_req_t* req;
    size_t remaining;
    size_t length;
    uint32_t start;
    int previous_percent;
    char buffer[HTTPD_UPLOAD_BUFFER_SIZE];
    char working[HTTPD_UPLOAD_WORKING_SIZE + 1];
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
    int handler_index = httpd_response_handler_match(req->uri);
    // we keep our response context on the stack if we can
    httpd_context_t resp_arg;
    // but for async responses it must be on the heap
    httpd_context_t* resp_arg_async;
    if (req->method == HTTP_GET || req->method == HTTP_POST) {  // async is only for GET and POST
        if (handler_index == HTTPD_RESPONSE_HANDLER_COUNT - 1) {  // this is our FS handler
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
                        size_t size = HTTPD_UPLOAD_WORKING_SIZE;
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
#ifndef HTTPD_NO_STORE_UPLOAD
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
                            size = HTTPD_UPLOAD_WORKING_SIZE;
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
                char buf[HTTPD_DOWNLOAD_BUFFER_SIZE];
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
            handler_fn = httpd_content_404_clasp;
        } else if (handler_index == -2) {
            handler_fn = httpd_content_500_clasp;
        } else {
            // choose the handler
            handler_fn =
                (httpd_work_fn_t)httpd_response_handlers[handler_index].handler;
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
        httpd_content_404_clasp(resp_arg_async);
    } else if (handler_index == -2) {
        httpd_content_500_clasp(resp_arg_async);
    } else {
        httpd_response_handlers[handler_index].handler(resp_arg_async);
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
    httpd_content_500_clasp(resp_arg_async);
    return ESP_OK;
}
static bool httpd_match(const char* cmp, const char* uri, size_t len) {
    return true;  // match anything.
}
void httpd_init(void) {
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

void httpd_end(void) {
    if (httpd_handle == nullptr) {
        return;
    }
    ESP_ERROR_CHECK(httpd_stop(httpd_handle));
    httpd_handle = nullptr;
    vSemaphoreDelete(httpd_ui_sync);
    httpd_ui_sync = nullptr;
}

stat_t fs_stat(const char* path) {
    stat_t s;
    stat(path, &s);
    return s;
}

int my_stricmp(const char* lhs, const char* rhs) {
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

