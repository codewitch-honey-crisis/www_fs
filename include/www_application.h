#ifndef WWW_APPLICATION_H
#define WWW_APPLICATION_H
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <ctype.h>
#include <dirent.h>
#include <time.h>
extern char enc_rfc3986[256];
extern char enc_html5[256];
#if defined(SD_CS) || defined(SDMMC_D0)
extern sdmmc_card_t* sd_card;
#endif
/// @brief Sends data to a connected socket
/// @param data The data to send
/// @param len The length of the data
/// @param arg The application defined context
static void httpd_send_block(const char* data, size_t len, void* arg);
/// @brief Send chunked data over a connected socket
/// @param data The data to send
/// @param len The length of the data
/// @param arg The application defined context
static void httpd_send_chunked(const char* data, size_t len, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
static void httpd_send_expr(int expr, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
static void httpd_send_expr(unsigned char expr, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
static void httpd_send_expr(float expr, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
static void httpd_send_expr(time_t expr, void* arg);
/// @brief Sends an expression to a connected socket
/// @param expr The expression
/// @param arg The application defined context
static void httpd_send_expr(const char* expr, void* arg);
/// @brief URL encodes a string
/// @param enc The encoded output buffer
/// @param size The size of the out buffer
/// @param s The string to encode
/// @param table The table to use (enc_rfc3986 or enc_html5)
/// @return The encoded output buffer
static char *httpd_url_encode(char *enc, size_t size, const char *s, const char *table);
typedef struct stat stat_t;
/// @brief Gets the stat for a path
/// @param path The path
/// @return A stat_t structure with the file or directory info
static stat_t fs_stat(const char* path);
#endif