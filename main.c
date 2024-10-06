#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <wchar.h>
#include <math.h>

#include <X11/Xatom.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>

#include <poppler.h>

#include "coordconv.h"
#include "rectangle.h"

#define AnyMask   UINT_MAX
#define EmptyMask 0

typedef struct {
    int page, offset;
} PageAndOffset;

typedef struct {
    PopplerDocument *doc;
    PopplerPage *page;
    int page_num;
    int total_pages;
    bool fit_page;
    bool scrolling_up;
    int next_pos_y;
    PageAndOffset *page_stack;
    int page_stack_size;
    int page_stack_capacity;

    Display *display;
    Window main;
    Rectangle main_pos;
    Pixmap pdf;
    Rectangle pdf_pos;

    GC selection_gc;
    Rectangle selection;
    Rectangle pdf_selection;
    bool selecting;

    char *primary;
    char *clipboard;

    GC status_gc;
    GC text_gc;
    XFontSet fset;
    int fheight;
    int fbase;
    Rectangle status_pos;
    char prompt[256];
    char value[256];
    bool status;
    bool input;

    double left, top, right, bottom;
    bool searching;

    bool xembed_init;

    bool magnifying;
    Rectangle magnify;
    int pre_mag_y;

    PopplerPage *second_page;
    bool two_page_view;

    int rotation;
    double zoom_level;

    bool continuous_mode;
    bool show_status_bar;
    bool dark_mode;
    char *file_name;
} AppState;

#include "config.h"

typedef struct {
    Display *display;
    Window main;
    GC selection;
    GC status;
    GC text;
    XFontSet fset;
    int fheight;
    int fbase;
} SetupXRet;

static void draw_status_bar(AppState *st);

static bool print_error(const char *m) {
    fprintf(stderr, "%s\n", m);
    return false;
}

