#ifndef CONFIG_H
#define CONFIG_H

#include <X11/keysym.h>

/* General Settings */
static const char *font = "-misc-fixed-medium-r-normal-*-14-*-*-*-*-*-*-*";
static const char *bg_color = "#2E3440";  // Nord theme background color

/* Scrolling and Zooming */
static const double arrow_scroll = 0.01;
static const double page_scroll  = 0.30;
static const double mouse_scroll = 0.02;
static const double zoom_step = 1.1;
static const double min_zoom = 0.1;
static const double max_zoom = 5.0;
static const double default_zoom = 1.0;

/* Selection */
static const unsigned long selection_color = 0x5E81AC;  // Nord theme blue

/* Rotation */
static const int default_rotation = 0;

/* Features */
static const int enable_text_selection = 1;
static const int enable_link_following = 1;
static const int cache_size_mb = 64;

/* View Modes */
static const int default_two_page_view = 0;
static const int default_continuous_mode = 0;

/* Status Bar */
static const int default_show_status_bar = 1;
static const int status_bar_height = 20;
static const char *status_bar_font = "-misc-fixed-medium-r-normal-*-13-*-*-*-*-*-*-*";

/* Color Scheme */
static const int default_dark_mode = 0;

// Light mode colors (Nord-inspired)
static const char *status_bar_text_color_light = "#2E3440";  // Nord Polar Night
static const char *status_bar_bg_color_light = "#D8DEE9";    // Nord Snow Storm

// Dark mode colors (Nord theme)
static const char *status_bar_text_color_dark = "#ECEFF4";   // Nord Snow Storm
static const char *status_bar_bg_color_dark = "#3B4252";     // Nord Polar Night
static const char *page_bg_color_dark = "#d4b595";           // Nord Polar Night (darker)

/* Keyboard Shortcuts */
typedef enum {
    QUIT, NEXT, PREV, FIRST, LAST, FIT_PAGE, FIT_WIDTH,
    DOWN, UP, PG_DOWN, PG_UP, BACK, RELOAD, COPY,
    GOTO_PAGE, SEARCH, PAGE, MAGNIFY, ROTATE_CW, ROTATE_CCW,
    ZOOM_IN, ZOOM_OUT, TOGGLE_TWO_PAGE_VIEW,
    TOGGLE_CONTINUOUS_MODE, TOGGLE_STATUS_BAR, TOGGLE_DARK_MODE
} Action;

typedef struct {
    unsigned mask;
    KeySym ksym;
    Action action;
} Shortcut;

static const Shortcut shortcuts[] = {
    {AnyMask,     XK_q,            QUIT},
    {EmptyMask,   XK_Escape,       QUIT},
    {ControlMask, XK_Page_Down,    NEXT},
    {ControlMask, XK_Page_Up,      PREV},
    {ControlMask, XK_Home,         FIRST},
    {ControlMask, XK_End,          LAST},
    {EmptyMask,   XK_z,            FIT_PAGE},
    {EmptyMask,   XK_w,            FIT_WIDTH},
    {EmptyMask,   XK_Down,         DOWN},
    {EmptyMask,   XK_Up,           UP},
    {EmptyMask,   XK_Page_Down,    PG_DOWN},
    {EmptyMask,   XK_Page_Up,      PG_UP},
    {EmptyMask,   XK_b,            BACK},
    {AnyMask,     XK_r,            RELOAD},
    {ControlMask, XK_c,            COPY},
    {AnyMask,     XK_g,            GOTO_PAGE},
    {AnyMask,     XK_s,            SEARCH},
    {EmptyMask,   XK_slash,        SEARCH},
    {EmptyMask,   XK_p,            PAGE},
    {EmptyMask,   XK_m,            MAGNIFY},
    {EmptyMask,   XK_bracketright, ROTATE_CW},
    {EmptyMask,   XK_bracketleft,  ROTATE_CCW},
    {ControlMask, XK_plus,         ZOOM_IN},
    {ControlMask, XK_minus,        ZOOM_OUT},
    {EmptyMask,   XK_t,            TOGGLE_TWO_PAGE_VIEW},
    {EmptyMask,   XK_c,            TOGGLE_CONTINUOUS_MODE},
    {EmptyMask,   XK_F7,           TOGGLE_STATUS_BAR},
    {EmptyMask,   XK_i,            TOGGLE_DARK_MODE}
};

#endif // CONFIG_H
