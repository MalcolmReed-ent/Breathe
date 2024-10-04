#ifndef RECTANGLE_H
#define RECTANGLE_H

#include <stdbool.h>

typedef struct {
    int x, y, width, height;
} Rectangle;

typedef struct {
    Rectangle *rectangles;
    int size;
} RectangleArray;

Rectangle rectangle_intersect(const Rectangle *a, const Rectangle *b);
RectangleArray rectangle_subtract(const Rectangle *a, const Rectangle *b);
bool rectangle_is_invalid(const Rectangle *p);
bool rectangle_equals(const Rectangle *a, const Rectangle *b);
Rectangle rectangle_normalize(const Rectangle *r);
Rectangle rectangle_pad(const Rectangle *r, int p);

#endif // RECTANGLE_H