static SetupXRet setup_x(unsigned width, unsigned height, const char *file_name,
    Window root)
{
    Display *display = XOpenDisplay(NULL);
    if (!display) {
        print_error("Cannot open X display.");
        return (SetupXRet){0};
    }

    XColor sc, ec;
    XAllocNamedColor(display, DefaultColormap(display, DefaultScreen(display)),
        bg_color, &sc, &ec);

    if (root == None)
        root = DefaultRootWindow(display);
    Window main = XCreateSimpleWindow(display, root, 0, 0, width, height, 2, 0, ec.pixel);

    char window_name[256];
    snprintf(window_name, sizeof(window_name), "Breathe: %s", file_name);
    const char *icon_name = "Breathe";

    Xutf8SetWMProperties(display, main, window_name, icon_name,
        NULL, 0, NULL, NULL, NULL);

    Atom utf8_string_atom  = XInternAtom(display, "UTF8_STRING", False);
    Atom wm_name_atom      = XInternAtom(display, "_NET_WM_NAME", False);
    Atom wm_icon_name_atom = XInternAtom(display, "_NET_WM_ICON_NAME", False);

    XChangeProperty(display, main, wm_name_atom, utf8_string_atom, 8,
        PropModeReplace, (const unsigned char*)window_name, strlen(window_name));
    XChangeProperty(display, main, wm_icon_name_atom, utf8_string_atom, 8,
        PropModeReplace, (const unsigned char*)icon_name, strlen(icon_name));

    Atom wmdel_atom = XInternAtom(display, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(display, main, &wmdel_atom, 1);

    XGCValues gcvals;
    gcvals.function = GXinvert;
    GC gc = XCreateGC(display, main, GCFunction, &gcvals);

    XSelectInput(display, main,
        ExposureMask | KeyPressMask | ButtonPressMask | ButtonReleaseMask |
        PointerMotionMask | StructureNotifyMask);

    XMapWindow(display, main);

    char **missing;
    int nmissing;
    char *def_string;
    XFontSet fset = XCreateFontSet(display, font, &missing, &nmissing, &def_string);
    if (!fset) {
        print_error("Cannot create font set.");
        return (SetupXRet){0};
    }

    XFreeStringList(missing);

    XFontSetExtents *fset_extents = XExtentsOfFontSet(fset);
    int fheight = fset_extents->max_logical_extent.height;
    int fbase = -fset_extents->max_logical_extent.y;

    XGCValues text_gcvals;
    text_gcvals.foreground = WhitePixel(display, DefaultScreen(display));
    GC text_gc = XCreateGC(display, main, GCForeground, &text_gcvals);

    XGCValues status_gcvals;
    status_gcvals.foreground = WhitePixel(display, DefaultScreen(display));
    status_gcvals.background = BlackPixel(display, DefaultScreen(display));
    GC status_gc = XCreateGC(display, main, GCForeground | GCBackground, &status_gcvals);

    return (SetupXRet){
        .display = display,
        .main = main,
        .selection = gc,
        .status = status_gc,
        .text = text_gc,
        .fset = fset,
        .fheight = fheight,
        .fbase = fbase
    };
}

static void cleanup_x(const AppState *st)
{
    if (st->fset != NULL)
        XFreeFontSet(st->display, st->fset);
    if (st->pdf != None)
        XFreePixmap(st->display, st->pdf);
    if (st->display != NULL)
        XCloseDisplay(st->display);
}

typedef struct {
    double dpi;
    Rectangle pos;
    Rectangle crop;
} PdfRenderConf;

static PdfRenderConf get_pdf_render_conf(bool fit_page, bool scrolling_up, int offset,
    Rectangle p, PopplerPage *page, bool magnifying, Rectangle m, int rotation, double zoom_level)
{
    double width, height;
    poppler_page_get_size(page, &width, &height);

    if (rotation % 180 != 0) {
        double temp = width;
        width = height;
        height = temp;
    }

    double x0 = 0, y0 = 0;
    if (magnifying) {
        x0 = m.x;
        y0 = m.y;
        width = m.width;
        height = m.height;
    }

    int x, y, w, h;
    double dpi;
    if (fit_page) {
        if ((double)p.width / (double)p.height > width / height) {
            h = p.height;
            dpi = (double)p.height * 72.0 / height;
            w = width * dpi / 72.0;

            y = 0;
            x = (p.width - w) / 2;
        } else {
            w = p.width;
            dpi = (double)p.width * 72.0 / width;
            h = height * dpi / 72.0;

            x = 0;
            y = (p.height - h) / 2;
        }
    } else {
        dpi = (double)p.width * 72.0 / width * zoom_level;
        w = width * dpi / 72.0;
        h = height * dpi / 72.0;

        x = p.x;
        y = p.y;

        if (w < p.width) {
            x = (p.width - w) / 2;
        }
        if (h < p.height) {
            y = (p.height - h) / 2;
        }
        if (w > p.width) {
            x = fmin(0, fmax(p.width - w, x));
        }
        if (h > p.height) {
            if (!scrolling_up) {
                y = fmin(0, fmax(p.height - h, y + offset));
            } else {
                y = fmin(0, fmax(p.height - h, y));
            }
        }
    }

    double scale = dpi / 72.0;
    return (PdfRenderConf){dpi, {x, y, w, h},
        {(int)(x0 * scale), (int)(y0 * scale), (int)(width * scale), (int)(height * scale)}};
}

#include <cairo/cairo.h>
#include <cairo/cairo-xlib.h>
#include <poppler.h>
#include "config.h"

static Pixmap render_pdf_page_to_pixmap(AppState *st, const PdfRenderConf *prc)
{
    Pixmap pixmap = XCreatePixmap(st->display, st->main, prc->pos.width, prc->pos.height,
                                  DefaultDepth(st->display, DefaultScreen(st->display)));

    cairo_surface_t *surface = cairo_xlib_surface_create(st->display, pixmap,
                                                         DefaultVisual(st->display, DefaultScreen(st->display)),
                                                         prc->pos.width, prc->pos.height);
    cairo_t *cr = cairo_create(surface);

    // Set background color based on dark mode
    if (st->dark_mode) {
        // Parse the hex color string to RGB values
        int r, g, b;
        sscanf(page_bg_color_dark + 1, "%02x%02x%02x", &r, &g, &b);
        cairo_set_source_rgb(cr, r / 255.0, g / 255.0, b / 255.0);
    } else {
        cairo_set_source_rgb(cr, 1, 1, 1);  // White background for light mode
    }
    cairo_paint(cr);

    cairo_save(cr);

    // Apply zoom and rotation
    cairo_translate(cr, prc->pos.width / 2.0, prc->pos.height / 2.0);
    cairo_rotate(cr, st->rotation * M_PI / 180.0);
    cairo_scale(cr, st->zoom_level, st->zoom_level);
    cairo_translate(cr, -prc->pos.width / 2.0, -prc->pos.height / 2.0);

    // Calculate the scale factor
    double page_width, page_height;
    poppler_page_get_size(st->page, &page_width, &page_height);
    double scale_x = prc->pos.width / page_width;
    double scale_y = prc->pos.height / page_height;
    double scale = (scale_x < scale_y) ? scale_x : scale_y;

    // Apply the scale
    cairo_scale(cr, scale, scale);

    // Render the PDF page
    poppler_page_render(st->page, cr);

    cairo_restore(cr);

    // Apply color inversion for dark mode
    if (st->dark_mode) {
        cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
        cairo_set_source_rgb(cr, 1, 1, 1);
        cairo_paint(cr);
    }

    // Render the second page if in two-page view mode
    if (st->two_page_view && st->second_page) {
        cairo_save(cr);

        // Calculate the position for the second page
        double second_page_x = prc->pos.width / 2.0;
        cairo_translate(cr, second_page_x, 0);

        // Apply zoom and rotation for the second page
        cairo_translate(cr, prc->pos.width / 4.0, prc->pos.height / 2.0);
        cairo_rotate(cr, st->rotation * M_PI / 180.0);
        cairo_scale(cr, st->zoom_level, st->zoom_level);
        cairo_translate(cr, -prc->pos.width / 4.0, -prc->pos.height / 2.0);

        // Apply the scale for the second page
        cairo_scale(cr, scale, scale);

        // Render the second PDF page
        poppler_page_render(st->second_page, cr);

        cairo_restore(cr);

        // Apply color inversion for dark mode on the second page
        if (st->dark_mode) {
            cairo_rectangle(cr, second_page_x, 0, prc->pos.width / 2.0, prc->pos.height);
            cairo_set_operator(cr, CAIRO_OPERATOR_DIFFERENCE);
            cairo_set_source_rgb(cr, 1, 1, 1);
            cairo_fill(cr);
        }
    }

    cairo_destroy(cr);
    cairo_surface_destroy(surface);

    return pixmap;
}

static void copy_pixmap_on_expose_event(const AppState *st, const Rectangle *prev,
    const XExposeEvent *e)
{
    if (st->pdf == None)
        return;

    if (prev->x != st->pdf_pos.x || prev->y != st->pdf_pos.y)
    {
        XClearArea(st->display, st->main,
            prev->x, prev->y, prev->width, prev->height, False);
        XClearArea(st->display, st->main,
            st->pdf_pos.x, st->pdf_pos.y, st->pdf_pos.width, st->pdf_pos.height, False);
    }

    Rectangle intersect_rect = rectangle_intersect(&(Rectangle){e->x, e->y, e->width, e->height}, &st->status_pos);
    if (rectangle_is_invalid(&intersect_rect))
    {
        XCopyArea(st->display, st->pdf, st->main, DefaultGC(st->display, DefaultScreen(st->display)),
            e->x - st->pdf_pos.x, e->y - st->pdf_pos.y,
            e->width, e->height, e->x, e->y);
    }
    else
    {
        Rectangle top = {e->x, e->y, e->width, intersect_rect.y - e->y};
        Rectangle bottom = {e->x, intersect_rect.y + intersect_rect.height,
            e->width, (e->y + e->height) - (intersect_rect.y + intersect_rect.height)};

        if (!rectangle_is_invalid(&top))
            XCopyArea(st->display, st->pdf, st->main, DefaultGC(st->display, DefaultScreen(st->display)),
                top.x - st->pdf_pos.x, top.y - st->pdf_pos.y,
                top.width, top.height, top.x, top.y);

        if (!rectangle_is_invalid(&bottom))
            XCopyArea(st->display, st->pdf, st->main, DefaultGC(st->display, DefaultScreen(st->display)),
                bottom.x - st->pdf_pos.x, bottom.y - st->pdf_pos.y,
                bottom.width, bottom.height, bottom.x, bottom.y);
    }

    if (st->selection.width > 0 && st->selection.height > 0)
    {
        XFillRectangle(st->display, st->main, st->selection_gc,
            st->selection.x, st->selection.y,
            st->selection.width, st->selection.height);
    }
}

static void force_render_page(AppState *st, bool clear)
{
    if (st->pdf != None && clear)
    {
        XFreePixmap(st->display, st->pdf);
        st->pdf = None;
    }

    XWindowAttributes attrs;
    XGetWindowAttributes(st->display, st->main, &attrs);

    XEvent e;
    e.type = ConfigureNotify;
    e.xconfigure.x = attrs.x;
    e.xconfigure.y = attrs.y;
    e.xconfigure.width  = attrs.width;
    e.xconfigure.height = attrs.height;
    XSendEvent(st->display, st->main, False, StructureNotifyMask, &e);

    e.type = Expose;
    e.xexpose.x = 0;
    e.xexpose.y = 0;
    e.xexpose.width  = attrs.width;
    e.xexpose.height = attrs.height;
    XSendEvent(st->display, st->main, False, ExposureMask, &e);

    // Add scroll indicator
    if (!st->fit_page) {
        double scroll_percent = (double)(-st->pdf_pos.y) / (st->pdf_pos.height - st->main_pos.height);
        int indicator_height = st->main_pos.height * (st->main_pos.height / (double)st->pdf_pos.height);
        int indicator_y = scroll_percent * (st->main_pos.height - indicator_height);
        
        XSetForeground(st->display, st->status_gc, st->dark_mode ? WhitePixel(st->display, DefaultScreen(st->display)) : BlackPixel(st->display, DefaultScreen(st->display)));
        XFillRectangle(st->display, st->main, st->status_gc, 
                       st->main_pos.width - 10, indicator_y, 
                       10, indicator_height);
    }

    // Draw status bar if enabled
    if (st->show_status_bar) {
        draw_status_bar(st);
    }
}

static void send_expose(const AppState *st, const Rectangle *r)
{
    XEvent e;
    e.type = Expose;
    e.xexpose.x = r->x;
    e.xexpose.y = r->y;
    e.xexpose.width  = r->width;
    e.xexpose.height = r->height;
    XSendEvent(st->display, st->main, False, ExposureMask, &e);
}

static int get_pdf_scroll_diff(const AppState *st, double percent)
{
    if (st->pdf_pos.height < st->main_pos.height)
        return 0;

    int sc = st->pdf_pos.height * percent;
    if (sc > 0)
        return sc < -st->pdf_pos.y ? sc : -st->pdf_pos.y;

    return -sc < st->pdf_pos.height - st->main_pos.height + st->pdf_pos.y ?
        sc : -(st->pdf_pos.height - st->main_pos.height + st->pdf_pos.y);
}

static bool find_page_link(AppState *st, const XButtonEvent *e)
{
    GList *link_mapping = poppler_page_get_link_mapping(st->page);
    if (link_mapping == NULL)
        return false;

    CoordConv cc = coord_conv_create(st->page, &st->pdf_pos, true, st->rotation);
    double ex = coord_conv_to_pdf_x(&cc, e->x);
    double ey = coord_conv_to_pdf_y(&cc, e->y);

    bool found = false;
    for (GList *l = link_mapping; l != NULL; l = l->next)
    {
        PopplerLinkMapping *link = (PopplerLinkMapping *)l->data;
        if (ex >= link->area.x1 && ex <= link->area.x2 &&
            ey >= link->area.y1 && ey <= link->area.y2)
        {
            PopplerAction *action = link->action;
            if (action != NULL && action->type == POPPLER_ACTION_GOTO_DEST)
            {
                PopplerActionGotoDest *goto_dest = &action->goto_dest;
                if (goto_dest->dest->type == POPPLER_DEST_NAMED)
                {
                    PopplerDest *dest = poppler_document_find_dest(st->doc, goto_dest->dest->named_dest);
                    if (dest != NULL)
                    {
                        int prev_page = st->page_num;
                        st->page_num = dest->page_num;
                        if (prev_page != st->page_num)
                        {
                            if (st->page_stack_size == st->page_stack_capacity)
                            {
                                st->page_stack_capacity *= 2;
                                st->page_stack = realloc(st->page_stack, st->page_stack_capacity * sizeof(PageAndOffset));
                            }
                            st->page_stack[st->page_stack_size++] = (PageAndOffset){prev_page, st->pdf_pos.y};
                            found = true;
                        }
                        poppler_dest_free(dest);
                    }
                }
                else if (goto_dest->dest->type == POPPLER_DEST_XYZ)
                {
                    int prev_page = st->page_num;
                    st->page_num = goto_dest->dest->page_num;
                    if (prev_page != st->page_num)
                    {
                        if (st->page_stack_size == st->page_stack_capacity)
                        {
                            st->page_stack_capacity *= 2;
                            st->page_stack = realloc(st->page_stack, st->page_stack_capacity * sizeof(PageAndOffset));
                        }
                        st->page_stack[st->page_stack_size++] = (PageAndOffset){prev_page, st->pdf_pos.y};
                        found = true;
                    }
                }
            }
            break;
        }
    }

    poppler_page_free_link_mapping(link_mapping);
    return found;
}

static void copy_text(AppState *st, bool primary)
{
    cairo_surface_t *surface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t *cr = cairo_create(surface);

    PopplerRectangle rect;
    CoordConv cc = coord_conv_create(st->page, &st->pdf_pos, false, st->rotation);
    Rectangle sr = rectangle_normalize(&st->selection);
    rect.x1 = coord_conv_to_pdf_x(&cc, sr.x);
    rect.y1 = coord_conv_to_pdf_y(&cc, sr.y);
    rect.x2 = coord_conv_to_pdf_x(&cc, sr.x + sr.width);
    rect.y2 = coord_conv_to_pdf_y(&cc, sr.y + sr.height);

    char *text = poppler_page_get_selected_text(st->page, POPPLER_SELECTION_GLYPH, &rect);

    if (primary)
    {
        free(st->primary);
        st->primary = strdup(text);
        XSetSelectionOwner(st->display, XA_PRIMARY, st->main, CurrentTime);
    }
    else
    {
        free(st->clipboard);
        st->clipboard = strdup(text);
        XSetSelectionOwner(st->display, XInternAtom(st->display, "CLIPBOARD", 0),
            st->main, CurrentTime);
    }

    g_free(text);
    cairo_destroy(cr);
    cairo_surface_destroy(surface);
}

static Rectangle get_status_pos(const AppState *st)
{
    return (Rectangle){0, st->main_pos.height - (st->fheight + 2), st->main_pos.width, st->fheight + 2};
}

static void draw_status_bar(AppState *st)
{
    char status[256];
    snprintf(status, sizeof(status), "[%d/%d] %s", st->page_num, st->total_pages, st->file_name);

    const char *text_color = st->dark_mode ? status_bar_text_color_dark : status_bar_text_color_light;
    const char *bg_color = st->dark_mode ? status_bar_bg_color_dark : status_bar_bg_color_light;

    XColor text_xcolor, bg_xcolor;
    XAllocNamedColor(st->display, DefaultColormap(st->display, DefaultScreen(st->display)),
                     text_color, &text_xcolor, &text_xcolor);
    XAllocNamedColor(st->display, DefaultColormap(st->display, DefaultScreen(st->display)),
                     bg_color, &bg_xcolor, &bg_xcolor);

    XSetForeground(st->display, st->status_gc, bg_xcolor.pixel);
    XFillRectangle(st->display, st->main, st->status_gc,
                   st->status_pos.x, st->status_pos.y,
                   st->status_pos.width, st->status_pos.height);

    XSetForeground(st->display, st->status_gc, text_xcolor.pixel);

    XFontStruct *font_info = XLoadQueryFont(st->display, status_bar_font);
    if (font_info == NULL) {
        fprintf(stderr, "Failed to load status bar font\n");
        return;
    }

    XSetFont(st->display, st->status_gc, font_info->fid);

    int text_width = XTextWidth(font_info, status, strlen(status));
    int x = (st->status_pos.width - text_width) / 2;
    int y = st->status_pos.y + (st->status_pos.height - font_info->ascent - font_info->descent) / 2 + font_info->ascent;

    XDrawString(st->display, st->main, st->status_gc, x, y, status, strlen(status));

    XFreeFont(st->display, font_info);
}

static void search_text(AppState *st)
{
    bool backwards   = false;
    bool ignore_case = false;
    bool whole_words = false;
    char *str = strdup(st->value);
    size_t len = strlen(str);
    while (len > 0 && (str[len-1] == '?' || str[len-1] == '~' || str[len-1] == '%'))
    {
        char flag = str[--len];
        if (flag == '?')
            backwards = true;
        if (flag == '~')
            ignore_case = true;
        if (flag == '%')
            whole_words = true;
        str[len] = '\0';
    }

    bool found = false;
    bool whole = false;
    int page   = st->page_num;

    PopplerFindFlags find_flags = 0;
    if (backwards)
        find_flags |= POPPLER_FIND_BACKWARDS;
    if (!ignore_case)
        find_flags |= POPPLER_FIND_CASE_SENSITIVE;
    if (whole_words)
        find_flags |= POPPLER_FIND_WHOLE_WORDS_ONLY;

    while (!whole)
    {
        GList *matches = poppler_page_find_text_with_options(st->page, str, find_flags);
        found = (matches != NULL);

        if (found)
        {
            PopplerRectangle *rect = (PopplerRectangle *)matches->data;
            st->left = rect->x1;
            st->top = rect->y1;
            st->right = rect->x2;
            st->bottom = rect->y2;
            g_list_free_full(matches, (GDestroyNotify)poppler_rectangle_free);
            break;
        }

        if (!backwards && page < st->total_pages)
        {
            ++page;
            st->searching = false;
            g_object_unref(st->page);
            st->page = poppler_document_get_page(st->doc, page - 1);
        }
        else if (backwards && page > 1)
        {
            --page;
            st->searching = false;
            g_object_unref(st->page);
            st->page = poppler_document_get_page(st->doc, page - 1);
        }
        else {
            whole = true;
        }
    }

    free(str);

    if (found)
    {
        if (page != st->page_num)
        {
            st->page_num = page;
            force_render_page(st, true);
        }

        CoordConv cc = coord_conv_create(st->page, &st->pdf_pos, false, st->rotation);

        st->pdf_selection = (Rectangle){(int)st->left, (int)st->top, (int)(st->right - st->left), (int)(st->bottom - st->top)};
        st->selection     = coord_conv_to_screen(&cc, &st->pdf_selection);
    }
    else {
        st->pdf_selection = (Rectangle){0, 0, 0, 0};
        st->selection = (Rectangle){0, 0, 0, 0};
    }

    st->searching = found;
}

typedef struct {
    char *fname;
    Window root;
} Args;

Args parse_args(int argc, char **argv)
{
    char *fname = NULL;
    Window root  = None;

    for (int i = 1; i < argc; ++i)
    {
        if (strcmp(argv[i], "-w") == 0)
        {
            if (i < argc - 1)
            {
                root = strtol(argv[++i], NULL, 0);
                if (root == 0) {
                    fprintf(stderr, "Invalid window (-w) value.\n");
                    exit(1);
                }
            }
            else {
                fprintf(stderr, "Missing window (-w) parameter.\n");
                exit(1);
            }
        }
        else
            fname = argv[i];
    }

    if (fname == NULL) {
        fprintf(stderr, "Missing pdf file, usage: breathe [-w window] pdf_file.\n");
        exit(1);
    }

    return (Args){fname, root};
}

static void render_page_lambda(AppState *st) {
    if (st->page) {
        g_object_unref(st->page);
    }
    st->page = poppler_document_get_page(st->doc, st->page_num - 1);
    if (!st->page) {
        char error_msg[100];
        snprintf(error_msg, sizeof(error_msg), "Cannot create page: %d.", st->page_num);
        print_error(error_msg);
    }
    
    if (st->two_page_view && st->page_num < st->total_pages) {
        if (st->second_page) {
            g_object_unref(st->second_page);
        }
        st->second_page = poppler_document_get_page(st->doc, st->page_num);
    } else {
        if (st->second_page) {
            g_object_unref(st->second_page);
            st->second_page = NULL;
        }
    }
    
    force_render_page(st, true);
    st->selection = (Rectangle){0, 0, 0, 0};
    st->pdf_selection = (Rectangle){0, 0, 0, 0};
    st->selecting = false;
}

int main(int argc, char **argv)
{
    setlocale(LC_ALL, "");

    if (argc < 2) {
        fprintf(stderr, "Missing pdf file, usage: breathe [-w window] pdf_file.\n");
        return 1;
    }

    Args args = parse_args(argc, argv);
    char *file_name = args.fname;

    GError *error = NULL;
    char *uri = g_filename_to_uri(file_name, NULL, &error);
    if (uri == NULL) {
        fprintf(stderr, "Error converting filename to URI: %s\n", error->message);
        g_error_free(error);
        return 1;
    }

    AppState st = {0};
    st.page_stack_capacity = 10;
    st.page_stack = malloc(st.page_stack_capacity * sizeof(PageAndOffset));
    st.rotation = 0;
    st.zoom_level = 1.0;
    st.two_page_view = false;
    st.continuous_mode = false;
    st.show_status_bar = true;
    st.dark_mode = false;
    st.file_name = strdup(file_name);

    st.doc = poppler_document_new_from_file(uri, NULL, &error);
    g_free(uri);

    if (!st.doc) {
        fprintf(stderr, "Error loading PDF file: %s\n", file_name);
        if (error) {
            fprintf(stderr, "Poppler error: %s\n", error->message);
            g_error_free(error);
        }
        return 1;
    }

    st.total_pages = poppler_document_get_n_pages(st.doc);
    if (st.total_pages <= 0) {
        fprintf(stderr, "Error: The document has no pages or failed to load properly.\n");
        g_object_unref(st.doc);
        return 1;
    }

    printf("Successfully loaded PDF with %d pages.\n", st.total_pages);

    st.page_num = 1;
    st.page = poppler_document_get_page(st.doc, st.page_num - 1);
    if (!st.page) {
        fprintf(stderr, "Error: Failed to load the first page of the document.\n");
        g_object_unref(st.doc);
        return 1;
    }

    printf("Successfully loaded first page.\n");

    double width, height;
    poppler_page_get_size(st.page, &width, &height);
    SetupXRet xret = setup_x((unsigned)width, (unsigned)height, file_name, args.root);
    if (xret.display == NULL) {
        fprintf(stderr, "Error: Failed to set up X window.\n");
        g_object_unref(st.doc);
        return 1;
    }

    st.display = xret.display;
    st.main    = xret.main;

    st.fit_page     = true;
    st.scrolling_up = false;

    st.selection_gc = xret.selection;
    st.status_gc    = xret.status;
    st.text_gc      = xret.text;

    st.fset    = xret.fset;
    st.fheight = xret.fheight;
    st.fbase   = xret.fbase;

    XEvent event;
    while (true)
    {
        XNextEvent(st.display, &event);

        if (event.type == Expose)
        {
            Rectangle prev = st.pdf_pos;
            if (st.pdf == None)
            {
                PdfRenderConf prc = get_pdf_render_conf(st.fit_page, st.scrolling_up,
                    st.next_pos_y, st.main_pos, st.page, st.magnifying, st.magnify,
                    st.rotation, st.zoom_level);
                st.scrolling_up = false;
                st.next_pos_y   = 0;

                st.pdf = render_pdf_page_to_pixmap(&st, &prc);
                st.pdf_pos = prc.pos;
            }
            copy_pixmap_on_expose_event(&st, &prev, &event.xexpose);
            
            if (st.show_status_bar) {
                draw_status_bar(&st);
            }
        }

        if (event.type == ConfigureNotify)
        {
            if (st.main_pos.width != event.xconfigure.width ||
                st.main_pos.height != event.xconfigure.height)
            {
                st.main_pos = (Rectangle){
                    event.xconfigure.x,
                    event.xconfigure.y,
                    event.xconfigure.width,
                    event.xconfigure.height
                };

                XClearWindow(st.display, st.main);
                if (st.pdf != None)
                {
                    XFreePixmap(st.display, st.pdf);
                    st.pdf = None;
                }

                st.status_pos = get_status_pos(&st);
            }
        }

        if (event.type == ClientMessage)
        {
            Atom xembed_atom = XInternAtom(st.display, "_XEMBED", False);
            Atom wmdel_atom  = XInternAtom(st.display, "WM_DELETE_WINDOW", False);

            if (event.xclient.message_type == xembed_atom && event.xclient.format == 32)
            {
                if (!st.xembed_init)
                {
                    force_render_page(&st, true);
                    st.xembed_init = true;
                }
            }
            else if (event.xclient.data.l[0] == (long)wmdel_atom)
            {
                goto endloop;
            }
        }

        if (event.type == KeyPress)
        {
            KeySym ksym;
            char buf[32];
            XLookupString(&event.xkey, buf, sizeof buf, &ksym, NULL);

            bool status = st.status;
            if (!status)
            {
                for (size_t i = 0; i < sizeof(shortcuts)/sizeof(shortcuts[0]); ++i)
                {
                    const Shortcut *sc = &shortcuts[i];
                    if ((sc->mask == AnyMask || sc->mask == event.xkey.state) &&
                        sc->ksym == ksym)
                    {
                        switch (sc->action)
                        {
                            case QUIT:
                                goto endloop;
                            case FIT_PAGE:
                                if (!st.fit_page) {
                                    st.fit_page = true;
                                    force_render_page(&st, true);
                                }
                                break;
                            case FIT_WIDTH:
                                if (st.fit_page) {
                                    st.fit_page = false;
                                    force_render_page(&st, true);
                                }
                                break;
                            case NEXT:
                            case PG_DOWN:
                                if (st.page_num < st.total_pages) {
                                    ++st.page_num;
                                    render_page_lambda(&st);
                                }
                                break;
                            case PREV:
                            case PG_UP:
                                if (st.page_num > 1) {
                                    --st.page_num;
                                    render_page_lambda(&st);
                                }
                                break;
                            case FIRST:
                                st.page_num = 1;
                                render_page_lambda(&st);
                                break;
                            case LAST:
                                st.page_num = st.total_pages;
                                render_page_lambda(&st);
                                break;
                            case DOWN:
                                if (!st.fit_page) {
                                    int diff = get_pdf_scroll_diff(&st, -arrow_scroll);
                                    if (diff != 0) {
                                        st.pdf_pos.y += diff;
                                        force_render_page(&st, false);
                                    }
                                }
                                break;
                            case UP:
                                if (!st.fit_page) {
                                    int diff = get_pdf_scroll_diff(&st, arrow_scroll);
                                    if (diff != 0) {
                                        st.pdf_pos.y += diff;
                                        force_render_page(&st, false);
                                    }
                                }
                                break;
                            case BACK:
                                if (st.page_stack_size > 0) {
                                    PageAndOffset elem = st.page_stack[--st.page_stack_size];
                                    st.page_num = elem.page;
                                    st.next_pos_y = elem.offset;
                                    render_page_lambda(&st);
                                }
                                break;
                            case RELOAD:
                                g_object_unref(st.doc);
                                st.doc = poppler_document_new_from_file(file_name, NULL, NULL);
                                if (!st.doc) {
                                    print_error("Error re-loading pdf file.");
                                }
                                if (st.page_num > st.total_pages) {
                                    st.page_num = 1;
                                }
                                render_page_lambda(&st);
                                break;
                            case COPY:
                                if (st.pdf_selection.width > 0 && st.pdf_selection.height > 0) {
                                    copy_text(&st, false);
                                }
                                break;
                            case GOTO_PAGE:
                                st.status = true;
                                st.input = true;
                                snprintf(st.prompt, sizeof(st.prompt), "goto page [1, %d]: ", st.total_pages);
                                st.value[0] = '\0';
                                send_expose(&st, &st.status_pos);
                                break;
                            case SEARCH:
                                st.status = true;
                                st.input = true;
                                strcpy(st.prompt, "search: ");
                                st.value[0] = '\0';
                                send_expose(&st, &st.status_pos);
                                break;
                            case PAGE:
                                st.status = true;
                                st.input = false;
                                snprintf(st.prompt, sizeof(st.prompt), "page %d/%d", st.page_num, st.total_pages);
                                st.value[0] = '\0';
                                send_expose(&st, &st.status_pos);
                                break;
                            case MAGNIFY:
                                if (st.pdf_selection.width > 0 && st.pdf_selection.height > 0) {
                                    st.magnifying = true;
                                    st.magnify = st.pdf_selection;
                                    st.selection = (Rectangle){0, 0, 0, 0};
                                    st.pdf_selection = (Rectangle){0, 0, 0, 0};
                                    st.status = true;
                                    st.input = false;
                                    strcpy(st.prompt, "magnify");
                                    st.value[0] = '\0';
                                    st.pre_mag_y = st.pdf_pos.y;
                                    st.pdf_pos.y = 0;
                                    force_render_page(&st, true);
                                }
                                break;
                            case ROTATE_CW:
                                st.rotation = (st.rotation + 90) % 360;
                                force_render_page(&st, true);
                                break;
                            case ROTATE_CCW:
                                st.rotation = (st.rotation - 90 + 360) % 360;
                                force_render_page(&st, true);
                                break;
                            case ZOOM_IN:
                                st.zoom_level *= zoom_step;
                                st.fit_page = false;
                                force_render_page(&st, true);
                                break;
                            case ZOOM_OUT:
                                st.zoom_level /= zoom_step;
                                st.fit_page = false;
                                force_render_page(&st, true);
                                break;
                            case TOGGLE_TWO_PAGE_VIEW:
                                st.two_page_view = !st.two_page_view;
                                render_page_lambda(&st);
                                break;
                            case TOGGLE_CONTINUOUS_MODE:
                                st.continuous_mode = !st.continuous_mode;
                                force_render_page(&st, true);
                                break;
                            case TOGGLE_STATUS_BAR:
                                st.show_status_bar = !st.show_status_bar;
                                force_render_page(&st, true);
                                break;
                            case TOGGLE_DARK_MODE:
                                st.dark_mode = !st.dark_mode;
                                force_render_page(&st, true);
                                break;
                        }
                    }
                }
            }

            if (status)
            {
                if (ksym == XK_Escape)
                {
                    st.status = false;
                    st.searching = false;
                    XClearArea(st.display, st.main,
                        st.status_pos.x, st.status_pos.y,
                        st.status_pos.width, st.status_pos.height, True);

                    if (st.magnifying)
                    {
                        st.magnifying = false;
                        st.next_pos_y = st.pre_mag_y;
                        force_render_page(&st, true);
                    }
                }

                if (ksym == XK_BackSpace)
                {
                    if (strlen(st.value) > 0)
                    {
                        st.value[strlen(st.value) - 1] = '\0';
                        XClearArea(st.display, st.main,
                            st.status_pos.x, st.status_pos.y,
                            st.status_pos.width, st.status_pos.height, True);
                    }
                }

                if (ksym == XK_Return)
                {
                    if (strncmp(st.prompt, "goto", 4) == 0)
                    {
                        int page = atoi(st.value);
                        if (page >= 1 && page <= st.total_pages)
                        {
                            st.status = false;
                            st.page_num = page;

                            XClearArea(st.display, st.main,
                                st.status_pos.x, st.status_pos.y,
                                st.status_pos.width, st.status_pos.height, True);
                            render_page_lambda(&st);
                        }
                    }

                    if (strncmp(st.prompt, "search", 6) == 0)
                    {
                        Rectangle normalized = rectangle_normalize(&st.selection);
                        send_expose(&st, &normalized);
                        search_text(&st);
                        normalized = rectangle_normalize(&st.selection);
                        send_expose(&st, &normalized);
                    }
                }

                if (st.input)
                {
                    if (strlen(buf) > 0 && !iscntrl((unsigned char)buf[0]))
                    {
                        strcat(st.value, buf);
                        send_expose(&st, &st.status_pos);
                    }
                }
            }
        }

        if (event.type == ButtonPress)
        {
            int button = event.xbutton.button;
            unsigned int state = event.xbutton.state;

            if ((state & ControlMask) && (button == Button4 || button == Button5))
            {
                double zoom_factor = (button == Button4) ? zoom_step : 1.0 / zoom_step;
                double old_zoom = st.zoom_level;
                st.zoom_level *= zoom_factor;
                st.zoom_level = fmax(min_zoom, fmin(max_zoom, st.zoom_level));
                st.fit_page = false;

                // Calculate the mouse position relative to the PDF
                double mouse_x = (event.xbutton.x - st.pdf_pos.x) / (double)st.pdf_pos.width;
                double mouse_y = (event.xbutton.y - st.pdf_pos.y) / (double)st.pdf_pos.height;

                // Calculate new PDF dimensions
                double new_width = st.pdf_pos.width * (st.zoom_level / old_zoom);
                double new_height = st.pdf_pos.height * (st.zoom_level / old_zoom);

                // Calculate new PDF position to keep the mouse point stationary
                st.pdf_pos.x = event.xbutton.x - mouse_x * new_width;
                st.pdf_pos.y = event.xbutton.y - mouse_y * new_height;

                // Ensure the PDF doesn't move out of bounds
                st.pdf_pos.x = fmin(st.pdf_pos.x, 0);
                st.pdf_pos.y = fmin(st.pdf_pos.y, 0);
                st.pdf_pos.x = fmax(st.pdf_pos.x, st.main_pos.width - new_width);
                st.pdf_pos.y = fmax(st.pdf_pos.y, st.main_pos.height - new_height);

                force_render_page(&st, true);
            }
            else if (button == Button4 && st.fit_page && !st.magnifying)
            {
                if (st.page_num > 1)
                {
                    st.scrolling_up = true;
                    --st.page_num;
                    render_page_lambda(&st);
                }
            }
            else if (button == Button5 && st.fit_page && !st.magnifying)
            {
                if (st.page_num < st.total_pages)
                {
                    ++st.page_num;
                    render_page_lambda(&st);
                }
            }
            else if (button == Button4 && !st.fit_page)
            {
                int diff = get_pdf_scroll_diff(&st, mouse_scroll);
                if (diff != 0)
                {
                    st.pdf_pos.y += diff;
                    force_render_page(&st, false);
                }
                else {
                    if (st.page_num > 1 && !st.magnifying)
                    {
                        st.scrolling_up = true;
                        --st.page_num;
                        render_page_lambda(&st);
                    }
                }
            }
            else if (button == Button5 && !st.fit_page)
            {
                int diff = get_pdf_scroll_diff(&st, -mouse_scroll);
                if (diff != 0)
                {
                    st.pdf_pos.y += diff;
                    force_render_page(&st, false);
                }
                else {
                    if (st.page_num < st.total_pages && !st.magnifying)
                    {
                        ++st.page_num;
                        render_page_lambda(&st);
                    }
                }
            }
            else if (button == Button1 && !st.magnifying)
            {
                if (event.xbutton.x >= st.pdf_pos.x &&
                    event.xbutton.y >= st.pdf_pos.y &&
                    event.xbutton.x <= st.pdf_pos.x + st.pdf_pos.width &&
                    event.xbutton.y <= st.pdf_pos.y + st.pdf_pos.height)
                {
                    if (find_page_link(&st, &event.xbutton))
                    {
                        render_page_lambda(&st);
                    }
                    else {
                        CoordConv cc = coord_conv_create(st.page, &st.pdf_pos, false, st.rotation);
                        st.selection = coord_conv_to_screen(&cc, &st.pdf_selection);

                        Rectangle padded = rectangle_normalize(&st.selection);
                        padded.x -= 5;
                        padded.y -= 5;
                        padded.width += 10;
                        padded.height += 10;
                        send_expose(&st, &padded);

                        st.selection = (Rectangle){event.xbutton.x, event.xbutton.y, 0, 0};
                        st.selecting = true;
                    }
                }
            }
        }

        if (event.type == ButtonRelease && event.xbutton.button == Button1)
        {
            if (st.selecting)
            {
                st.selection.width = event.xbutton.x - st.selection.x;
                st.selection.height = event.xbutton.y - st.selection.y;

                CoordConv cc = coord_conv_create(st.page, &st.pdf_pos, false, st.rotation);
                Rectangle normalized = rectangle_normalize(&st.selection);
                st.pdf_selection = coord_conv_to_pdf(&cc, &normalized);
                st.selecting = false;

                copy_text(&st, true);
            }
        }

        if (event.type == MotionNotify && st.selecting)
        {
            Rectangle pr = rectangle_normalize(&st.selection);

            st.selection.width = event.xbutton.x - st.selection.x;
            st.selection.height = event.xbutton.y - st.selection.y;

            Rectangle nr = rectangle_normalize(&st.selection);

            RectangleArray diff1 = rectangle_subtract(&pr, &nr);
            RectangleArray diff2 = rectangle_subtract(&nr, &pr);

            for (int i = 0; i < diff1.size; i++)
                send_expose(&st, &diff1.rectangles[i]);
            for (int i = 0; i < diff2.size; i++)
                send_expose(&st, &diff2.rectangles[i]);

            free(diff1.rectangles);
            free(diff2.rectangles);
        }

        if (event.type == SelectionRequest)
        {
            Atom targets_atom = XInternAtom(st.display, "TARGETS", False);
            Atom utf8_string_atom = XInternAtom(st.display, "UTF8_STRING", False);
            Atom clipboard_atom = XInternAtom(st.display, "CLIPBOARD", False);

            XSelectionRequestEvent xselreq = event.xselectionrequest;

            XEvent e;
            e.type = SelectionNotify;
            e.xselection.requestor = xselreq.requestor;
            e.xselection.selection = xselreq.selection;
            e.xselection.target = xselreq.target;
            e.xselection.time = xselreq.time;
            e.xselection.property = None;

            if (xselreq.target == targets_atom)
            {
                XChangeProperty(xselreq.display, xselreq.requestor,
                    xselreq.property, XA_ATOM, 32, PropModeReplace,
                    (unsigned char*)&utf8_string_atom, 1);
                e.xselection.property = xselreq.property;
            }

            if (xselreq.target == utf8_string_atom || xselreq.target == XA_STRING)
            {
                char *ptr = NULL;
                if (xselreq.selection == XA_PRIMARY)
                    ptr = st.primary;
                if (xselreq.selection == clipboard_atom)
                    ptr = st.clipboard;

                if (ptr) {
                    XChangeProperty(xselreq.display, xselreq.requestor,
                        xselreq.property, xselreq.target, 8, PropModeReplace,
                        (const unsigned char*)ptr, strlen(ptr));
                    e.xselection.property = xselreq.property;
                }
            }

            XSendEvent(xselreq.display, xselreq.requestor, True, 0, &e);
        }
    }
endloop:
    cleanup_x(&st);
    g_object_unref(st.doc);
    free(st.page_stack);
    free(st.primary);
    free(st.clipboard);
    free(st.file_name);
    free(args.fname);
    return 0;
}
