#ifndef GRAPHICS_H
#define GRAPHICS_H

#include <stdint.h>
#include "vector_font.h"

// Declare external variables
extern uint8_t* frame_buffer;
extern uint16_t width;
extern uint16_t height;

// Function declarations
void set_pixel(uint16_t x, uint16_t y, uint8_t value);
void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t value);
void draw_thick_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, uint8_t value);
void draw_rectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t value);
void draw_filled_rectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t value);
void draw_circle(int16_t cx, int16_t cy, int16_t radius, uint8_t value);
void draw_digit(int16_t x, int16_t y, uint8_t digit, uint8_t scale, uint8_t value);
void draw_number(int16_t x, int16_t y, int32_t number, uint8_t scale, uint8_t value);
void draw_vector_character(int16_t x, int16_t y, const VectorCharacter* character, float scale, float vert_scale, uint8_t value);
void draw_vector_string(int16_t x, int16_t y, const char* text, float scale, float vert_scale, uint8_t value);

#endif // GRAPHICS_H
