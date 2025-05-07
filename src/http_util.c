#include "http_util.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>
static char s_enc_rfc3986[256] = {0};
static char s_enc_html5[256] = {0};
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
static void s_enc_init() {
    if (!s_enc_html5[65]) {
        for (int i = 0; i < 256; i++) {
            s_enc_rfc3986[i] =
                isalnum(i) || i == '~' || i == '-' || i == '.' || i == '_' ? i
                                                                           : 0;
            s_enc_html5[i] =
                isalnum(i) || i == '*' || i == '-' || i == '.' || i == '_' ? i
                : (i == ' ')                                               ? '+'
                                                                           : 0;
        }
    }
}
static char* http_url_encode(const char* s, char* enc, size_t size,
                             const char* table) {
    *enc = '\0';
    s_enc_init();
    char* result = enc;
    for (; *s; s++) {
        if (table[(int)*s]) {
            *enc++ = table[(int)*s];
            *enc = '\0';
            --size;
        } else {
            snprintf(enc, size, "%%%02X", *s);
            while (*++enc) {
                --size;
            }
        }
    }
    return result;
}
static char* http_url_encode_part(const char* s, size_t len, char* enc,
                                  size_t size, const char* table) {
    *enc = '\0';
    s_enc_init();
    char* result = enc;
    for (; *s && len; len--, s++) {
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
    return result;
}
char* http_url_decode(const char* data, char* out_buffer, size_t out_size) {
    char a, b;
    while (*data && out_size) {
        if ((*data == '%') && ((a = data[1]) && (b = data[2])) &&
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
            *out_buffer++ = 16 * a + b;
            --out_size;
            if (out_size <= 1) {
                break;
            }
            data += 3;
        } else if (*data == '+') {
            *out_buffer++ = ' ';
            --out_size;
            if (out_size <= 1) {
                break;
            }
            data++;

        } else {
            *out_buffer++ = *data++;
            --out_size;
            if (out_size <= 1) {
                break;
            }
        }
    }
    if (out_size) {
        *out_buffer++ = '\0';
    }
    return out_buffer;
}
char* http_url_decode_part(const char* data, size_t size, char* out_buffer,
                           size_t out_size) {
    char a, b;
    while (*data && size && out_size) {
        if ((*data == '%') && ((a = data[1]) && (b = data[2])) &&
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
            *out_buffer++ = 16 * a + b;
            --out_size;
            if (out_size <= 1) {
                break;
            }
            data += 3;
            size -= 3;
        } else if (*data == '+') {
            *out_buffer++ = ' ';
            --out_size;
            if (out_size <= 1) {
                break;
            }
            data++;
            --size;

        } else {
            *out_buffer++ = *data++;
            --size;
            --out_size;
            if (out_size <= 1) {
                break;
            }
        }
    }
    if (out_size) {
        *out_buffer++ = '\0';
    }
    return out_buffer;
}
const char* http_crack_path(char* out_segment, size_t segment_size,
                            const char* next_path_part) {
    if (!*next_path_part || *next_path_part == '?') return NULL;

    const char start = *next_path_part;
    if (start == '/') {
        ++next_path_part;
    }
    size_t i = 0;
    char* segment_cur = out_segment;
    while (*next_path_part && *next_path_part != '/' &&
           *next_path_part != '?') {
        if (i < segment_size) {
            *segment_cur++ = *next_path_part;
        }
        ++next_path_part;
        ++i;
    }
    if (segment_size) {
        *segment_cur = '\0';
    }
    return next_path_part;
}

const char* http_crack_query(char* out_name, size_t name_size, char* out_value,
                             size_t value_size, const char* next_query_part) {
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

char* http_url_encode_rfc3986(const char* data, char* out_buffer,
                              size_t out_size) {
    return http_url_encode(data, out_buffer, out_size, s_enc_rfc3986);
}
char* http_url_encode_html5(const char* data, char* out_buffer,
                            size_t out_size) {
    return http_url_encode(data, out_buffer, out_size, s_enc_html5);
}
char* http_url_encode_rfc3986_part(const char* data, size_t size,
                                   char* out_buffer, size_t out_size) {
    return http_url_encode_part(data, size, out_buffer, out_size,
                                s_enc_rfc3986);
}
char* http_url_encode_html5_part(const char* data, size_t size,
                                 char* out_buffer, size_t out_size) {
    return http_url_encode_part(data, size, out_buffer, out_size, s_enc_html5);
}
char* http_url_encode_path(const char* path, char* out_buffer,
                           size_t out_size) {
    char seg[256];
    char enc[384];
    *out_buffer = '\0';
    if (path[0] == '/' && path[1] == '\0') {
        strncpy(out_buffer, path, out_size - 1);
        return out_buffer;
    }
    const char* sz = path;
    while (sz && *sz) {
        sz = http_crack_path(seg, sizeof(seg), sz);
        strncat(out_buffer, "/", out_size - 1);
        http_url_encode(seg, enc, sizeof(enc), s_enc_rfc3986);
        strncat(out_buffer, enc, out_size - 1);
    }
    return out_buffer;
}

char* http_url_decode_path(const char* path, char* out_buffer,
                           size_t out_size) {
    char seg[256];
    char enc[256];
    *out_buffer = '\0';
    if (path[0] == '/' && path[1] == '\0') {
        strncpy(out_buffer, path, out_size - 1);
        return out_buffer;
    }
    const char* sz = path;
    while (sz && *sz) {
        sz = http_crack_path(seg, sizeof(seg), sz);
        strncat(out_buffer, "/", out_size - 1);
        http_url_decode(seg, enc, sizeof(enc));
        strncat(out_buffer, enc, out_size - 1);
    }
    return out_buffer;
}
