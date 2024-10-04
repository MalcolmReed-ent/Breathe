#include <stdbool.h>
#include <math.h>
#include "coordconv.h"
#include <poppler.h>

CoordConv coord_conv_create(PopplerPage *p, const Rectangle *r, bool i, int rotation)
{
    CoordConv cc;
    cc.rect = *r;
    cc.inverty = i;

    double width, height;
    poppler_page_get_size(p, &width, &height);

    if (rotation % 180 != 0)
    {
        double temp = width;
        width = height;
        height = temp;
    }

    cc.xscale = width / (double)r->width;
    cc.yscale = height / (double)r->height;

    return cc;
}

double coord_conv_to_pdf_x(const CoordConv *cc, int x)
{
    return (x - cc->rect.x) * cc->xscale;
}

double coord_conv_to_pdf_y(const CoordConv *cc, int y)
{
    if (cc->inverty)
        return (cc->rect.height - (y - cc->rect.y)) * cc->yscale;
    return (y - cc->rect.y) * cc->yscale;
}

Rectangle coord_conv_to_pdf(const CoordConv *cc, const Rectangle *r)
{
    return (Rectangle){
        (int)coord_conv_to_pdf_x(cc, r->x),
        (int)coord_conv_to_pdf_y(cc, r->y),
        (int)(coord_conv_to_pdf_x(cc, r->x + r->width) - coord_conv_to_pdf_x(cc, r->x)),
        (int)(coord_conv_to_pdf_y(cc, r->y + r->height) - coord_conv_to_pdf_y(cc, r->y))
    };
}

double coord_conv_to_screen_x(const CoordConv *cc, int x)
{
    return x / cc->xscale + cc->rect.x;
}

double coord_conv_to_screen_y(const CoordConv *cc, int y)
{
    if (cc->inverty)
        return cc->rect.y + cc->rect.height - y / cc->yscale;
    return y / cc->yscale + cc->rect.y;
}

Rectangle coord_conv_to_screen(const CoordConv *cc, const Rectangle *r)
{
    return (Rectangle){
        (int)coord_conv_to_screen_x(cc, r->x),
        (int)coord_conv_to_screen_y(cc, r->y),
        (int)(coord_conv_to_screen_x(cc, r->x + r->width) - coord_conv_to_screen_x(cc, r->x)),
        (int)(coord_conv_to_screen_y(cc, r->y + r->height) - coord_conv_to_screen_y(cc, r->y))
    };
}
