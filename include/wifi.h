#ifndef WIFI_H
#define WIFI_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif

typedef enum { WIFI_WAITING,
               WIFI_CONNECTED,
               WIFI_CONNECT_FAILED 
} wifi_status_t;

bool wifi_load(const char* path, char* ssid, char* pass);
void wifi_init(const char* ssid, const char* password);
void wifi_restart(void);
uint32_t wifi_ip_address(void);
wifi_status_t wifi_status(void);

#ifdef __cplusplus
}
#endif
#endif // WIFI_H