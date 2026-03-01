#include "framebuffer_gfx.h"
#include <Arduino.h>
#include "graphics.h"

Framebuffer_GFX::Framebuffer_GFX(uint16_t w, uint16_t h)
  : Adafruit_GFX(w, h), buffer(nullptr), fb_w(w), fb_h(h)
{
}

void Framebuffer_GFX::setBuffer(uint8_t* buf)
{
  buffer = buf;
}

void Framebuffer_GFX::drawPixel(int16_t x, int16_t y, uint16_t color)
{
  if (!buffer)
    return;
  if (x < 0 || y < 0 || x >= (int16_t)fb_w || y >= (int16_t)fb_h)
    return;

  // Map any non-zero color to full white (255), zero to black (0)
  buffer[y * fb_w + x] = color;
}
