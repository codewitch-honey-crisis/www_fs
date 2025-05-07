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
#ifdef SD_CS
extern sdmmc_card_t* sd_card;
#endif
static void httpd_send_block(const char* data, size_t len, void* arg);
static void httpd_send_chunked(const char* data, size_t len, void* arg);
static void httpd_send_expr(int expr, void* arg);
static void httpd_send_expr(unsigned char expr, void* arg);
static void httpd_send_expr(float expr, void* arg);
static void httpd_send_expr(time_t expr, void* arg);
static void httpd_send_expr(const char* expr, void* arg);
static char *httpd_url_encode(char *enc, size_t size, const char *s, const char *table);
typedef struct stat stat_t;
static const int fs_id(dirent* d);
static const char* fs_name(dirent* d);
static bool fs_is_dir(dirent* d);
static stat_t fs_stat(const char* path);
#endif