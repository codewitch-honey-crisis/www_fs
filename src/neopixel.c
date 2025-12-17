#include "neopixel.h"
#include <memory.h>
#include "hardware.h"
#ifdef NEOPIXEL_DOUT
#include "led_strip.h"
#include "led_strip_rmt.h"
static led_strip_handle_t neopixel_handle = NULL;
#endif
bool neopixel_init(void) {
#ifdef NEOPIXEL_DOUT
    led_strip_config_t led_cfg;
    memset(&led_cfg, 0, sizeof(led_cfg));
    led_cfg.color_component_format = NEOPIXEL_FORMAT;
    led_cfg.led_model = NEOPIXEL_TYPE;
    led_cfg.max_leds = 1;
    led_cfg.strip_gpio_num = (gpio_num_t)NEOPIXEL_DOUT;
    led_strip_rmt_config_t led_rmt_cfg;
    memset(&led_rmt_cfg, 0, sizeof(led_rmt_cfg));
    return ESP_OK==led_strip_new_rmt_device(&led_cfg, &led_rmt_cfg, &neopixel_handle);
#else
    return false;
#endif
}
void neopixel_color(uint8_t r, uint8_t g, uint8_t b) {
    if(neopixel_handle==NULL) return;
#ifdef NEOPIXEL_DOUT
    if(r==0&&g==0&&b==0) {
        led_strip_clear(neopixel_handle);
        led_strip_refresh(neopixel_handle);
    } else {
        led_strip_set_pixel(neopixel_handle,0,r,g,b);
        led_strip_refresh(neopixel_handle);
    }
#endif
}