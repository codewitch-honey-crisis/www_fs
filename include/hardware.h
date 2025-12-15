#ifndef HARDWARE_H
#define HARDWRE_H

// SD support on the Core2
#ifdef M5STACK_CORE2
#define SPI_PORT SPI3_HOST
#define SPI_CLK 18
#define SPI_MISO 38
#define SPI_MOSI 23

#define SD_PORT SPI_PORT
#define SD_CS 4
#endif
// SD support on the Freenove S3 Devkit
#ifdef FREENOVE_S3_DEVKIT
#define SDMMC_D0 40
#define SDMMC_CLK 39
#define SDMMC_CMD 38
#endif

#ifdef NEOPIXEL
#ifdef C6DEVKITC1
#define NEOPIXEL_DOUT 8
#define NEOPIXEL_FORMAT LED_STRIP_COLOR_COMPONENT_FMT_RGB
#define NEOPIXEL_TYPE LED_MODEL_SK6812
#endif
#ifdef FREENOVE_S3_DEVKIT
#define NEOPIXEL_DOUT 48
#define NEOPIXEL_FORMAT LED_STRIP_COLOR_COMPONENT_FMT_GRB
#define NEOPIXEL_TYPE LED_MODEL_WS2812
#endif

#endif

#endif