#ifdef NEOPIXEL
led_strip_clear(neopixel_handle);
led_strip_refresh(neopixel_handle);
#endif
if(((httpd_context_t*)resp_arg)->fd>-1) free(resp_arg);