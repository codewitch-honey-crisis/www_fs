#include <sys/stat.h>
#include <sys/unistd.h>
#include <string.h>
#include "esp_http_server.h"
#include "www_application.h"

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

