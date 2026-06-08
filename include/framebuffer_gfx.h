#ifndef FRAMEBUFFER_GFX_H
#define FRAMEBUFFER_GFX_H

#include <Adafruit_GFX.h>
#include <stdint.h>

class Framebuffer_GFX : public Adafruit_GFX {
public:
  Framebuffer_GFX(uint16_t w, uint16_t h);
  void setBuffer(uint8_t* buf);
  void drawPixel(int16_t x, int16_t y, uint16_t color) override;

private:
  uint8_t* buffer;
  uint16_t fb_w;
  uint16_t fb_h;
};

#endif // FRAMEBUFFER_GFX_H
