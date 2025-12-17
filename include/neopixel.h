#ifndef NEOPIXEL_H
#define NEOPIXEL_H
#include <stdbool.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
bool neopixel_init(void);
void neopixel_color(uint8_t r, uint8_t g, uint8_t b);
#ifdef __cplusplus
}
#endif
#endif // NEOPIXEL_H