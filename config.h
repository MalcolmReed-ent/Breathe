/*
 * Background color, X11 color name (see: https://en.wikipedia.org/wiki/X11_color_names).
 */
static const char *bg_color = "Gray50";

/*
 * Parameters are: mask, keysym, action.
 * Mask can be any mask from XKeyEvent.state (see: man XKeyEvent) or
 * AnyMask (mask is ignored), or EmptyMask (empty mask, no key modifiers).
 * Keysym can be any keysym from /usr/include/X11/keysymdef.h.
 */
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
    {EmptyMask,   XK_bracketleft,  ROTATE_CCW}
};

/*
 * Scrolling speed (in page fractions).
 */
static const double arrow_scroll = 0.01;
static const double page_scroll  = 0.30;
static const double mouse_scroll = 0.02;

/*
 * Font for any text rendering, must be in X logical font description format
 * (see: https://en.wikipedia.org/wiki/X_logical_font_description).
 */
static const char *font = "-misc-fixed-medium-r-normal-*-14-*-*-*-*-*-*-*";

/*
 * Selection color (RGB format)
 */
static const unsigned long selection_color = 0x7F7F7F; // Light gray

/*
 * Default zoom level when opening a new document (1.0 = 100%)
 */
static const double default_zoom = 1.0;

/*
 * Maximum zoom level
 */
static const double max_zoom = 5.0;

/*
 * Minimum zoom level
 */
static const double min_zoom = 0.1;

/*
 * Zoom step for zoom in/out operations
 */
static const double zoom_step = 0.1;

/*
 * Default rotation angle (in degrees, must be a multiple of 90)
 */
static const int default_rotation = 0;

/*
 * Enable or disable text selection (1 = enabled, 0 = disabled)
 */
static const int enable_text_selection = 1;

/*
 * Enable or disable link following (1 = enabled, 0 = disabled)
 */
static const int enable_link_following = 1;

/*
 * Cache size (in MB) for storing rendered pages
 */
static const int cache_size_mb = 64;
