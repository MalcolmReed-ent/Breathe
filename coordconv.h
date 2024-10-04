#ifndef COORDCONV_H
#define COORDCONV_H

#include <stdbool.h>
#include <poppler.h>
#include "rectangle.h"

typedef struct {
    Rectangle rect;
    bool inverty;
    double xscale;
    double yscale;
} CoordConv;

CoordConv coord_conv_create(PopplerPage *p, const Rectangle *r, bool i, int rotation);
double coord_conv_to_pdf_x(const CoordConv *cc, int x);
double coord_conv_to_pdf_y(const CoordConv *cc, int y);
Rectangle coord_conv_to_pdf(const CoordConv *cc, const Rectangle *r);
double coord_conv_to_screen_x(const CoordConv *cc, int x);
double coord_conv_to_screen_y(const CoordConv *cc, int y);
Rectangle coord_conv_to_screen(const CoordConv *cc, const Rectangle *r);

#endif // COORDCONV_H
