#include "graphics.h"
#include <math.h>
#include <stdio.h>

uint8_t* frame_buffer;
uint16_t width;
uint16_t height;

void set_pixel(uint16_t x, uint16_t y, uint8_t value)
{
    if (x < width && y < height)
    {
        frame_buffer[y * width + x] = value;
    }
}

void draw_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t value)
{
    // Bresenham's line algorithm
    int16_t dx = abs(x1 - x0);
    int16_t dy = abs(y1 - y0);
    int16_t sx = (x0 < x1) ? 1 : -1;
    int16_t sy = (y0 < y1) ? 1 : -1;
    int16_t err = dx - dy;

    int16_t x = x0;
    int16_t y = y0;

    while (true)
    {
        set_pixel(x, y, value);

        if (x == x1 && y == y1)
            break;

        int16_t e2 = 2 * err;
        if (e2 > -dy)
        {
            err -= dy;
            x += sx;
        }
        if (e2 < dx)
        {
            err += dx;
            y += sy;
        }
    }
}

void draw_thick_line(int16_t x0, int16_t y0, int16_t x1, int16_t y1, uint8_t thickness, uint8_t value)
{
    // Draw multiple parallel lines to create a thick line
    int16_t dx = x1 - x0;
    int16_t dy = y1 - y0;
    int16_t len = sqrt(dx * dx + dy * dy);
    
    if (len == 0)
        return;
    
    // Perpendicular direction
    float px = -dy / (float)len;
    float py = dx / (float)len;
    
    int16_t offset = thickness / 2;
    for (int16_t i = -offset; i <= offset; i++)
    {
        int16_t x0_offset = x0 + (int16_t)(px * i);
        int16_t y0_offset = y0 + (int16_t)(py * i);
        int16_t x1_offset = x1 + (int16_t)(px * i);
        int16_t y1_offset = y1 + (int16_t)(py * i);
        
        draw_line(x0_offset, y0_offset, x1_offset, y1_offset, value);
    }
}

void draw_rectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t value)
{
    draw_line(x, y, x + w, y, value);
    draw_line(x + w, y, x + w, y + h, value);
    draw_line(x + w, y + h, x, y + h, value);
    draw_line(x, y + h, x, y, value);
}

void draw_filled_rectangle(int16_t x, int16_t y, int16_t w, int16_t h, uint8_t value)
{
    for (int16_t yy = y; yy < y + h; yy++)
    {
        for (int16_t xx = x; xx < x + w; xx++)
        {
            set_pixel(xx, yy, value);
        }
    }
}

void draw_circle(int16_t cx, int16_t cy, int16_t radius, uint8_t value)
{
    // Midpoint circle algorithm
    int16_t x = radius;
    int16_t y = 0;
    int16_t d = 3 - 2 * radius;
    
    while (x >= y)
    {
        set_pixel(cx + x, cy + y, value);
        set_pixel(cx - x, cy + y, value);
        set_pixel(cx + x, cy - y, value);
        set_pixel(cx - x, cy - y, value);
        set_pixel(cx + y, cy + x, value);
        set_pixel(cx - y, cy + x, value);
        set_pixel(cx + y, cy - x, value);
        set_pixel(cx - y, cy - x, value);
        
        if (d < 0)
            d = d + 4 * y + 6;
        else
        {
            d = d + 4 * (y - x) + 10;
            x--;
        }
        y++;
    }
}

void draw_vector_character(int16_t x, int16_t y, const VectorCharacter* character, float scale, float vert_scale, uint8_t value)
{
    if (character == NULL || character->points == NULL)
        return;

    // Draw lines connecting the points
    for (int i = 0; i < character->point_count - 1; i++)
    {
        int16_t x0 = x + (int16_t)(character->points[i].X/75.0 * scale);
        int16_t y0 = y + (int16_t)(-character->points[i].Y/88.0 * scale * vert_scale);
        int16_t x1 = x + (int16_t)(character->points[i + 1].X/75.0 * scale);
        int16_t y1 = y + (int16_t)(-character->points[i + 1].Y/88.0 * scale * vert_scale);

        draw_thick_line(x0, y0, x1, y1, 5, value);
    }
}

void draw_vector_string(int16_t x, int16_t y, const char* text, float scale, float vert_scale, uint8_t value)
{
    if (text == NULL)
        return;

    int16_t cursor_x = x;
    int16_t char_spacing = (int16_t)(0.7 * scale); // Space between characters

    while (*text)
    {
        if (*text >= '0' && *text <= '9')
        {
            draw_vector_character(cursor_x, y, &vector_chars[*text - '0'], scale, vert_scale, value);
            cursor_x += char_spacing;
        }
        else if (*text == '/')
        {
            draw_vector_character(cursor_x, y, &vector_chars[11], scale, vert_scale, value);
            cursor_x += char_spacing;
        }
        else if (*text == ' ')
        {
            cursor_x += char_spacing / 2;
        }

        text++;
    }
}

