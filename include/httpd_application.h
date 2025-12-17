#pragma once
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
#include "hardware.h"
#include "sdcard.h"
#include "neopixel.h"

// used by the page handlers
typedef struct {
    char path_and_query[513];
    int method;
    void* handle;
    int fd;
} httpd_context_t;

extern char enc_rfc3986[256];
extern char enc_html5[256];

// not all compiler vendors implement stricmp
int my_stricmp(const char* lhs, const char* rhs);
/// @brief Initializes the URL encoding tables
void httpd_init_encoding(void);
/// @brief Sends data to a connected socket
/// @param data The data to send
/// @param len The length of the data
/// @param arg The application defined context
void httpd_send_block(const char* data, size_t len, void* arg);
/// @brief Send chunked data over a connected socket
/// @param data The data to send
/// @param len The length of the data
/// @param arg The application defined context
void httpd_send_chunked(const char* data, size_t len, void* arg);
// /// @brief Sends an expression to a connected socket
// /// @param expr The expression
// /// @param arg The application defined context
// void httpd_send_expr(int expr, void* arg);
// /// @brief Sends an expression to a connected socket
// /// @param expr The expression
// /// @param arg The application defined context
// void httpd_send_expr(unsigned char expr, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
void httpd_send_expr(float expr, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
void httpd_send_expr(time_t expr, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
void httpd_send_expr(const char* expr, void* arg);
/// @brief URL encodes a string
/// @param enc The encoded output buffer
/// @param size The size of the out buffer
/// @param s The string to encode
/// @param table The table to use (enc_rfc3986 or enc_html5)
/// @return The encoded output buffer
char *httpd_url_encode(char *enc, size_t size, const char *s, const char *table);
/// @brief Decodes an URL encoded string
/// @param dst The decoded output buffer
/// @param dstlen The size of the output buffer
/// @param src The source to encode
/// @return The decoded output buffer
char* httpd_url_decode(char* dst, size_t dstlen, const char* src);

/// @brief Cracks a query string into name/value pairs
/// @param next_query_part The next part of the string to crack
/// @param out_name A buffer to hold the name
/// @param name_size The size of the out buffer
/// @param out_value A buffer to hold the value
/// @param value_size The size of the out buffer
/// @return The next query segment
const char* httpd_crack_query(const char* next_query_part,
                                     char* out_name, size_t name_size,
                                     char* out_value, size_t value_size);
/// @brief Retrieves the MIME content-type for the path
/// @param path The path to check
/// @return The MIME type
const char* httpd_content_type(const char* path);

/// @brief Start the httpd service
void httpd_init(void);

/// @brief Stop the httpd service
void httpd_end(void);

typedef struct stat stat_t;
/// @brief Gets the stat for a path
/// @param path The path
/// @return A stat_t structure with the file or directory info
stat_t fs_stat(const char* path);
