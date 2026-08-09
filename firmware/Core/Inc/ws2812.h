#ifndef WS2812_H
#define WS2812_H

#include <stddef.h>
#include <stdint.h>

#define WS2812_MAX_LEDS 4U

void ws2812_init();

void ws2812_set_rgb(size_t index, uint8_t red, uint8_t green, uint8_t blue);

void
ws2812_set_hsv(size_t index, uint8_t hue, uint8_t saturation, uint8_t value);

#endif /* WS2812_H */
