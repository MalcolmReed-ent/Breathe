#include <stdlib.h>
#include <stdbool.h>
#include "rectangle.h"

Rectangle rectangle_intersect(const Rectangle *a, const Rectangle *b)
{
    int x1 = (a->x > b->x) ? a->x : b->x;
    int x2 = ((a->x + a->width) < (b->x + b->width)) ? (a->x + a->width) : (b->x + b->width);

    int y1 = (a->y > b->y) ? a->y : b->y;
    int y2 = ((a->y + a->height) < (b->y + b->height)) ? (a->y + a->height) : (b->y + b->height);

    return (Rectangle){x1, y1, x2 - x1, y2 - y1};
}

RectangleArray rectangle_subtract(const Rectangle *a, const Rectangle *b)
{
    RectangleArray result = {NULL, 0};
    Rectangle intersection = rectangle_intersect(a, b);

    if (rectangle_is_invalid(&intersection))
    {
        result.rectangles = malloc(sizeof(Rectangle));
        result.rectangles[0] = *a;
        result.size = 1;
        return result;
    }

    result.rectangles = malloc(4 * sizeof(Rectangle));
    result.size = 0;

    if (a->y < b->y)
    {
        result.rectangles[result.size++] = (Rectangle){a->x, a->y, a->width, b->y - a->y};
    }

    if (a->y + a->height > b->y + b->height)
    {
        result.rectangles[result.size++] = (Rectangle){a->x, b->y + b->height, a->width, a->y + a->height - (b->y + b->height)};
    }

    if (a->x < b->x)
    {
        result.rectangles[result.size++] = (Rectangle){a->x, b->y, b->x - a->x, b->height};
    }

    if (a->x + a->width > b->x + b->width)
    {
        result.rectangles[result.size++] = (Rectangle){b->x + b->width, b->y, a->x + a->width - (b->x + b->width), b->height};
    }

    return result;
}

bool rectangle_is_invalid(const Rectangle *p)
{
    return p->x < 0 || p->y < 0 || p->width < 0 || p->height < 0;
}

bool rectangle_equals(const Rectangle *a, const Rectangle *b)
{
    return a->x == b->x && a->y == b->y && a->width == b->width && a->height == b->height;
}

Rectangle rectangle_normalize(const Rectangle *r)
{
    Rectangle result = *r;

    if (result.width < 0)
    {
        result.width *= -1;
        result.x -= result.width;
    }

    if (result.height < 0)
    {
        result.height *= -1;
        result.y -= result.height;
    }

    return result;
}

Rectangle rectangle_pad(const Rectangle *r, int p)
{
    return (Rectangle){r->x - p, r->y - p, r->width + 2 * p, r->height + 2 * p};
}
